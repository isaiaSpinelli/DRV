#ifndef TFA_H_
#define TFA_H_

#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include <linux/kfifo.h>
#include <linux/sched.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#define TFA_DRIVER_VERSION              "0xC"

#define TFA_DEVICE_NAME                 "tfa"
#define TFA_DEVICE_VENDOR               0x10EE
#define TFA_DEVICE_ID                   0x7028
#define TFA_SUPPORTED_SYSTEM_VERSION    0x0B
#define TFA_DATA_DEVICE_NAME            "tfa_data"
#define TFA_CONFIG_DEVICE_NAME          "tfa_config"

#define TFA_USE_MSI                     0

#define TFA_PAGE_SIZE                   4096
#define MB                              (1024*1024)
#define TFA_DATA_IN_KFIFO_SIZE          (4*MB)
#define TFA_DATA_OUT_KFIFO_SIZE         (4*MB)
#define TFA_FIFO_IN_SIZE_DW             1024
#define TFA_FIFO_OUT_SIZE_QW            1024
#define TFA_CONFIG_SPACE_SIZE_DW        2048

#define TFA_SYSTEM_VERSION_OFF          0x050
#define TFA_TEST_RETURN_OFF             0x051
#define TFA_RW_TEST_NUMBER              0xCAFEBABE

#define TFA_ROWS_NO_OFF                 0x058
#define TFA_COLS_NO_OFF                 0x059

#define TFA_GLOBAL_RESET_OFF            0x090
#define TFA_DATA_RESET_OFF              0x091
#define TFA_RESET_CMD                   0x1

#define TFA_LOOPBACK_ENABLE_OFF         0x092

#define TFA_FIFO_IN_STATUS_OFF          0x052
/* If FIFO empty, ignore this value */
#define TFA_FIFO_IN_COUNT_OFF           0x053
#define TFA_FIFO_OUT_STATUS_OFF         0x054
/* If FIFO empty, ignore this value */
#define TFA_FIFO_OUT_COUNT_OFF          0x055

#define TFA_FIFO_EMPTY                  0x1
#define TFA_FIFO_FULL                   0x80

#define TFA_FIFO_IN_AEMPTY_TH_OFF       0x056
#define TFA_FIFO_OUT_AFULL_TH_OFF       0x057
#define TFA_FIFO_IN_AEMPTY_DEFAULT_TH   (TFA_DMA_MAX_TRANSFER_SIZE/(TFA_DMA_DATUM_SIZE_DW*2))
#define TFA_FIFO_OUT_AFULL_DEFAULT_TH   (TFA_DMA_MAX_TRANSFER_SIZE/(TFA_DMA_DATUM_SIZE_DW*2))

#define TFA_CONFIG_STATUS_OFF           0x084
#define TFA_CONFIG_DONE                 0x002

#define TFA_DATA_STATUS_OFF             0x085

#define TFA_RAM_START_OFF               0x800
#define TFA_CONFIG_HEADER_SIZE_DW       0x004
#define TFA_CONFIG_COMMAND_OFF          TFA_RAM_START_OFF+0
/* 16 MSB = size, 16 LSB = offset */
#define TFA_CONFIG_SIZE_AND_OFFSET_OFF  TFA_RAM_START_OFF+1
#define TFA_CONFIG_STATUS_CLEAR_OFF     TFA_RAM_START_OFF+2
/* 16 MSB = size, 16 LSB = offset */
#define TFA_PATTERN_SIZE_AND_OFFSET_OFF TFA_RAM_START_OFF+3
#define TFA_CONFIG_VALUES_OFF           TFA_RAM_START_OFF+TFA_CONFIG_HEADER_SIZE_DW

#define TFA_CONFIG_COMMAND_NOP_OP       0x000
#define TFA_CONFIG_COMMAND_START_OP     0x001
#define TFA_CONFIG_STATUS_CLEAR_CMD     0x000

/* Interrupts */
#define TFA_IRQ_MASK_OFF                0x040
#define TFA_IRQ_CLEAR_OFF               0x041
#define TFA_IRQ_REGISTER_OFF            0x042
#define TFA_IRQ_GENERATION_OFF          0x043

#define TFA_IRQ_INVALID                 0x0
#define TFA_IRQ_DMA_WRITE_END           (1 << 1)
#define TFA_IRQ_DMA_READ_END            (1 << 2)
#define TFA_IRQ_FIFO_IN_EMPTY           (1 << 4)
#define TFA_IRQ_FIFO_IN_FULL            (1 << 5)
#define TFA_IRQ_FIFO_OUT_FULL           (1 << 7)
#define TFA_IRQ_CONFIG_DONE             (1 << 8)
#define TFA_IRQ_FIFO_IN_AEMPTY          (1 << 10)
#define TFA_IRQ_FIFO_OUT_AFULL          (1 << 11)
#define TFA_IRQ_ALL                     0xFFFFFFFF

#define TFA_RECONFIG_IN_PROGRESS        0x0
#define TFA_RECONFIG_DONE               0x1
#define TFA_CONFIG_INVALID              0x2

/* DMA */
/* How many bytes are transferred for a single value in FIFO */
#define TFA_DMA_DATUM_SIZE              16
#define TFA_DMA_W_DATUM_SIZE            4
#define TFA_DMA_DATUM_SIZE_DW           2
#define TFA_DMA_DW_PER_DATUM            (TFA_DMA_DATUM_SIZE/4)
#define TFA_DMA_W_DW_PER_DATUM          (TFA_DMA_W_DATUM_SIZE/4)
/* In bytes */
#define TFA_DMA_MAX_PKT_SIZE            128
/* In bytes */
#define TFA_DMA_MAX_READ_PKT_SIZE       512
/* In bytes */
#define TFA_DMA_MAX_TRANSFER_SIZE       8192
#define TFA_PC_FPGA_DMA_MEM_SIZE        TFA_DMA_MAX_TRANSFER_SIZE
#define TFA_FPGA_PC_DMA_MEM_SIZE        TFA_DMA_MAX_TRANSFER_SIZE

#define TFA_DMA_COMMAND_OFF             0x0
#define TFA_DMA_FPGA_PC_TRANSFER        (1 << 1)
#define TFA_DMA_PC_FPGA_TRANSFER        (1 << 2)

/* Write = from FPGA to PC */
#define TFA_DMA_W_FPGA_OFF              0x001
#define TFA_DMA_W_PC_OFF                0x002
#define TFA_DMA_W_LENGTH_OFF            0x003
#define TFA_DMA_W_PKTS_NO_OFF           0x004

/* Read = from PC to FPGA */
#define TFA_DMA_R_FPGA_OFF              0x005
#define TFA_DMA_R_PC_OFF                0x006
#define TFA_DMA_R_LENGTH_OFF            0x007
#define TFA_DMA_R_PKTS_NO_OFF           0x008
#define TFA_DMA_READ_PIPE_SIZE_OFF      0x00D
#define TFA_DMA_READ_PIPE_SIZE          0x1

#define TFA_DMA_TRANSFER_DONE           0x0
#define TFA_WAITING_DMA_END             0x1

#define TFA_AEMPTY_ISR_DONE             0x0
#define TFA_WAITING_AEMPTY_ISR          0x1


/**
 * struct tfa_private - Private data of the device driver
 *
 * @dev                    : device-driver structure
 * @bar                    : pointer to base address register
 * @fifo_in_aempty_lvl     : current FIFO IN AEMPTY level
 * @fifo_out_afull_lvl     : current FIFO OUT AFULL level
 * @fpga_pc_dma_cpu_addr   : address for FPGA->PC DMA transfers
 * @fpga_pc_dma_handle     : handle for FPGA->PC DMA transfers
 * @pc_fpga_dma_cpu_addr   : address for PC->FPGA DMA transfers
 * @pc_fpga_dma_handle     : handle for PC->FPGA DMA transfers
 * @pc_to_fpga_mutex       : mutex protecting PC->FPGA DMA transfers
 * @fpga_to_pc_mutex       : mutex protecting FPGA->PC DMA transfers
 * @dma_mutex              : mutex protecting DMA transfers (prevents multiple
 *                           DMA operations to happen at the same time on the
 *                           unique available channel)
 * @op_lock                : spinlock used in interrupt handling operations
 * @dma_write_transfer_size: size of the current FPGA->PC DMA transfer
 * @dma_read_transfer_size : size of the current PC->FPGA DMA transfer
 * @overlay_rows_no        : number of rows in the current overlay
 * @overlay_cols_no        : number of columns in the current overlay
 * @data_device            : data device file
 * @config_device          : configuration device file
 * @config_queue           : wait queue for the end of the reconfiguration
 * @in_aempty_queue        : wait queue for operations waiting for the input
 *                           FIFO to be almost empty
 * @dma_pc_to_fpga_queue   : wait queue for operations waiting for the end of PC
 *                           to FPGA DMA transfers
 * @dma_fpga_to_pc_queue   : wait queue for operations waiting for the end of
 *                           FPGA to PC DMA transfers
 * @kfifo_in_emptied_queue : wait queue for operations waiting for the input
 *                           KFIFO to be emptied a bit
 * @kfifo_out_full_queue   : wait queue for operations waiting for the output
 *                           KFIFO to be emptied a bit
 * @config_dev_write_buf   : kernel-space buffer for storing the overlay's
 *                           configuration
 * @data_in_kfifo          : input KFIFO
 * @data_out_kfifo         : output KFIFO
 * @config_status          : reconfiguration status
 * @irq                    : interrupt line number
 * @dma_pc_to_fpga_state   : status of the PC->FPGA data transfer
 * @dma_fpga_to_pc_state   : status of the FPGA->PC data transfer
 * @aempty_state           : state used by workers waiting for the input FIFO to
 *                           be AEMPTY
 */
struct tfa_private {
    struct pci_dev *dev;
    void *bar;

    u32 fifo_in_aempty_lvl;
    u32 fifo_out_afull_lvl;

    void *fpga_pc_dma_cpu_addr;
    dma_addr_t fpga_pc_dma_handle;
    void *pc_fpga_dma_cpu_addr;
    dma_addr_t pc_fpga_dma_handle;

    struct mutex pc_to_fpga_mutex;
    struct mutex fpga_to_pc_mutex;
    struct mutex dma_mutex;

    spinlock_t op_lock;

    size_t dma_write_transfer_size;
    size_t dma_read_transfer_size;

    u32 overlay_rows_no;
    u32 overlay_cols_no;

    struct miscdevice data_device;
    struct miscdevice config_device;

    wait_queue_head_t config_queue;

    wait_queue_head_t in_aempty_queue;
    wait_queue_head_t dma_pc_to_fpga_queue;
    wait_queue_head_t dma_fpga_to_pc_queue;
    wait_queue_head_t kfifo_in_emptied_queue;
    wait_queue_head_t kfifo_out_full_queue;

    u32 *config_dev_write_buf;

    struct kfifo data_in_kfifo;
    struct kfifo data_out_kfifo;

    int config_status;

    unsigned int irq;

    char dma_pc_to_fpga_state;
    char dma_fpga_to_pc_state;
    char aempty_state;
};


/**
 * struct tfa_wk_t - structure used to pass parameters to the workqueue
 * @work: work structure itself
 * @ptr : pointer to the driver's private data structure
 */
struct tfa_wk_t {
    struct work_struct work;
    void *ptr;
};


/**
 * Workqueue for jobs that perform DMA transfers from PC to FPGA (aka DMA read)
 */
static struct workqueue_struct *pc_to_fpga_wq;


/**
 * Workqueue for jobs that perform DMA transfers from FPGA to PC (aka DMA write)
 */
static struct workqueue_struct *fpga_to_pc_wq;


/**
 * PC to FPGA job's structure
 */
static struct tfa_wk_t *tfa_pc_to_fpga_wq_work;


/**
 * FPGA to PC job's structure
 */
static struct tfa_wk_t *tfa_fpga_to_pc_wq_work;


/* Initialization */
static inline void setup_initial_config(struct tfa_private * const);
static int tfa_probe(struct pci_dev *, const struct pci_device_id *);
static void tfa_remove(struct pci_dev *);


/* Driver operations */
static inline u32 __tfa_register_read(const struct tfa_private * const,
                                      const u32);
static inline void __tfa_register_write(const struct tfa_private * const,
                                        const u32,
                                        const u32);
static inline void disable_interrupts(struct tfa_private * const,
                                      const u32);
static inline void enable_interrupts(struct tfa_private * const,
                                     const u32);
static inline void clear_interrupts(struct tfa_private * const,
                                    const u32);
static inline u32 get_fifo_in_level(const struct tfa_private * const);
static inline u32 fifo_in_full(const struct tfa_private * const);
static inline u32 get_fifo_out_level(const struct tfa_private * const);


/* Device files handling */
static ssize_t tfa_data_read(struct file *, char __user *, size_t, loff_t *);
static int tfa_data_open(struct inode *, struct file *);
static int tfa_data_release(struct inode *, struct file *);
static ssize_t tfa_data_write(struct file *, const char __user *,
                              size_t, loff_t *);
static int tfa_config_open(struct inode *, struct file *);
static int tfa_config_release(struct inode *, struct file *);
static ssize_t tfa_config_write(struct file *, const char __user *,
                                size_t, loff_t *);


/* Sysfs */
static ssize_t show_version(struct device *, struct device_attribute *, char *);
static ssize_t store_data_reset(struct device *, struct device_attribute *,
                                const char *, size_t);
static ssize_t store_global_reset(struct device *, struct device_attribute *,
                                  const char *, size_t);
static ssize_t show_in_aempty_th(struct device *, struct device_attribute *,
                                 char *);
static ssize_t store_in_aempty_th(struct device *, struct device_attribute *,
                                  const char *, size_t);
static ssize_t show_out_afull_th(struct device *, struct device_attribute *,
                                 char *);
static ssize_t store_out_afull_th(struct device *, struct device_attribute *,
                                  const char *, size_t);
static ssize_t show_loopback_enable(struct device *, struct device_attribute *,
                                    char *);
static ssize_t store_loopback_enable(struct device *, struct device_attribute *,
                                     const char *, size_t);
static inline void show_fifos(const struct tfa_private *const tfa_priv);


/* IRQ handler */
static irqreturn_t irq_handler(int, void *);


/* DMA transfer */
u32 __tfa_DMA_prepare_data_fpga_to_pc(struct tfa_private *, const u32);
u32 __tfa_DMA_prepare_data_pc_to_fpga(struct tfa_private *, const u32);
static void pc_to_fpga_wq_handler(struct work_struct *);
static void fpga_to_pc_wq_handler(struct work_struct *);


/* Sysfs attributes */
static DEVICE_ATTR(version, S_IRUSR, show_version, NULL);
static DEVICE_ATTR(data_reset, S_IWUSR, NULL, store_data_reset);
static DEVICE_ATTR(global_reset, S_IWUSR, NULL, store_global_reset);
static DEVICE_ATTR(in_aempty_th, S_IRUSR | S_IWUSR,
                   show_in_aempty_th, store_in_aempty_th);
static DEVICE_ATTR(out_afull_th, S_IRUSR | S_IWUSR,
                   show_out_afull_th, store_out_afull_th);
static DEVICE_ATTR(loopback_enable, S_IRUSR | S_IWUSR,
                   show_loopback_enable, store_loopback_enable);


static struct attribute *tfa_device_attrs[] = {
    &dev_attr_version.attr,
    &dev_attr_data_reset.attr,
    &dev_attr_global_reset.attr,
    &dev_attr_in_aempty_th.attr,
    &dev_attr_out_afull_th.attr,
    &dev_attr_loopback_enable.attr,
    NULL,
};


/* Sysfs attribute group -- store this sysfs group and name it */
const struct attribute_group tfa_device_attribute_group = {
    .name = "tfa_sysfs",
    .attrs = tfa_device_attrs,
};


/* Perform the ceil() operation */
#define ceil(n, d) (((n) < 0) ? (-((-(n))/(d))) : (n)/(d) + ((n)%(d) != 0))


#define SHOW_FIFO_STATUS(MSG)           \
    do {                                        \
        pr_debug("### " MSG ":\n"                           \
                 "\tIN KFIFO: %d bytes, OUT KFIFO: %d bytes\n"  \
                 "\tIN fifo: %d elements, OUT fifo: %d elements\n", \
                 kfifo_len(&tfa_priv->data_in_kfifo),               \
                 kfifo_len(&tfa_priv->data_out_kfifo),              \
                 get_fifo_in_level(tfa_priv),                       \
                 get_fifo_out_level(tfa_priv));                     \
    } while (0);


/* Verbose debug messages */
#ifdef VDEBUG
#ifndef pr_vdebug
#define pr_vdebug(fmt, arg...)                  \
    pr_debug(fmt, ##arg)
#endif /* pr_vdebug */
#else
#ifndef pr_vdebug
#define pr_vdebug(fmt, arg...)                  \
    ({ if (0) pr_debug(fmt, ##arg); })
#endif /* pr_vdebug */
#endif /* VDEBUG */

#endif /* TFA_H_ */
