#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fork, exec, poll, clock_gettime : la commande (3.04) */
#endif
/* GrymoiR : l'interface d'entrée et de sortie en console.
 * Spécification : docs/vm.md (révision 1.32), § 13 ; grammaire, § 4.3, § 17 à 19.
 */
#include "interface.h"

#include <stdlib.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif
#include <string.h>

char *champ_invite(const Champ *c) {
    if (c->question) return grym_dupliquer(c->libelle);
    if (c->valeur) return grym_formater("%s [%s]%s ?", c->libelle, c->valeur, c->videable ? " (- pour vider)" : "");
    return grym_formater("%s ?", c->libelle);
}

/* La ligne, sans espaces ni tabulations autour, est-elle exactement s ? */
static int seule(const char *ligne, const char *s) {
    while (*ligne == ' ' || *ligne == '\t') ligne++;
    size_t n = strlen(s);
    if (strncmp(ligne, s, n) != 0) return 0;
    ligne += n;
    while (*ligne == ' ' || *ligne == '\t') ligne++;
    return *ligne == '\0';
}

static int console_disponible(void *contexte) {
    return ((Console *)contexte)->lire != NULL;
}

static Issue console_formulaire(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                                Validation valider, void *vcontexte) {
    Console *c = contexte;
    for (size_t k = 0; k < n; k++) {
        for (;;) {
            char *invite = champ_invite(&champs[k]);
            char *ligne = c->lire(c->contexte, sortie, invite);
            free(invite);
            if (!ligne) { *arret = k; return ISSUE_FIN; }
            if (seule(ligne, ".")) { free(ligne); *arret = k; return ISSUE_ANNULE; }   /* § 17 */
            champs[k].vider = champs[k].videable && seule(ligne, "-");                  /* § 19 */
            if (champs[k].vider) ligne[0] = '\0';
            champs[k].ligne = ligne;
            char *message = NULL;
            int r = valider(vcontexte, k, &message);
            if (r == 0) break;
            if (r < 0) { free(message); *arret = k; return ISSUE_ARRET; }
            /* la relance s'affiche avant la question suivante ; la première dit comment annuler */
            chaine_ajouter(sortie, message ? message : "");
            free(message);
            if (!c->annonce) {
                chaine_ajouter(sortie, " Tape « . » seul pour annuler.");
                c->annonce = 1;
            }
            chaine_ajouter(sortie, "\n");
        }
    }
    return ISSUE_REPONDU;
}

static void console_effacer(void *contexte, Chaine *sortie) {
    /* hors d'un terminal, rien n'est écrit : une sortie redirigée reste propre (§ 4.3) */
    if (((Console *)contexte)->terminal) chaine_ajouter(sortie, "\033[2J\033[H");
}

Interface console_interface(Console *c) {
    Interface i = { c, console_disponible, console_formulaire, console_effacer, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, console_commande };
    return i;
}

/* ---------- La commande (3.04, grammaire § 15.10) ---------- */

#ifdef _WIN32
int console_commande(void *contexte, const char *programme, const char *const *arguments, size_t nb, char **sortie,
                     size_t *taille, char **erreurs, int *code, char **erreur) {
    (void)contexte; (void)arguments; (void)nb; (void)sortie; (void)taille; (void)erreurs; (void)code;
    *erreur = grym_formater("La commande « %s » ne se lance pas encore dans la console sous Windows : l'atelier le sait.", programme);
    return 0;
}
#else
static long ms_maintenant(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int console_commande(void *contexte, const char *programme, const char *const *arguments, size_t nb, char **sortie,
                     size_t *taille, char **erreurs, int *code, char **erreur) {
    (void)contexte;
    enum { LIMITE = 16 * 1024 * 1024, DELAI_MS = 30000 };
    int tout[2], terr[2], texec[2];   /* la sortie, les erreurs, et l'échec d'exec (fermé à l'exec réussi) */
    if (pipe(tout) != 0) { *erreur = grym_dupliquer("La commande ne peut pas démarrer : plus de tuyau disponible."); return 0; }
    if (pipe(terr) != 0) { close(tout[0]); close(tout[1]); *erreur = grym_dupliquer("La commande ne peut pas démarrer."); return 0; }
    if (pipe(texec) != 0) { close(tout[0]); close(tout[1]); close(terr[0]); close(terr[1]); *erreur = grym_dupliquer("La commande ne peut pas démarrer."); return 0; }
    fcntl(texec[1], F_SETFD, FD_CLOEXEC);
    char **argv = grym_allouer((nb + 2) * sizeof *argv);
    argv[0] = (char *)programme;
    for (size_t k = 0; k < nb; k++) argv[k + 1] = (char *)arguments[k];
    argv[nb + 1] = NULL;
    const pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        close(tout[0]); close(tout[1]); close(terr[0]); close(terr[1]); close(texec[0]); close(texec[1]);
        *erreur = grym_dupliquer("La commande ne peut pas démarrer : le système refuse un processus de plus.");
        return 0;
    }
    if (pid == 0) {   /* l'enfant : sans shell, ses sorties dans les tuyaux, son entrée vide */
        const int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        dup2(tout[1], 1);
        dup2(terr[1], 2);
        close(tout[0]); close(tout[1]); close(terr[0]); close(terr[1]); close(texec[0]);
        execvp(programme, argv);
        const int e = errno;
        ssize_t w = write(texec[1], &e, sizeof e);
        (void)w;
        _exit(127);
    }
    free(argv);
    close(tout[1]); close(terr[1]); close(texec[1]);
    int e_exec = 0;
    const ssize_t lu_exec = read(texec[0], &e_exec, sizeof e_exec);   /* 0 octet : exec a réussi */
    close(texec[0]);
    Chaine out = {0}, err = {0};
    int ouverts = 2, delai = 0, trop = 0, binaire = 0;
    const long fin = ms_maintenant() + DELAI_MS;
    struct pollfd p[2] = { { tout[0], POLLIN, 0 }, { terr[0], POLLIN, 0 } };
    while (lu_exec <= 0 && ouverts > 0) {
        const long reste = fin - ms_maintenant();
        if (reste <= 0) { delai = 1; break; }
        const int r = poll(p, 2, (int)reste);
        if (r < 0) { if (errno == EINTR) continue; break; }
        for (int k = 0; k < 2; k++) {
            if (p[k].fd < 0 || !(p[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char tampon[8192];
            const ssize_t n = read(p[k].fd, tampon, sizeof tampon);
            if (n <= 0) { close(p[k].fd); p[k].fd = -1; ouverts--; continue; }
            Chaine *c = k == 0 ? &out : &err;
            if (c->n + (size_t)n > LIMITE) { trop = 1; continue; }
            for (ssize_t q = 0; q < n; q++) {
                if (!tampon[q]) { if (k == 0) binaire = 1; continue; }   /* un octet nul : pas du texte */
                char u[2] = { tampon[q], 0 };
                chaine_ajouter(c, u);
            }
        }
    }
    for (int k = 0; k < 2; k++) if (p[k].fd >= 0) close(p[k].fd);
    if (delai || trop) kill(pid, SIGKILL);
    int statut = 0;
    while (waitpid(pid, &statut, 0) < 0 && errno == EINTR) {}
    if (lu_exec > 0) {
        free(chaine_rendre(&out)); free(chaine_rendre(&err));
        *erreur = e_exec == ENOENT ? grym_formater("La commande « %s » est introuvable.", programme)
                                   : grym_formater("La commande « %s » ne peut pas démarrer : %s.", programme, strerror(e_exec));
        return 0;
    }
    if (delai || trop) {
        free(chaine_rendre(&out)); free(chaine_rendre(&err));
        *erreur = delai ? grym_formater("La commande « %s » n'a pas fini en 30 secondes : arrêtée.", programme)
                        : grym_formater("La commande « %s » écrit plus de 16 Mo : arrêtée.", programme);
        return 0;
    }
    if (binaire) chaine_ajouter(&out, "\xff");   /* un octet impossible en UTF-8 : la machine dira « pas un texte » */
    *taille = out.n;
    *sortie = chaine_rendre(&out);
    *erreurs = chaine_rendre(&err);
    *code = WIFEXITED(statut) ? WEXITSTATUS(statut) : WIFSIGNALED(statut) ? 128 + WTERMSIG(statut) : -1;
    return 1;
}
#endif
