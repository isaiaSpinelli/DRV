/*
 * Auteurs : Spinelli Isaia
 * Date : 24.12.19
 * 
 */
 
#ifndef AFF_NUM_H_
#define AFF_NUM_H_

 
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
#include <linux/fs.h>        
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include "address_map_arm.h"
#include "interrupt_ID.h"


#define AFF_NUM_DRIVER_VERSION              "0x1"


/* Constantes pour les devices char */
#define DEVICE_NAME		"AffNum"
#define MAJOR_NUM		101
#define MINOR_NUM_0 	0
#define MINOR_NUM_1 	1
#define MY_DEV_COUNT 	2
#define NODE_FIFO_NAME			"fifo"
#define NODE_READ_SIZE_NAME		"readSize"

/* Tempes initial en seconde du chrono (1h et 15 sec)*/
#define TEMPS_INIT_SEC 	3615

/* Secondes + nanosecondes pour la période du timer */
#define PERIODE_TIMER_SEC 0
#define PERIODE_TIMER_NS  500000000

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
    /* Pointeur sur les boutons */
    volatile int *SW_ptr;
    /* Pointeur sur la base de la mémoire du module*/
    void *MEM_ptr;
    /* Pointeur de structure sur des informations du devices tree */
    struct resource *MEM_info;
    /* Numéro d'interruption du module */
    int IRQ_num;
    /* Structure du timer */
    struct hrtimer hr_timer;
    
    
    /* DEVICE / NODE */
    
    /* structure du device */
	struct cdev my_cdev;
	struct cdev my_cdev_read;
	
	dev_t dev_fifo;
	dev_t dev_read_size;
	
	struct class *my_cdev_class;
	
	
	
	
	
	/* structure pour la gestion du chrono */
	struct chrono my_chrono;

};


/* Tableau de transformation pour les 7 seg */
static const unsigned char hex_digits[16] = {
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
    0x77, // A
    0x7C, // B
    0x39, // C
    0x5E, // D
    0x79, // E
    0x71, // F
};





/* IRQ handler */

/* Fonction de handler appelée lors d'une interruption */
irq_handler_t irq_handler(int irq, void *dev_id, struct pt_regs *regs);
/* Fonction callback du timer */
enum hrtimer_restart my_hrtimer_callback (struct hrtimer * timer);



/* Initialization */

/* Fonction probe appelée lors du branchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device détécté) */
static int Aff_FIFO_probe(struct platform_device *pdev);
/* Fonction remove appelée lors du débranchement du périphérique (pdev est un poiteur sur une structure contenant toutes les informations du device retiré) */
static int Aff_FIFO_remove(struct platform_device *pdev);


/* Driver operations */




/* Device files handling */

static int SIZE_open(struct inode* node, struct file * f);
static ssize_t SIZE_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
            
static int FIFO_open(struct inode* node, struct file * f);
static ssize_t FIFO_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
static ssize_t FIFO_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);



/* Fonctions utiles */

/* Permet d'afficher 4 chiffres sur les 4 premiers 7 seg */
void display_Seg0_3(volatile int * ioctrl, char num4, char num3, char num2, char num1);
/* Permet d'afficher 2 chiffre sur les 2 derniers 7 seg */
void display_Seg4_5(volatile int * ioctrl, char num6, char num5);
                   






#endif /* AFF_NUM_H_ */
