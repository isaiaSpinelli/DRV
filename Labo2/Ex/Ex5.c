/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex4.c Labo2
 * Author     : Spinelli Isaia
 * Created on : 25.10.2019
 *
 * Description  : affiche sur l’écran un message lorsque l’utilisateur appuie sur le bouton KEY3.
 *
 * Reference :  https://yurovsky.github.io/2014/10/10/linux-uio-gpio-interrupt.html
 ************************************************************************** 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <sys/mman.h>


 // definition des constantes
typedef volatile unsigned int 		vuint;
#define BASE_ADDR_SEG      			(vuint *) 0xFF200000
#define LED		            		0x00
#define SEG7_3_0            		0x08
#define SEG7_5_4            		0x0C
#define KEY		            		0x14

#define NB_DISPLAY  16

#define REG_SIZE		            0x1000
#define MASK_REG		            KEY+2
#define CLEAR_REG		            KEY+3

bool initDRV(int* fd, uint32_t ** seg);
void gestionBlague(uint32_t keyNum);


int main() {


	int fd;
	 
	// Pointeur sur la zone memoire 
	uint32_t *seg = NULL;
	
	// Gestion des interruptions
	uint32_t info = 1; 
	ssize_t nb =0;
	uint32_t numKey = 0;
	

	printf("******************************\nExercice 5 -- Labo 2\n******************************\n");
	
	// init du DRV
	if ( initDRV(&fd, &seg) == EXIT_FAILURE ) {
		perror("initDRV\n");
		exit(EXIT_FAILURE);
	}
	
	
	
	while(1) {
		
		// demasque les interrupts
		nb = write(fd, &info, sizeof(info));
		if (nb != (ssize_t)sizeof(info)) {
			perror("write\n");
			close(fd);
			exit(EXIT_FAILURE);
		}
		
		 /* Wait for interrupt */
		nb = read(fd, &info, sizeof(info));
		if (nb == (ssize_t)sizeof(info)) {

			// Lis quel key a ete pressee
			numKey = seg[KEY+3];

			// clear l'inerrupt
			seg[CLEAR_REG] = (uint32_t)numKey;

			/* Do something in response to the interrupt. */
			// printf("Interrupt #%u!\n", info);
			
			// gestion de la blague
			gestionBlague(numKey);
			
			
		}
				

	}


	// ferme le fichier
	close(fd);
	return EXIT_SUCCESS;
}

// initialisation du driver
bool initDRV(int* fd, uint32_t ** seg) {
	
	// Ouvre le fichier /dev/uio0
	if ( (*fd = open("/dev/uio0",O_RDWR)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/uio0 impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	// Pas besoin d'offset car /dev/uio0 est déjà à la bonne addresse (dts)
	if ( (*seg = (uint32_t *) mmap(0, REG_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, (off_t)0)) == NULL){
		fprintf(stderr,"mmap impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		close(*fd);
		return EXIT_FAILURE;
	}
	
	// Ecriture de 1 dans l'interruptmask register (demasque les interruptions)
	(*seg)[MASK_REG] = (uint32_t)0xf;
	
	// ecrire 1 dans edgecapture (clear les interruptions)
	(*seg)[CLEAR_REG] = (uint32_t)0xf;
	
	return EXIT_SUCCESS;
}



// Gere la demande ou l'evaluation de blague
void gestionBlague(uint32_t keyNum){
	static bool blague = false; 
	
	// si il demande une blague
	if (keyNum == 8 && blague == false){
		blague = true;
		printf("Blague !\n");
	} else if (blague) { // evalutation de la blague
		blague = false;
		switch(keyNum){
			case 1: printf(":-D\n"); break;
			case 2: printf(":-)\n"); break;
			case 4: printf(":-(\n"); break;
			case 8: printf(":\"-(\n"); break;
			default: printf("Error !\n"); break;
		}
	}
}
