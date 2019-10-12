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
	uint32_t info = 1; /* unmask */
	ssize_t nb = 0;
	 
	// Pointeur sur la zone memoire 
	vuint *seg = NULL;
	
	// indice du tableau de la valeur a afficher
	unsigned char indice = 8;
	unsigned int valKey = 0;
	bool clikedKey3 = false;
	
	printf("******************************\nExercice 4 -- Labo 2\n******************************\n");
	
	// Ouvre le fichier /dev/mem
	if ( (fd = open("/dev/uio0",O_RDWR)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/uio0 impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	
	while (1) {
        info = 1; /* unmask */

        nb = write(fd, &info, sizeof(info));
        if (nb != (ssize_t)sizeof(info)) {
            perror("write");
            close(fd);
            exit(EXIT_FAILURE);
        }

        /* Wait for interrupt */
        nb = read(fd, &info, sizeof(info));
        if (nb == (ssize_t)sizeof(info)) {
            /* Do something in response to the interrupt. */
            printf("Interrupt #%u!\n", info);
        }
    }


	// ferme le fichier
	close(fd);
	return EXIT_SUCCESS;
}
