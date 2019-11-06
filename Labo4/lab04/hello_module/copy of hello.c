#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/fs.h>           /* Needed for file_operations */

/* For strlen. Only includes a subset of libc's library */
#include <linux/string.h>

/* For copy_to_user */
#include <linux/uaccess.h>

/* For cdev fonctions */
#include <linux/cdev.h>

#define DEVICE_NAME	"He\tllo"
#define MAJOR_NUM	100
#define MINOR_NUM 		0
#define MY_DEV_COUNT 	1

char *message = DEVICE_NAME;
unsigned long msgLength = 0;

/* structure du device */
struct cdev my_cdev;

/* Fonction de lecture */
static ssize_t
hello_read(struct file *filep, char *buffer, size_t count, loff_t *ppos)
{
    /* Test les entrées */
    if (buffer == 0 || count < msgLength) {
        return 0;
    }

    if (*ppos >= msgLength) {
        return 0;
    }

    /* Copier le message dans l'espace utilisateur (buf) */
    if ( copy_to_user(buffer , message, msgLength) != 0 ) {
		return 0;
	}

	/* màj de la position */
    *ppos = msgLength;
    

    return msgLength;
}

/* Binding with standard device nodes interface */
const struct 
file_operations hello_fops = {
    .owner = THIS_MODULE,
    .read = hello_read
};


static int
__init hello_init(void)
{
    dev_t devno;
	unsigned int count = MY_DEV_COUNT;
	int err;
	
	/* Définie le numéro major et mineur*/
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Enregistre l'appareil à caractère avec un nombre de device max */
	if ( (err = register_chrdev_region(devno, count , DEVICE_NAME))  != 0 ) {
		printk(KERN_ERR "register_chrdev_region error%d\n", err);
		return -1;
	}

	/* Initialiser une structure cdev */
	cdev_init(&my_cdev, &hello_fops);
	my_cdev.owner = THIS_MODULE;
	/* Ajouter le périphérique char au système */
	err = cdev_add(&my_cdev, devno, count);

	/* Check si il y a une erreur */
	if (err < 0)
	{
		printk(KERN_ERR "Device Add Error\n");
		return -1;
	}
	

    msgLength = strlen(message);
    printk(KERN_INFO "Hello!\nworld\n");
    return 0;
}

static void
__exit hello_exit(void)
{
	dev_t devno;
	
	/* Retirer le device du système */
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Annule l'enregistrement du numéro de l'appareil*/
	unregister_chrdev_region(devno, MY_DEV_COUNT);
	cdev_del(&my_cdev);
	
    printk(KERN_INFO "Bye!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(hello_init);
module_exit(hello_exit);
