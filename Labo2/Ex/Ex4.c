/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex4.c Labo2
 * Author     : Spinelli Isaia
 * Created on : 12.11.2019
 *
 * Description  : affiche sur l’écran un message lorsque l’utilisateur appuie sur le bouton KEY3.
 *
 * Reference : https://yurovsky.github.io/2014/10/10/linux-uio-gpio-interrupt.html
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


int main() {


	int fd;
	 
	// Pointeur sur la zone memoire 
	vuint *seg = NULL;
	
	// indice du tableau de la valeur a afficher
	unsigned int valKey = 0;
	bool clikedKey3 = false;
	
	printf("******************************\nExercice 4 -- Labo 2\n******************************\n");
	
	// Ouvre le fichier /dev/uio0
	if ( (fd = open("/dev/uio0",O_RDWR)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/uio0 impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	// Pas besoin d'offset car /dev/uio0 est déjà à la bonne addresse
	if ( (seg = (vuint *) mmap(0, REG_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)0)) == NULL){
		fprintf(stderr,"mmap impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}

	
	while(1) {
		
		// recupere la valeur des boutons
		valKey = seg[KEY];
		
		// s'il y a decrementation
		if (valKey == 8) {
			// si le bouton etait pas presse avant
			if(!clikedKey3) {
				clikedKey3=true;
				
				printf("Click !\n");
			}
		} else {
			clikedKey3=false;
		}
		
		// Quit
		if (valKey == 1) { 
			printf("Bye !!\n");
			return EXIT_SUCCESS;
		} 
	}


	// ferme le fichier
	close(fd);
	return EXIT_SUCCESS;
}
