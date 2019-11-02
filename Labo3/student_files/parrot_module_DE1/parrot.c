#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* Needed for file_operations */
#include <linux/slab.h>         /* Needed for kmalloc */
#include <linux/uaccess.h>      /* copy_(to|from)_user */

#include <linux/string.h>

#define MAJOR_NUM       97
#define DEVICE_NAME     "parrot"

#define PARROT_CMD_TOGGLE   0
#define PARROT_CMD_ALLCASE  1

char *global_buffer;
int buffer_size;

void
strManip(char *s, int swapLower, int swapUpper)
{
    for(; *s != '\0'; s++) {
        if (*s >= 'a' && *s <= 'z'  && swapLower) {
            *s = *s + ('A' - 'a');
        } else if (*s >= 'A' && *s <= 'Z' && swapUpper) {
            *s = *s + ('a' - 'A');
        }
    }
}

static ssize_t
parrot_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
    if (buf == 0 || count < buffer_size) {
        return 0;
    }

    if (*ppos >= buffer_size) {
        return 0;
    }

    copy_to_user(buf, global_buffer, buffer_size);

    *ppos = buffer_size;

    return buffer_size;
}

static ssize_t
parrot_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
    if (count == 0) {
        return 0;
    }

    *ppos = 0;

    if (buffer_size != 0) {
        kfree(global_buffer);
    }

    global_buffer = kmalloc(count+1, GFP_KERNEL);

    copy_from_user(global_buffer, buf, count);
    global_buffer[count] = '\0';

    buffer_size = count+1;

    return count;
}

static long
parrot_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    if (buffer_size == 0) {
        return -1;
    }

    switch(cmd) {
    case PARROT_CMD_ALLCASE:
        if(arg == 1) {
            strManip(global_buffer, 0, 1);
        } else {
            strManip(global_buffer, 1, 0);
        }
        break;
    case PARROT_CMD_TOGGLE:
        strManip(global_buffer, 1, 1);
        break;
    default:
        break;
    }
    return 0;
}

const static struct
file_operations parrot_fops = {
    .owner         = THIS_MODULE,
    .read          = parrot_read,
    .write         = parrot_write,
    .unlocked_ioctl= parrot_ioctl,
};

static int
__init parrot_init(void)
{
    register_chrdev(MAJOR_NUM, DEVICE_NAME, &parrot_fops);

    buffer_size = 0;

    printk(KERN_INFO "Parrot ready!\n");
    return 0;
}

static void
__exit parrot_exit(void)
{
    if(global_buffer != 0) {
        kfree(global_buffer);
    }

    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

    printk(KERN_INFO "Parrot bye!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(parrot_init);
module_exit(parrot_exit);
