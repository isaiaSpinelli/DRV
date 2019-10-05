/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex3.c Labo1
 * Author     : Spinelli Isaia
 * Created on : 05.10.2019
 *
 * Description  : ecrit sur les 7 seg mon prenom et le fait glisser vers
 * 					la droite.
 *
 ************************************************************************** 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

 // definition des constantes
typedef volatile unsigned int 		vuint;
#define BASE_ADDR_SEG      			(vuint *) 0xFF200000
#define SEG7_3_0            		0x08
#define SEG7_5_4            		0x0C

#define NB_SEG  6

#define I  		0x86
#define S  		0x6d
#define A  		0x77
#define TRAIT  	0x40



int main() {
	// Tableau des lettres dans l'ordre
	unsigned char lettreT[NB_SEG] = {I, S, A, I, A, TRAIT};
	// compteur pour l'incrementation
	char i;
	int erno,fd;
	// Pointeur sur la zone memoire 
	vuint *seg = NULL;
	
	printf("Hello REDS!\n");
	
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
	
	
	while(1) {
		// Pour chaque segment
		for(i=NB_SEG; i >= 0; i-- ) {
			// Allume les segements avec la bonne lettre
			seg[SEG7_5_4] = 	(vuint) ( (0x0000 << 8*2) | (lettreT[i%NB_SEG] << 8*1) | lettreT[(i+1)%NB_SEG] );
			seg[SEG7_3_0] = 	(vuint) ( (lettreT[(i+2)%NB_SEG] << 8*3) | (lettreT[(i+3)%NB_SEG] << 8*2) | (lettreT[(i+4)%NB_SEG] << 8*1) | lettreT[(i+5)%NB_SEG] );
			// attend 1 seconde
			sleep(1);
			
		}
	}

	// ferme le fichier
	close(fd);
	return EXIT_SUCCESS;
}
