/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex2.c Labo2
 * Author     : Spinelli Isaia
 * Created on : 12.10.2019
 *
 * Description  : affiche sur l’écran un message lorsque l’utilisateur appuie sur le bouton KEY3.
 *
 ************************************************************************** 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdbool.h>

 // definition des constantes
typedef volatile unsigned int 		vuint;
#define BASE_ADDR_SEG      			(vuint *) 0xFF200000
#define LED		            		0x00
#define SEG7_3_0            		0x08
#define SEG7_5_4            		0x0C
#define KEY		            		0x14

#define NB_DISPLAY  16


int main() {


	int erno,fd;
	// Pointeur sur la zone memoire 
	vuint *seg = NULL;
	
	// indice du tableau de la valeur a afficher
	unsigned char indice = 8;
	unsigned int valKey = 0;
	bool clikedKey3 = false;
	
	printf("******************************\nExercice 2 -- Labo 2\n******************************\n");
	
	// Ouvre le fichier /dev/mem
	if ( (fd = open("/dev/mem",O_RDWR|O_SYNC)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/mem impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	// getpagesize() = 4096 = 0x1000
	// map la zone memoire voulu
	if ( (seg = (vuint *) mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)BASE_ADDR_SEG)) == NULL){
		fprintf(stderr,"mmap impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	
	while(1) {
		
		// recupere la valeur des boutons
		valKey = seg[KEY];
		
		// s'il le bouton KEY3 est presse
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
