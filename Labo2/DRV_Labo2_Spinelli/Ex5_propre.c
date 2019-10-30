/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex5.c Labo2
 * Author     : Spinelli Isaia
 * Created on : 28.10.2019
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
#include <unistd.h>


// Definition des constantes
typedef volatile uint32_t 			vuint32;
#define LW_BRIDGE_BASE        		0xFF200000
#define KEY_BASE              		0x00000050

#define BASE      					0x00

#define REG_SIZE		            getpagesize()
#define MASK_REG		            2
#define EDGE_REG		            3

#define NB_BLAGUE		            11

// Definition des fonctions
bool initDRV(int* fd, void ** addr);
bool closeDRV(int fd, void * LW_virtual);
int gestionBlague(uint32_t keyNum);
uint32_t handler(vuint32 * seg);

/*Prototypes for functions used to access physical memory addresses*/
int open_physical (int); 
void* map_physical (int,unsigned int,unsigned int);
void close_physical (int);
int unmap_physical (void*, unsigned int);


// Definition globale
char *blagueTab[NB_BLAGUE] = { 
  "Quel est le fromage préféré de Brigitte Macron ?\nLe Président\n",
  "Qu’est-ce qu’un nem avec des écouteurs ?\nUn NemP3…\n",
  "Un portugais ne chuchote pas\nIl murmure\n",
  "C’est l’histoire d’un schtroumpf qui tombe\nEt qui se fait un bleu…\n",
  "On ne dit pas un microprocesseur, mais un petit prof.\n",
  "Que faire lorsqu’un geek pleure ? \nOn le console\n",
  "Pourquoi les pêcheurs ne sont pas gros ?\nParce qu’ils surveillent leur ligne.\n",
  "C'est l'histoire de 2 grains de sable qui arrivent à la plage :\n« Putain, c’est blindé aujourd’hui… »\n",
  "Comment appelle-t-on un chien qui n'a pas de pattes ?\nOn ne l’appelle pas, on va le chercher…\n",
  "Que prend un éléphant dans un bar ?\nBeaucoup de place.\n",
  "Il y a plus de blague... desole !\n"
};


int main() {

	// used to open /dev/uio1
	int fd = -1;
	 
	// Pointeur memoire
	void* LW_virtual;
	vuint32* KEYS_ptr;
	
	// Gestion des interruptions
	uint32_t info = 1; 
	ssize_t nb =0;
	uint32_t numKey= 0;
	
	// Pour quitter le programme
	bool quit = false;
	

	printf("******************************\nExercice 5 -- Labo 2\n******************************\n");
	
	// init du DRV
	if ( initDRV(&fd, &LW_virtual) == EXIT_FAILURE ) {
		perror("initDRV() fail...\n");
		exit(EXIT_FAILURE);
	}
	
	// maj du pointeur sur les boutons
	KEYS_ptr = (vuint32*) (LW_virtual + KEY_BASE);
	// Ecriture de 1 dans l'interruptmask register (demasque les interruptions)
	KEYS_ptr[MASK_REG] = (uint32_t)0xf;
	// Ecrire 1 dans edgecapture (clear les interruptions)
	KEYS_ptr[EDGE_REG] = (uint32_t)0xf;
	
	while(!quit) {
		
		// Demasque les interruptions
		nb = write(fd, &info, sizeof(info));
		if (nb != (ssize_t)sizeof(info)) {
			perror("write\n");
			close(fd);
			exit(EXIT_FAILURE);
		}
		
		// indique qu'on traite l'interruption
		nb = read(fd, &info, sizeof(info));
		if (nb == (ssize_t)sizeof(info)) {
			// appelle le handler d'interruption
			numKey = handler(KEYS_ptr);
			// gestion de la blague
			if ( gestionBlague(numKey) == -1 ) {
				printf("Quit !\n"); 
				quit=true;
			}
		}
               
	}
	
	// Quitte correctement le programme
	if ( closeDRV(fd, LW_virtual) == EXIT_FAILURE ) {
		perror("closeDRV() fail...\n");
		exit(EXIT_FAILURE);
	}
	
	
	return EXIT_SUCCESS;
}

// Initialisation du driver
bool initDRV(int* fd, void ** addr) {

	// Ouvre le fichier /dev/uio0	
	if((*fd = open_physical(*fd)) == -1)
		return EXIT_FAILURE;
		
	
	// Mappage de l'adresse 
	if ((*addr = map_physical (*fd, BASE, REG_SIZE)) == NULL)
		return EXIT_FAILURE;
		
	return EXIT_SUCCESS;
}

// Fermeture du driver
bool closeDRV(int fd, void * addr) {
	
	unmap_physical (addr, REG_SIZE);
	close_physical (fd);
	
	return EXIT_SUCCESS;
}


// handler de l'interruption
uint32_t handler(vuint32 * addr) {
	// Lis quel key a ete pressee
	uint32_t numKey = addr[EDGE_REG];

	// clear l'inerrupt
	addr[EDGE_REG] = numKey;
	
	return numKey;

}


// Gere la demande ou l'evaluation de blague
int gestionBlague(uint32_t keyNum){
	static bool blague = false; 
	static int compteurBlague = 0;
	
	// si il demande une blague
	if (keyNum == 8 && blague == false){
		blague = true;
		if (compteurBlague >= NB_BLAGUE)
			compteurBlague = 0;
		printf(blagueTab[compteurBlague++]);
	} else if (blague && keyNum != 0) { // evalutation de la blague
		blague = false;
		switch(keyNum){
			case 1: printf(":-D\n"); break;
			case 2: printf(":-)\n"); break;
			case 4: printf(":-(\n"); break;
			case 8: printf(":\"-(\n"); break;
			default: printf("Error !\n"); break;
		}
	} else if (keyNum == 1) { // quitte le programme
		return -1;
	}
	
	return 1;
}

// Open /dev/uio0 to give access to physical addresses
int open_physical(int fd) {
	if (fd == -1){
		if ((fd = open( "/dev/uio0", (O_RDWR | O_SYNC))) == -1) {
			fprintf(stderr,"ouverture du fichier /dev/uio0 impossible\n(err=%d / file=%s / line=%d)\n", fd, __FILE__,__LINE__);
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
