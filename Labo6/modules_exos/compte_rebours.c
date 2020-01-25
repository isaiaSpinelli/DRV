/*
 * Auteurs : Spinelli Isaia
 * Date : 27.11.19
 * 
 * Amelioration : Il est possible de modifier quelques fonction en 
 * 	ajoutant "devm_" afin que la libération de la mémoire soit faite 
 * 	auomatiquement lors du détachement du pilote ( Ex :devm_kmalloc, 
 *  devm_ioremap_nocache, devm_request_irq)
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

#include <linux/hrtimer.h>
#include <linux/ktime.h>


/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS_spinelli");
/* Indique une description du module*/
MODULE_DESCRIPTION("afficher un compte à rebours sur les afficheurs 7-segments");

/* CONSTANTES */

/* Constantes pour le device char */
#define DEVICE_NAME		"CompteRebours"
#define MAJOR_NUM		100
#define MINOR_NUM 		0
#define MY_DEV_COUNT 	1

/* Tempes initial en seconde du chrono (1h et 15 sec)*/
#define TEMPS_INIT_SEC 	3615

/* Secondes + nanosecondes pour la période du timer */
#define PERIODE_TIMER_SEC 1
#define PERIODE_TIMER_NS  0

/* Bit offsets pour les boutons */
#define KEY0    0x1
#define KEY1    0x2
#define KEY2    0x4
#define KEY3    0x8


/* Offset du registre pour clear les interruptions */
#define CLEAR_INT 	3	
/* Offset du registre pour masquer/demasquer les interruptions */
#define DMASQ_INT 	2	

/* Nombre de clignotement des leds lors de la fin du chrono */
#define LEDS_NB_CLIGN 	3
/* Mask des leds toutes allumées */
#define LEDS_ON		 	0x3FF	


/* Structure permettant de mémoriser les informations importantes pour la gestion du chronomètre */
struct chrono
{	
	/* Temps initial du chrono en seconde */
	unsigned long temps_init;
	/* Valeur du chronomètre */
	unsigned long compteur_chrono ;
	/* Mode du chrono (idle or active) */
	unsigned char active ;

};

/* Structure permettant de mémoriser les informations importantes du module */
struct priv
{	/* Pointeur sur les leds */
    volatile int *LED_ptr;
    /* Pointeur sur les 7 seg 0-3 */
    volatile int *SEG0_3_ptr;
    /* Pointeur sur les 7 seg 4-5 */
    volatile int *SEG4_5_ptr;
    /* Pointeur sur les boutons */
    volatile int *KEY_ptr;
    /* Pointeur sur la base de la mémoire du module*/
    void *MEM_ptr;
    /* Pointeur de structure sur des informations du devices tree */
    struct resource *MEM_info;
    /* Numéro d'interruption du module */
    int IRQ_num;
    /* Structure du timer */
    struct hrtimer hr_timer;
    /* structure du device */
	struct cdev my_cdev;
	/* structure pour la gestion du chrono */
	struct chrono my_chrono;

};


/* Tableau de transformation pour les 7 seg */
static const unsigned char hex_digits[10] = {
    0x3f, // 0
    0x06, // 1
    0x5b, // 2
    0xcf, // 3
    0xe6, // 4
    0xed, // 5
    0xfd, // 6
    0x87, // 7
    0xff, // 8
    0xef, // 9
};
/* Permet d'afficher les minutes et les secondes sur les 7 seg */
void display_time_sec_min(volatile int * ioctrl,
                  uint8_t minutes, uint8_t seconds)
{
	*ioctrl = 0x0 | hex_digits[minutes/10] << 24 | 
					hex_digits[minutes%10] << 16 | 
					hex_digits[seconds/10] << 8 | 
					hex_digits[seconds%10];
}
/* Permet d'afficher les heures sur les 7 seg */
void display_time_heure(volatile int * ioctrl,
                  uint8_t heures)
{
    *ioctrl = 0x0 | hex_digits[heures/10] << 8 | hex_digits[heures%10];
}

enum hrtimer_restart my_hrtimer_callback (struct hrtimer * timer)
{
	static unsigned int compteur_end = 0;
	ktime_t interval;
	/* Récupère la structure privé */
	struct priv *priv_s = from_timer(priv_s, timer, hr_timer);
	
	/* Si le compteur est terminé */
	if (!priv_s->my_chrono.compteur_chrono){
		/* Clignote un certain nombre de fois les leds*/
		*priv_s->LED_ptr = ~(*priv_s->LED_ptr) & LEDS_ON;
		++compteur_end;
		/* Met à jour le chrono */
		if (compteur_end >= 2*LEDS_NB_CLIGN){
			compteur_end=0;
			priv_s->my_chrono.compteur_chrono = priv_s->my_chrono.temps_init;
		}
	} else { /* Sinon, décrémente le chrono */
		priv_s->my_chrono.compteur_chrono--;
	}
	
	/* Met à jour les afficheurs 7 seg */
	display_time_sec_min( priv_s->SEG0_3_ptr, (priv_s->my_chrono.compteur_chrono/60)%60 , priv_s->my_chrono.compteur_chrono % 60  ); //
	display_time_heure( priv_s->SEG4_5_ptr, priv_s->my_chrono.compteur_chrono/3600);
	
	/* Reconfigure le timer (periode = 1 secondes) */
	interval = ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS);
	hrtimer_start( &priv_s->hr_timer, interval, HRTIMER_MODE_REL );
	
	return HRTIMER_RESTART;
}


/* 
 * Ref : https://linux-kernel-labs.github.io/master/labs/device_drivers.html
 */
static int compte_open(struct inode* node, struct file * f)
{
	/* Récupération des informations du module (structure privée) */
    struct priv *priv_s ;
    priv_s = container_of(node->i_cdev, struct priv, my_cdev);
    /* Placement de la structure privée dans les data du fichier */
    f->private_data = priv_s;
    
    return 0;
	
}



/* Une lecture permettra de récupérer le temps écoulé dans l’espace utilisateur */
static ssize_t
compte_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
    struct priv *priv_s ;
	char TempsEcouleStr[128] = "";
	/* Calcul le temps ecoulé */
	long temps_ecoule ; 
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
	/* Met à jour le temps ecoulé */
	temps_ecoule = priv_s->my_chrono.temps_init - priv_s->my_chrono.compteur_chrono;
	
	/* Transforme en string le temps écoulé */
	sprintf(TempsEcouleStr, "%lu", temps_ecoule);
	
	/* Copie le temps écoulé en string dans le buf dans l'espace user */
	if ( copy_to_user(buf, TempsEcouleStr, sizeof(TempsEcouleStr)) != 0 ) {
		return 0;
	}
	
	// màj de la position
    *ppos = sizeof(TempsEcouleStr);
	
    return *ppos;
}

/* Une écriture sur le device node associé permettra de choisir un temps initial (en secondes) */
static ssize_t
compte_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
	const int DECIMAL = 10;
	char * temps_init_str;
	unsigned long temps_init_s = 0;
	int rc = -1;
	struct priv *priv_s ;
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
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
	priv_s->my_chrono.temps_init = temps_init_s;
	
	printk(KERN_DEBUG "%s\n", temps_init_str);
	
    return count;
}



const static struct
file_operations compte_fops = {
    .owner         = THIS_MODULE,
    .read          = compte_read,
    .open          = compte_open,
    .write         = compte_write,
};


/* Fonction de handler appelée lors d'une interruption  
 * Ref : https://git.sphere.ly/santhosh/kernel_cyanogen_msm8916/commit/ad53743afc04e4c2f24e06c54ed29c123e9c7cc0#2825dc24cfc91f16d3e2e9520e961dcb464759c3 
 */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)

{	/* Récupération des informations du module préparées dans le fonction probe (request_irq) */
    struct priv *priv = (struct priv *) dev_id;
    
    /* Récupère quel bouton a été pressé */
    unsigned int valKey = *(priv->KEY_ptr + CLEAR_INT);

    /* Pression sur le bouton gauche (demarre ou stop le compte a rebours) */
    if (valKey >= KEY3) {
		/* Si le timer est actif*/
		if (priv->my_chrono.active){
			/* Stop le timer */
			hrtimer_cancel(&priv->hr_timer);
			priv->my_chrono.active = 0;
		} else {
			/* Sinon restart le timer*/
			hrtimer_start(&priv->hr_timer, ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS), HRTIMER_MODE_REL_PINNED);
			priv->my_chrono.active = 1;
		}
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY3;
	} else if (valKey == KEY0) { /* Pression sur le bouton droite  */
		/* réinitialiser le compteur à sa valeur initiale */
		priv->my_chrono.compteur_chrono = priv->my_chrono.temps_init;
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY0;
	} else { 
		/* Clear les autres interruptions */
		*(priv->KEY_ptr + CLEAR_INT) = KEY1 | KEY2;
	}
    
	
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
    
    ktime_t ktime;
    
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
	/* Eteint toute les leds */
    *(priv->LED_ptr) = 0x000;
    
    /* Met à jour l'adresse des segments via l'adresse mappée du module*/
    priv->SEG0_3_ptr = priv->MEM_ptr + HEX3_HEX0_BASE;
    
    /* Met à jour l'adresse des segments via l'adresse mappée du module*/
    priv->SEG4_5_ptr = priv->MEM_ptr + HEX5_HEX4_BASE;
	
	/* Met à jour l'adresse des boutons via l'adresse mappée du module*/
    priv->KEY_ptr = priv->MEM_ptr + KEY_BASE;
	/* Clear les interruptions */
    *(priv->KEY_ptr + CLEAR_INT) = 0xF;
	/* Démasque les intérruption du module */
    *(priv->KEY_ptr + DMASQ_INT) = 0xF;

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
	printk(KERN_INFO "sudo mknod /dev/nodeCompte c %d 0 \n", MAJOR_NUM);
	printk(KERN_INFO "sudo chmod 666 /dev/nodeCompte\n");
    
 

    /* Init des variables du chrono */
    priv->my_chrono.temps_init = TEMPS_INIT_SEC;
    priv->my_chrono.compteur_chrono = priv->my_chrono.temps_init;
    
    printk(KERN_INFO "Timer module installing\n");

	/* Initilisation du timer (periode = 1 secondes) */
	ktime = ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS);
	
	hrtimer_init( &priv->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	
	priv->hr_timer.function = &my_hrtimer_callback;
	
	hrtimer_start( &priv->hr_timer, ktime, HRTIMER_MODE_REL );
	priv->my_chrono.active = 1;
 
    
    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 register_chrdev_region_fail:
 cdev_add_fail:
 request_irq_fail:
 get_irq_fail:
	/* Démappage de l'adresse virtuelle */
    iounmap(priv->MEM_ptr);
 ioremap_fail:
 get_mem_fail:
	/* Libère la mémoire alloué */
    kfree(priv);
 kmalloc_fail:
	/* Retourne le numéro de l'erreur */
    return rc;
}

/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int compte_remove(struct platform_device *pdev)
{	
	int rc;
    dev_t devno;
    
    /* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
    
    /* Supprime le timer */
    rc = hrtimer_cancel( &priv->hr_timer );
	if (rc) printk("The timer was still in use...\n");
	
	/* Retirer le device du système */
	devno = MKDEV(MAJOR_NUM, MINOR_NUM);
	/* Annule l'enregistrement du numéro de l'appareil*/
	unregister_chrdev_region(devno, MY_DEV_COUNT);
	cdev_del(&priv->my_cdev);
	

	/* éteint les leds du module */
    *(priv->LED_ptr) = 0;
    
    /* éteint les 7 seg du module */
    *(priv->SEG0_3_ptr) = 0x0;
	*(priv->SEG4_5_ptr) = 0x0;
    
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
