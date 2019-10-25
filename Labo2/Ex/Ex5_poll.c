/**************************************************************************
 * HEIG-VD, Institut REDS
 *
 * File       : Ex5_poll.c Labo2
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
#include <unistd.h>
#include <poll.h>


 // definition des constantes
typedef volatile unsigned int 		vuint;
#define BASE_ADDR_SEG      			(vuint *) 0xFF200000
#define LED		            		0x00
#define SEG7_3_0            		0x08
#define SEG7_5_4            		0x0C
#define KEY		            		0x14

#define BASE      					0x00

#define NB_DISPLAY  16

#define REG_SIZE		            0x1000
#define MASK_REG		            2
#define EDGE_REG		            3

#define NB_BLAGUE		            11

// definition des fonctions
bool initDRV(int* fd, uint32_t ** seg);
bool closeDRV(int* fd, void * seg);
int gestionBlague(uint32_t keyNum);
uint32_t handler(uint32_t * seg);

// definition globale
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
	 
	// Pointeur sur la zone memoire 
	uint32_t *seg = NULL;
	
	// Gestion des interruptions
	uint32_t info = 1; 
	ssize_t nb =0;
	uint32_t numKey= 0;
	
	// pour quitter le programme
	bool quit = false;
	
	
	printf("******************************\nExercice 5 -- Labo 2\n******************************\n");
	
	// init du DRV
	if ( initDRV(&fd, &seg) == EXIT_FAILURE ) {
		perror("initDRV() fail...\n");
		exit(EXIT_FAILURE);
	}
	
	struct pollfd fds = {
		.fd = fd,
		.events = POLLIN,
	};
	
	while(!quit) {
		
		// demasque les interrupts
		nb = write(fd, &info, sizeof(info));
		if (nb != (ssize_t)sizeof(info)) {
			perror("write\n");
			close(fd);
			exit(EXIT_FAILURE);
		}
		
		printf("test\n");
		
		// Poll les interruptions
		int ret = poll(&fds, 1, -1);
        if (ret >= 1) {
			// S'il y a bien une interrupt
            nb = read(fd, &info, sizeof(info));
            if (nb == (ssize_t)sizeof(info)) {
                // appelle le handler d'interruption
				numKey = handler(seg);
				// gestion de la blague
				if ( gestionBlague(numKey) == -1 ) {
					printf("Quit !\n"); 
					quit=true;
				}
            }
        } else {
            perror("poll()");
            close(fd);
            exit(EXIT_FAILURE);
        }
        
				

	}
	
	// quitte correctement le programme
	if ( closeDRV(&fd, seg) == EXIT_FAILURE ) {
		perror("closeDRV() fail...\n");
		exit(EXIT_FAILURE);
	}
	
	
	return EXIT_SUCCESS;
}

// initialisation du driver
bool initDRV(int* fd, uint32_t ** seg) {
	
	// Ouvre le fichier /dev/uio0
	if ( (*fd = open("/dev/uio0",O_RDWR)) < 0 ) {
		fprintf(stderr,"ouverture du fichier /dev/uio0 impossible\n(err=%d / file=%s / line=%d)\n", *fd, __FILE__,__LINE__);
		return EXIT_FAILURE;
	}
	// Pas besoin d'offset car /dev/uio0 est déjà à la bonne addresse (dts)
	if ( (*seg = (uint32_t *) mmap(NULL, REG_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, *fd, (off_t)BASE)) == NULL){
		fprintf(stderr,"mmap impossible\n(err=%d / file=%s / line=%d)\n", *fd, __FILE__,__LINE__);
		close(*fd);
		return EXIT_FAILURE;
	}
	
	// Ecriture de 1 dans l'interruptmask register (demasque les interruptions)
	(*seg)[KEY+MASK_REG] = (uint32_t)0xf;
	
	// ecrire 1 dans edgecapture (clear les interruptions)
	(*seg)[KEY+EDGE_REG] = (uint32_t)0xf;
	
	return EXIT_SUCCESS;
}

// Fermeture du driver
bool closeDRV(int* fd, void * seg) {
	
	// ferme la zone memoire virutelle mappee
	if(munmap (seg, REG_SIZE) != 0) {
		fprintf(stderr,"munmap() impossible\n(err=%d / file=%s / line=%d)\n", *fd, __FILE__,__LINE__);
		close(*fd);
		return EXIT_FAILURE;
	}

	// ferme le fichier
	close(*fd);
	
	return EXIT_SUCCESS;
}


// handler de l'interruption
uint32_t handler(uint32_t * seg) {
	// Lis quel key a ete pressee
	uint32_t numKey = seg[KEY+EDGE_REG];

	// clear l'inerrupt
	seg[KEY+EDGE_REG] = numKey;
	
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
	} else if (blague) { // evalutation de la blague
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
