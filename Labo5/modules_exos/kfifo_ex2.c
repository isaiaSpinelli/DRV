/*
 * Auteurs : Spinelli Isaia
 * Date : 07.12.19
 * 
 * 
 */
 
 
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

#include <linux/fs.h>           /* Needed for file_operations */

/* For cdev fonctions */
#include <linux/cdev.h>

#include <linux/kfifo.h>


/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS_spinelli");
/* Indique une description du module*/
MODULE_DESCRIPTION("Maintien une kfifo avec des nombres premiers");

/* CONSTANTES */

/* Constantes pour le device char */
#define DEVICE_NAME		"Kfifo_ex2"
#define MAJOR_NUM		101
#define MINOR_NUM 		0
#define MY_DEV_COUNT 	1

/* Delcare la kfifo */
static DECLARE_KFIFO_PTR(my_kfifo, int);
/* Init le nombre de nombre premier dans la kfifo*/
static unsigned int N = 50;

/* Tableau des 50 premiers nombres premiers */
static const int NOMBRES_PREMIER[] = {
	 2,  3,  5, 7, 11, 13, 17, 19, 23, 29,
	 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 
	 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 
	 127,131, 137, 139, 149, 151, 157, 163, 167, 173,
	179, 181, 191, 193, 197, 199, 211, 223, 227, 229
};
/* Permet de récupèrer un paramètre lors de l'insertion du module */
module_param(N, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(N, "Les premiers N nombres premiers (N <= 50)");

/* Structure permettant de mémoriser les informations importantes du module */
struct priv
{
    /* structure du device */
	struct cdev my_cdev;

};

/* Remplie la fifo avec les nombres premiers souhaités */
void fill_kfifo(unsigned int NB){
	int i;
	for (i = 0; i < NB; i++){
		kfifo_put(&my_kfifo, NOMBRES_PREMIER[i]);
	}
}

static int kfifo_open(struct inode* node, struct file * f)
{
	/* Récupération des informations du module (structure privée) */
    struct priv *priv_s ;
    priv_s = container_of(node->i_cdev, struct priv, my_cdev);
    /* Placement de la structure privée dans les data du fichier */
    f->private_data = priv_s;
    
    return 0;
	
}

/* Une lecture permettra de récupérer le nombre premier dans la kfifo*/
static ssize_t
kfifo_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
	int rc = -1;
	int nombre_premier[1];
	char nb_premier_str[4] = "";
	
    struct priv *priv_s ;
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
	/* Test les entrées */
    if (buf == 0 || count < 2) {
        return 0;
    }
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
	
	/* Récuère le nombre premier suivant dans la kfifo*/
	rc = kfifo_out(&my_kfifo, nombre_premier, 1);
	
	/* Transforme en string le nombre premier */
	sprintf(nb_premier_str, "%u", nombre_premier[0]);
	
	/* Copie nombre premier en string dans le buf dans l'espace user */
	if ( copy_to_user(buf, nb_premier_str, sizeof(nb_premier_str)) != 0 ) {
		return 0;
	}
	
	/* Si la kfifo est vide, la remplie*/
	if (kfifo_is_empty(&my_kfifo)) {
		fill_kfifo(N);
	}
	
	// màj de la position
	*ppos = sizeof(nb_premier_str);
	
    return *ppos;
}

/*
static ssize_t
kfifo_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
	struct priv *priv_s ;

	priv_s = (struct priv *) filp->private_data;
	
	// Test les entrées
    if (count == 0) {
        return 0;
    }
	
	
    return count;
}
*/



const static struct
file_operations compte_fops = {
    .owner         = THIS_MODULE,
    .read          = kfifo_read,
    .open          = kfifo_open,
//    .write         = kfifo_write,
};


/* Fonction probe appelée lors du branchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device détécté) */
static int kfifo_probe(struct platform_device *pdev)
{	/* Déclaration de la structure priv */
    struct priv *priv;
	/* Valeur de retour */
    int rc;
    int i;
    
    dev_t devno;
	unsigned int count = MY_DEV_COUNT;
    
	/* Allocation mémoire kernel pour la structure priv (informations du module)*/
    priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	/* Si la fonction kmallos à échoué */
    if (priv == NULL) {
        printk(KERN_ERR "Failed to allocate memory for private data!\n");
		/* Met à jour la valeur de retour (Out of memory)*/
        rc = -ENOMEM;
		/* Fin de la fonction et retourne rc */
        goto kmalloc_fail;
    }
	/* Lis le pointeur de la structure contenant les informations du module au platform_device coresspondant (permet de le get dans la fonction remove)*/
    platform_set_drvdata(pdev, priv);

    
    /* Définie le numéro major et mineur*/
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Enregistre l'appareil à caractère avec un nombre de device max */
	if ( (rc = register_chrdev_region(devno, count , DEVICE_NAME))  != 0 ) {
		rc = -rc;
		printk(KERN_ERR "register_chrdev_region error%d\n", rc);
		goto register_chrdev_region_fail;
	}

	/* Initialiser une structure cdev */
	cdev_init(&priv->my_cdev, &compte_fops);
	priv->my_cdev.owner = THIS_MODULE;
	/* Ajouter le périphérique char au système */
	rc = cdev_add(&priv->my_cdev, devno, count);

	/* Check si il y a une erreur */
	if (rc < 0)
	{
		printk(KERN_ERR "Device Add Error\n");
		goto cdev_add_fail;
	}
	
	printk(KERN_INFO "Execute ceci pour tester :\n");
	printk(KERN_INFO "sudo mknod /dev/nodeKfifo c %d 0 \n", MAJOR_NUM);
	printk(KERN_INFO "sudo chmod 666 /dev/nodeKfifo\n");
	
	/* Test du paramètre */
	if (N > 50 || N < 0){
		printk(KERN_ERR "Error, N est pas dans le range ( 0 < %d > 50 )\n", N);
		N = 50;
	}
	printk(KERN_INFO "N = %d\n", N);
    

    /* Init la kfifo */
    rc = kfifo_alloc(&my_kfifo, N, GFP_KERNEL);
	if (rc) {
		rc = -rc;
		printk(KERN_ERR "error kfifo_alloc (%d)\n", rc);
		goto kfifo_alloc_fail;
	}
    
    /* Remplie la fifo avec les nombres premiers souhaités */
	for (i = 0; i < N; i++){
		kfifo_put(&my_kfifo, NOMBRES_PREMIER[i]);
	}


    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 kfifo_alloc_fail:
 register_chrdev_region_fail:
 cdev_add_fail:
	/* Libère la mémoire alloué */
    kfree(priv);
 kmalloc_fail:
	/* Retourne le numéro de l'erreur */
    return rc;
}

/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int kfifo_remove(struct platform_device *pdev)
{	
    dev_t devno;
    
    /* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
    
    /* Libre la kfifo allouée */
    kfifo_free(&my_kfifo);
    
	
	/* Retirer le device du système */
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Annule l'enregistrement du numéro de l'appareil*/
	unregister_chrdev_region(devno, MY_DEV_COUNT);
	cdev_del(&priv->my_cdev);
	

	/* Libère la mémoire alloué */
    kfree(priv);
	/* Retoune 0 */
    return 0;
}

/* Tableau de structure of_device_id qui permet de déclarer les compatibiltés des périphériques gérés par ce module */
static const struct of_device_id kfifo_driver_id[] = {
	/* Déclare la compatibilité du périphérique géré par ce module */
    { .compatible = "drv" },
    { /* END */ },
};

/* Cette macro décrit les périphériques que le module peut prendre en charge (recherche de la dtb)*/
MODULE_DEVICE_TABLE(of, kfifo_driver_id);

/* Structure permettant  de représenter le driver */
static struct platform_driver kfifo_driver = {
	/* Déclare le nom, le module et la table des compatibilités du module*/
    .driver = {
        .name = "drv-lab4",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(kfifo_driver_id),
    },
	/* Définit la fonction probe appelée lors du branchement d'un préiphérique pris en charge par ce module */
    .probe = kfifo_probe,
	/* Définit la fonction remove appelée lors du débranchement d'un préiphérique pris en charge par ce module */
    .remove = kfifo_remove,
};

/* Macro d'assistance pour les pilotes qui ne font rien de spécial dans les fonctions init / exit. Permet d'éliminer du code (fonction init et exit) */
/* Enregistre le driver via la structure ci-dessus */
module_platform_driver(kfifo_driver);
