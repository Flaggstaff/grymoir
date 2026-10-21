/* GrymoiR : machine virtuelle, v0.2
 * Spécification : docs/vm.md (révision 1.32).
 */
#ifndef GRYM_VM_H
#define GRYM_VM_H

#include "analyseur.h"
#include "bytecode.h"
#include "interface.h"
#include "texte.h"

#include <signal.h>

/* Mis à 1 par un gestionnaire de Ctrl+C : l'exécution en cours s'arrête proprement
 * au prochain saut arrière ou appel, et le journal l'annule (docs/vm.md, § 6). */
extern volatile sig_atomic_t grym_interruption;

/* Une machine garde les valeurs des noms d'une exécution à l'autre
 * (boucle interactive, rechargement de blocs). */
typedef struct Machine Machine;

Machine *machine_creer(void);
void machine_detruire(Machine *m);
/* grym exemples (grammaire, § 9.6) : exemples vérifiés ; *rates reçoit ceux qui sont faux ou en erreur. */
size_t machine_exemples(const Machine *m, size_t *rates);

/* Vérifie un module, enregistre ses formules (la machine en prend possession :
 * les entrées correspondantes du module passent à NULL ; une formule du même nom
 * est remplacée), puis exécute le programme. Chaque AFFICHER ajoute une ligne à *sortie.
 * En cas d'échec, le journal d'annulation rend à chaque nom sa valeur d'avant,
 * la table des formules revient à son état d'avant (docs/vm.md, § 6), et *diag décrit l'erreur. */
int machine_executer(Machine *m, Module *module, Chaine *sortie, Diagnostic *diag);

/* Objets encore vivants après le dernier ramassage (pour les tests). */
size_t machine_objets_vivants(const Machine *m);
/* Vérifications complètes de règles depuis la création de la machine ; une règle sautée ne compte pas (docs/vm.md, § 3.3). */
size_t machine_regles_verifiees(const Machine *m);

/* Dossier du programme : les chemins relatifs des fichiers (§ 15.2) partent de là. */
void machine_dossier(Machine *m, const char *dossier);
/* Où s'écrivent les fichiers aux chemins relatifs (Enregistrer, Exporter), s'il diffère du dossier du programme. */
void machine_dossier_ecriture(Machine *m, const char *dossier);
/* Branche un transcripteur de sons sur l'interface en place (§ 15.8). */
void machine_transcripteur(Machine *m, char *(*transcrire)(void *, const unsigned char *, size_t, char **));

/* Fichier de la base des entités (§ 16.5) ; NULL : base en mémoire. Ouverte au premier besoin. */
void machine_base(Machine *m, const char *chemin);

/* La base chiffrée (« La base est chiffrée. », 2.37) : son mot de passe, donné d'avance (NULL : l'oublier), ou un hôte
 * qui le demande au premier besoin (`nouveau` : il n'existe pas encore, à choisir et confirmer) et rend une copie à
 * libérer, ou NULL si l'utilisateur renonce. Le mot de passe reste en mémoire seulement. */
void machine_mot_de_passe_base(Machine *m, const char *mdp);
void machine_demande_mot_de_passe(Machine *m, char *(*demander)(void *contexte, int nouveau), void *contexte);

/* La sortie est un terminal : « Effacer l'écran. » n'écrit sa séquence que dans ce cas (§ 4.3). */
void machine_terminal(Machine *m, int terminal);

/* Lecteur des réponses de l'utilisateur (grammaire, § 17) : la machine écrit d'abord ce qui attend
 * dans `sortie` et la vide, affiche la question, puis rend la ligne tapée, sans son saut de ligne
 * (NULL en fin d'entrée). Sans lecteur, poser une question est une erreur d'exécution. */
void machine_lecteur(Machine *m, char *(*lire)(void *contexte, Chaine *sortie, const char *question),
                     void *contexte);

/* Remplace l'interface d'entrée et de sortie (docs/vm.md, § 13) ; NULL : la console. L'interface est
 * copiée ; son contexte doit vivre aussi longtemps que la machine. */
void machine_interface(Machine *m, const Interface *i);

/* Essai de migration (docs/atelier.md, A2-c) : la prochaine machine_executer prépare la base comme pour une
 * exécution, écrit dans *rapport ce que la migration ferait (une ligne par changement), annule tout, et
 * s'arrête sans exécuter le programme. Une migration refusée est une erreur, dans *diag, comme au lancement.
 * Une base qui n'existe pas encore n'est pas créée : le rapport le dit. */
void machine_essai_migration(Machine *m, Chaine *rapport);

/* Après une exécution ratée : ce qui a été annulé, à dire à l'utilisateur (§ 3.3), ou NULL
 * s'il n'y a rien à dire. À libérer. */
char *machine_annulation(const Machine *m, int interactif);

#endif
