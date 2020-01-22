/*
 * Auteurs : Spinelli Isaia
 * Date : 24.12.19
 * 
 */
 
#include "Aff_num.h"

/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS institute, HEIG-VD, Yverdon-les-Bains Spinelli");
/* Indique une description du module*/
MODULE_DESCRIPTION("Utilise les afficheurs 7-segments pour afficher les numéros (entre 0x000000 et 0xFFFFFF) donnés par l’utilisateur en utilisant un device node.");
MODULE_VERSION(AFF_NUM_DRIVER_VERSION);

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
        .name = "drv-lab6",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pushbutton_driver_id),
    },
	/* Définit la fonction probe appelée lors du branchement d'un préiphérique pris en charge par ce module */
    .probe = Aff_FIFO_probe,
	/* Définit la fonction remove appelée lors du débranchement d'un préiphérique pris en charge par ce module */
    .remove = Aff_FIFO_remove,
};

const static struct
file_operations FIFO_fops = {
    .owner         = THIS_MODULE,
    .read          = FIFO_read,
    .open          = FIFO_open,
    .write         = FIFO_write,
};

const static struct
file_operations SIZE_fops = {
    .owner         = THIS_MODULE,
    .read          = SIZE_read,
    .open          = SIZE_open,
};

/* Macro d'assistance pour les pilotes qui ne font rien de spécial dans les fonctions init / exit. Permet d'éliminer du code (fonction init et exit) */
/* Enregistre le driver via la structure ci-dessus */
module_platform_driver(pushbutton_driver);



/* Permet d'afficher 4 chiffres sur les 4 premiers 7 seg */
void display_Seg0_3(volatile int * ioctrl,
                  char num4, char num3, char num2, char num1)
{
	*ioctrl = 0x0 | hex_digits[num4 - '0'] << 24 | 
					hex_digits[num3 - '0'] << 16 | 
					hex_digits[num2 - '0'] << 8 | 
					hex_digits[num1 - '0'];
}
/* Permet d'afficher 2 chiffre sur les 2 derniers 7 seg */
void display_Seg4_5(volatile int * ioctrl,
                   char num6, char num5)
{
    *ioctrl = 0x0 | hex_digits[num6 - '0'] << 8 | hex_digits[num5 - '0'];
}

enum hrtimer_restart my_hrtimer_callback (struct hrtimer * timer)
{
	ktime_t interval;
	/* Récupère la structure privé */
	struct priv *priv_s = from_timer(priv_s, timer, hr_timer);
	
	
	printk(KERN_INFO "my_hrtimer_callback\n");
	
	/* Reconfigure le timer (periode = 1 secondes) */
	interval = ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS);
	hrtimer_start( &priv_s->hr_timer, interval, HRTIMER_MODE_REL );
	
	return HRTIMER_RESTART;
}


static int SIZE_open(struct inode* node, struct file * f)
{
	/* Récupération des informations du module (structure privée) */
    struct priv *priv_s ;
    priv_s = container_of(node->i_cdev, struct priv, my_cdev);
    /* Placement de la structure privée dans les data du fichier */
    f->private_data = priv_s;
    
    return 0;
}



/* */
static ssize_t
SIZE_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
    struct priv *priv_s ;
	
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
	printk(KERN_ERR "SIZE_read\n");
	
    return *ppos;
}


/* 
 * Ref : https://linux-kernel-labs.github.io/master/labs/device_drivers.html
 */
static int FIFO_open(struct inode* node, struct file * f)
{
	/* Récupération des informations du module (structure privée) */
    struct priv *priv_s ;
    priv_s = container_of(node->i_cdev, struct priv, my_cdev);
    /* Placement de la structure privée dans les data du fichier */
    f->private_data = priv_s;
    
    return 0;
}



/* */
static ssize_t
FIFO_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
    struct priv *priv_s ;
	
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
	printk(KERN_ERR "FIFO_read\n");
	
    return *ppos;
}

/*  */
static ssize_t
FIFO_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
	struct priv *priv_s ;
	/* Récupération des informations du module (structure privée) */
	priv_s = (struct priv *) filp->private_data;
	
	// Test les entrées
    if (count == 0) {
		pr_err("Empty write requested!\n");
        return 0;
    }
	printk(KERN_ERR "FIFO_write\n");
	
    return count;
}






/* Fonction de handler appelée lors d'une interruption */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{	
	int valTailleKfifo;
	
	/* Récupération des informations du module préparées dans le fonction probe (request_irq) */
    struct priv *priv = (struct priv *) dev_id;
    
    /* Récupère quel bouton a été pressé */
    unsigned int valKey = *(priv->KEY_ptr + CLEAR_INT);

    if (valKey == KEY3) { /* ---------------------Pression sur KEY3  
		lecture des numéros qui se trouvent dans la FIFO ou 
		* compte le NB de pression */
		
		
		printk(KERN_ERR "KEY3\n");
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY3;
	} else if (valKey == KEY0) { /* -----Pression sur le bouton KEY0  
		Changement de la taille de la kfifo */
		
		/* Lecture de la valeur des switches ( SW0 - SW5 ) */
		valTailleKfifo = ( *(priv->SW_ptr) & 0x3F ) + 1;
		
		printk(KERN_ERR "KEY0 sw : %d\n", valTailleKfifo );
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY0;

	} else if (valKey == KEY1) { /* -------------------Pression sur KEY1  
		reset globale */
		
		
		printk(KERN_ERR "KEY1\n");
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY1;
	} else if (valKey == KEY2) { /* -------------------Pression sur KEY2 
		L’affichage est interrompu */
		
		
		printk(KERN_ERR "KEY2\n");
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY2;
	} else { 
		printk(KERN_ERR "Autre\n");
		/* Clear les autres interruptions */
		*(priv->KEY_ptr + CLEAR_INT) = 0xFFFFFFFF;
	}
    
	
	/* Indique que l'interruption est traitée */
    return (irq_handler_t) IRQ_HANDLED;
}

/* Fonction probe appelée lors du branchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device détécté) */
static int Aff_FIFO_probe(struct platform_device *pdev)
{	/* Déclaration de la structure priv */
    struct priv *priv;
	/* Valeur de retour */
    int rc;
    int valTailleKfifo;
    ktime_t ktime;
    
    
	/* Allocation mémoire kernel pour la structure priv (informations du module)*/
    priv = devm_kzalloc(&pdev->dev,
		sizeof(*priv),
		GFP_KERNEL);
		
	/* Si la fonction kmallos à échoué */
    if (priv == NULL) {
        pr_err("Failed to allocate memory for private data!\n");
		/* Retourne (Out of memory)*/
        return -ENOMEM;
    }
    
	/* Lis le pointeur de la structure contenant les informations du module au platform_device coresspondant (permet de le get dans la fonction remove)*/
    platform_set_drvdata(pdev, priv);
    
    
    
    /*--------------ENREGISTREMENT DES ADRESSES UTILES----------------*/
    
	/* Récupère l'adresse de base de la mémoire du module depuis la dtb*/
    priv->MEM_info = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	/* Si l'adresse récupéré est fausse */
    if (priv->MEM_info == NULL) {
        pr_err("Failed to get memory resource from device tree!\n");
		/* Retourne (Invalid argument)*/
        return -EINVAL;
    }
	
	/* Récupère une adresse virtuelle en mappant avec l'adresse du module et sa taille */
    priv->MEM_ptr = ioremap_nocache(priv->MEM_info->start,
                                    priv->MEM_info->end - priv->MEM_info->start);
	/* Si le mappage c'est mal passé */									
    if (priv->MEM_ptr == NULL) {
        pr_err("Failed to map memory!\n");
		/* Retourne (Invalid argument)*/
        return -EINVAL;
    }
	
	/* Met à jour l'adresse des leds via l'adresse mappée du module*/
    priv->LED_ptr = priv->MEM_ptr + LEDR_BASE;
	/* Eteint toute les leds */
    *(priv->LED_ptr) = 0x000;
    
    /* Met à jour l'adresse des segments via l'adresse mappée du module*/
    priv->SEG0_3_ptr = priv->MEM_ptr + HEX3_HEX0_BASE;
    priv->SEG4_5_ptr = priv->MEM_ptr + HEX5_HEX4_BASE;
	
	/* Met à jour l'adresse des boutons via l'adresse mappée du module*/
    priv->KEY_ptr = priv->MEM_ptr + KEY_BASE;
	/* Clear les interruptions */
    *(priv->KEY_ptr + CLEAR_INT) = 0xF;
	/* Démasque les intérruption du module */
    *(priv->KEY_ptr + DMASQ_INT) = 0xF;
    
    /* Met à jour l'adresse des switches via l'adresse mappée du module*/
    priv->SW_ptr = priv->MEM_ptr + SW_BASE;
    
    /* Init de la taille de la kfifo ( SW0 - SW5 ) */
	valTailleKfifo = ( *(priv->SW_ptr) & 0x3F ) + 1;
	printk(KERN_INFO "KEY0 sw : %d\n", valTailleKfifo ); 
		
		
	/*--------------ENREGISTREMENT DE L'INTERRUPTION------------------*/
	
	
	/* Récupère le numéro d'interruption du module dans la dtb*/
    priv->IRQ_num = platform_get_irq(pdev, 0);
	/* Si la récupération du numéro d'interruption à echoué */
    if (priv->IRQ_num < 0) {
        pr_err("Failed to get interrupt resource from device tree!\n");
		/* Met à jour l'erreur avec l'erreur coresspondante au retour de la fonction platform_get_irq() */
        rc = priv->IRQ_num;
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto get_irq_fail;
    }
    
	/* Déclare un gestionnaire d'interruptions avec le numéro d'irq du module (récupére la valeur de retour)*/
    rc = devm_request_irq(&pdev->dev,
					 priv->IRQ_num,
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
        pr_err("pushbutton_irq_handler: cannot register IRQ %d (err=%d)\n",
               priv->IRQ_num, rc);
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto request_irq_fail;
    }
    
    
    
    
    /*------------ENREGISTREMENT DES CARACTERES DEVICES---------------*/
    
    /* Définie le numéro major et mineurs */
	priv->dev_fifo = MKDEV(MAJOR_NUM, MINOR_NUM_0);
	priv->dev_read_size = MKDEV(MAJOR_NUM, MINOR_NUM_1);
	
	/* Enregistre les devices à caractère avec un nombre de device max */
	if ( (rc = register_chrdev_region(priv->dev_fifo, MY_DEV_COUNT , DEVICE_NAME))  != 0 ) {
		pr_err("register_chrdev_region error %d\n", rc);
		goto register_chrdev_region_fail;
	}

	/* Initialise les structures cdev */
	cdev_init(&priv->my_cdev, &FIFO_fops);
	priv->my_cdev.owner = THIS_MODULE;
	cdev_init(&priv->my_cdev_read, &SIZE_fops);
	priv->my_cdev_read.owner = THIS_MODULE;
	
	/* Ajouter le périphérique char au système */
	rc = cdev_add(&priv->my_cdev, priv->dev_fifo, 1);
	if (rc < 0){
		pr_err("Device Add Error\n");
		goto cdev_add_fail;
	}
	/* Ajouter le périphérique char au système */
	rc = cdev_add(&priv->my_cdev_read, priv->dev_read_size, 1);
	if (rc < 0){
		pr_err("Device Add Error\n");
		goto cdev_add_fail;
	}
	/* créer une structure de classe struct */
	priv->my_cdev_class = class_create(THIS_MODULE, DEVICE_NAME);
	if(IS_ERR(priv->my_cdev_class) ){
		pr_err("Cannot create the struct class for device\n");
		rc = PTR_ERR(priv->my_cdev_class) ;
		goto my_cdev_class_fail;
	}
	/* crée les périphériques et les enregistre dans sysfs */
	if(IS_ERR(device_create(priv->my_cdev_class, NULL, priv->dev_fifo, NULL, NODE_FIFO_NAME))){
		pr_err("Error device_create\n");
		rc = -1;
		goto device_create_fail;
	}
	if(IS_ERR(device_create(priv->my_cdev_class, NULL, priv->dev_read_size, NULL, NODE_READ_SIZE_NAME))){
		pr_err("Error device_create\n");
		rc = -1;
		goto device_create_fail;
	}
	
     /*------------------ENREGISTREMENT DU TIMER----------------------*/

	/* Initilisation du timer (periode = 0.5 secondes) */
	ktime = ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS);
	hrtimer_init( &priv->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	priv->hr_timer.function = &my_hrtimer_callback;
	hrtimer_start( &priv->hr_timer, ktime, HRTIMER_MODE_REL );
	priv->my_chrono.active = 1;
 
    
    
    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 device_create_fail:
	class_destroy(priv->my_cdev_class);
 my_cdev_class_fail:
	cdev_del(&priv->my_cdev);
	cdev_del(&priv->my_cdev_read);
 cdev_add_fail:
	unregister_chrdev_region(priv->dev_fifo, MY_DEV_COUNT);
 register_chrdev_region_fail:
 request_irq_fail:
 get_irq_fail:
	/* Démappage de l'adresse virtuelle */
    iounmap(priv->MEM_ptr);
	/* Retourne le numéro de l'erreur */
    return rc;
}

/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int Aff_FIFO_remove(struct platform_device *pdev)
{	
	int rc;
    
    /* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
    
    /* Supprime le timer */
    rc = hrtimer_cancel( &priv->hr_timer );
	if (rc) pr_err("The timer was still in use...\n");
	
	
	/* ANNULE L'ENREGISTREMENT DES CARACTERES DEVICE */
	device_destroy(priv->my_cdev_class, priv->dev_fifo);
    device_destroy(priv->my_cdev_class, priv->dev_read_size);
    class_destroy(priv->my_cdev_class);
	
	cdev_del(&priv->my_cdev);
	cdev_del(&priv->my_cdev_read);
	unregister_chrdev_region(priv->dev_fifo, MY_DEV_COUNT);
	

	/* éteint les leds du module */
    *(priv->LED_ptr) = 0;
    /* éteint les 7 seg du module */
    *(priv->SEG0_3_ptr) = 0x0;
	*(priv->SEG4_5_ptr) = 0x0;
    
	/* Démappage de l'adresse virtuelle */
    iounmap(priv->MEM_ptr);

	/* Retoune 0 */
    return 0;
}




