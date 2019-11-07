#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "address_map_arm.h"
#include "interrupt_ID.h"

/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS");
/* Indique une description du module*/
MODULE_DESCRIPTION("Pushbutton example");

/* Structure permettant de mémoriser */
struct priv
{
    volatile int *LED_ptr;
    volatile int *KEY_ptr;
    void *MEM_ptr;
    struct resource *MEM_info;
    int IRQ_num;
};

/* */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{	/* */
    struct priv *priv = (struct priv *) dev_id;
	
    printk(KERN_NOTICE "Button pushed!\n");
	/* */
    *(priv->LED_ptr) = *(priv->LED_ptr) + 1;
	/* */
    *(priv->KEY_ptr + 3) = 0xF;
	
	/* */
    return (irq_handler_t) IRQ_HANDLED;
}

/* */
static int pushbutton_probe(struct platform_device *pdev)
{	
    struct priv *priv;
    int rc;
	/* */
    priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	/* */
    if (priv == NULL) {
        printk(KERN_ERR "Failed to allocate memory for private data!\n");
		/* */
        rc = -ENOMEM;
		/* */
        goto kmalloc_fail;
    }
	/* */
    platform_set_drvdata(pdev, priv);
	/* */
    priv->MEM_info = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* */
    if (priv->MEM_info == NULL) {
        printk(KERN_ERR "Failed to get memory resource from device tree!\n");
		/* */
        rc = -EINVAL;
        goto get_mem_fail;
    }
	
	/* */
    priv->MEM_ptr = ioremap_nocache(priv->MEM_info->start,
                                    priv->MEM_info->end - priv->MEM_info->start);
	/* */									
    if (priv->MEM_ptr == NULL) {
        printk(KERN_ERR "Failed to map memory!\n");
		/* */
        rc = -EINVAL;
        goto ioremap_fail;
    }
	
	/* */
    priv->LED_ptr = priv->MEM_ptr + LEDR_BASE;
    *(priv->LED_ptr) = 0x200;
	
	/* */
    priv->KEY_ptr = priv->MEM_ptr + KEY_BASE;
	/* */
    *(priv->KEY_ptr + 3) = 0xF;
	/* */
    *(priv->KEY_ptr + 2) = 0xF;

    printk(KERN_INFO "Registering interrupt...\n");
	
	/* */
    priv->IRQ_num = platform_get_irq(pdev, 0);
	/* */
    if (priv->IRQ_num < 0) {
        printk(KERN_ERR "Failed to get interrupt resource from device tree!\n");
		/* */
        rc = priv->IRQ_num;
        goto get_irq_fail;
    }
	
	/* */
    rc = request_irq(priv->IRQ_num,
                     (irq_handler_t) irq_handler,
                     IRQF_SHARED,
                     "pushbutton_irq_handler",
                     (void *) priv);
	/* */
    if (rc != 0) {
        printk(KERN_ERR "pushbutton_irq_handler: cannot register IRQ %d\n",
               priv->IRQ_num);
		/* */
        printk(KERN_ERR "error code: %d\n", rc);
        goto request_irq_fail;
    }

    printk(KERN_INFO "Interrupt registered!\n");
    printk(KERN_INFO "Driver ready!\n");
    return 0;

/* */
 request_irq_fail:
 get_irq_fail:
    iounmap(priv->MEM_ptr);
 ioremap_fail:
 get_mem_fail:
    kfree(priv);
 kmalloc_fail:
    return rc;
}

/* */
static int pushbutton_remove(struct platform_device *pdev)
{	/* */
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
	
	/* */
    *(priv->LED_ptr) = 0;
	/* */
    iounmap(priv->MEM_ptr);
	/* */
    free_irq(priv->IRQ_num, (void*) priv);
	/* */
    kfree(priv);

    return 0;
}

/* */
static const struct of_device_id pushbutton_driver_id[] = {
	/* */
    { .compatible = "drv" },
    { /* END */ },
};

/* */
MODULE_DEVICE_TABLE(of, pushbutton_driver_id);

/* */
static struct platform_driver pushbutton_driver = {
	/* */
    .driver = {
        .name = "drv-lab4",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pushbutton_driver_id),
    },
	/* */
    .probe = pushbutton_probe,
	/* */
    .remove = pushbutton_remove,
};

/* */
module_platform_driver(pushbutton_driver);
