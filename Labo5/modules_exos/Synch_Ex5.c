/*
 * Auteurs : Spinelli Isaia
 * Date : 11.12.19
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
/* Pour les opérations atomiques*/
#include<asm/atomic.h>


/* Déclare la license du module */
MODULE_LICENSE("GPL");
/* Déclare l'auteur du module */
MODULE_AUTHOR("REDS_Spinelli");
/* Indique une description du module*/
MODULE_DESCRIPTION("Exercice 4 de Synchronisation");

/* Variable entière paratgée init à 7 */
static int shared_var = 7;
/* spinlock permettant de protéger la variable shared_var */
static spinlock_t lock_shared_var;

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
    /* Permet de créer un dossier dans le sysfs */
	struct kobject *kobj_Synch_ex5;
    
};

/* Fonction de handler appelée lors d'une interruption  */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{	
	unsigned long flags;
	/* Récupération des informations du module préparées dans le fonction probe (request_irq) */
    struct priv *priv = (struct priv *) dev_id;
	
	/* Incrémente la variable partagée à chaque fois qu'un bouton est pressé */
	spin_lock_irqsave(&lock_shared_var, flags);
	shared_var++;
	spin_unlock_irqrestore(&lock_shared_var, flags);
	
    printk(KERN_NOTICE "Button pushed!\n");
	/* Incrémentation des leds */
    *(priv->LED_ptr) = *(priv->LED_ptr) + 1;
	/* Clear l'interruption */
    *(priv->KEY_ptr + 3) = 0xF;
	
	/* Indique que l'interruption est traitée */
    return (irq_handler_t) IRQ_HANDLED;
}


/*
 * Fonction appellée lors de le lecture d'un paramètre dans /sys/kernel/Synch_ex5/
 */
static ssize_t shared_var_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{	
	unsigned long flags;
	int rc = -1;
	/* Permet de lock pour lire la variable partagé*/ // read_lock_irqsave(&xxx_lock, flags);
	spin_lock_irqsave(&lock_shared_var, flags);
	/* Place dans le buf la valeur de shared_var */
	rc = sprintf(buf, "%d\n", shared_var);
	spin_unlock_irqrestore(&lock_shared_var, flags);
	
	return rc; 
}

/* Lors d'une addition de shared_var via une ecriture dans /sys/kernel/Synch_ex5/add_qty */
static ssize_t add_qty_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{	
	unsigned long flags;
	int rc;
	int addVal ;
	/* Récupére la valeur à ajouter*/
	rc = kstrtoint(buf, 10, &addVal);
	if (rc < 0)
		return rc;
	
	/* Additionne la variable shared_var avec la valeur écrite dans le fichier */
	spin_lock_irqsave(&lock_shared_var, flags);
	shared_var += addVal;
	spin_unlock_irqrestore(&lock_shared_var, flags);

	return count;
}

/* Lors d'une decrémenter de shared_var via une ecriture dans /sys/kernel/Synch_ex5/decr */
static ssize_t decr_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{	
	unsigned long flags;
	/* Decrémente la variable shared_var */
	spin_lock_irqsave(&lock_shared_var, flags);
	shared_var--;
	spin_unlock_irqrestore(&lock_shared_var, flags);
	
	return count;
}

/* attribut pour lire shared_var */
static struct kobj_attribute shared_var_attribute =
	__ATTR(shared_var, 0444, shared_var_show, NULL);
	
/* attribut pour additionner shared_var d'une valeur écrite */
static struct kobj_attribute add_qty_attribute =
	__ATTR(add_qty, 0664, shared_var_show, add_qty_store);
	
/* attribut pour decrémenter shared_var */
static struct kobj_attribute decr_attribute =
	__ATTR(decr, 0664, shared_var_show, decr_store);
	
	
/*
 * Créez un groupe d'attributs afin que nous puissions les créer et 
 * les détruire tous en même temps.
 */
static struct attribute *attrs[] = {
	&shared_var_attribute.attr,
	&add_qty_attribute.attr,
	&decr_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

/*
 * An unnamed attribute group will put all of the attributes directly in
 * the kobject directory.  If we specify a name, a subdirectory will be
 * created for the attributes with the directory being the name of the
 * attribute group.
 */
static struct attribute_group attr_group = {
	.attrs = attrs,
};

/* Fonction probe appelée lors du branchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device détécté) */
static int pushbutton_probe(struct platform_device *pdev)
{	/* Déclaration de la structure priv */
    struct priv *priv;
	/* Valeur de retour */
    int rc;
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
	/* Si la la fonction request_irq() à echoué */
    if (rc != 0) {
        printk(KERN_ERR "pushbutton_irq_handler: cannot register IRQ %d\n",
               priv->IRQ_num);
        printk(KERN_ERR "error code: %d\n", rc);
		/* Fin de la fonction avec un démappage des adresse, un free de la structure priv et le retour de l'erreur */
        goto request_irq_fail;
    }
    
    /* Crée une structure kobject dynamiquement et l'enregistre dans sysfs */
	priv->kobj_Synch_ex5 = kobject_create_and_add("Synch_ex5", kernel_kobj);
	if (!priv->kobj_Synch_ex5) {
		rc = -ENOMEM;
		printk(KERN_ERR "error kobject_create_and_add (%d)\n", rc);
		goto kobject_create_fail;
	}
		

	/* crée le fichier associé avec le kobjet */
	rc = sysfs_create_group(priv->kobj_Synch_ex5, &attr_group); 
	if (rc) {
		kobject_put(priv->kobj_Synch_ex5);
	}
	
	
	spin_lock_init(&lock_shared_var);

    printk(KERN_INFO "Interrupt registered!\n");
    printk(KERN_INFO "Driver ready!\n");
	/* Retourne 0 si la fonction probe c'est correctement effectué */
    return 0;

/* Déclaration de labels afin de gérer toutes les erreurs possibles de la fonction probe ci-dessus */
 kobject_create_fail:
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
static int pushbutton_remove(struct platform_device *pdev)
{	/* Récupère l'adresse de la structure priv correspondant au platform_device reçu (précèdemment lié dans la fonction probe)*/
    struct priv *priv = platform_get_drvdata(pdev);

    printk(KERN_INFO "Removing driver...\n");
    
    /* Retire la structure kobject */
    kobject_put(priv->kobj_Synch_ex5);
	
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
    .probe = pushbutton_probe,
	/* Définit la fonction remove appelée lors du débranchement d'un préiphérique pris en charge par ce module */
    .remove = pushbutton_remove,
};

/* Macro d'assistance pour les pilotes qui ne font rien de spécial dans les fonctions init / exit. Permet d'éliminer du code (fonction init et exit) */
/* Enregistre le driver via la structure ci-dessus */
module_platform_driver(pushbutton_driver);
