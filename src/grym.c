/* grym : programme principal de GrymoiR, v0.2
 *
 *   grym                                boucle interactive (grammaire, § 3.3)
 *   grym lancer fichier.grym            compile puis exécute
 *   grym lancer fichier.grymb           exécute un bytecode compilé
 *   grym compiler fichier.grym          produit fichier.grymb
 *   grym desassembler fichier.grym(b)   affiche les instructions (docs/vm.md, § 10)
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fileno, isatty */
#endif

#include "analyseur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "imprimeur.h"
#include "vm.h"
#include "texte.h"

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define terminal() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define terminal() isatty(fileno(stdin))
#endif

#define VERSION "0.2"

static char *lire_fichier(FILE *f, size_t *taille) {
    size_t cap = 4096, n = 0;
    char *d = grym_allouer(cap);
    for (;;) {
        if (n == cap) {
            char *e = realloc(d, cap *= 2);
            if (!e) { free(d); return NULL; }
            d = e;
        }
        size_t lu = fread(d + n, 1, cap - n, f);
        n += lu;
        if (lu == 0) break;
    }
    *taille = n;
    return d;
}

/* Une ligne de l'entrée standard, sans le saut de ligne ; NULL en fin d'entrée. */
static char *lire_ligne(void) {
    size_t cap = 256, n = 0;
    char *l = grym_allouer(cap);
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        if (n + 1 >= cap) {
            char *e = realloc(l, cap *= 2);
            if (!e) { free(l); return NULL; }
            l = e;
        }
        l[n++] = (char)c;
    }
    if (c == EOF && n == 0) { free(l); return NULL; }
    if (n && l[n - 1] == '\r') n--;
    l[n] = '\0';
    return l;
}

/* Ctrl+C : la machine s'arrête proprement au prochain saut arrière ou appel (docs/vm.md, § 6). */
static void sur_interruption(int signal_recu) {
    (void)signal_recu;
    grym_interruption = 1;
    signal(SIGINT, sur_interruption);   /* certains systèmes réinstallent le gestionnaire par défaut */
}

static void signaler(const char *fichier, const Diagnostic *d) {
    if (d->ligne) fprintf(stderr, "%s:%d:%d : erreur : %s\n", fichier, d->ligne, d->colonne, d->message);
    else          fprintf(stderr, "%s : erreur : %s\n", fichier, d->message);
}

/* Charge un fichier : bytecode s'il commence par « GRYM », source sinon (compilée). */
static int est_compact(const char *chemin) {
    size_t l = strlen(chemin);
    return l > 6 && strcmp(chemin + l - 6, ".grymc") == 0;
}

static Module *charger(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", chemin);
        return NULL;
    }
    size_t taille = 0;
    char *donnees = lire_fichier(f, &taille);
    fclose(f);
    if (!donnees) return NULL;

    Module *b = NULL;
    if (est_fichier_bytecode((const unsigned char *)donnees, taille)) {
        char *erreur = NULL;
        b = module_lire((const unsigned char *)donnees, taille, &erreur);
        if (!b) {
            fprintf(stderr, "%s : erreur : %s\n", chemin, erreur);
            free(erreur);
        }
    } else {
        Portee *portee = portee_creer();
        Programme p;
        Diagnostic d;
        int lu = est_compact(chemin) ? analyser_compact(donnees, taille, portee, &p, &d)
                                     : analyser(donnees, taille, portee, 0, &p, &d);
        if (!lu) {
            signaler(chemin, &d);
            diagnostic_liberer(&d);
        } else {
            b = compiler(&p, &d);
            if (!b) {
                signaler(chemin, &d);
                diagnostic_liberer(&d);
            }
            programme_liberer(&p);
        }
        portee_detruire(portee);
    }
    free(donnees);
    return b;
}

static int lancer(const char *chemin) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    Machine *m = machine_creer();
    Chaine sortie = {0};
    Diagnostic d;
    grym_interruption = 0;
    int ok = machine_executer(m, b, &sortie, &d);
    if (sortie.d) fputs(sortie.d, stdout);
    free(sortie.d);
    if (!ok) {
        fflush(stdout);
        signaler(chemin, &d);
        diagnostic_liberer(&d);
    }
    machine_detruire(m);
    module_detruire(b);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int compiler_fichier(const char *chemin) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    size_t l = strlen(chemin);
    char *cible = (l > 5 && strcmp(chemin + l - 5, ".grym") == 0)
                ? grym_formater("%sb", chemin)
                : grym_formater("%s.grymb", chemin);
    size_t taille = 0;
    unsigned char *octets = module_serialiser(b, &taille);
    FILE *f = fopen(cible, "wb");
    int ok = f && fwrite(octets, 1, taille, f) == taille;
    if (f && fclose(f) != 0) ok = 0;
    if (ok) printf("%s : %lu octets\n", cible, (unsigned long)taille);
    else fprintf(stderr, "Impossible d'écrire « %s ».\n", cible);
    free(octets);
    free(cible);
    module_detruire(b);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Analyse un fichier source littéraire ; NULL et un message en cas d'erreur. */
static int analyser_fichier(const char *chemin, Programme *p) {
    FILE *f = fopen(chemin, "rb");
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", chemin);
        return 0;
    }
    size_t taille = 0;
    char *source = lire_fichier(f, &taille);
    fclose(f);
    if (!source) return 0;
    Portee *portee = portee_creer();
    Diagnostic d;
    int ok = est_compact(chemin) ? analyser_compact(source, taille, portee, p, &d)
                                 : analyser(source, taille, portee, 0, p, &d);
    if (!ok) {
        signaler(chemin, &d);
        diagnostic_liberer(&d);
    }
    portee_detruire(portee);
    free(source);
    return ok;
}

/* grym formater : forme canonique sur la sortie standard (§ 12), dans la forme du fichier. */
static int formater(const char *chemin) {
    Programme p;
    if (!analyser_fichier(chemin, &p)) return EXIT_FAILURE;
    char *t = est_compact(chemin) ? imprimer_compact(&p) : imprimer_litteraire(&p);
    fputs(t, stdout);
    free(t);
    programme_liberer(&p);
    return EXIT_SUCCESS;
}

/* grym traduire : .grym → .grymc, ou .grymc → .grym (§ 11). Le fichier produit ne doit pas exister :
 * écraser un fichier source en silence ferait perdre du travail. */
static int traduire(const char *chemin) {
    size_t l = strlen(chemin);
    int compact = est_compact(chemin);
    Programme p;
    if (!analyser_fichier(chemin, &p)) return EXIT_FAILURE;
    char *t = compact ? imprimer_litteraire(&p) : imprimer_compact(&p);
    programme_liberer(&p);
    char *cible;
    if (compact) cible = grym_formater("%.*s", (int)(l - 1), chemin);                 /* .grymc → .grym */
    else if (l > 5 && strcmp(chemin + l - 5, ".grym") == 0) cible = grym_formater("%sc", chemin);
    else cible = grym_formater("%s.grymc", chemin);
    FILE *existe = fopen(cible, "rb");
    if (existe) {
        fclose(existe);
        fprintf(stderr, "« %s » existe déjà : supprimez-le ou renommez-le avant de traduire.\n", cible);
        free(cible);
        free(t);
        return EXIT_FAILURE;
    }
    FILE *f = fopen(cible, "wb");
    int ok = f && fputs(t, f) >= 0;
    if (f && fclose(f) != 0) ok = 0;
    if (ok) printf("%s\n", cible);
    else fprintf(stderr, "Impossible d'écrire « %s ».\n", cible);
    free(cible);
    free(t);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int desassembler(const char *chemin) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    char *texte = module_desassembler(b);
    fputs(texte, stdout);
    free(texte);
    module_detruire(b);
    return EXIT_SUCCESS;
}

static int est_quitter(const char *l) {
    while (*l == ' ' || *l == '\t') l++;
    const char *mot = "quitter";
    size_t k = 0;
    while (mot[k] && tolower((unsigned char)l[k]) == mot[k]) k++;
    if (mot[k]) return 0;
    l += k;
    while (*l == ' ' || *l == '\t' || *l == '.') l++;
    return *l == '\0';
}

static int termine_par_deux_points(const char *l) {
    size_t n = strlen(l);
    while (n && (l[n - 1] == ' ' || l[n - 1] == '\t')) n--;
    return n && l[n - 1] == ':';
}

/* Boucle interactive : une saisie ratée n'a aucun effet (grammaire, § 3.3).
 * La machine annule ses propres écritures (journal) ; la portée de l'analyseur,
 * qui a déjà enregistré les noms de la saisie, est restaurée à part. */
static int boucle(void) {
    int tty = terminal();
    if (tty) printf("GrymoiR %s, boucle interactive. Tapez « quitter » pour sortir.\n", VERSION);
    Portee *portee = portee_creer();
    Machine *m = machine_creer();
    for (;;) {
        if (tty) { fputs("> ", stdout); fflush(stdout); }
        char *ligne = lire_ligne();
        if (!ligne) break;
        if (est_quitter(ligne)) { free(ligne); break; }

        /* Une ligne terminée par « : » ouvre un bloc : on lit la suite jusqu'à une ligne vide. */
        if (termine_par_deux_points(ligne)) {
            Chaine saisie = {0};
            chaine_ajouter(&saisie, ligne);
            free(ligne);
            for (;;) {
                if (tty) { fputs("… ", stdout); fflush(stdout); }
                char *suite = lire_ligne();
                if (!suite || suite[strspn(suite, " ")] == '\0') { free(suite); break; }
                chaine_ajouter(&saisie, "\n");
                chaine_ajouter(&saisie, suite);
                free(suite);
            }
            ligne = chaine_rendre(&saisie);
        }

        Portee *sauve = portee_cloner(portee);
        Programme p;
        Diagnostic d;
        Chaine sortie = {0};
        int ok = analyser(ligne, strlen(ligne), portee, 1, &p, &d);
        if (ok) {
            Module *b = compiler(&p, &d);
            ok = b != NULL;
            grym_interruption = 0;   /* un Ctrl+C tapé à l'invite ne compte pas */
            if (ok) ok = machine_executer(m, b, &sortie, &d);
            module_detruire(b);
            programme_liberer(&p);
        }
        if (ok) {
            if (sortie.d) fputs(sortie.d, stdout);
            portee_detruire(sauve);
        } else {
            if (tty && d.ligne == 1 && d.colonne > 0 && !strchr(ligne, '\n'))
                printf("%*s^\n", d.colonne + 1, "");   /* curseur sous la colonne fautive */
            printf("Erreur : %s\n", d.message);
            diagnostic_liberer(&d);
            portee_detruire(portee);
            portee = sauve;
        }
        free(sortie.d);
        free(ligne);
        fflush(stdout);
    }
    if (tty) putchar('\n');
    machine_detruire(m);
    portee_detruire(portee);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    signal(SIGINT, sur_interruption);
    if (argc == 1) return boucle();
    if (argc == 3 && strcmp(argv[1], "lancer") == 0) return lancer(argv[2]);
    if (argc == 3 && strcmp(argv[1], "compiler") == 0) return compiler_fichier(argv[2]);
    if (argc == 3 && strcmp(argv[1], "formater") == 0) return formater(argv[2]);
    if (argc == 3 && strcmp(argv[1], "traduire") == 0) return traduire(argv[2]);
    if (argc == 3 && (strcmp(argv[1], "desassembler") == 0 || strcmp(argv[1], "désassembler") == 0))
        return desassembler(argv[2]);
    fprintf(stderr,
            "GrymoiR %s\n"
            "Usage :\n"
            "  grym                                boucle interactive\n"
            "  grym lancer fichier.grym            compile puis exécute\n"
            "  grym lancer fichier.grymb           exécute un bytecode compilé\n"
            "  grym compiler fichier.grym          produit fichier.grymb\n"
            "  grym desassembler fichier.grym(b)   affiche les instructions\n"
            "  grym formater fichier.grym(c)       affiche la forme canonique\n"
            "  grym traduire fichier.grym          produit la forme compacte fichier.grymc\n"
            "  grym traduire fichier.grymc         produit la forme littéraire fichier.grym\n", VERSION);
    return EXIT_FAILURE;
}
