#include "tfa.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("REDS institute, HEIG-VD, Yverdon-les-Bains");
MODULE_DESCRIPTION("TFA driver");
MODULE_VERSION(TFA_DRIVER_VERSION);


/* Declare the IDs of the cards managed by this driver */
static DEFINE_PCI_DEVICE_TABLE(tfa_id_table) = {
    { PCI_DEVICE(TFA_DEVICE_VENDOR, PCI_ANY_ID) },
    { 0, /* sentinel */ }
};


/* Generate map files for depmod */
MODULE_DEVICE_TABLE(pci, tfa_id_table);


/* pci_driver structure used when registering the driver to the PCI manager */
static struct pci_driver tfa_driver = {
    .name     = TFA_DEVICE_NAME,  /* device name */
    .id_table = tfa_id_table,     /* cards supported by this driver */
    .probe    = tfa_probe,        /* card probing function */
    .remove   = tfa_remove,       /* card removal function */
};


/*
 * The owner field is set to THIS_MODULE to prevent unloading while the device
 * is still active
 */
static struct file_operations tfa_data_fops = {
    .owner   = THIS_MODULE,
    .read    = tfa_data_read,
    .write   = tfa_data_write,
    .open    = tfa_data_open,
    .release = tfa_data_release,
};
static struct file_operations tfa_config_fops = {
    .owner   = THIS_MODULE,
    .write   = tfa_config_write,
    .open    = tfa_config_open,
    .release = tfa_config_release,
};


/*
 * Automatically create init and exit, which will call register()/deregister()
 */
module_pci_driver(tfa_driver);


/**
 * pc_to_fpga_wq_handler() - Perform PC->FPGA DMA transfers
 *
 * @work: enqueued job that signals that a transfer has to be made
 *
 * Transfer data from PC to the FPGA via DMA until the input KFIFO is empty.
 * DMA transfers are protected by a mutex that prevents transfers in both
 * directions from occurring simultaneously, while another mutex prevents
 * multiple instances of this same function to run concurrently.
 */
static void pc_to_fpga_wq_handler(struct work_struct *work)
{
    struct tfa_wk_t *w_p = (struct tfa_wk_t *)work;
    struct tfa_private *tfa_priv = (struct tfa_private *)w_p->ptr;
    int rc;

    /*
     * This lock prevents multiple instances of the workqueue to run
     * concurrently
     */
    pr_vdebug("+++ Locking pc_to_fpga_mutex +++\n");
    mutex_lock(&tfa_priv->pc_to_fpga_mutex);
    pr_vdebug("+++ pc_to_fpga_mutex LOCKED! +++\n");

    /* Iterate until the input KFIFO is not empty */
    while (!kfifo_is_empty(&(tfa_priv->data_in_kfifo))) {
        pr_debug("Waiting for less than TFA_IRQ_FIFO_IN_AEMPTY elements"
                 " in input FIFO (currently %d/%d)...\n",
                 get_fifo_in_level(tfa_priv),
                 tfa_priv->fifo_in_aempty_lvl);

        /*
         * The current AEMPTY level is considered the maximum filling
         * before the wq stops, waiting for the input FIFO to empty a
         * bit
         */
        while (get_fifo_in_level(tfa_priv) > tfa_priv->fifo_in_aempty_lvl) {
            tfa_priv->aempty_state = TFA_WAITING_AEMPTY_ISR;
            /*
             * Important! We first clear the IRQ to avoid previous
             * occurrences of this IRQ to immediately trigger the
             * ISR
             */
            clear_interrupts(tfa_priv, TFA_IRQ_FIFO_IN_AEMPTY);
            enable_interrupts(tfa_priv, TFA_IRQ_FIFO_IN_AEMPTY);
            /*
             * AEMPTY's IRQ will wake the thread up as soon as
             * enough room is available
             */
            while (tfa_priv->aempty_state != TFA_AEMPTY_ISR_DONE) {
                rc = wait_event_interruptible(tfa_priv->in_aempty_queue,
                                              tfa_priv->aempty_state == TFA_AEMPTY_ISR_DONE);
                if (rc == -ERESTARTSYS) {
                    pr_err("An external signal woken up the"
                           " worker while it was waiting "
                           "for input FIFO to be AEMPTY\n");
                }
            }
            clear_interrupts(tfa_priv, TFA_IRQ_FIFO_IN_AEMPTY);
        }

        /*
         * Take the biggest possible quantity of data to transfer,
         * either enough to fill the available input FIFO space or to
         * empty the input buffer.
         * Particular care has to be taken here, because while the FPGA
         * can accept a certain number of inputs in its FIFO (considered
         * as 40 bits elements), we are transferring elements that are
         * 128 bits in length, and this could overflow the available
         * space.
         */
        tfa_priv->dma_read_transfer_size =
            __tfa_DMA_prepare_data_pc_to_fpga(tfa_priv,
                                              min(kfifo_len(&tfa_priv->data_in_kfifo)
                                                  /TFA_DMA_W_DATUM_SIZE,
                                                  TFA_FIFO_IN_SIZE_DW-get_fifo_in_level(tfa_priv)));

        /* Move data from KFIFO to DMAable memory */
        if ((rc = kfifo_out(&(tfa_priv->data_in_kfifo),
                            tfa_priv->pc_fpga_dma_cpu_addr,
                            tfa_priv->dma_read_transfer_size)) !=
            tfa_priv->dma_read_transfer_size) {
            pr_err("kfifo_out() ERROR!\n"
                   "Expected %zu bytes, got %d\n",
                   tfa_priv->dma_read_transfer_size,
                   rc);
            return;
        }

        tfa_priv->dma_pc_to_fpga_state = TFA_WAITING_DMA_END;

        /*
         * Acquire the lock that prevents simultaneous transfers in the
         * two directions (we only have a single channel!)
         */
        pr_vdebug("+++ Locking dma_mutex +++\n");
        mutex_lock(&tfa_priv->dma_mutex);
        pr_vdebug("+++ dma_mutex LOCKED! +++\n");

        pr_debug("Starting PC->FPGA DMA transfer...\n");
        __tfa_register_write(tfa_priv,
                             TFA_DMA_COMMAND_OFF,
                             TFA_DMA_PC_FPGA_TRANSFER);
        pr_debug("Waiting for PC->FPGA DMA transfer to end...\n");

        /*
         * Wait for a DMA_READ_END IRQ that acknowledges the end of the
         * DMA transfer by setting the proper state
         */
        while (tfa_priv->dma_pc_to_fpga_state != TFA_DMA_TRANSFER_DONE) {
            rc = wait_event_interruptible(tfa_priv->dma_pc_to_fpga_queue,
                                          tfa_priv->dma_pc_to_fpga_state ==
                                          TFA_DMA_TRANSFER_DONE);
            if (rc == -ERESTARTSYS) {
                pr_err("An external signal woken up the worker "
                       "while it was waiting for PC->FPGA DMA "
                       "completion\n");
            }
        }
        pr_debug("PC->FPGA DMA transfer received!\n");
        show_fifos(tfa_priv);


        /* The DMA channel is now free */
        pr_vdebug("--- Unlocking dma_mutex ---\n");
        mutex_unlock(&tfa_priv->dma_mutex);
        pr_vdebug("--- dma_mutex UNLOCKED! ---\n");
    }

    /*
     * Now that we have emptied the KFIFO_IN, we can wake up
     * tasks that are possibly waiting (namely, tfa_data_write()).
     * While we could have it woken up just after the data is moved to
     * DMAable memory, this would have risked making the current job a
     * "thief" that steals the data from the subsequent one (not a big
     * deal, but we would have to deal with useless wq jobs)
     */
    wake_up_interruptible(&(tfa_priv->kfifo_in_emptied_queue));

    /* Concurrent wq jobs can now safely run */
    pr_vdebug("--- Unlocking pc_to_fpga_mutex ---\n");
    mutex_unlock(&tfa_priv->pc_to_fpga_mutex);
    pr_vdebug("--- pc_to_fpga_mutex UNLOCKED! ---\n");
}


/**
 * show_fifos() - Print the fill level of each fifo and kfifo in the system
 *
 * @tfa_priv: pointer to driver's private data
 */
static inline void show_fifos(const struct tfa_private *const tfa_priv)
{
    pr_debug("+--------------- FIFO status ---------------+\n");
    pr_debug(" Input FIFO  : %6d/%4d [AEMPTY_TH: %4d]\n",
             get_fifo_in_level(tfa_priv), TFA_FIFO_IN_SIZE_DW,
             tfa_priv->fifo_in_aempty_lvl);
    pr_debug(" Input KFIFO : %6d/%6d\n",
             kfifo_len(&(tfa_priv->data_in_kfifo))/4,
             TFA_DATA_IN_KFIFO_SIZE/4);
    pr_debug(" Output FIFO : %6d/%4d [AFULL_TH : %4d]\n",
             get_fifo_out_level(tfa_priv), TFA_FIFO_OUT_SIZE_QW,
             tfa_priv->fifo_out_afull_lvl);
    pr_debug(" Output KFIFO: %6d/%6d\n",
             kfifo_len(&(tfa_priv->data_out_kfifo))/4,
             TFA_DATA_OUT_KFIFO_SIZE/4);
    pr_debug("+-------------------------------------------+\n");
}


/**
 * fpga_to_pc_wq_handler() - Perform FPGA->PC DMA transfers
 *
 * @work: enqueued job that signals that a transfer has to be made
 *
 * Transfer data from FPGA to the PC via DMA until the output FIFO is empty.
 * If moving the data to the output KFIFO would result in an overflow, the
 * transferred data is discarded int the IRQ handler that manages the end
 * of the DMA transfer, and an error is reported in the logs.
 * DMA transfers are protected by a mutex that prevents transfers in both
 * directions from occurring simultaneously, while another mutex prevents
 * multiple instances of this same function to run concurrently.
 */
static void fpga_to_pc_wq_handler(struct work_struct *work)
{
    struct tfa_wk_t *w_p = (struct tfa_wk_t *)work;
    struct tfa_private *tfa_priv = (struct tfa_private *)w_p->ptr;
    int to_transfer;
    int rc;

    /*
     * This lock prevents multiple instances of the workqueue to run
     * concurrently
     */
    pr_vdebug("+++ Locking fpga_to_pc_mutex +++\n");
    mutex_lock(&tfa_priv->fpga_to_pc_mutex);
    pr_vdebug("+++ fpga_to_pc_mutex LOCKED! +++\n");

    /*
     * Get the number of items currently available in the output FIFO, and
     * work to transfer them
     */
    to_transfer = get_fifo_out_level(tfa_priv);
    while (to_transfer > 0) {
        pr_debug("%d elements to transfer from FPGA to PC\n",
                 to_transfer);

        /*
         * Check if the output KFIFO is full, if this is the case than
         * just wait for the reader to empty it a bit. The system might
         * get asleep for a bit -- the queues will be filled and the
         * whole machine will block until the data is read, unblocking
         * the system.
         */
        if (kfifo_avail(&tfa_priv->data_out_kfifo) < TFA_DMA_DATUM_SIZE) {
            pr_debug("Output KFIFO is full, waiting for a reader "
                     "to remove some elements...\n");
            rc = wait_event_interruptible(tfa_priv->kfifo_out_full_queue,
                                          kfifo_avail(&tfa_priv->data_out_kfifo)
                                          >= TFA_DMA_DATUM_SIZE);
            if (rc == -ERESTARTSYS) {
                pr_err("An external signal woken up the "
                       "FPGA->PC handler while it was waiting "
                       "for KFIFO_OUT to empty!\n");
                mutex_unlock(&tfa_priv->fpga_to_pc_mutex);
                return;
            }
        }

        /*
         * Try to send the maximum possible quantity of data (the
         * function will limit this quantity, and we will iterate again
         * to send the remaining data)
         */
        tfa_priv->dma_write_transfer_size =
            __tfa_DMA_prepare_data_fpga_to_pc(tfa_priv,
                                              min((u32)to_transfer,
                                                  kfifo_avail(&tfa_priv->data_out_kfifo)/
                                                  TFA_DMA_DATUM_SIZE));

        /* The system puts itself in the DMA waiting state */
        tfa_priv->dma_fpga_to_pc_state = TFA_WAITING_DMA_END;

        /*
         * Acquire the lock that prevents simultaneous transfers in the
         * two directions (we only have a single channel!)
         */
        pr_vdebug("+++ Locking dma_mutex +++\n");
        mutex_lock(&tfa_priv->dma_mutex);
        pr_vdebug("+++ dma_mutex LOCKED! +++\n");

        /* Start DMA transfer */
        pr_debug("Starting FPGA->PC DMA transfer...\n");
        __tfa_register_write(tfa_priv,
                             TFA_DMA_COMMAND_OFF,
                             TFA_DMA_FPGA_PC_TRANSFER);
        pr_debug("Waiting for FPGA->PC DMA transfer to end...\n");

        /*
         * Wait for a DMA_WRITE_END IRQ that acknowledges the end of the
         * DMA transfer by setting the proper state
         */
        while (tfa_priv->dma_fpga_to_pc_state != TFA_DMA_TRANSFER_DONE) {
            rc = wait_event_interruptible(tfa_priv->dma_fpga_to_pc_queue,
                                          tfa_priv->dma_fpga_to_pc_state ==
                                          TFA_DMA_TRANSFER_DONE);
            if (rc == -ERESTARTSYS) {
                pr_err("An external signal woken up the worker "
                       "while it was waiting for FPGA->PC DMA "
                       "completion\n");
            }
        }

        /* The DMA channel is now free */
        pr_vdebug("--- Unlocking dma_mutex ---\n");
        mutex_unlock(&tfa_priv->dma_mutex);
        pr_vdebug("--- dma_mutex UNLOCKED! ---\n");

        /* Compute the new quantity of data to transfer */
        to_transfer -= tfa_priv->dma_write_transfer_size/TFA_DMA_DATUM_SIZE;
    }

    /*
     * Important! We first clear the IRQ to avoid previous occurrences of
     * this IRQ to immediately trigger the ISR
     */
    clear_interrupts(tfa_priv, TFA_IRQ_FIFO_OUT_AFULL);
    enable_interrupts(tfa_priv, TFA_IRQ_FIFO_OUT_AFULL);

    /* Concurrent wq jobs can now safely run */
    pr_vdebug("--- Unlocking fpga_to_pc_mutex ---\n");
    mutex_unlock(&tfa_priv->fpga_to_pc_mutex);
    pr_vdebug("--- fpga_to_pc_mutex UNLOCKED! ---\n");
}


/**
 * __tfa_register_read() - Read a value from a memory-mapped I/O register
 *
 * @tfa_priv: pointer to driver's private data
 * @offset  : offset of the register of interest
 *
 * NOTE: Do NOT skip the cast --- very bad and incomprehensible things would
 *   happen!
 *
 * Return: Register's value.
 */
static inline u32 __tfa_register_read(const struct tfa_private *const tfa_priv,
                                      const u32 offset)
{
    /* NOTE: the cast here is *mandatory* */
    return ioread32((int *)tfa_priv->bar + offset);
}


/**
 * __tfa_register_write() - Write a value to a memory-mapped I/O register
 *
 * @tfa_priv: pointer to driver's private data
 * @offset  : offset of the register of interest
 * @value   : value that has to be written in the register
 *
 * NOTE: Do NOT skip the cast --- very bad and incomprehensible things would
 *   happen!
 */
static inline void __tfa_register_write(const struct tfa_private *const tfa_priv,
                                        const u32 offset,
                                        const u32 value)
{
    /* NOTE: the cast here is *mandatory* */
    iowrite32(value, (int *)tfa_priv->bar + offset);
}


/**
 * __tfa_DMA_prepare_data_fpga_to_pc() - Helper function to prepare a FPGA->PC
 *                                       DMA transfer
 *
 * @tfa_priv  : pointer to driver's private data
 * @data_count: number of elements that have to be transferred
 *
 * Prepare the system to perform a DMA transfer, setting the proper values in
 * the registers. The quantity of data that will be sent by the DMA prepared by
 * this function could be (and normally is) below the requested quantity.
 * In effect, DMA transfers have to obey some rules. In particular:
 * - the memory available on the board for the DMA transfer puts an hard limit
 *   on the maximum transfer size;
 * - each packet has to have the same exact size of the others (it is the value
 *   specified in the register whose offset is given as a parameter);
 * - the packet size is limited by the root complex.
 *
 * @note: The source/destination address (only the PC part has to be configured,
 *        as the board ignores this value on its side) is set during the
 *        probe(), therefore it will not be considered by this function.
 *
 * Return: Number of bytes sent by this DMA transfer.
 */
u32 __tfa_DMA_prepare_data_fpga_to_pc(struct tfa_private * const tfa_priv,
                                      const u32 data_count)
{
    u32 transfer_length = min(data_count*TFA_DMA_DATUM_SIZE,
                              (u32)TFA_DMA_MAX_TRANSFER_SIZE);

    /* Default number of packets is 1 */
    u32 pkts_no = 1;

    pr_debug("Data in FIFO: %d elements\n"
             "Expected transfer length (before limiting packet size) "
             "in bytes: %d\n",
             data_count, transfer_length);

    /*
     * If the length exceeds the size of a packet, split over multiple
     * packets and transfer only a multiple of packet length
     */
    if (transfer_length >= TFA_DMA_MAX_PKT_SIZE) {
        pkts_no = transfer_length / TFA_DMA_MAX_PKT_SIZE;
        /* Round to nearest packet multiple */
        transfer_length = pkts_no * TFA_DMA_MAX_PKT_SIZE;
        /* Each packet will be full size */
        __tfa_register_write(tfa_priv,
                             TFA_DMA_W_LENGTH_OFF,
                             TFA_DMA_MAX_PKT_SIZE/TFA_DMA_DATUM_SIZE*TFA_DMA_DW_PER_DATUM);
    } else {
        /* All data belongs to a single transfer */
        __tfa_register_write(tfa_priv,
                             TFA_DMA_W_LENGTH_OFF,
                             transfer_length /TFA_DMA_DATUM_SIZE*TFA_DMA_DW_PER_DATUM);
    }

    /* Number of packets */
    __tfa_register_write(tfa_priv, TFA_DMA_W_PKTS_NO_OFF, pkts_no);

    pr_debug( "DMA transfer details:\n"
              "  - initial data_count (elements): %d\n"
              "  - number of currently transferred elements: %d\n"
              "  - %d bytes to transfer\n"
              "  - %d packet(s)\n",
              data_count,
              transfer_length/TFA_DMA_DATUM_SIZE,
              transfer_length,
              pkts_no);

    return transfer_length;
}


/**
 * __tfa_DMA_prepare_data_pc_to_fpga() - Helper function to prepare a data write
 *                                       via DMA
 *
 * @tfa_priv  : pointer to driver's private data
 * @data_count: number of elements that have to be transferred
 *
 * Prepare the system to perform a DMA transfer, setting the proper values in
 * the registers. The quantity of data that will be sent by the DMA prepared by
 * this function could be (and normally is) below the requested quantity.
 * In effect, DMA transfers have to obey some rules. In particular:
 * - the memory available on the board for the DMA transfer puts an hard limit
 *   on the maximum transfer size;
 * - each packet has to have the same exact size of the others (it is the value
 *   specified in the register whose offset is given as a parameter);
 * - the packet size is limited by the root complex.
 *
 * @note: The source/destination address (only the PC part has to be configured,
 *        as the board ignores this value on its side) is set during the
 *        probe(), therefore it will not be considered by this function.
 *
 * Return: Number of bytes sent by this DMA transfer.
 */
u32 __tfa_DMA_prepare_data_pc_to_fpga(struct tfa_private * const tfa_priv,
                                      const u32 data_count)
{
    u32 transfer_length = min(data_count*TFA_DMA_W_DATUM_SIZE,
                              (u32)TFA_DMA_MAX_TRANSFER_SIZE);

    /* Default number of packets is 1 */
    u32 pkts_no = 1;

    pr_debug("--- DMA PC->FPGA ---\nData in FIFO: %d elements\n"
             "Expected transfer length (before limiting packet size) "
             "in bytes: %d\n",
             data_count, transfer_length);

    /*
     * If the length exceeds the size of a packet, split over multiple
     * packets and transfer only a multiple of packet length
     */
    if (transfer_length >= TFA_DMA_MAX_READ_PKT_SIZE) {
        pkts_no = transfer_length / TFA_DMA_MAX_READ_PKT_SIZE;
        /* Round to nearest packet multiple */
        transfer_length = pkts_no * TFA_DMA_MAX_READ_PKT_SIZE;
        /* Each packet will be full size */
        __tfa_register_write(tfa_priv,
                             TFA_DMA_R_LENGTH_OFF,
                             TFA_DMA_MAX_READ_PKT_SIZE/TFA_DMA_W_DATUM_SIZE*TFA_DMA_W_DW_PER_DATUM);
    } else {
        /* All data belongs to a single transfer */
        __tfa_register_write(tfa_priv,
                             TFA_DMA_R_LENGTH_OFF,
                             transfer_length /TFA_DMA_W_DATUM_SIZE*TFA_DMA_W_DW_PER_DATUM);
    }

    /* Number of packets */
    __tfa_register_write(tfa_priv, TFA_DMA_R_PKTS_NO_OFF, pkts_no);

    pr_debug( "DMA PC->FPGA transfer details:\n"
              "  - initial data_count (elements): %d\n"
              "  - number of currently transferred elements: %d\n"
              "  - %d bytes to transfer\n"
              "  - %d packet(s)\n",
              data_count,
              transfer_length/TFA_DMA_W_DATUM_SIZE,
              transfer_length,
              pkts_no);

    return transfer_length;
}


/**
 * get_fifo_in_level() - Get the number of elements currently in FIFO IN
 *
 * @tfa_priv: pointer to driver's private data
 *
 * Return: Number of elements in FIFO IN.
 */
static inline u32 get_fifo_in_level(const struct tfa_private * const tfa_priv)
{
    if (!(__tfa_register_read(tfa_priv, TFA_FIFO_IN_STATUS_OFF)
          & TFA_FIFO_EMPTY)) {
        return __tfa_register_read(tfa_priv, TFA_FIFO_IN_COUNT_OFF);
    }
    /*
     * If the input FIFO is empty, the value read in the register might be
     * wrong --- we return therefore an hardcoded value
     */
    return 0;
}


/**
 * fifo_in_full() - Checks whether the input FIFO is full
 *
 * @tfa_priv: pointer to driver's private data
 *
 * Return: 1 if the FIFO IN is full, 0 otherwise.
 */
static inline u32 fifo_in_full(const struct tfa_private * const tfa_priv)
{
    return (__tfa_register_read(tfa_priv, TFA_FIFO_IN_STATUS_OFF)
            & TFA_FIFO_FULL);
}


/**
 * get_fifo_in_level() - Get the number of elements currently in FIFO OUT
 *
 * @tfa_priv: pointer to driver's private data
 *
 * Return: Number of elements in FIFO OUT.
 */
static inline u32 get_fifo_out_level(const struct tfa_private * const tfa_priv)
{
    if (!(__tfa_register_read(tfa_priv, TFA_FIFO_OUT_STATUS_OFF)
          & TFA_FIFO_EMPTY)) {
        return __tfa_register_read(tfa_priv, TFA_FIFO_OUT_COUNT_OFF);
    }
    /*
     * If the output FIFO is empty, the value read in the register might be
     * wrong --- we return therefore an hardcoded value
     */
    return 0;
}


/**
 * enable_interrupts() - Enable a specific interrupt or set of interrupts
 *
 * @tfa_priv  : pointer to driver's private data
 * @interrupts: mask to the interrupt(s) to enable
 */

static inline void enable_interrupts(struct tfa_private * const tfa_priv,
                                     const u32 interrupts)
{
    u32 status;
    unsigned long flags;

    /*
     * Since the operation is performed in two steps (read the mask+write
     * the modified mask), we protect it with a spinlock (otherwise a
     * concurrent thread might alter the mask before we are able to write it
     * and its changes would be lost)
     */
    spin_lock_irqsave(&tfa_priv->op_lock, flags);

    __tfa_register_write(tfa_priv, TFA_IRQ_CLEAR_OFF, interrupts);
    status = __tfa_register_read(tfa_priv, TFA_IRQ_MASK_OFF);

    /* OR with the interrupts to enable -> enables them */
    __tfa_register_write(tfa_priv, TFA_IRQ_MASK_OFF, status | interrupts);

    spin_unlock_irqrestore(&tfa_priv->op_lock, flags);
}


/**
 * disable_interrupts() - Disable a specific interrupt or set of interrupts
 *
 * @tfa_priv  : pointer to driver's private data
 * @interrupts: mask to the interrupt(s) to disable
 */

static inline void disable_interrupts(struct tfa_private * const tfa_priv,
                                      const u32 interrupts)
{
    u32 status;
    unsigned long flags;

    /*
     * Since the operation is performed in two steps (read the mask+write
     * the modified mask), we protect it with a spinlock (otherwise a
     * concurrent thread might alter the mask before we are able to write it
     * and its changes would be lost)
     */
    spin_lock_irqsave(&tfa_priv->op_lock, flags);

    status = __tfa_register_read(tfa_priv, TFA_IRQ_MASK_OFF);

    /*
     * NAND with a mask where the interrupts to disable are set to 1
     * -> disables them
     */
    __tfa_register_write(tfa_priv, TFA_IRQ_MASK_OFF, status & ~interrupts);

    spin_unlock_irqrestore(&tfa_priv->op_lock, flags);
}


/**
 * clear_interrupts() - Clear a specific interrupt or set of interrupts from the
 *                      interrupt vector
 *
 * @tfa_priv  : pointer to driver's private data
 * @interrupts: mask marking the interrupts to clear
 */
static inline void clear_interrupts(struct tfa_private * const tfa_priv,
                                    const u32 interrupts)
{
    __tfa_register_write(tfa_priv, TFA_IRQ_CLEAR_OFF, interrupts);
}


/**
 * setup_initial_config() - Initialize FPGA's registers
 *
 * @tfa_priv: pointer to driver's private data
 */
static inline void setup_initial_config(struct tfa_private * const tfa_priv)
{
    /* Mark the overlay as not configured */
    tfa_priv->config_status = TFA_CONFIG_INVALID;

    /* Disable all interrupts */
    disable_interrupts(tfa_priv, TFA_IRQ_ALL);

    /* Initialize the FIFO IN AEMPTY and OUT AFULL values */
    tfa_priv->fifo_in_aempty_lvl = TFA_FIFO_IN_AEMPTY_DEFAULT_TH;
    tfa_priv->fifo_out_afull_lvl = TFA_FIFO_OUT_AFULL_DEFAULT_TH;

    /* Set config values that are not going to change */
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_COMMAND_OFF,
                         TFA_CONFIG_COMMAND_NOP_OP);
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_STATUS_CLEAR_OFF,
                         TFA_CONFIG_STATUS_CLEAR_CMD);

    /* Force the number of reads that are piped together */
    __tfa_register_write(tfa_priv,
                         TFA_DMA_READ_PIPE_SIZE_OFF,
                         TFA_DMA_READ_PIPE_SIZE);

    /* DMA destinations are fixed */
    __tfa_register_write(tfa_priv, TFA_DMA_W_PC_OFF,
                         tfa_priv->fpga_pc_dma_handle);
    __tfa_register_write(tfa_priv, TFA_DMA_R_PC_OFF,
                         tfa_priv->pc_fpga_dma_handle);

    pr_debug("FPGA->PC DMA handle: %#llX\n", tfa_priv->fpga_pc_dma_handle);
    pr_debug("PC->FPGA DMA handle: %#llX\n", tfa_priv->pc_fpga_dma_handle);

    /* Enable TFA_IRQ_FIFO_OUT_AFULL */
    __tfa_register_write(tfa_priv,
                         TFA_FIFO_OUT_AFULL_TH_OFF,
                         TFA_FIFO_OUT_AFULL_DEFAULT_TH);
    __tfa_register_write(tfa_priv,
                         TFA_FIFO_IN_AEMPTY_TH_OFF,
                         TFA_FIFO_IN_AEMPTY_DEFAULT_TH);

    /*
     * We enable the FIFO_OUT_AFULL interrupt because we react to this event
     * (we have to transfer the data to the output KFIFO. We also enable
     * READ/WRITE DMA END since in this way we do not need to enable/disable
     * them each time --- they're triggered only once the event happens, and
     * not at every clock cycle
     */
    enable_interrupts(tfa_priv, TFA_IRQ_FIFO_OUT_AFULL);
    enable_interrupts(tfa_priv, TFA_IRQ_DMA_READ_END);
    enable_interrupts(tfa_priv, TFA_IRQ_DMA_WRITE_END);
}


/**
 * INIT_WORKQUEUES() - Initialize a work queue and its corresponding work
 *                     structure
 *
 * @WQ_NAME       : name of the work queue to initialize
 * @CLEANUP_LABEL1: label to use if work queue initialization fails
 * @CLEANUP_LABEL2: label to use if the memory allocation for the work fails
 */
#define INIT_WORKQUEUES(WQ_NAME,            \
                        CLEANUP_LABEL1,     \
                        CLEANUP_LABEL2)     \
    do {                                        \
        /* Initialize workqueue */                          \
        WQ_NAME = create_singlethread_workqueue(#WQ_NAME);  \
        if (WQ_NAME == NULL) {                              \
            printk(KERN_ERR "Unable to allocate " #WQ_NAME  \
                   " work queue\n");                        \
            rc = -ENOMEM;                                   \
            goto CLEANUP_LABEL1;                            \
        }                                                   \
        /* Initialize work structure */                     \
        tfa_ ## WQ_NAME ## _work = (struct tfa_wk_t *)      \
            devm_kzalloc(&pdev->dev,                        \
                         sizeof(struct tfa_wk_t),           \
                         GFP_KERNEL);                       \
        if (tfa_ ## WQ_NAME ## _work == NULL) {             \
            printk(KERN_ERR "Unable to allocate " #WQ_NAME  \
                   " work's structure\n");                  \
            rc = -ENOMEM;                                   \
            goto CLEANUP_LABEL2;                            \
        }                                                           \
        INIT_WORK((struct work_struct *)(tfa_ ## WQ_NAME ## _work), \
                  WQ_NAME ## _handler);                             \
        tfa_ ## WQ_NAME ## _work->ptr = (void *)tfa_priv;           \
    } while (0);


/**
 * tfa_probe() - Probe function for the card, called when PCI core has a
 *               pci_dev structure it thinks we should manage
 *
 * @pdev   : pointer to our PCI device structure, passed as a parameter to
 *           pci_register_driver()
 * @card_id: structure containing vendor and class information
 *
 * Return: 0 on success, a negative number if an error has occurred.
 */
static int tfa_probe(struct pci_dev *pdev, const struct pci_device_id *card_id)
{
    /* Board's private data used by the driver */
    struct tfa_private *tfa_priv;

    /* Function's return value in case of error */
    int rc;

    pr_debug("TFA driver, version " TFA_DRIVER_VERSION);

    /* Creating the group in sysfs */
    rc = sysfs_create_group(&pdev->dev.kobj,
                            &tfa_device_attribute_group);
    if (rc) {
        pr_err("Failed to create a sysfs group for TFA");
        goto cleanup_exit;
    }

    /* Allocate private data */
    tfa_priv = devm_kzalloc(&pdev->dev,
                            sizeof(struct tfa_private),
                            GFP_KERNEL);
    if (tfa_priv == NULL) {
        pr_err("Unable to allocate memory for TFA "
               "driver's private data\n");
        rc = -ENOMEM;
        goto cleanup_sysfs_group;
    }

    /* Set private driver data pointer for the pci_dev */
    tfa_priv->dev = pdev;

    /*
     * Initialize the board (enable I/O and memory, wake up device if
     * suspended, ...) before using it
     */
    rc = pcim_enable_device(pdev);
    if (rc < 0) {
        pr_err("Unable to initialize TFA PCI device\n");
        goto cleanup_sysfs_group;
    }

    /* Map memory (everything's in BAR1, BAR0 is unused) */
    rc = pcim_iomap_regions(pdev, 1 << 1, TFA_DEVICE_NAME);
    if (rc != 0) {
        pr_err("Unable to reserve TFA PCI I/O and "
               "memory resources\n");
        goto cleanup_sysfs_group;
    }

    tfa_priv->bar = pcim_iomap_table(pdev)[1];

    /* If requested at compile-time, enable MSI interrupts */
#if TFA_USE_MSI
    if (pci_enable_msi(pdev) < 0) {
        pr_err("Cannot initialize MSI interrupts --- "
               "fall-back to legacy ones!\n");
    } else {
        pr_debug("MSI interrupts enabled! (%d obtained)",
                 pci_msi_vec_count(pdev));
    }
#endif /* TFA_USE_MSI */

    /* Request an interrupt handler */
    rc = devm_request_irq(&pdev->dev, pdev->irq, irq_handler, IRQF_SHARED,
                          TFA_DEVICE_NAME, tfa_priv);
    if (rc < 0) {
        pr_err("Unable to register TFA IRQ handler\n");
        goto cleanup_sysfs_group;
    }
    tfa_priv->irq = pdev->irq;

    pr_debug("Obtained IRQ line %d\n", tfa_priv->irq);

    pci_set_drvdata(pdev, tfa_priv);

    /* -- Activate board in the kernel -- */

    /* Enable bus-mastering for the device */
    pci_set_master(pdev);

    /* Prepare the devices */
    tfa_priv->data_device.minor = MISC_DYNAMIC_MINOR;
    tfa_priv->data_device.name = TFA_DATA_DEVICE_NAME;
    tfa_priv->data_device.fops = &tfa_data_fops;
    tfa_priv->data_device.parent = &pdev->dev;

    tfa_priv->config_device.minor = MISC_DYNAMIC_MINOR;
    tfa_priv->config_device.name = TFA_CONFIG_DEVICE_NAME;
    tfa_priv->config_device.fops = &tfa_config_fops;
    tfa_priv->config_device.parent = &pdev->dev;

    /* Register the data device */
    rc = misc_register(&tfa_priv->data_device);
    if (rc) {
        pr_err("Unable to instantiate device /dev/%s\n",
               TFA_DATA_DEVICE_NAME);
        goto cleanup_sysfs_group;
    }

    /* Register the device */
    rc = misc_register(&tfa_priv->config_device);
    if (rc) {
        pr_err("Unable to instantiate device /dev/%s\n",
               TFA_CONFIG_DEVICE_NAME);
        goto cleanup_deregister_data_device;
    }

    /* Allocate space for the config written in the device file */
    tfa_priv->config_dev_write_buf =
        devm_kzalloc(&pdev->dev,
                     TFA_CONFIG_SPACE_SIZE_DW*sizeof(u32),
                     GFP_KERNEL);
    if (tfa_priv->config_dev_write_buf == NULL) {
        pr_err("Unable to allocate kernel memory for "
               "data device write operation\n");
        rc = -ENOMEM;
        goto cleanup_deregister_config_device;
    }

    /* Allocate input KFIFO */
    if ((rc = kfifo_alloc(&tfa_priv->data_in_kfifo,
                          TFA_DATA_IN_KFIFO_SIZE, GFP_KERNEL)) != 0) {
        pr_err("Cannot allocate TFA input KFIFO (error: %d)\n", rc);
        rc = -ENOMEM;
        goto cleanup_deregister_config_device;
    }

    /* Allocate output KFIFO */
    if ((rc = kfifo_alloc(&tfa_priv->data_out_kfifo,
                          TFA_DATA_OUT_KFIFO_SIZE, GFP_KERNEL)) != 0) {
        pr_err("Cannot allocate TFA output KFIFO (error: %d)\n", rc);
        rc = -ENOMEM;
        goto cleanup_data_in_kfifo;
    }

    /* Initialize workqueues */
    INIT_WORKQUEUES(pc_to_fpga_wq,
                    cleanup_data_out_kfifo,
                    cleanup_pc_fpga_workqueue);
    INIT_WORKQUEUES(fpga_to_pc_wq,
                    cleanup_pc_fpga_workqueue,
                    cleanup_fpga_pc_workqueue);

    /* Initialize mutexes used in DMA transfers */
    mutex_init(&tfa_priv->pc_to_fpga_mutex);
    mutex_init(&tfa_priv->fpga_to_pc_mutex);
    mutex_init(&tfa_priv->dma_mutex);

    /* Initialize spinlock */
    spin_lock_init(&tfa_priv->op_lock);

    /* Initialize wait-queues */
    init_waitqueue_head(&(tfa_priv->config_queue));
    init_waitqueue_head(&(tfa_priv->in_aempty_queue));
    init_waitqueue_head(&(tfa_priv->dma_pc_to_fpga_queue));
    init_waitqueue_head(&(tfa_priv->dma_fpga_to_pc_queue));
    init_waitqueue_head(&(tfa_priv->kfifo_in_emptied_queue));
    init_waitqueue_head(&(tfa_priv->kfifo_out_full_queue));

    /* Setup DMA --- the FPGA supports 32 bit addressing only! */
    if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)) != 0) {
        pr_err("Cannot perform DMA on this platform!\n");
        rc = -ENODEV;
        goto cleanup_fpga_pc_workqueue;
    } else {
        pr_debug("Using 32-bit DMA address space!\n");
    }

    /* Allocate memory for FPGA->PC DMA */
    tfa_priv->fpga_pc_dma_cpu_addr =
        dmam_alloc_coherent(&pdev->dev,
                            TFA_FPGA_PC_DMA_MEM_SIZE,
                            &tfa_priv->fpga_pc_dma_handle,
                            GFP_KERNEL);
    if (tfa_priv->fpga_pc_dma_cpu_addr == NULL) {
        pr_err("TFA FPGA->PC DMA allocation error\n");
        rc = PTR_ERR(tfa_priv->fpga_pc_dma_cpu_addr);
        goto cleanup_fpga_pc_workqueue;
    }

    /* Allocate memory for PC->FPGA DMA */
    tfa_priv->pc_fpga_dma_cpu_addr =
        dmam_alloc_coherent(&pdev->dev,
                            TFA_PC_FPGA_DMA_MEM_SIZE,
                            &tfa_priv->pc_fpga_dma_handle,
                            GFP_KERNEL);
    if (tfa_priv->pc_fpga_dma_cpu_addr == NULL) {
        pr_err("TFA PC->FPGA DMA allocation error\n");
        rc = PTR_ERR(tfa_priv->pc_fpga_dma_cpu_addr);
        goto cleanup_fpga_pc_workqueue;
    }

    /* -- The device is now alive! -- */

    /*
     * Get FPGA version. If this version does not *exactly* match the
     * version supported by the driver, quit.
     */
    pr_debug("FPGA version number %#X\n",
             __tfa_register_read(tfa_priv, TFA_SYSTEM_VERSION_OFF));
    if (__tfa_register_read(tfa_priv, TFA_SYSTEM_VERSION_OFF) !=
        TFA_SUPPORTED_SYSTEM_VERSION) {
        pr_err("Unsupported design version!\n"
               "FPGA version: %#X\n"
               "Supported version: %#X\n",
               __tfa_register_read(tfa_priv, TFA_SYSTEM_VERSION_OFF),
               TFA_SUPPORTED_SYSTEM_VERSION);
        rc = -EACCES;
        goto cleanup_fpga_pc_workqueue;
    }

    /*
     * Read current overlay's number of rows and columns --- used when
     * reconfiguring to ensure that the configuration matches the hw
     */
    tfa_priv->overlay_rows_no = __tfa_register_read(tfa_priv,
                                                    TFA_ROWS_NO_OFF);
    tfa_priv->overlay_cols_no = __tfa_register_read(tfa_priv,
                                                    TFA_COLS_NO_OFF);

    pr_debug("Rows no: %d, cols no: %d\n",
             tfa_priv->overlay_rows_no, tfa_priv->overlay_cols_no);

    /*
     * Check that test_return works properly (we write a value to this
     * specific register, and expect to read back the same value)
     */
    __tfa_register_write(tfa_priv,
                         TFA_TEST_RETURN_OFF,
                         TFA_RW_TEST_NUMBER);
    if (__tfa_register_read(tfa_priv, TFA_TEST_RETURN_OFF) !=
        TFA_RW_TEST_NUMBER) {
        pr_err("Error encountered during R/W test!\n"
               "Written value: %#X\n"
               "Read value: %#X\n",
               TFA_RW_TEST_NUMBER,
               __tfa_register_read(tfa_priv, TFA_TEST_RETURN_OFF));
        rc = -EACCES;
        goto cleanup_fpga_pc_workqueue;
    }
    pr_debug("R/W self-check passed!\n");

    /* Reset device */
    __tfa_register_write(tfa_priv,
                         TFA_GLOBAL_RESET_OFF,
                         TFA_RESET_CMD);

    /* Perform initial setup */
    setup_initial_config(tfa_priv);

    pr_debug("FPGA config status: %#X\n",
             __tfa_register_read(tfa_priv, TFA_CONFIG_STATUS_OFF));
    pr_debug("FPGA data status: %#X\n",
             __tfa_register_read(tfa_priv, TFA_DATA_STATUS_OFF));
    return 0;

 cleanup_fpga_pc_workqueue:
    flush_workqueue(fpga_to_pc_wq);
    destroy_workqueue(fpga_to_pc_wq);
 cleanup_pc_fpga_workqueue:
    flush_workqueue(pc_to_fpga_wq);
    destroy_workqueue(pc_to_fpga_wq);
 cleanup_data_out_kfifo:
    kfifo_free(&tfa_priv->data_out_kfifo);
 cleanup_data_in_kfifo:
    kfifo_free(&tfa_priv->data_in_kfifo);
 cleanup_deregister_config_device:
    misc_deregister(&tfa_priv->config_device);
 cleanup_deregister_data_device:
    misc_deregister(&tfa_priv->data_device);
 cleanup_sysfs_group:
    sysfs_remove_group(&pdev->dev.kobj,
                       &tfa_device_attribute_group);
 cleanup_exit:
    return rc;
}


/**
 * tfa_remove() - Remove function, called when either the structure pci_dev
 *                is being removed or when the PCI driver is unloaded from
 *                the kernel
 *
 * @device: pointer to our PCI device structure, passed as a parameter to
 *          pci_register_driver()
 */
static void tfa_remove(struct pci_dev *device)
{
    struct tfa_private *tfa_priv;

    /* Get information on the card we want to remove */
    tfa_priv = pci_get_drvdata(device);

    pr_debug("TFA driver unloading...\n");

    /* Reset device */
    __tfa_register_write(tfa_priv,
                         TFA_GLOBAL_RESET_OFF,
                         TFA_RESET_CMD);

    /* Destroy workqueues */
    flush_workqueue(fpga_to_pc_wq);
    destroy_workqueue(fpga_to_pc_wq);
    flush_workqueue(pc_to_fpga_wq);
    destroy_workqueue(pc_to_fpga_wq);

    /* Free the KFIFOs */
    kfifo_free(&tfa_priv->data_in_kfifo);
    kfifo_free(&tfa_priv->data_out_kfifo);

    /* Remove devices */
    misc_deregister(&tfa_priv->data_device);
    misc_deregister(&tfa_priv->config_device);

    /* Removing sysfs group */
    sysfs_remove_group(&device->dev.kobj,
                       &tfa_device_attribute_group);

    /* Inform the system that the PCI device is not in use anymore */
    pci_disable_device(device);
}


/**
 * show_version() - Export the design's version value in a sysfs file (useful to
 *                  test if the board is still responding after a crash)
 *
 * @dev : pointer to the device we are considering
 * @attr: sought attribute (unused)
 * @buf : pointer to the output buffer
 *
 * Read the DWORD corresponding to the design's version from the mapped memory,
 * then export it in a sysfs file. The page size limit is enforced.
 *
 * Return: The number of characters printed into the buffer.
 */
static ssize_t show_version(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
{
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    return snprintf(buf, TFA_PAGE_SIZE-sizeof(int), "%i\n",
                    __tfa_register_read(tfa_priv, TFA_SYSTEM_VERSION_OFF));
}


/**
 * store_global_reset() - Reset the FPGA's config & data
 *
 * @dev  : pointer to the device we are considering
 * @attr : sought attribute (unused)
 * @buf  : pointer to the output buffer
 * @count: buffer's size
 *
 * Return: The number of characters consumed from the buffer.
 */
static ssize_t store_global_reset(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count)
{
    int tmp;
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    if ((sscanf(buf, "%i", &tmp)) != 1)
        return -EINVAL;

    if (tmp != 0) {
        pr_debug("GLOBAL reset requested\n");
        __tfa_register_write(tfa_priv,
                             TFA_GLOBAL_RESET_OFF,
                             TFA_RESET_CMD);
        /*
         * Wait for a bit, just to be sure that the reset has reached
         * all the FIFOs
         */
        udelay(100);
        /* Reset internal KFIFOs */
        kfifo_reset(&tfa_priv->data_in_kfifo);
        kfifo_reset(&tfa_priv->data_out_kfifo);
        /* Destroy workqueues */
        flush_workqueue(fpga_to_pc_wq);
        flush_workqueue(pc_to_fpga_wq);

        setup_initial_config(tfa_priv);
    }

    return strnlen(buf, count);
}


/**
 * store_data_reset() - Reset the data in FPGA's FIFOs
 *
 * @dev  : pointer to the device we are considering
 * @attr : sought attribute (unused)
 * @buf  : pointer to the output buffer
 * @count: buffer's size
 *
 * Return: The number of characters consumed from the buffer.
 */
static ssize_t store_data_reset(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf,
                                size_t count)
{
    int tmp;
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    if ((sscanf(buf, "%i", &tmp)) != 1)
        return -EINVAL;

    if (tmp != 0) {
        pr_debug("DATA reset requested\n");
        __tfa_register_write(tfa_priv,
                             TFA_DATA_RESET_OFF,
                             TFA_RESET_CMD);
        /*
         * Wait for a bit, just to be sure that the reset has reached
         * all the FIFOs
         */
        udelay(100);
        /* Reset internal KFIFOs */
        kfifo_reset(&tfa_priv->data_in_kfifo);
        kfifo_reset(&tfa_priv->data_out_kfifo);
        /* Flush workqueues */
        flush_workqueue(fpga_to_pc_wq);
        flush_workqueue(pc_to_fpga_wq);
    }

    return strnlen(buf, count);
}


/**
 * show_in_aempty_th() - Export the FIFO IN almost empty threshold in a sysfs
 *                       file
 *
 * @dev : pointer to the device we are considering
 * @attr: sought attribute (unused)
 * @buf : pointer to the output buffer
 *
 * Return: The number of characters printed into the buffer.
 */
static ssize_t show_in_aempty_th(struct device *dev,
                                 struct device_attribute *attr,
                                 char *buf)
{
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    return snprintf(buf, TFA_PAGE_SIZE-sizeof(int), "%i\n",
                    __tfa_register_read(tfa_priv,
                                        TFA_FIFO_IN_AEMPTY_TH_OFF));
}


/**
 * store_in_aempty_th() - Set the threshold for the FIFO IN almost empty
 *                        interrupt
 *
 * @dev  : pointer to the device we are considering
 * @attr : sought attribute (unused)
 * @buf  : pointer to the output buffer
 * @count: buffer's size
 *
 * @note: The threshold's value is automatically limited to the FIFO's size -1.
 *
 * Return: The number of characters consumed from the buffer.
 */
static ssize_t store_in_aempty_th(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count)
{
    int tmp;
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    if ((sscanf(buf, "%i", &tmp)) != 1)
        return -EINVAL;

    if (tmp < 0) {
        tmp = 0;
    }
    if (tmp >= TFA_FIFO_IN_SIZE_DW) {
        tmp = TFA_FIFO_IN_SIZE_DW-1;
    }

    __tfa_register_write(tfa_priv,
                         TFA_FIFO_IN_AEMPTY_TH_OFF,
                         tmp);
    tfa_priv->fifo_in_aempty_lvl = tmp;

    pr_debug("FIFO IN AEMPTY threshold set to %d\n", tmp);

    return strnlen(buf, count);
}


/**
 * show_out_afull_th() - Export the FIFO OUT almost full threshold in a sysfs
 *                       file
 *
 * @dev : pointer to the device we are considering
 * @attr: sought attribute (unused)
 * @buf : pointer to the output buffer
 *
 * Return: The number of characters printed into the buffer.
 */
static ssize_t show_out_afull_th(struct device *dev,
                                 struct device_attribute *attr,
                                 char *buf)
{
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    return snprintf(buf, TFA_PAGE_SIZE-sizeof(int), "%i\n",
                    __tfa_register_read(tfa_priv,
                                        TFA_FIFO_OUT_AFULL_TH_OFF));
}


/**
 * store_out_afull_th() - Set the threshold for the FIFO OUT almost full
 *                        interrupt
 *
 * @dev  : pointer to the device we are considering
 * @attr : sought attribute (unused)
 * @buf  : pointer to the output buffer
 * @count: buffer's size
 *
 * @note: The threshold's value is automatically limited to the FIFO's size -1.
 *
 * Return: The number of characters consumed from the buffer.
 */
static ssize_t store_out_afull_th(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf,
                                  size_t count)
{
    int tmp;
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    if ((sscanf(buf, "%i", &tmp)) != 1)
        return -EINVAL;

    if (tmp < 1) {
        tmp = 1;
    }
    if (tmp >= TFA_FIFO_OUT_SIZE_QW) {
        tmp = TFA_FIFO_OUT_SIZE_QW-1;
    }

    __tfa_register_write(tfa_priv,
                         TFA_FIFO_OUT_AFULL_TH_OFF,
                         tmp);
    tfa_priv->fifo_out_afull_lvl = tmp;

    pr_debug("FIFO OUT AFULL threshold set to %d\n", tmp);

    return strnlen(buf, count);
}


/**
 * show_loopback_enable() - Show whether data loopback is enabled or not
 *
 * @dev : pointer to the device we are considering
 * @attr: sought attribute (unused)
 * @buf : pointer to the output buffer
 *
 * Return: The number of characters printed into the buffer.
 */
static ssize_t show_loopback_enable(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    return snprintf(buf, TFA_PAGE_SIZE-sizeof(int), "%i\n",
                    __tfa_register_read(tfa_priv,
                                        TFA_LOOPBACK_ENABLE_OFF));
}


/**
 * store_loopback_enable() - Enable/disable loopback data (that is, disconnect
 *                           the overlay and directly output the input data)
 *
 * @dev  : pointer to the device we are considering
 * @attr : sought attribute (unused)
 * @buf  : pointer to the output buffer
 * @count: buffer's size
 *
 * Return: The number of characters consumed from the buffer.
 */
static ssize_t store_loopback_enable(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
    int tmp;
    struct tfa_private *tfa_priv = dev_get_drvdata(dev);

    if ((sscanf(buf, "%i", &tmp)) != 1)
        return -EINVAL;

    if (tmp != 0) {
        __tfa_register_write(tfa_priv,
                             TFA_LOOPBACK_ENABLE_OFF,
                             0x1);
    } else {
        __tfa_register_write(tfa_priv,
                             TFA_LOOPBACK_ENABLE_OFF,
                             0x0);
    }

    return strnlen(buf, count);
}


/**
 * irq_handler() - Handler for interrupt requests
 *
 * @irq   : interrupt line number
 * @dev_id: pointer to the structure given in the request_irq call, in our case
 *          the device's private data
 *
 * Return: IRQ_NONE if an interrupt 0 has been received (bus locked??) --- we
 *         don't know how to deal with this situation ---, IRQ_HANDLED
 *         otherwise.
 */
static irqreturn_t irq_handler(int irq, void *dev_id)
{
    /* Retrieve the pointer to the driver's private data */
    struct tfa_private *tfa_priv = (struct tfa_private *)dev_id;

    /* IRQ register's value */
    u32 irq_reg_val;
    /* Mask telling which IRQs are enabled */
    u32 status;

    /* Retrieve current interrupt vector */
    irq_reg_val = __tfa_register_read(tfa_priv,
                                      TFA_IRQ_REGISTER_OFF);

    /* Retrieve current interrupt status bits */
    status =  __tfa_register_read(tfa_priv,
                                  TFA_IRQ_MASK_OFF);

    pr_debug("##### ISR (reg: %#X - mask: %#X - result: %#X) START #####\n",
             irq_reg_val, status, irq_reg_val & status);

    /* Interrupt 0 received -- PCIe bus problem? */
    if (irq_reg_val == TFA_IRQ_INVALID) {
        pr_err("!!! Interrupt 0 received !!!\n");
        dump_stack();
        return IRQ_NONE;
    }

    /*
     * Even if a given interrupt is disabled, it can still be set to 1 in
     * the interrupt vector. The difference is that it won't trig the ISR.
     * To take this situation into account, we do a logical AND with the
     * status register, to avoid handling disabled interrupts.
     */
    irq_reg_val &= status;

    /*
     * Check if a masked interrupt has been received. For an unknown reason,
     * this happens very frequently, especially with MSI interrupts. Since
     * we could not find a fix, we simply ignore this situation.
     */
    if (irq_reg_val == 0x0) {
        pr_debug("MASKED interrupt received!"
                 "IRQ register: %#X, mask: %#X\n",
                 irq_reg_val, status);

        return IRQ_HANDLED;
    }

    /*
     * End of a DMA READ operation --- set the proper state and wake up the
     * worker
     */
    if (irq_reg_val & TFA_IRQ_DMA_READ_END) {
        pr_debug("Received interrupt: TFA_IRQ_DMA_READ_END\n");

        clear_interrupts(tfa_priv, TFA_IRQ_DMA_READ_END);

        tfa_priv->dma_pc_to_fpga_state = TFA_DMA_TRANSFER_DONE;
        wake_up_interruptible(&(tfa_priv->dma_pc_to_fpga_queue));

        pr_debug("######## ISR END #######\n");
        return IRQ_HANDLED;
    }

    /*
     * End of a DMA WRITE operation --- if enough room is available in the
     * KFIFO, push data in it, set the proper state, and wake up the worker.
     * If not enough room is available, discard the data with an error and
     * get back to the worker.
     */
    if (irq_reg_val & TFA_IRQ_DMA_WRITE_END) {
        pr_debug("Received interrupt: TFA_IRQ_DMA_WRITE_END\n");

        clear_interrupts(tfa_priv, TFA_IRQ_DMA_WRITE_END);

        /*
         * If we exceed KFIFO capacity, then return with an error and
         * discard all the transferred data
         */
        if (kfifo_avail(&tfa_priv->data_out_kfifo) <
            tfa_priv->dma_write_transfer_size) {
            pr_err("Transferred data exceeds internal memory!\n"
                   "This should **NEVER** happen!\n");
        } else {
            /* Push data in the KFIFO */
            kfifo_in(&tfa_priv->data_out_kfifo,
                     tfa_priv->fpga_pc_dma_cpu_addr,
                     tfa_priv->dma_write_transfer_size);
            pr_debug("Transferred %ld bytes to KFIFO",
                     tfa_priv->dma_write_transfer_size);
        }

        tfa_priv->dma_fpga_to_pc_state = TFA_DMA_TRANSFER_DONE;
        wake_up_interruptible(&(tfa_priv->dma_fpga_to_pc_queue));

        pr_debug("######## ISR END #######\n");
        return IRQ_HANDLED;
    }

    /*
     * Output FIFO is almost full, issue a new worker to deal with this
     * situation
     */
    if (irq_reg_val & TFA_IRQ_FIFO_OUT_AFULL) {
        pr_debug("Received interrupt: TFA_IRQ_FIFO_OUT_AFULL "
                 "(threshold was %u, we have %d)\n",
                 tfa_priv->fifo_out_afull_lvl,
                 get_fifo_out_level(tfa_priv));

        disable_interrupts(tfa_priv, TFA_IRQ_FIFO_OUT_AFULL);
        clear_interrupts(tfa_priv, TFA_IRQ_FIFO_OUT_AFULL);

        queue_work(fpga_to_pc_wq,
                   (struct work_struct *)tfa_fpga_to_pc_wq_work);

        pr_debug("######## ISR END #######\n");
        return IRQ_HANDLED;
    }

    /*
     * Input FIFO is almost empty, wake up the worker that has to move
     * data to the FPGA
     */
    if (irq_reg_val & TFA_IRQ_FIFO_IN_AEMPTY) {
        pr_debug("Received interrupt: FIFO_IN_AEMPTY\n");

        disable_interrupts(tfa_priv, TFA_IRQ_FIFO_IN_AEMPTY);
        clear_interrupts(tfa_priv, TFA_IRQ_FIFO_IN_AEMPTY);
        tfa_priv->aempty_state = TFA_AEMPTY_ISR_DONE;
        /*
         * Wake up the PC->FPGA workqueue's job that was waiting for
         * enough room in the input FIFO to start a new DMA transfer
         */
        wake_up_interruptible(&(tfa_priv->in_aempty_queue));

        pr_debug("######## ISR END #######\n");
        return IRQ_HANDLED;
    }

    /* End of overlay's reconfiguration */
    if (irq_reg_val & TFA_IRQ_CONFIG_DONE) {
        pr_debug("Received interrupt: CONFIG_DONE\n");

        disable_interrupts(tfa_priv, TFA_IRQ_CONFIG_DONE);
        clear_interrupts(tfa_priv, TFA_IRQ_CONFIG_DONE);

        /* Set the config status as valid */
        tfa_priv->config_status = TFA_RECONFIG_DONE;

        /*
         * Wake up the config_write() which was sleeping in the
         * meantime
         */
        wake_up_interruptible(&(tfa_priv->config_queue));

        pr_debug("######## ISR END #######\n");
        return IRQ_HANDLED;
    }

    printk(KERN_ERR "Unexpected IRQ (%#X) received!\n", irq_reg_val);
    dump_stack();
    return IRQ_NONE;
}


/**
 * tfa_data_open() - Handle the open operation on the data device file
 *
 * @inode: device's inode
 * @file : pointer to file
 *
 * @warning: Do NOT remove this function! It seems to be doing nothing but,
 *           possibly owing to a bug in the kernel, if it is not here the file
 *           data structure won't receive the pointer to the private data
 *           -> everything will crash!!
 *
 * Return: 0 on success -- no errors should happen here.
 */
static int tfa_data_open(struct inode *inode, struct file *file)
{
    pr_vdebug("device %s OPENED\n", TFA_DATA_DEVICE_NAME);
    return 0;
}


/**
 * tfa_data_release() - Release the data device file, following a user-space
 *                      close operation
 *
 * @inode: device's inode
 * @file : pointer to file
 *
 * @warning: Do NOT remove this function! It seems to be doing nothing but,
 *           possibly owing to a bug in the kernel, if it is not here the file
 *           data structure won't receive the pointer to the private data
 *           -> everything will crash!!
 *
 * Return: 0 on success -- no errors should happen here.
 */
static int tfa_data_release(struct inode *inode, struct file *file)
{
    pr_vdebug("device %s CLOSED\n", TFA_DATA_DEVICE_NAME);
    return 0;
}


/**
 * tfa_data_read() - Read data from the data device file
 *
 * @filp : pointer to file device
 * @buf  : user-space buffer where read data will be put
 * @count: size of the data to be read
 * @f_pos: position offset in file -- unused in our case
 *
 * Return: The number of written bytes on success, a negative number if an error
 *         occurs. In particular, -ERESTARTSYS is returned if a sleeping read is
 *         woken up by an external signal, and -EFAULT if kfifo_to_user() fails.
 */
static ssize_t tfa_data_read(struct file *filp,
                             char __user *buf,
                             size_t count,
                             loff_t *f_pos)
{
    struct tfa_private *tfa_priv;
    u32 copied;
    int rc;
    u32 new_fifo_out_afull_lvl = 0;
    u32 old_kfifo_len;

    /* Retrieve private data through the file interface */
    tfa_priv = (struct tfa_private *)
        container_of(filp->private_data,
                     struct tfa_private, data_device);

    if (count > TFA_DATA_OUT_KFIFO_SIZE) {
        pr_err("Requested more data (%zu bytes) than that the KFIFO can "
               "hold (%d bytes)!\n", count, TFA_DATA_OUT_KFIFO_SIZE);
        return -EIO;
    }

    pr_debug("tfa_data_read() of %zd bytes requested, %d already in "
             "output KFIFO\n",
             count, kfifo_len(&(tfa_priv->data_out_kfifo)));

    /*
     * If the output KFIFO does not have enough data to satisfy the
     * user's request, set the AFULL threshold appropriately and
     * then sleep
     */
    while ((old_kfifo_len = kfifo_len(&(tfa_priv->data_out_kfifo))) < count) {
        new_fifo_out_afull_lvl = ceil(count-old_kfifo_len,
                                      TFA_DMA_DATUM_SIZE);
        pr_debug("Not enough data available, waiting for a DMA "
                 "transfer to fill the gap "
                 "(%zu bytes = %d elements)\n",
                 count-old_kfifo_len,
                 new_fifo_out_afull_lvl);

        /*
         * If the number of missing elements is below the current AFULL
         * threshold, reduce the latter and restore it later
         */
        if (new_fifo_out_afull_lvl < tfa_priv->fifo_out_afull_lvl) {
            __tfa_register_write(tfa_priv,
                                 TFA_FIFO_OUT_AFULL_TH_OFF,
                                 new_fifo_out_afull_lvl);
        }

        rc = wait_event_interruptible(tfa_priv->dma_fpga_to_pc_queue,
                                      kfifo_len(&(tfa_priv->data_out_kfifo)) != old_kfifo_len);
        if (rc == -ERESTARTSYS) {
            pr_err("An external signal woken up the read function "
                   "while it was waiting for DMA completion\n");
            return rc;
        }

        if (new_fifo_out_afull_lvl != 0) {
            /* Restore previous AFULL value */
            __tfa_register_write(tfa_priv,
                                 TFA_FIFO_OUT_AFULL_TH_OFF,
                                 tfa_priv->fifo_out_afull_lvl);
        }

        pr_debug("Some missing data received, new level: %d bytes "
                 "(%zu requested)!\n",
                 kfifo_len(&(tfa_priv->data_out_kfifo)), count);
    }

    if ((rc = kfifo_to_user(&(tfa_priv->data_out_kfifo),
                            buf,
                            count,
                            &copied)) != 0) {
        pr_err("Error encountered while copying data in userspace "
               "(%d)\n", rc);
        return rc;
    }

    /*
     * A worker might be waiting for this read to empty a bit the output
     * KFIFO -> inform it that there is some space now!
     */
    wake_up_interruptible(&(tfa_priv->kfifo_out_full_queue));

    pr_debug("Successfully transferred %d bytes\n", copied);

    return copied;
}


/**
 * tfa_data_write() - Write a given buffer of data to the data device file
 *
 * @filp : pointer to file device
 * @buf  : user-space buffer that contains the data to write
 * @count: size of the data to be written
 * @f_pos: position offset in file -- unused in our case
 *
 * Return: The number of written bytes on success, a negative number if an error
 *         occurs.
 */
static ssize_t tfa_data_write(struct file *filp,
                              const char __user *buf,
                              size_t count,
                              loff_t *f_pos)
{
    struct tfa_private *tfa_priv;
    u32 to_transfer;
    u32 transferred = 0;
    u32 copied;
    int rc;

    /* Retrieve private data through the file interface */
    tfa_priv = (struct tfa_private *)
        container_of(filp->private_data, struct tfa_private,
                     data_device);

    if (count == 0) {
        pr_err("Empty write requested!\n");
        return 0;
    }

    pr_vdebug("tfa_data_write() of %zd bytes requested\n", count);

    /*
     * The input data might not fit entirely in the input KFIFO. If this is
     * the case, then move to the KFIFO a bit at a time. Once all the data
     * has been moved to the KFIFO, the function returns.
     */
    while (transferred < count) {
        to_transfer = min((unsigned int)(count-transferred),
                          kfifo_avail(&(tfa_priv->data_in_kfifo)));

        if (to_transfer > 0) {
            /* Transfer the data in kernel space */
            if ((rc = kfifo_from_user(&(tfa_priv->data_in_kfifo),
                                      buf+transferred,
                                      to_transfer,
                                      &copied)) != 0) {
                pr_err("kfifo_from_user() returned -EFAULT\n");
                return rc;
            }
            transferred += copied;

            pr_debug("Transferred %u bytes (out of %zu) to KFIFO "
                     "IN, issuing workqueue "
                     "(%u/%zu bytes transferred so far)\n",
                     copied, count, transferred, count);

            queue_work(pc_to_fpga_wq,
                       (struct work_struct *)tfa_pc_to_fpga_wq_work);
        }

        /*
         * Wait that the KFIFO empties a bit before transferring some
         * more data
         */
        if (transferred != count) {
            pr_debug("Waiting that the KFIFO IN empties before "
                     "restarting transfers\n");

            while (kfifo_is_full(&(tfa_priv->data_in_kfifo))) {
                rc = wait_event_interruptible(tfa_priv->kfifo_in_emptied_queue,
                                              !kfifo_is_full(&(tfa_priv->data_in_kfifo)));
                if (rc == -ERESTARTSYS) {
                    pr_err("An external signal woken up the"
                           " write function while it was "
                           "waiting for the input FIFO to "
                           "empty\n");
                }
            }
        }
    }

    return transferred;
}


/**
 * tfa_config_open() - Handle the open operation on the config device file
 *
 * @inode: device's inode
 * @file : pointer to file
 *
 * @warning: Do NOT remove this function! It seems to be doing nothing but,
 *           possibly owing to a bug in the kernel, if it is not here the file
 *           data structure won't receive the pointer to the private data
 *           -> everything will crash!!
 *
 * Return: 0 on success -- no errors should happen here.
 */
static int tfa_config_open(struct inode *inode, struct file *file)
{
    pr_vdebug("device %s OPENED\n", TFA_CONFIG_DEVICE_NAME);
    return 0;
}


/**
 * tfa_config_release() - Release the config device file, following a user-space
 *                        close operation
 *
 * @inode: device's inode
 * @file : pointer to file
 *
 * @warning: Do NOT remove this function! It seems to be doing nothing but,
 *           possibly owing to a bug in the kernel, if it is not here the file
 *           data structure won't receive the pointer to the private data
 *           -> everything will crash!!
 *
 * Return: 0 on success -- no errors should happen here.
 */
static int tfa_config_release(struct inode *inode, struct file *file)
{
    pr_vdebug("device %s CLOSED\n", TFA_CONFIG_DEVICE_NAME);
    return 0;
}


/**
 * tfa_config_write() - Write a given buffer of data to the config device file
 *
 * @filp : pointer to file device
 * @buf  : user-space buffer that contains the data to write
 * @count: size of the data to be written
 * @f_pos: position offset in file -- unused in our case
 *
 * Return: The number of written bytes on success, a negative number if an error
 *         occurs.
 */
static ssize_t tfa_config_write(struct file *filp,
                                const char __user *buf,
                                size_t count,
                                loff_t *f_pos)
{
    struct tfa_private *tfa_priv;
    int dword_to_write;
    u32 cell_cfg;
    int i;
    int rc;
    int cfg_size;

    /* Retrieve private data through the file interface */
    tfa_priv = (struct tfa_private *)
        container_of(filp->private_data, struct tfa_private,
                     config_device);

    /* Compute the number of DWORDs to write */
    dword_to_write = count / sizeof(u32);

    if (dword_to_write > TFA_CONFIG_SPACE_SIZE_DW) {
        pr_err("The configuration size (%d DW) exceeds the reserved "
               "space (%d DW)\n",
               dword_to_write, TFA_CONFIG_SPACE_SIZE_DW);
        return -EINVAL;
    }

    /* Mark the reconfiguration as in progress */
    tfa_priv->config_status = TFA_RECONFIG_IN_PROGRESS;

    /* Copy user-space config in kernel-space */
    if ((rc = copy_from_user(tfa_priv->config_dev_write_buf,
                             buf, count)) != 0) {
        pr_err("Unable to copy %d bytes of config from user to kernel "
               "space\n", rc);
        return -ENOMEM;
    }

    /* Sanity check on configuration size */
    cfg_size = (tfa_priv->config_dev_write_buf[1] >> 16) & 0xFFFF;
    if (cfg_size != tfa_priv->overlay_rows_no*tfa_priv->overlay_cols_no) {
        pr_err("The overlay size (%dx%d) does not match the size of the "
               "written configuration (%d)\n",
               tfa_priv->overlay_rows_no, tfa_priv->overlay_cols_no, cfg_size);
    }

    /* Store size and offset for both the configuration and the pattern */
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_SIZE_AND_OFFSET_OFF,
                         tfa_priv->config_dev_write_buf[1]);
    __tfa_register_write(tfa_priv,
                         TFA_PATTERN_SIZE_AND_OFFSET_OFF,
                         tfa_priv->config_dev_write_buf[3]);

    /* Write config one element at a time */
    for (i = TFA_CONFIG_HEADER_SIZE_DW; i < dword_to_write; ++i) {
        cell_cfg = *((u32 *)(tfa_priv->config_dev_write_buf)+i);
        __tfa_register_write(tfa_priv,
                             TFA_RAM_START_OFF+i,
                             cell_cfg);
        pr_vdebug("Written config value %d = %#X\n",
                  i, cell_cfg);
    }

    /*
     * Prepare ourselves to receive a CONFIG_DONE IRQ as the configuration
     * terminates
     */
    /*
     * Important! We first clear the IRQ to avoid previous occurrences of
     * this IRQ to trigger the ISR immediately
     */
    clear_interrupts(tfa_priv, TFA_IRQ_CONFIG_DONE);
    enable_interrupts(tfa_priv, TFA_IRQ_CONFIG_DONE);

    /* Start reconfiguration */
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_COMMAND_OFF,
                         TFA_CONFIG_COMMAND_START_OP);

    /* Wait for reconfiguration end */
    while (tfa_priv->config_status != TFA_RECONFIG_DONE) {
        rc = wait_event_interruptible(tfa_priv->config_queue,
                                      tfa_priv->config_status == TFA_RECONFIG_DONE);

        if (rc) {
            pr_err("write_config() operation woken up by "
                   "external signal before reconfiguration end");
            return -ERESTARTSYS;
        }

    }

    pr_debug("Reconfiguration done!\n");

    /* Clear commands */
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_COMMAND_OFF,
                         TFA_CONFIG_COMMAND_NOP_OP);
    __tfa_register_write(tfa_priv,
                         TFA_CONFIG_STATUS_CLEAR_OFF,
                         TFA_CONFIG_STATUS_CLEAR_CMD);

    return count;
}
