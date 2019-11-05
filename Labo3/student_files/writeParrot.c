/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : writeParrot.c Labo3
 * Author     : Spinelli Isaia
 * Created on : 05.11.2019
 *
 * Description  : Test le module parrot modifié afin de écrire sur notre périphérique
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


int open_physical (int); 
void close_physical (int);


int main() {
	int fd = -1;
	int nb = 0;
	
	char buffWrite[] = {0,1,2,3,4,5,6,7,8,9};
	
	printf("******************************\nExercice write de parrot -- Labo 3\n******************************\n");
	

	// Ouvre le fichier /dev/nodeParrot	
	if((fd = open_physical(fd)) == -1)
		return EXIT_FAILURE;
		
	printf("Ecriture de 0 à 9 en binaire dans /dev/nodeParrot\n");
	nb = write(fd, buffWrite, strlen(buffWrite)); //sizeof(buffWrite)
	if (nb != sizeof(buffWrite)) {
		perror("write\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	
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
