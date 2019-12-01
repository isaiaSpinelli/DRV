/*
 * Auteurs : Spinelli Isaia
 * Date : 27.11.19
 * 
 * Amelioration : devm_kmalloc()
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

/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS_spinelli");
/* Indique une description du module*/
MODULE_DESCRIPTION("afficher un compte à rebours sur les afficheurs 7-segments");

/* Constantes */
#define DEVICE_NAME		"CompteRebours"
#define MAJOR_NUM		100
#define MINOR_NUM 		0
#define MY_DEV_COUNT 	1

/* Tmpes initial en seconde du chrono */
#define TEMPS_INIT_SEC 	65

/* structure du device */
struct cdev my_cdev;

/* Structure pour le timer*/
static struct timer_list my_timer;

/* Temps initial du chrono en seconde */
unsigned long temps_init;
/* Valeur du chronomètre */
unsigned long compteur_chrono ;

void my_timer_callback( unsigned long data )
{
	int rc = -1;
	
	printk( "my_timer_callback called (%ld).\n", jiffies );
	rc = mod_timer( &my_timer, jiffies + msecs_to_jiffies(1000) );
	if (rc) printk("Error in mod_timer\n");
	//compteur_chrono--;
}




/* Une lecture permettra de récupérer le temps écoulé dans l’espace utilisateur */
static ssize_t
compte_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
	
	char TempsEcouleStr[128] = "";
	/* Calcul le temps ecoulé */
	long temps_ecoule ; 
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
	
	temps_ecoule = temps_init - compteur_chrono;
	

	/* Transforme en string le temps écoulé */
	sprintf(TempsEcouleStr, "%lu", temps_ecoule);
	/*
	if ( (rc = kstrtol(TempsEcouleStr, DECIMAL, &temps_ecoule)) != 0)
		return -rc;
	*/
	
	/* Copie le temps écoulé en string dans le buf dans l'espace user */
	if ( copy_to_user(buf, TempsEcouleStr, sizeof(TempsEcouleStr)) != 0 ) {
		return 0;
	}
	
	// màj de la position
    *ppos = sizeof(TempsEcouleStr);
	
	compteur_chrono--;
	
    return 6;
}

/* Une écriture sur le device node associé permettra de choisir un temps initial (en secondes) */
static ssize_t
compte_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
	const int DECIMAL = 10;
	char * temps_init_str;
	unsigned long temps_init_s = 5;
	int rc = -1;
	
	// Test les entrées
    if (count == 0) {
        return 0;
    }
	
	// Alloue de la mémoire pour kernel_buffer
    temps_init_str = kmalloc(count+1, GFP_KERNEL);
    
    // Copier un bloc de données à partir de l'espace utilisateur (buf)
	// dans la mémoire alloué ( kernel_buffer)
	if ( copy_from_user(temps_init_str, buf, count) != 0) { 
		return 0;
	}
	
	temps_init_str[count] = '\0';
	
	/* Transforme le string en unsigned long pour le temps inital */
	if ( (rc = kstrtoul(temps_init_str, DECIMAL, &temps_init_s)) != 0) {
		printk(KERN_ERR "\nrecu : %lu\n", temps_init_s);
		return -rc;
	}
		
		
	/* Met à jour le temps initial en seconde recu par une écriture dans le node du module*/
	temps_init = temps_init_s;
	
	printk(KERN_DEBUG "%s\n", temps_init_str);
	
    return count;
}



const static struct
file_operations compte_fops = {
    .owner         = THIS_MODULE,
    .read          = compte_read,
    .write         = compte_write,
};


/* Structure permettant de mémoriser les informations importantes du module */
struct priv
{	/* Pointeur sur les leds */
    volatile int *LED_ptr;
    /* Pointeur sur les boutons */
    volatile int *KEY_ptr;
    /* Pointeur sur la base de la mémoire du module*/
    void *MEM_ptr;
    /* Pointeur de structure sur des informations du devices tree */
    struct resource *MEM_info;
    /* Numéro d'interruption du module */
    int IRQ_num;
};

/* Fonction de handler appelée lors d'une interruption  */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{	/* Récupération des informations du module préparées dans le fonction probe (request_irq) */
    struct priv *priv = (struct priv *) dev_id;
    unsigned int valKey = *(priv->KEY_ptr + 3);
	
    printk(KERN_NOTICE "Button pushed! %d\n", valKey);
    
    /* Pression sur le bouton gauche (demarre ou stop le compte a rebours) */
    if (valKey >= 8) {
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + 3) = 0x8;
	} else if (valKey == 1) { /* Pression sur le bouton droite  */
		/* réinitialiser le compteur à sa valeur initiale */
		compteur_chrono = temps_init;
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + 3) = 0x1;
	} else { 
		
		/* Clear les autres interruptions */
		*(priv->KEY_ptr + 3) = 0x6;
	}
    
    
	/* Incrémentation des leds */
    *(priv->LED_ptr) = *(priv->LED_ptr) + 1;
	
	
	/* Indique que l'interruption est traitée */
    return (irq_handler_t) IRQ_HANDLED;
}

/* Fonction probe appelée lors du branchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device détécté) */
static int compte_probe(struct platform_device *pdev)
{	/* Déclaration de la structure priv */
    struct priv *priv;
	/* Valeur de retour */
    int rc;
    
    dev_t devno;
	unsigned int count = MY_DEV_COUNT;
    
    
    /* Définie le numéro major et mineur*/
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Enregistre l'appareil à caractère avec un nombre de device max */
	if ( (rc = register_chrdev_region(devno, count , DEVICE_NAME))  != 0 ) {
		rc = -rc;
		printk(KERN_ERR "register_chrdev_region error%d\n", rc);
		goto register_chrdev_region_fail;
	}

	/* Initialiser une structure cdev */
	cdev_init(&my_cdev, &compte_fops);
	my_cdev.owner = THIS_MODULE;
	/* Ajouter le périphérique char au système */
	rc = cdev_add(&my_cdev, devno, count);

	/* Check si il y a une erreur */
	if (rc < 0)
	{
		printk(KERN_ERR "Device Add Error\n");
		goto cdev_add_fail;
	}
	
	printk(KERN_INFO "Execute ceci pour tester :\n");
	printk(KERN_INFO "sudo mknod /dev/nodeCompte c %d 0 \n", MAJOR_NUM);
	printk(KERN_INFO "sudo chmod 666 /dev/nodeCompte\n");
    
    
    
    
    
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
	/* Récupère l'adresse de base de la mémoire du module depuis la dtb*/
    priv->MEM_info = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* Si l'adresse récupéré est fausse */
    if (priv->MEM_info == NULL) {
        printk(KERN_ERR "Failed to get memory resource from device tree!\n");
		/* Met à jour l'erreur (Invalid argument)*/
        rc = -EINVAL;
		/* Fin de la fonction avec free de la structure priv */
        goto get_mem_fail;
    }
	
	/* Récupère une adresse virtuelle en mappant avec l'adresse du module et sa taille */
    priv->MEM_ptr = ioremap_nocache(priv->MEM_info->start,
                                    priv->MEM_info->end - priv->MEM_info->start);
	/* Si le mappage c'est mal passé */									
    if (priv->MEM_ptr == NULL) {
        printk(KERN_ERR "Failed to map memory!\n");
		/* Met à jour l'erreur (Invalid argument)*/
        rc = -EINVAL;
		/* Fin de la fonction avec free de la structure priv et le retour de l'erreur */
        goto ioremap_fail;
    }
	
	/* Met à jour l'adresse des leds via l'adresse mappée du module*/
    priv->LED_ptr = priv->MEM_ptr + LEDR_BASE;
	/* Allume la led 9 */
    *(priv->LED_ptr) = 0x200;
	
	/* Met à jour l'adresse des boutons via l'adresse mappée du module*/
    priv->KEY_ptr = priv->MEM_ptr + KEY_BASE;
	/* Clear les interruptions */
    *(priv->KEY_ptr + 3) = 0xF;
	/* Démasque les intérruption du module */
    *(priv->KEY_ptr + 2) = 0xF;

    printk(KERN_INFO "Registering interrupt...\n");
	
	/* Récupère le numéro d'interruption du module dans la dtb*/
    priv->IRQ_num = platform_get_irq(pdev, 0);
	/* Si la récupération du numéro d'interruption à echoué */
    if (priv->IRQ_num < 0) {
        printk(KERN_ERR "Failed to get interrupt resource from device tree!\n");
		/* Met à jour l'erreur avec l'erreur coresspondante au retour de la fonction platform_get_irq() */
        rc = priv->IRQ_num;
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto get_irq_fail;
    }
	
	/* Déclare un gestionnaire d'interruptions avec le numéro d'irq du module (récupére la valeur de retour)*/
    rc = request_irq(priv->IRQ_num,
					 /* Fonction handler (gestionnaire) appelée lors d'une interruption */
                     (irq_handler_t) irq_handler,
					 /* Ce bit indique que le gestionnaire supporte le partage de l'interruption avec d'autres gestionnaires */
                     IRQF_SHARED,
					 /* Nom court pour le périphérique qui est affiché dans la liste /proc/interrupts */
                     "pushbutton_irq_handler",
					 /* Pointeur sur la structure d'information du module qui sera utilisée dans le handler */
                     (void *) priv);
	/* Si la fonction request_irq() à echoué */
    if (rc != 0) {
        printk(KERN_ERR "pushbutton_irq_handler: cannot register IRQ %d\n",
               priv->IRQ_num);
        printk(KERN_ERR "error code: %d\n", rc);
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto request_irq_fail;
    }

    printk(KERN_INFO "Interrupt registered!\n");
    
    /* Init des variables du chrono */
    temps_init = TEMPS_INIT_SEC;
    compteur_chrono = temps_init;
    
    printk(KERN_INFO "Timer module installing\n");

	/* Initilisation du timer (periode = 1 secondes) */
	setup_timer( &my_timer, my_timer_callback, 0 );
	rc = mod_timer( &my_timer, jiffies + msecs_to_jiffies(1000) );
	/* Si l'init du timer échoue */
    if (rc != 0) {
        printk(KERN_ERR "Init timer failed\n");
        printk(KERN_ERR "error code: %d\n", rc);
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto init_timer_fail;
    }
    
    
    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 init_timer_fail:
 request_irq_fail:
 get_irq_fail:
	/* Démappage de l'adresse virtuelle */
    iounmap(priv->MEM_ptr);
 ioremap_fail:
 get_mem_fail:
	/* Libère la mémoire alloué */
    kfree(priv);
 kmalloc_fail:
 cdev_add_fail:
 register_chrdev_region_fail:
	/* Retourne le numéro de l'erreur */
    return rc;
}

/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int compte_remove(struct platform_device *pdev)
{	/* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
    
    int rc;
    
    dev_t devno;
    
    rc = del_timer( &my_timer );
	if (rc != 0) 
		printk(KERN_ERR "delete timer failed ! \n");


	
	/* Retirer le device du système */
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Annule l'enregistrement du numéro de l'appareil*/
	unregister_chrdev_region(devno, MY_DEV_COUNT);
	cdev_del(&my_cdev);
	
	
	
	
	
	
	/* éteint les leds du module */
    *(priv->LED_ptr) = 0;
	/* Démappage de l'adresse virtuelle */
    iounmap(priv->MEM_ptr);
	/* Supprime le gestionnaire d'interruption précèdemment déclaré */
    free_irq(priv->IRQ_num, (void*) priv);
	/* Libère la mémoire alloué */
    kfree(priv);
	/* Retoune 0 */
    return 0;
}

/* Tableau de structure of_device_id qui permet de déclarer les compatibiltés des périphériques gérés par ce module */
static const struct of_device_id pushbutton_driver_id[] = {
	/* Déclare la compatibilité du périphérique géré par ce module */
    { .compatible = "drv" },
    { /* END */ },
};

/* Cette macro décrit les périphériques que le module peut prendre en charge (recherche de la dtb)*/
MODULE_DEVICE_TABLE(of, pushbutton_driver_id);

/* Structure permettant  de représenter le driver */
static struct platform_driver pushbutton_driver = {
	/* Déclare le nom, le module et la table des compatibilités du module*/
    .driver = {
        .name = "drv-lab4",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pushbutton_driver_id),
    },
	/* Définit la fonction probe appelée lors du branchement d'un préiphérique pris en charge par ce module */
    .probe = compte_probe,
	/* Définit la fonction remove appelée lors du débranchement d'un préiphérique pris en charge par ce module */
    .remove = compte_remove,
};

/* Macro d'assistance pour les pilotes qui ne font rien de spécial dans les fonctions init / exit. Permet d'éliminer du code (fonction init et exit) */
/* Enregistre le driver via la structure ci-dessus */
module_platform_driver(pushbutton_driver);
