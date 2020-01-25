/*
 * Auteurs : Spinelli Isaia
 * Date : 24.12.19
 * 
 * Amélioration : Ajouter des protections de concurrences (exemple : des spin_lock pour les kfifo (kfifo_in_spinlocked))
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

/* Fonction de work queue pour la modification de la kfifo */
static void wq_fn_modif_kfifo(struct work_struct *work)
{
	int size_free,rc,val;
	int val_read[SIZE_INT_MAX_KFIFO];
    struct priv *priv = container_of(work, struct priv, modifie_kfifo);

	/* Si des valeurs sont en attentes, remplis la kfifo principale  */
	/* Calcul le nombre de place maintenant libre */
	size_free = priv->size_kfifo - (kfifo_len(&priv->Kfifo)/SIZE_INT_IN_BYTE) ;
	if ( size_free <= 0) {
		if ( (rc = kfifo_out(&priv->Kfifo, val_read, (-size_free)*SIZE_INT_IN_BYTE)) < 0) {
			pr_err("Error kfifo_out (err=%d)\n",rc);
		}	
	/* Si des valeurs peuvent être ajouté à la kfifo */
	} else {
	
		/* Améliroation possible avec MIN(size_free, kfifo_len)
		 * et kfifo_out - kfifo_in la valeur min  */
		
		printk(KERN_DEBUG "refill kfifo\n");
		
		/* Tant qu'il y a de la place et que des valeurs sont en attente*/
		while ( size_free-- != 0 && kfifo_len(&priv->Kfifo_wait) ) {
		
			/* Remplie la kfifo avec les valeurs en attentes */
			if ( (rc = kfifo_out(&priv->Kfifo_wait, &val, 4)) < 0) {
				pr_err("Error kfifo_out (err=%d)\n",rc);
			}
			
			/* La place dans la kfifo */
			if ( (rc = kfifo_in(&priv->Kfifo, &val, 4)) <= 0 ) {
				pr_err("Error kfifo_in (err=%d)\n",rc);
			}
		}
	}
} 

/* Fonction de work queue pour le reset 
 * Amélioration : remplacer la fonction msleep avec un timer et une wait_queue_head_t*/
static void wq_fn_reset(struct work_struct *work)
{
	struct list_head *pos, *q;
    struct priv *priv = container_of(work, struct priv, reset);
    struct size_list *size;
    
    printk(KERN_DEBUG "reset...\n");
    /* Indique que nous faisons un reset */
    priv->inReset=1;
		
	/* reset les kfifo */
	kfifo_reset(&priv->Kfifo);
	kfifo_reset(&priv->Kfifo_wait);
	
	/* reset la liste */
	list_for_each_safe( pos, q, &priv->size_list1) {
		struct size_list *tmp;
		tmp = list_entry( pos, struct size_list, full_list);
		list_del( pos );
		kfree( tmp );
	}
	
	/* Eteint les 7 seg */
	*priv->SEG0_3_ptr = 0;
	*priv->SEG4_5_ptr = 0;
	/* Attend 5 secondes */
	msleep(5000);
	/* Indique la fin du reset */
	priv->inReset=0;
	
	/* Ajoute le noeud de la taille actuelle */
	size = (struct size_list *) kmalloc( sizeof(struct size_list), GFP_KERNEL );
	size->size = priv->size_kfifo;
	list_add(&size->full_list, &priv->size_list1);
	printk(KERN_DEBUG "reset ok\n");

}


/* Callback du timer (période = 1/2 secondes )*/
enum hrtimer_restart my_hrtimer_callback (struct hrtimer * timer)
{
	int val = VALUE_DEFAULT;
	int i, rc;
	char val_str[7];
	/* Récupère la structure privé */
	struct priv *priv = from_timer(priv, timer, hr_timer);

	/* Si on veut afficher la kfifo vide */
	if (priv->active==1 && kfifo_is_empty(&priv->Kfifo)){
		/* Insére (3x) la valeur par défaut (FOOD) dans la kfifo*/
		for (i=0; i < COUNT_DISPLAY_VALUE_DEFAULT; ++i) {
			if ( (rc = kfifo_in(&priv->Kfifo, &val, SIZE_INT_IN_BYTE)) <= 0 ) {
				rc = -rc;
				pr_err("Error kfifo_in (err=%d)\n",rc);
				return rc;
			}
		}
	}
	
	/* Récupère la prochaine valeur à afficher */
	if ( (rc = kfifo_out(&priv->Kfifo, &val, SIZE_INT_IN_BYTE)) < 0) {
		pr_err("Error kfifo_out (err=%d)\n",rc);
        return rc;
    /* Si elle est vide */
	} else if (rc == 0) {
		printk(KERN_DEBUG "Finish val \n");
		
		 /* Transforme en string la valeur à afficher */
		sprintf(val_str, "%06x", priv->nbKey3Pressed);
		/* Affiche les caractères sur les 7 seg*/
		display_Seg0_3(priv->SEG0_3_ptr, val_str[2],val_str[3],val_str[4],val_str[5]);
		display_Seg4_5(priv->SEG4_5_ptr, val_str[0],val_str[1]);
		/* Indique qu'on a fini */
		priv->active= 0;
		
		/* Si des valeurs attendaient, remplie la kfifo */
		queue_work(priv->work_queue, &priv->modifie_kfifo);
		
		
		return HRTIMER_NORESTART;
	/* S'il y a encore des valeurs */
	}else {
		 /* Transforme en string la valeur à afficher */
		sprintf(val_str, "%06x", val);
		/* Affiche les caractères sur les 7 seg*/
		display_Seg0_3(priv->SEG0_3_ptr, val_str[2],val_str[3],val_str[4],val_str[5]);
		display_Seg4_5(priv->SEG4_5_ptr, val_str[0],val_str[1]);
		
		
		/* Reconfigure le timer (periode = 0.5 secondes) */
		hrtimer_start( &priv->hr_timer, priv->interval, HRTIMER_MODE_REL );
		priv->active= 2;
		
		return HRTIMER_RESTART;
	}
	return -1;
}

/* Permet de paratger la structure privé */
static int SIZE_open(struct inode* node, struct file * f)
{
	/* Récupération des informations du module (structure privée) */
    struct priv *priv_s ;
    priv_s = container_of(node->i_cdev, struct priv, my_cdev_read);
    /* Placement de la structure privée dans les data du fichier */
    f->private_data = priv_s;
    
    return 0;
}



/* Lecture des différentes tailles de la kfifo */
static ssize_t
SIZE_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{
	char size_str[4] = "";
	char all_size_str[4*MAX_VALUE_SIZE_DIFF] = "";
    struct priv *priv ;
    struct size_list *size_lu;
    
	
	/* Récupération des informations du module (structure privée) */
	priv = (struct priv *) filp->private_data;
	
	/* Si le programme est en reset */
	if (priv->inReset == 1){
		return 0;
	}
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
    /* Get chaque noeud dans la liste */
    list_for_each_entry( size_lu, &priv->size_list1, full_list ) {
		/* Transforme en string la valeur lue */
		sprintf(size_str, "%02d ", size_lu->size);
		/* Concate toutes les valeurs */
		strcat(all_size_str, size_str);	
		/* Met à jour la pos */
		*ppos += sizeof(size_str);
	}
    
	/* Ecris dans le buffer user space toutes les valeurs lues */
	if ( copy_to_user(buf, all_size_str, sizeof(all_size_str)) != 0 ) {
		return 0;
	}
		
			
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



/* Lecture des valeurs dans la kfifo */
static ssize_t
FIFO_read(struct file *filp, char __user *buf,
            size_t count, loff_t *ppos)
{	
	int i,rc,taille;
    struct priv *priv ;
    int val_read[SIZE_INT_MAX_KFIFO];
    char val_str[10] = "";
    char all_val_str[10*SIZE_INT_MAX_KFIFO] = "";
	
	/* Récupération des informations du module (structure privée) */
	priv = (struct priv *) filp->private_data;
	
	/* Si le programme est en reset */
    if (priv->inReset == 1){
		return 0;
	}
	
	/* Si on vient de lire la valeur */
	if (*ppos > 0) {
		*ppos = 0;
        return 0;
    }
    
	
	/* Récupère les différents nombre dans la kfifo*/
	taille = kfifo_len(&priv->Kfifo) / sizeof(int);
	if ( (rc = kfifo_out_peek(&priv->Kfifo, val_read, SIZE_INT_MAX_KFIFO*SIZE_INT_IN_BYTE)) < 0) {
        pr_err("Error kfifo_out_peek (err=%d)\n",rc);
        return rc;
	}
	
	/* Affiche les valeurs lues */
	for (i=0; i < taille; ++i){
		 /* Transforme en string la valeur lue */
		sprintf(val_str, "0x%x ", val_read[i]);
		/* Concate toutes les valeurs */
		strcat(all_val_str, val_str);	
		/* Met à jour la pos */
		*ppos += sizeof(val_str);
	}
	
	/* Ecris dans le buffer user space toutes les valeurs lues */
	if ( copy_to_user(buf, all_val_str, sizeof(all_val_str)) != 0 ) {
		return 0;
	}
	
    return *ppos;
}

/* Ecriture d'une nouvelle valeur dans la kfifo */
static ssize_t
FIFO_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *ppos)
{
	int rc;
	unsigned long long get_val;
	unsigned int new_val;
	
	struct priv *priv ;
	
	/* Récupération des informations du module (structure privée) */
	priv = (struct priv *) filp->private_data;
	
	/* Si le programme est en reset */
    if (priv->inReset == 1){
		return count;
	}
	
	// Test les entrées
    if (count == 0) {
		pr_err("Empty write requested!\n");
        return 0;
    } 
    
    
    /* Récupère la nouvelle valeur */
    if ( (rc = kstrtoull_from_user(buf, count, HEXA_BASE, &get_val)) ) {
		rc = -rc;
        pr_err("Error kstrtoull_from_user (err=%d)\n",rc);
        return rc;
    }
	/* Si la valeur est plus grande que la valeur max */
	if (get_val > VALUE_MAX){
		pr_err("Error new value to insert is too big (max = 0x%x)\n", HEXA_BASE);
        return count;
	}
	
	new_val = (int)get_val;
	
	/* Si la kfifo est déjà pleine ou qu'on affiche les valeurs */
	if ( ((kfifo_len(&priv->Kfifo)/SIZE_INT_IN_BYTE) >= priv->size_kfifo) || priv->active == 2 ){
		printk(KERN_DEBUG "KFIFO full ! \n");
		/* Place la valeur dans la kfifo d'attente */
		if ( (rc = kfifo_in(&priv->Kfifo_wait, &new_val, SIZE_INT_IN_BYTE)) <= 0 ) {
			rc = -rc;
			pr_err("Error kfifo_in (err=%d)\n",rc);
			return rc;
		}
		
	/* Sinon ajout la valeur dans la kfifo */	
	} else if ( (rc = kfifo_in(&priv->Kfifo, &new_val, SIZE_INT_IN_BYTE)) <= 0 ) {
		rc = -rc;
        pr_err("Error kfifo_in (err=%d)\n",rc);
        return rc;
    }
	
    return count;
}






/* Fonction de handler appelée lors d'une interruption */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{	
	/* Structure contenant une eventuel prochaine taille */
    struct size_list *another_size;
	/* Récupération des informations du module préparées dans le fonction probe (request_irq) */
    struct priv *priv = (struct priv *) dev_id;
    
    
    /* Récupère quel bouton a été pressé */
    unsigned int valKey = *(priv->KEY_ptr + CLEAR_INT);

    if (valKey == KEY3) { /* ---------------------Pression sur KEY3  
		lecture des numéros qui se trouvent dans la FIFO ou 
		* compte le NB de pression */
		
		/* Si le timer est déjà actif */
		if (priv->active) {
			priv->nbKey3Pressed++;
		} else {
			/* Lance le timer */
			priv->nbKey3Pressed = 0;
			priv->active = 1;
			hrtimer_start( &priv->hr_timer, priv->interval, HRTIMER_MODE_REL );
		}
		
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY3;
	} else if (valKey == KEY0) { /* -----Pression sur le bouton KEY0  
		Changement de la taille de la kfifo */

		/* Lecture de la valeur des switches ( SW0 - SW5 ) */
		/* Met à jour la taille de la kfifo */
		priv->size_kfifo = (( *(priv->SW_ptr) & 0x3F ) + 1 ) ;
		printk(KERN_DEBUG "New size : %d\n", priv->size_kfifo );
		
		/* Ajoute la new taille dans la liste */
		another_size = (struct size_list *) kmalloc( sizeof(struct size_list), GFP_KERNEL );
		another_size->size = priv->size_kfifo;
		list_add(&another_size->full_list, &priv->size_list1);
		/* Lance la fonction pour la mise à jour de la kfifo */
		queue_work(priv->work_queue, &priv->modifie_kfifo);
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY0;

	} else if (valKey == KEY1) { /* -------------------Pression sur KEY1  
		reset globale */
		
		/* Lance la fonction pour le reset global */
		queue_work(priv->work_queue, &priv->reset);
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY1;
	} else if (valKey == KEY2) { /* -------------------Pression sur KEY2 
		L’affichage est interrompu */
		
		/* Stop le timer */
		priv->active= 0;
		hrtimer_cancel(&priv->hr_timer);
		
		/* Si des valeurs sont en attentes, remplis la kfifo principale */
		queue_work(priv->work_queue, &priv->modifie_kfifo);
		
		/* Clear l'interruption du bouton */
		*(priv->KEY_ptr + CLEAR_INT) = KEY2;
	} else { 
		printk(KERN_ERR "Autre boutons... !?\n");
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
    /* Structure contenant la première taille */
    struct size_list *first_size;
    
    
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
		goto device_create_fail1;
	}
	if(IS_ERR(device_create(priv->my_cdev_class, NULL, priv->dev_read_size, NULL, NODE_READ_SIZE_NAME))){
		pr_err("Error device_create\n");
		rc = -1;
		goto device_create_fail2;
	}
	
     
 
    
    /*----------------------------KFIFO-------------------------------*/
    
    if ((rc = kfifo_alloc(&priv->Kfifo,
                          SIZE_INT_MAX_KFIFO * SIZE_INT_IN_BYTE, 
                          GFP_KERNEL)) != 0) {
        pr_err("Cannot allocate main KFIFO (error: %d)\n", rc);
        rc = -ENOMEM;
        goto cleanup_data_in_kfifo1;
    }
     /* Init de la taille de la kfifo ( SW0 - SW5 ) */
    priv->size_kfifo = (( *(priv->SW_ptr) & 0x3F ) + 1 );
	printk(KERN_INFO "Size kfifo = %d\n", priv->size_kfifo ); 
	
	/* Kfifo d'attente */
	if ((rc = kfifo_alloc(&priv->Kfifo_wait,
                          SIZE_INT_MAX_KFIFO * SIZE_INT_IN_BYTE, 
                          GFP_KERNEL)) != 0) {
        pr_err("Cannot allocate main KFIFO (error: %d)\n", rc);
        rc = -ENOMEM;
        goto cleanup_data_in_kfifo2;
    }
    
    
    
    /*------------------ENREGISTREMENT DE LA LISTE--------------------*/

    INIT_LIST_HEAD(&priv->size_list1);
    first_size = (struct size_list *) kmalloc(sizeof(*first_size), GFP_KERNEL );
    if ( first_size == NULL) {
		pr_err("Failed to kmalloc size object \n");
        rc = -ENOMEM;
        goto kmalloc_size_fail;
	}
    first_size->size = priv->size_kfifo;
    list_add(&first_size->full_list, &priv->size_list1);
    
    
    /*------------------ENREGISTREMENT DU TIMER----------------------*/

	/* Initilisation du timer (periode = 0.5 secondes) */
	priv->interval = ktime_set(PERIODE_TIMER_SEC, PERIODE_TIMER_NS);
	hrtimer_init( &priv->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	priv->hr_timer.function = &my_hrtimer_callback;
	priv->active= 0;
	
	
	
	 /*------------------ENREGISTREMENT WORKQUEUE---------------------*/
	priv->work_queue = create_workqueue(NAME_WORK_QUEUE);
	BUG_ON(!priv->work_queue);

    INIT_WORK(&priv->modifie_kfifo, wq_fn_modif_kfifo);
    INIT_WORK(&priv->reset, wq_fn_reset);
	 
	 
    
    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 kmalloc_size_fail:
	Cleaning_list(priv);
    kfifo_free(&priv->Kfifo_wait);
 cleanup_data_in_kfifo2:
    kfifo_free(&priv->Kfifo);
 cleanup_data_in_kfifo1:
	device_destroy(priv->my_cdev_class, priv->dev_fifo);
 device_create_fail2:
    device_destroy(priv->my_cdev_class, priv->dev_read_size);
 device_create_fail1:
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

void Cleaning_list(struct priv* priv){
	
	struct list_head *pos, *q;
	struct size_list *tmp;
	
	/* Free la liste */
	list_for_each_safe( pos, q, &priv->size_list1 ) {
		tmp = list_entry( pos, struct size_list, full_list );
		list_del( pos );
		kfree( tmp );
	}
	
}

/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int Aff_FIFO_remove(struct platform_device *pdev)
{
	int rc;

	/* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);
    
    /* Supprime la workqueue */
    flush_workqueue(priv->work_queue);
    destroy_workqueue(priv->work_queue);
    
    /* Supprime le timer */
    rc = hrtimer_cancel( &priv->hr_timer );
	if (rc) pr_err("The timer was still in use...\n");
	
	/* Free la liste */
	Cleaning_list(priv);
	
	/* Free the KFIFOs */
    kfifo_free(&priv->Kfifo_wait);
    kfifo_free(&priv->Kfifo);
	
	
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




/* Permet d'afficher 4 chiffres sur les 4 premiers 7 seg */
void display_Seg0_3(volatile int * ioctrl,
                  char num4, char num3, char num2, char num1)
{
	*ioctrl = 0x0 | hex_digits[char_to_int(num4)] << 24 | 
					hex_digits[char_to_int(num3) ] << 16 | 
					hex_digits[char_to_int(num2)] << 8 | 
					hex_digits[char_to_int(num1)];
}
/* Permet d'afficher 2 chiffre sur les 2 derniers 7 seg */
void display_Seg4_5(volatile int * ioctrl,
                   char num6, char num5)
{
    *ioctrl = 0x0 | hex_digits[char_to_int(num6)] << 8 | 
					hex_digits[char_to_int(num5)];
}
