/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : readParrot.c Labo3
 * Author     : Spinelli Isaia
 * Created on : 05.11.2019
 *
 * Description  : Test le module parrot modifié afin de lire sur notre périphérique
 *
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
#include <unistd.h>
#include <string.h>


/*Prototypes for functions used to access physical memory addresses*/
int open_physical (int); 
void close_physical (int);
int main() {

	
	size_t BUF_SIZE = 10;
	// Buffer pour lire le device node
	char buffRead[] = { 0,0,0,0,0,0,0,0,0,0 } ;

	int fd = -1;
	
	int bytes_read;
    int i=0;
	

	printf("******************************\nExercice read de parrot -- Labo 3\n******************************\n");
	

	// Ouvre le fichier /dev/nodeParrot	
	if((fd = open_physical(fd)) == -1)
		return EXIT_FAILURE;
		
	// Lis le device node
	printf("Lecture du fichier /dev/nodeParrot :\n");
	bytes_read = read(fd, buffRead, BUF_SIZE);
	// Affiche les valeurs lues
	for( i=0; i<BUF_SIZE; i++) {
		printf("%c", buffRead[i]);
	}
	
	// Ferme le fichier 
	close_physical(fd);
	
	return EXIT_SUCCESS;
}


// Open /dev/uio0 to give access to physical addresses
int open_physical(int fd) {
	if (fd == -1){
		if ((fd = open( "/dev/nodeParrot", (O_RDWR))) == -1) {
			fprintf(stderr,"ouverture du fichier /dev/nodeParrot impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
			return -1;
		}
	}
	return fd;
}
// Close /dev/uio0 to give access to physical addresses
void close_physical (int fd) {
	close(fd);
}
