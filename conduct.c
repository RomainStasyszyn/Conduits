/************************************************************
	Implémentation des fonctions concernant les conduits.
*************************************************************/



#include "conduct.h"



/* ATTENTION EN CAS D'ERREUR ON RETOURNE NULL, ON EVITE DE QUITTER DIRECTEMENT LE PROGRAMME */
/* FAIT */
struct conduct *conduct_create(const char *name, size_t a, size_t c) {
	/* On repositionne la valeur d'errno au cas où */
	errno = 0;
	/* Pointeur vers la nouvelle structure à créer */
    struct conduct *nouveau_conduit = NULL;
	/* Descripteur du fichier qu'on va mapper */
	int descripteur = 0;
	/* Variable servant à savoir si le ftruncate a fonctionné, on s'en sert aussi pour l'initialisation du verrou et des variables condition */
	int truncate_reussi = 0;

	/* Verrou et variables condition pour la structure en cours de création */
	static pthread_mutex_t verrou = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t condition_ecriture = PTHREAD_COND_INITIALIZER;
	static pthread_cond_t condition_ecriture_atomique = PTHREAD_COND_INITIALIZER;
	static pthread_cond_t condition_lecture = PTHREAD_COND_INITIALIZER;
	if(a < 1) {
		errno = -1;
		perror("Problème avec la taille atomique du futur conduit : valeur inférieure à un");
		return NULL;
	}
	if(c < 1) {
		errno = -1;
		perror("Problème avec la capacité du futur conduit : valeur inférieure à un");
		return NULL;
	}
	if(a > c) {
		errno = -1;
		perror("Problème avec la capacité du futur conduit : la capacité totale du conduit est inférieur à la valeur atomique de celui-ci");
		return NULL;
	}
	/* On alloue la mémoire nécessaire pour la structure */
	if((nouveau_conduit = (struct conduct*)malloc(sizeof(struct conduct))) == NULL) {
		perror("Problème avec malloc pour allouer la structure");
		return NULL;
	}
	/* On initialise la structure */
	if((nouveau_conduit = memset(nouveau_conduit, 0, sizeof(struct conduct))) == NULL) {
		perror("Problème avec memset");
		return NULL;
	}
    if(name == NULL) {
    /* On mappe une zone de la mémoire (anonyme) et le nom du fichier lié à la structure est NULL */
		if((nouveau_conduit = mmap(NULL, sizeof(struct conduct), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0L)) == MAP_FAILED) {
    		perror("Problème avec mmap lors de l'ouveture du conduit anonyme");
			return NULL;
    	}
		nouveau_conduit->nom_fichier = NULL;
    }
	/* Création d'un conduit nommé */
    else {
        /* Ici on ouvre le fichier persistant en mémoire */
        if((descripteur = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) {
            perror("Problème lors de l'ouverture du fichier");
			return NULL;
        }
		/* Là on fixe la taille dudit fichier, s'il y a un problème on supprime la référence courante au fichier car il est déjà ouvert à ce moment */
		if((truncate_reussi = ftruncate(descripteur, sizeof(struct conduct))) < 0) {
			perror("Problème avec le truncate lors de l'ouverture du fichier");
			unlink(name);
			return NULL;
		}
		/* Ici on mappe une zone quelconque de la mémoire pour contenir le conduit nommé et son fichier */
        if((nouveau_conduit = mmap(NULL, sizeof(struct conduct) + c * sizeof(char *), PROT_READ | PROT_WRITE, MAP_SHARED, descripteur, 0L)) == MAP_FAILED) {
            perror("Problème avec mmap lors de l'ouveture du conduit nommé");
			return NULL;
        }
		nouveau_conduit->nom_fichier = name;
    }
	/* Remplissage final de la structure après mappage */
    nouveau_conduit->a = a;
    nouveau_conduit->c = c;
    if((nouveau_conduit->tampon_circulaire = malloc(sizeof(char*) * c)) == NULL) {
		perror("Problème lors de l'allocation mémoire du buffer du conduit");
		return NULL;
	}
	//nouveau_conduit->tampon_circulaire = tampon;
	nouveau_conduit->verrou = &verrou;
	nouveau_conduit->condition_ecriture = &condition_ecriture;
	nouveau_conduit->condition_lecture = &condition_lecture;
	nouveau_conduit->condition_ecriture_atomique = &condition_ecriture_atomique;
	/* Redondant suite au memset mais on sait jamais */
    nouveau_conduit->debut_lecture = 0;
    nouveau_conduit->debut_ecriture = 0;
    return nouveau_conduit;
}


/* FAIT */
struct conduct *conduct_open(const char *name) {
	perror("je rentre dans open");
	errno = 0;
	struct conduct *nouveau_conduit = NULL;
    int descripteur = 0;
	/* Si le nom passé en argument est NULL on ouvre rien */
	if(name == NULL) {
		perror("Le nom du conduit à ouvrir est problématique");
		return NULL;
	}
	/* On ouvre le fichier dont le nom est passé en paramètre */
    if((descripteur = open(name, O_RDWR, 0644)) < 0) {
        perror("Problème lors de l'ouverture du fichier");
		return NULL;
    }
	/* On mappe à nouveau la zone indiquée par name qui est un fichier persistant sur disque */
    if((nouveau_conduit = mmap(NULL, sizeof(struct conduct), PROT_READ | PROT_WRITE, MAP_SHARED, descripteur, 0L)) == MAP_FAILED) {
        perror("Problème avec mmap lors de l'ouveture");
		return NULL;
    }
	/* On alloue le nom du fichier qu'on vient d'ouvrir */
	nouveau_conduit->nom_fichier = name;
	perror("je sors d'open");
    return nouveau_conduit;
}


ssize_t conduct_read(struct conduct *c, void *buff, size_t count) {
	if(capture_lock(c) != 0) {
		return -1;
	}
	errno = 0;
	perror("je rentre dans read");
	int compteur = 0;
	char tampon_temporaire[count];
	/* Si le conduit passé en paramètre est NULL alors on ne fait rien */
	if(c == NULL) {
		perror("Tentative de lecture dans un conduit non initialisé");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return 0;
	}
	/* On considère qu'une opération de moins de un octet n'a pas lieu d'être */
	if(count < 1) {
		perror("Tentative de lecture de moins d'un octet");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return 0;
	}
	/* Si le conduit contient EOF et est vide alors on renvoit zéro */
	perror("je suis avant le test vide et eof");
	if(c->place_prise == 0 && c->eof == 1) {
		if(capture_unlock(c) != 0) {
		return -1;
	}
	    return compteur;
	}
	perror("je vais tester si vide");
    /* On bloque si le buffer est vide */
	while(c->place_prise == 0 && c->eof == 0) {
		perror("je boucle car vide dans le read");
		perror("je boucle car vide dans le read après verrou");
		pthread_cond_wait(c->condition_lecture, c->verrou);
		perror("je boucle car vide dans le read après wait");
	}
	/* Ici c'est que le tampon n'est pas vide, ni "fermé" via EOF */
	perror("après le test si vide, je vais tester si je peux lire");
	if(c->debut_lecture != c->debut_ecriture || c->place_prise == c->c) {
		perror("je suis en train de lire avant verrou");
		perror("je suis en train de lire après verrou");
		/* tant qu'on a pas lu autant d'octets que voulu ou que le tampon n'a pas été vidé on continue à lire */
		for(compteur = 0 ; compteur < count && (c->debut_lecture != c->debut_ecriture || c->place_prise == c->c) ; compteur++) {
			perror("je boucle pour lire");
			tampon_temporaire[compteur] = c->tampon_circulaire[c->debut_lecture];
			c->debut_lecture = ( (c->debut_lecture + 1) % (c->c) );
			c->place_prise--;
		}
		if(msync(c, sizeof(c), MS_INVALIDATE) < 0) {
			perror("Problème avec lors de la synchronisation sur disque");
			if(capture_unlock(c) != 0) {
				return -1;
			}
			return -1;
		}
		/* A la fin des lectures on copie le contenu du tampon temporaire dans le tampon passé en paramètre */
		memcpy(buff, tampon_temporaire, compteur);
		/* On signale que les écritures bloquées peuvent se remettre au travail */
		perror("j'ai copié les données je veux signaler");
		pthread_cond_signal(c->condition_ecriture);
		perror("je viens de signaler");
		/* On part du postulat qu'on débloque l'écriture de n octets (n<=a) lorsque a octets sont de nouveau libres (cas écriture atomique) */
		if((c->c - c->place_prise) >= c->a) {
			perror("je signale qu'on peut ecrire n<=a octects");
			pthread_cond_signal(c->condition_ecriture_atomique);
		}
		perror("je vais sortir");
		perror("j'ai relâché le verrou à la fin de read");
	}
	/* Si la lecture est supérieure à la capacité du conduit ou bien a été partielle alors on appelle à nouveau conduct_read */
	if(capture_unlock(c) != 0) {
		return -1;
	}
	return compteur;
}


ssize_t conduct_write(struct conduct *c, const void *buff, size_t count) {
	if(capture_lock(c) != 0) {
		return -1;
	}
	errno = 0;
	perror("je rentre dans write");
	int compteur = 0;
	int ecritures_restantes = 0;
	int ecritures_faites = 0;
	/* Si le conduit passé en paramètre est NULL alors on ne fait rien */
	if(c == NULL) {
		perror("Tentative d'écriture dans un conduit non initialisé");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return 0;
	}
	/* On considère qu'une opération de moins de un octet n'a pas lieu d'être */
	if(count < 1) {
		perror("Tentative d'écriture de moins d'un octet");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return 0;
	}
	/* Si l'écriture est sensée se faire à partir d'un buffer vide ou non initialisé on arrête direct */
	if(buff == NULL || sizeof(buff) == 0) {
		perror("Problème avec le tampon d'où les octets à écrire proviennent qui est vide");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return 0;
	}
    /* Ici on bloque si c'est plein, par contre si pas plein on écrit a ou n octets */
	/* Si un caractère EOF a été inséré on positionne la variable errno à EPIPE et on retourne -1 */
	perror("avant test eof de write");
	if(c->eof == 1) {
		errno = EPIPE;
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return -1;
	}
	perror("avant test si plein dans write");
	/* Si le conduit est plein alors on bloque */
	while(c->place_prise == c->c && c->eof == 0) {
		perror("je boucle car plein dans le write");
		perror("je boucle car plein dans le write verrou pris");
		pthread_cond_wait(c->condition_ecriture, c->verrou);
		perror("je boucle car plein dans le write verrou relaché");
	}
	/* Si le nombre d'octets à lire est inférieur ou égal à la taille atomique du conduit alors on bloque jusqu'à pouvoir écrire count octets */
	if(count <= c->a) {
		perror("je suis en n<=a");
		/* On bloque tant que la place disponible est inférieure à count octets */
		while((c->c - c->place_prise) < count ) {
			perror("je boucle car j'attend n octects avec n<=a");
			pthread_cond_wait(c->condition_ecriture_atomique, c->verrou);
		}
		perror("je chope le verrou avant écriture");
		perror("j'ai chopé le verrou avant écriture");
		for(compteur = 0 ; compteur < count ; compteur++) {
			perror("je suis à l'étape d'écriture (boucle)");
			c->tampon_circulaire[c->debut_ecriture] = ((char *)buff)[compteur];	/* Faire attention avec les opération sur un pointeur générique ! */
			c->debut_ecriture = ( (c->debut_ecriture + 1) % (c->c) );
			c->place_prise++;
		}
		if(msync(c, sizeof(c), MS_INVALIDATE) < 0) {
			perror("Problème avec msync");
			if(capture_unlock(c) != 0) {
		return -1;
	}
			return -1;
		}
		perror("je viens d'écrire, avant signal de lecture : tu peux lire mec");
		pthread_cond_signal(c->condition_lecture);
		perror("je viens d'écrire, après le signal tu peux lire mec");
		perror("je suis à l'étape apres avoir relâché le verrou");
		if(capture_unlock(c) != 0) {
		return -1;
	}
		return compteur;
	}
	else if(count > c->a) {
		perror("j'ecris sans bloquer");
		/* Tant qu'il reste de la place dans le conduit et qu'on a pas encore écrit count octets ou c octets, on écrit (potentiellement de manière partielle) */
		for(compteur = 0 ; (compteur < count) && (c->c >= c->place_prise) ; compteur++) {
			perror("je suis en etape bouclage d'écriture sans blocage");
			c->tampon_circulaire[compteur] = ((char *)buff)[compteur];
			c->debut_ecriture = ( (c->debut_ecriture + 1) % (c->c) );
			c->place_prise++;
		}
		if(msync(c, sizeof(c), MS_INVALIDATE) < 0) {
			perror("Problème avec msync");
			if(capture_unlock(c) != 0) {
				return -1;
			}
			return -1;
		}
		perror("je suis avant signal condition_lecture de n>a");
		pthread_cond_signal(c->condition_lecture);
		perror("je suis après signal condition_lecture de n>a");
		perror("je relâche le verrou après écriture n>a");
		if(capture_unlock(c) != 0) {
			return -1;
		}
		/* Si l'écriture est supérieure à la capacité du conduit ou bien a été partielle alors on appelle à nouveau conduct_write */
		if(count > c->c || compteur < count) {
			ecritures_restantes = count - compteur;
			if((ecritures_faites = conduct_write(c, ((char *)buff + compteur), ecritures_restantes)) > 0) {
				compteur = compteur + ecritures_faites;
			}
		}
		if(capture_unlock(c) != 0) {
			return -1;
		}
		return compteur;
	}
	if(capture_unlock(c) != 0) {
		return -1;
	}
    return 0;
}


/* FAIT */
int conduct_write_eof(struct conduct *c) {
	//pthread_mutex_lock(c->verrou);
	if(capture_lock(c) != 0) {
		return -1;
	}
	errno = 0;
	if(c == NULL) {
		perror("Tentative d'écriture EOF dans un conduit non initialisé");
		pthread_mutex_unlock(c->verrou);
		return 0;
	}
    /* Ici on écrit un caractère de fin de fichier */
    /* Si on essaye d'en réécrire un, ça ne fait rien */
	if((c->eof) == 0) {
		c->eof = 1;
		/* On force tout autre thread à recharger sa version du conduit pour que tous soient sur la même longueur d'onde */
		if(msync(c, sizeof(c), MS_INVALIDATE) < 0) {
			perror("Problème avec msync");
			pthread_mutex_unlock(c->verrou);
			return -1;
		}
		pthread_mutex_unlock(c->verrou);
		return 1;
	}
	//pthread_mutex_unlock(c->verrou);
	if(capture_unlock(c) != 0) {
		return -1;
	}
    return 0;
}


/* FAIT */
void conduct_close(struct conduct *conduct) {
	errno = 0;
	if(conduct == NULL) {
		perror("Tentative de fermeture d'un conduit non initialisé");
	}
	/* On libère le tampon */
	free(conduct->tampon_circulaire);
    /* On vide la structure en la remplissant de zéros */
	conduct = memset(conduct, 0, sizeof(struct conduct));
}


/* FAIT */
void conduct_destroy(struct conduct *conduct) {
	errno = 0;
	if(conduct == NULL) {
		perror("Tentative de destruction d'un conduit non initialisé");
	}
	/* On commence par virer le fichier lié à la structure en cas de conduit nommé */
	if(conduct->nom_fichier != NULL) {
		unlink(conduct->nom_fichier);
	}
	/* Ici on libère la structure et on détruit le mapping */
    conduct_close(conduct);
	munmap(conduct, sizeof(struct conduct));
}




int main(int argc, char **argv) {
	struct conduct *conduit1;
	const char buff[5] = { 's', 'a', 'l', 'u', 't' };
	conduit1 = conduct_create("test_open", 10, 50);
	fprintf(stdout,"avant close");
	conduct_close(conduit1);
	fprintf(stdout,"apres close");
	conduit1 = conduct_open("test_open");
	fprintf(stdout,"avant write");
	conduct_write(conduit1, buff, sizeof(buff));
	fprintf(stdout,"apres write");
	return 0;
	/*pid_t pid;
	struct conduct *conduit1, *conduit2;
	conduit1 = conduct_create(NULL, 10, 50);
	conduit2 = conduct_create(NULL, 10, 50);
	pid = fork();
	if(pid == 0) {
		const char buff[5] = { 's', 'a', 'l', 'u', 't' };
		int res = conduct_write(conduit1, buff, sizeof(buff));
		fprintf(stdout, "Retour 1 : %d\n", res);
		fprintf(stdout, "Contenu : %c %c %c %c %c |\n", conduit1->tampon_circulaire[0],conduit1->tampon_circulaire[1],conduit1->tampon_circulaire[2],conduit1->tampon_circulaire[3],conduit1->tampon_circulaire[4]);
	}
	else if(pid > 0) {
		const char buff[6] = { 's', 'a', 'l', 'u', 't', 'e' };
		int res = conduct_write(conduit2, buff, sizeof(buff));
		fprintf(stdout, "Retour 2 : %d\n", res);
		fprintf(stdout, "Contenu 2 : %c %c %c %c %c %c |\n", conduit2->tampon_circulaire[0],conduit2->tampon_circulaire[1],conduit2->tampon_circulaire[2],conduit2->tampon_circulaire[3],conduit2->tampon_circulaire[4],conduit2->tampon_circulaire[5]);

		wait(NULL);
		char buf[5];
		int resul = conduct_read(conduit1, buf, sizeof(buf));
		fprintf(stdout, "Retour 1 : %d\n", resul);
		fprintf(stdout, "Contenu : %c %c %c %c %c |\n", conduit1->tampon_circulaire[0],conduit1->tampon_circulaire[1],conduit1->tampon_circulaire[2],conduit1->tampon_circulaire[3],conduit1->tampon_circulaire[4]);
		fprintf(stdout, "Contenu buf : %c %c %c %c %c |\n", buf[0],buf[1],buf[2],buf[3],buf[4]);
		fprintf(stdout, "taille : %d\n", (int)sizeof(buf));
	}
	return 0;*/
}
/*
int main(int argc, char **argv) {
	struct conduct *conduit1;
	const char buff[5] = { 's', 'a', 'l', 'u', 't' };
	conduit1 = conduct_create("test_open", 10, 50);
	conduct_close(conduit1);
	conduit1 = conduct_open("test_open");
	conduct_write(conduit1, buff, sizeof(buff));
	return 0;
}*/




/**********************************************
	Fonctions concernant les locks/unlocks.
**********************************************/


int capture_lock(struct conduct *c) {
	if(pthread_mutex_lock(c->verrou) != 0) {
		if(errno == EINVAL) {
			perror("Verrou non initialisé : la structure a-t-elle été crée ?");
			return -1;
		}
		if(errno == EDEADLK) {
			perror("Verrou déjà pris");
			return -1;
		}
	}
	return 0;
}


int capture_unlock(struct conduct *c) {
	if(pthread_mutex_unlock(c->verrou) != 0) {
		if(errno == EINVAL) {
			perror("Verrou non initialisé : la structure a-t-elle été crée ?");
			return -1;
		}
		if(errno == EPERM) {
			perror("Verrou n'appartenant pas au thread appelant");
			return -1;
		}
	}
	return 0;
}




/**************************************
	Lectures et écritures par lots.
**************************************/


ssize_t conduct_readv(struct conduct *c, struct iovec *iov, int iovcnt) {
	errno = 0;
	int compteur = 0;
	int compteur_secondaire = 0;
	int compteur_tertiaire = 0;
	int compteur_buffer = 0;
	int taille_totale_buffer = 0;
	/* On additionne la taille de tous les buffers car on part du principe qu'ils ne sont pas tous de la même taille */
	for(compteur_buffer = 0 ; compteur_buffer < iovcnt ; compteur_buffer++) {
		taille_totale_buffer = taille_totale_buffer + iov[compteur_buffer].iov_len;
	}
	/* On crée le tampon temporaire qu'on enverra à conduct_read */
	char buffer[taille_totale_buffer];
	compteur = conduct_read(c, buffer, taille_totale_buffer);
	if(compteur > 0) {
		compteur_buffer = 0;
		/* On parcourt la liste de structures */
		for(compteur_secondaire = 0 ; compteur_secondaire < iovcnt ; compteur_secondaire++) {
			/* On parcourt les éléments de chaque structure tant que ne dépasse pas la taille de celle-ci et tant qu'on a pas tout recopié */
			for(compteur_tertiaire = 0 ; compteur_tertiaire < iov[compteur_secondaire].iov_len && compteur_buffer < compteur ; compteur_tertiaire++) {
				*( ( (char *) (iov[compteur_secondaire].iov_base) ) + compteur_tertiaire) = buffer[compteur_buffer];
				compteur_buffer++;
			}
		}
	}
	return compteur;
}


ssize_t conduct_writev(struct conduct *c, const struct iovec *iov, int iovcnt) {
	errno = 0;
	int compteur = 0;
	int compteur_buffer = 0;
	int taille_totale_buffer = 0;
	int compteur_tableau_structure = 0;
	int compteur_structure = 0;
	int taille_structure = 0;
	/* On additionne la taille de tous les buffers car on part du principe qu'ils ne sont pas tous de la même taille */
	for(compteur_buffer = 0 ; compteur_buffer < iovcnt ; compteur_buffer++) {
		taille_totale_buffer = taille_totale_buffer + iov[compteur_buffer].iov_len;
	}
	/* On crée le tampon temporaire qu'on enverra à conduct_write */
	char buffer[taille_totale_buffer];
	/* On le remplit */
	for(compteur_tableau_structure = 0 ; compteur_tableau_structure < iovcnt ; compteur_tableau_structure++) {
		taille_structure = iov[compteur_tableau_structure].iov_len;
		for(compteur_structure = 0 ; compteur_structure < taille_structure ; compteur_structure++) {
			buffer[compteur] = *( ( (char *) (iov[compteur_tableau_structure].iov_base) ) + compteur_structure);
			compteur++;
		}
	}
	compteur = conduct_write(c, buffer, taille_totale_buffer);
    return compteur;
}

