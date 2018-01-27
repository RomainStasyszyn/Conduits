#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>
#include <sys/uio.h>
#include <sys/wait.h>




/*****************************************************************************
	Structure représentant le conduit à travers lequel on écrit ou on lit.
	- a est la taille atomique des opérations.
	- c est la capacité totale du conduit (contenance d'octets).
	- buffer est le conteneur d'octets du conduit.
******************************************************************************/


struct conduct {
	size_t a;										/* Taille atomique du conduit */
	size_t c;										/* Capacité maximale du conduit */
	size_t debut_lecture;	   						/* Tête de lecture du conduit */
  	size_t debut_ecriture;							/* Tête d'écriture du conduit */
	size_t place_prise;								/* Nombre d'octets utilisés dans le buffer pour savoir s'il est vide ou plein */
	size_t eof;			 							/* Permet de savoir si un caractère EOF a été inséré dans le conduit */
	pthread_mutex_t *verrou;						/* Verrou sur le conduit afin de gérer les accès concurrents */
	pthread_cond_t *condition_ecriture;				/* Variable condition pour les écritures */
	pthread_cond_t *condition_ecriture_atomique;	/* Variable condition pour l'écriture atomique qui bloque tant que pas n octects de libres */
	pthread_cond_t *condition_lecture;				/* Variable condition pour les lectures */
	const char *nom_fichier;						/* Nom du fichier qui sera mapé en cas de conduit nommé */
	char *tampon_circulaire;						/* Buffer du conduit (tampon circulaire) */
};




/********************************************************
	Prototypes des fonctions concernant les conduits.
*********************************************************/


struct conduct *conduct_create(const char *name, size_t a, size_t c);

struct conduct *conduct_open(const char *name);

ssize_t conduct_read(struct conduct *c, void *buff, size_t count);

ssize_t conduct_write(struct conduct *c, const void *buff, size_t count);

int conduct_write_eof(struct conduct *c);

void conduct_close(struct conduct *conduct);

void conduct_destroy(struct conduct *conduct);




/*************************************************************
	Prototypes des fonctions concernant les locks/unlocks.
*************************************************************/


int capture_lock(struct conduct *c);

int capture_unlock(struct conduct *c);

