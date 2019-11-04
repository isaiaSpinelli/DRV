/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : testParrot.c Labo3
 * Author     : Spinelli Isaia
 * Created on : 03.11.2019
 *
 * Description  : Test le module parrot modifié afin de lire et écrire sur notre périphérique
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

// Definition des constantes
typedef volatile uint32_t 			vuint32;
#define LW_BRIDGE_BASE        		0xFF200000
#define KEY_BASE              		0x00000050

#define BASE      					0x00

#define REG_SIZE		            getpagesize()


/*Prototypes for functions used to access physical memory addresses*/
int open_physical (int); 
void* map_physical (int,unsigned int,unsigned int);
void close_physical (int);
int unmap_physical (void*, unsigned int);


int main() {

	
	size_t BUF_SIZE = 128;
	char bufRead[BUF_SIZE] ;
	
	char buffWrite[] = "Salut les amis ! 123\n";

	size_t nb =0;
	FILE *ptr = NULL;
	

	printf("******************************\nExercice ẗest de parrot -- Labo 3\n******************************\n");
	
	int sizeString = strlen(buffWrite);

	// Ouvre le fichier en binaire
	if ( (ptr = fopen("/dev/nodeParrot","rb+")) == NULL ) {
		fprintf(stderr,"Error fopen\n(file=%s / line=%d)\n", __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	
	fseek(ptr, 0, SEEK_SET);
	
	
	// Ecris dans le fichier 
	nb = fwrite(buffWrite, sizeof(char), sizeString, ptr);
	if (nb != strlen(buffWrite)) {
		fprintf(stderr,"Error fopen\n(file=%s / line=%d)\n", __FILE__,__LINE__);
		fclose(ptr);
		return EXIT_FAILURE;
	}
	
	//fprintf(ptr, "This is testing for fprintf...\n");
	//fseek(ptr, 0, SEEK_END);
	//fputs("This is testing for fputs...\n", ptr);
	
	// Lis dans le fichier
	nb = fread(bufRead, sizeof(char), sizeof(bufRead), ptr);
	// Si on a bien tous lu (+1 = EOF)
	if (nb != sizeString+1) {
		printf("error fread\n");
	}
	
	printf("lu : %s\n", bufRead);
	

	// Ouvre le fichier /dev/nodeParrot	
	/*if((fd = open_physical(fd)) == -1)
		return EXIT_FAILURE;*/
		
	
	// Mappage de l'adresse 
	//if ((addr = map_physical (fd, BASE, REG_SIZE)) == NULL)
	//	return EXIT_FAILURE;

	
	// maj du pointeur sur les boutons
	//KEYS_ptr = (vuint32*) (LW_virtual + KEY_BASE);

/*
	nb = write(fd, buffWrite, strlen(buffWrite));
	
	if (nb != strlen(buffWrite)) {
		perror("write\n");
		close(fd);
		exit(EXIT_FAILURE);
	}
	
	printf("read\n");
	nb = read(fd, buf, BUF_SIZE);
	printf("nb=%lu\n",strlen(buffWrite));
	printf("lu : %s\n",buf);*/

	
	//unmap_physical (addr, REG_SIZE);
	fclose(ptr);
	//close_physical (fd);
	
	return EXIT_SUCCESS;
}


// Open /dev/uio0 to give access to physical addresses
int open_physical(int fd) {
	if (fd == -1){
		if ((fd = open( "/dev/nodeParrot", (O_RDWR | O_SYNC))) == -1) {
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

// Establish a virtual address mapping for the physical addresses starting at base, and extending by span bytes
void* map_physical(int fd,unsigned int base,unsigned int span) {
	
	void * virtual_base;
	
	// Get a mapping from physical addresses to virtual addresses
	virtual_base = mmap (NULL, REG_SIZE, (PROT_READ | PROT_WRITE), MAP_SHARED,fd, (off_t) base);
	
	if (virtual_base == MAP_FAILED) {
		fprintf(stderr,"mmap impossible\n(file=%s / line=%d)\n", __FILE__,__LINE__);
		close (fd);
		return(NULL);
	}
	
	return virtual_base;
}

/*Close the previously-opened virtual address mapping*/
int unmap_physical(void* virtual_base, unsigned int span) { 
	
	if(munmap (virtual_base, span) != 0) {
		fprintf(stderr,"munmap() impossible\n(file=%s / line=%d)\n", __FILE__,__LINE__);
		return(-1); 
	}
	
	return 0;
}
