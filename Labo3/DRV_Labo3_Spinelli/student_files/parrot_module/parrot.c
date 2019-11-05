// INCLUDES
#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* Needed for file_operations */
#include <linux/slab.h>         /* Needed for kmalloc */
#include <linux/uaccess.h>      /* copy_(to|from)_user */
#include <linux/module.h>
#include <linux/cdev.h>

#include <linux/string.h>

// DEFINES
#define MAJOR_NUM       97
#define MINOR_NUM 		0
#define MY_DEV_COUNT 	1

#define DEVICE_NAME     "parrot"

#define PARROT_CMD_TOGGLE   0
#define PARROT_CMD_ALLCASE  1

// VARIABLES GLOBALES
// "Fichier" côté kernel
char *global_buffer;
// Donnée venant de l'espace utilisateur
int buffer_size;

// structure du device 
struct cdev my_cdev;

/* 
* Modifie une chaine de caractère afin d'echanger 
* les minuscules ou/et les majuscules
*/
void strManip(char *s, int swapLower, int swapUpper)
{
    for(; *s != '\0'; s++) {
        if (*s >= 'a' && *s <= 'z'  && swapLower) {
            *s = *s + ('A' - 'a');
        } else if (*s >= 'A' && *s <= 'Z' && swapUpper) {
            *s = *s + ('a' - 'A');
        }
    }
}

/*
 * Permet de lire un fichier(buffer global) depuis l'espace utilisateur
*/
static ssize_t parrot_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{	
	// Test les entrées 
    if (buf == 0 || count < buffer_size) {
        return 0;
    }

    if (*ppos >= buffer_size) {
        return 0;
    }
    
    printk("Lecture\n");
    
	// Copier le buffer global dans l'espace utilisateur (buf).
    if ( copy_to_user(buf, global_buffer, buffer_size) != 0 ) {
		return 0;
	}
	
	// màj de la position
    *ppos = buffer_size;

    return buffer_size;
}

/*
 * Permet d'écrire dans un fichier (global buffer) 
 * depuis l'espace utilisateur (buf)
 */
static ssize_t parrot_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{	
	int i;
	// Test les entrées
    if (count == 0) {
        return 0;
    }

    *ppos = 0;

    if (buffer_size != 0) {
		// Libere la mémoire allouée précédemment
        kfree(global_buffer);
    }
	// Alloue de la mémoire pour global_buffer
    global_buffer = kmalloc(count+1, GFP_KERNEL);
	
	printk("Ecriture\n");
	
	// Copier un bloc de données à partir de l'espace utilisateur (buf)
	// dans la mémoire alloué (global buffer)
	if ( copy_from_user(global_buffer, buf, count) != 0) { 
		return 0;
	}
	
	// Modification de 0 à 9 en binaire pour avoir un caractère de 0 à 9
    for (i=0; i<count; ++i){
		if (global_buffer[i] <= 9 ) {
			global_buffer[i] += 48;
		}
	}
    
    global_buffer[count] = '\0';

    buffer_size = count+1;

    return count;
}

/*
 * Permet de changer la configuration de la modification 
 * à effectuer sur le fichier
*/
static long parrot_ioctl(struct file *filep, 
	unsigned int cmd, unsigned long arg)
{	// Test les entrées
    if (buffer_size == 0) {
        return -1;
    }
	// En fonction de la configuration choisis
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

// Structure constante statique pour les différentes opérations 
const static struct file_operations parrot_fops = {
    .owner         = THIS_MODULE,
    .read          = parrot_read,
    .write         = parrot_write,
    .unlocked_ioctl= parrot_ioctl,
};

/*
 * Initialise le module parrot
 */
static int __init parrot_init(void)
{	
	dev_t devno;
	unsigned int count = MY_DEV_COUNT;
	int err;
	
	// Définie le numéro major et mineur
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	// Enregistre l'appareil à caractère avec un nombre de device max
	if ( register_chrdev_region(devno, count , DEVICE_NAME)  != 0 ) {
		printk("register_chrdev_region error\n");
		return -1;
	}

	// Initialiser une structure cdev 
	cdev_init(&my_cdev, &parrot_fops);
	my_cdev.owner = THIS_MODULE;
	// Ajouter le périphérique char au système
	err = cdev_add(&my_cdev, devno, count);

	// Check si il y a une erreur
	if (err < 0)
	{
		printk("Device Add Error\n");
		return -1;
	}
	
    buffer_size = 0;

	printk(KERN_INFO "Parrot ready!\n");
	printk("Execute ceci pour tester :\n");
	printk("'sudo mknod /dev/nodeParrot c %d 0'\n", MAJOR_NUM);
	printk("'sudo chmod 666 /dev/nodeParrot'\n");
    
    return 0;
}

/*
 * Demonte le module parrot
 */ 
static void __exit parrot_exit(void)
{	
	dev_t devno;
	// s'il faut, libère le buffer global
    if(global_buffer != 0) {
        kfree(global_buffer);
    }
   
	// retirer le device du système
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	// Annule l'enregistrement du numéro de l'appareil
	unregister_chrdev_region(devno, MY_DEV_COUNT);
	cdev_del(&my_cdev);

    printk(KERN_INFO "Parrot bye!\n");
}

// Documente le module
MODULE_AUTHOR("REDS");
MODULE_LICENSE("GPL");

// Définies les fonctions lors de l'insértion et du démontage du module
module_init(parrot_init);
module_exit(parrot_exit);
