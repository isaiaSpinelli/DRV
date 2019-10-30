#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_* */

static int
__init empty_init(void)
{
    printk(KERN_INFO "Hello!\n");
    return 0;
}

static void
__exit empty_exit (void)
{
    printk(KERN_DEBUG "Bye!\n");
}

MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

module_init(empty_init);
module_exit(empty_exit);
