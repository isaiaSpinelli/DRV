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
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/delay.h>

#include "address_map_arm.h"
#include "interrupt_ID.h"


#define AFF_NUM_DRIVER_VERSION              "0x1"


/* Constantes pour les devices char */
#define DEVICE_NAME				"AffNum"
#define MAJOR_NUM				100
#define MINOR_NUM_0 			0
#define MINOR_NUM_1 			1
#define MY_DEV_COUNT 			2
#define NODE_FIFO_NAME			"fifo"
#define NODE_READ_SIZE_NAME		"readSize"
#define NAME_WORK_QUEUE			"workqueue"

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

/* Base de l'hexadécimal */
#define HEXA_BASE 		16

/* Valeur par défaut à afficher sur les 7 seg si la kfifo est vide */
#define VALUE_DEFAULT 					0XF00D
/* Nombre de fois à afficher la valeur par défaut */
#define COUNT_DISPLAY_VALUE_DEFAULT 	3
/* Valeur max de la kfifo */
#define VALUE_MAX 		0xFFFFFF

/* Nombre maximum de taille différente de la kfifo */
#define MAX_VALUE_SIZE_DIFF   	128
/* Taille max avec 5 Switches = 63 + 1 = 64*/
#define SIZE_INT_MAX_KFIFO   	64
/* Nombe de byte dans un int */
#define SIZE_INT_IN_BYTE	sizeof(int)

struct size_list {
    int size;
    struct list_head full_list;
} ;


/* Structure permettant de mémoriser les informations importantes du module */
struct priv
{	
	/*----------RESSOURCES----------*/
	
	/* Pointeur sur les leds */
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
    
    /*----------INFORAMTIONS----------*/
    
    /* Numéro d'interruption du module */
    int IRQ_num;
    /* Indique si un reset est en cours */
    int inReset;
    /* Indique le nombre de foi que Key3 a été pressé */
    int nbKey3Pressed;
   
	
	
    /*----------TIMER----------*/
    
    struct hrtimer hr_timer;
    ktime_t interval;
     /* Mode du chrono (idle or active) */
	unsigned char active ;
    
    
    /*----------DEVICE / NODE----------*/
    
	struct cdev my_cdev;
	struct cdev my_cdev_read;
	
	dev_t dev_fifo;
	dev_t dev_read_size;
	
	struct class *my_cdev_class;
	

	/*----------KFIFO----------*/
	struct kfifo Kfifo;
	unsigned int size_kfifo;

	struct kfifo Kfifo_wait;
	
	
	
	/*----------LISTE----------*/
	struct list_head size_list1;
	
	/*----------WORK-QUEUES----------*/
	struct workqueue_struct *work_queue;
	struct work_struct modifie_kfifo;
	struct work_struct reset;

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

/* Clean la list */
void Cleaning_list(struct priv* priv);




/* Device files handling */

static int SIZE_open(struct inode* node, struct file * f);
static ssize_t SIZE_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
            
static int FIFO_open(struct inode* node, struct file * f);
static ssize_t FIFO_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
static ssize_t FIFO_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);



/* Fonctions utiles */
static void wq_fn_modif_kfifo(struct work_struct *work);
static void wq_fn_reset(struct work_struct *work);


/* Permet d'afficher 4 chiffres sur les 4 premiers 7 seg */
void display_Seg0_3(volatile int * ioctrl, char num4, char num3, char num2, char num1);
/* Permet d'afficher 2 chiffre sur les 2 derniers 7 seg */
void display_Seg4_5(volatile int * ioctrl, char num6, char num5);
                   

/* Convertie un char entre 1-9 ou a-f en valeur décimal */
#define char_to_int(c) (((c) <= '9') ? (c-48) : ((c)-(87)))


#endif /* AFF_NUM_H_ */
