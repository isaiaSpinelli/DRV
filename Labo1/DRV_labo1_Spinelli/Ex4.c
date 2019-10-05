/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex4.c Labo1
 * Author     : Spinelli Isaia
 * Created on : 05.10.2019
 *
 * Description  : ecrit sur les derniers 7seg un chiffre incrementable 
 * 					par un utilisateur
 *
 ************************************************************************** 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
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

// reference de converssion : https://fr.wikipedia.org/wiki/Afficheur_7_segments
#define ZERO 	0x3F
#define UN 		0x06
#define DEUX 		0x5B
#define TROIS 		0x4F
#define QUATRE 		0x66
#define CINQ 		0x6D
#define SIX 		0x7D
#define SEPT		0x07
#define HUIT 		0x7F
#define NEUF 		0x6F
#define A 		0x77
#define B 		0x7C
#define C 		0x39
#define D 		0x5E
#define E 		0x79
#define F 		0x71

void clignotement(vuint * led, unsigned int miliseconde);


int main() {
	// Tableau des valeurs a afficher
	unsigned char DisplayTab[NB_DISPLAY] = {ZERO, UN, DEUX , TROIS,
											QUATRE, CINQ, SIX, SEPT,
											HUIT, NEUF, A, B,
											C, D, E, F
											};
	// compteur pour l'incrementation
	char i;
	int erno,fd;
	// Pointeur sur la zone memoire 
	vuint *seg = NULL;
	
	// indice du tableau de la valeur a afficher
	unsigned char indice = 8;
	unsigned int valKey = 0;
	bool clikedKey0,clikedKey1 = false;
	
	printf("Hello REDS! Exercice 4\n");
	
	// Ouvre le fichier /dev/mem
	if ( (fd = open("/dev/mem",O_RDWR|O_SYNC)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/mem impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	// getpagesize() = 4096
	// map la zone memoire voulu
	if ( (seg = (vuint *) mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)BASE_ADDR_SEG)) == NULL){
		fprintf(stderr,"mmap impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	// clear 
	seg[SEG7_5_4] = 0x00000000 ;
	seg[SEG7_3_0] = 0x00000000 ;
	seg[LED] = 0x00000000;
	
	
	while(1) {
		// Allume les segements avec la bonne lettre
		seg[SEG7_3_0] = (vuint) ( (0x000000 << 8*1) |  DisplayTab[indice]); 		
		
		// recupere la valeur des boutons
		valKey = seg[KEY];
		
		// s'il y a decrementation
		if (valKey == 1) {
			// si le bouton etait pas presse avant
			if(!clikedKey0) {
				clikedKey0=true;
				// decrementation
				if(--indice > NB_DISPLAY) {
					indice = 15;
					// Clignotement de 300ms
					clignotement(&seg[LED], 300);
				}
			}
		} else {
			clikedKey0=false;
		}
		
		// incrementation
		if (valKey == 2) { 
			if(!clikedKey1) {
				clikedKey1=true;
				indice = (++indice >= NB_DISPLAY) ? --indice : indice ;
			}
		} else {
			clikedKey1=false;
		}
		
		// Quit
		if (valKey == 8) { 
			return EXIT_SUCCESS;
		} 
	}

	// ferme le fichier
	close(fd);
	return EXIT_SUCCESS;
}

// fait clignoter les leds
void clignotement(vuint * led, unsigned int miliseconde){
	unsigned long useconde = miliseconde*1000 ;
	*led = 0x000003ff;
	usleep(useconde);
	*led = 0x00000000;
	usleep(useconde);
	*led = 0x000003ff;
	usleep(useconde);
	*led = 0x00000000;
}
