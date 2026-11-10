/* grym : programme principal de GrymoiR
 *
 *   grym                                boucle interactive (grammaire, § 3.3)
 *   grym lancer fichier.grym            compile puis exécute
 *   grym lancer fichier.grymb           exécute un bytecode compilé
 *   grym servir fichier.grym            l'application dans le navigateur (docs/v2.md)
 *   grym compiler fichier.grym          produit fichier.grymb
 *   grym desassembler fichier.grym(b)   affiche les instructions (docs/vm.md, § 12)
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 700   /* fileno, isatty ; realpath (--plist) */
#endif

#include "chemins.h"
#include "analyseur.h"
#include "lexeur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "imprimeur.h"
#include "vm.h"
#include "serveur.h"
#include "aiguilleur.h"
#include "tls.h"
#include "lsp.h"
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
#include <termios.h>
#include <unistd.h>
#include <sys/wait.h>
#define terminal() isatty(fileno(stdin))
#endif

#define VERSION "3.2.0"   /* la version publiée (charte, art. 12) : 3.0 le 8 novembre 2026, 3.0.1 le même soir, 3.1 le 10, 3.1.1, 3.1.2 et 3.2 le même jour */

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

/* Lecteur des réponses (grammaire, § 17) : ce qui attend s'affiche, puis la question, puis on lit. */
static char *lire_pour_la_machine(void *contexte, Chaine *sortie, const char *question) {
    (void)contexte;
    if (sortie->d) {
        fputs(sortie->d, stdout);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    fputs(question, stdout);
    fputc(' ', stdout);
    fflush(stdout);
    return lire_ligne();
}

/* Une ligne tapée sans écho (le mot de passe de la base, 2.37) ; hors d'un terminal, une ligne ordinaire. */
static char *lire_masque(const char *invite) {
    fputs(invite, stderr);
    fflush(stderr);
    if (!terminal()) return lire_ligne();
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const int regle = GetConsoleMode(h, &mode) && SetConsoleMode(h, mode & ~(DWORD)ENABLE_ECHO_INPUT);
    char *l = lire_ligne();
    if (regle) SetConsoleMode(h, mode);
#else
    struct termios avant, sans;
    const int regle = tcgetattr(fileno(stdin), &avant) == 0;
    if (regle) {
        sans = avant;
        sans.c_lflag &= ~(tcflag_t)ECHO;
        tcsetattr(fileno(stdin), TCSAFLUSH, &sans);
    }
    char *l = lire_ligne();
    if (regle) tcsetattr(fileno(stdin), TCSAFLUSH, &avant);
#endif
    fputc('\n', stderr);
    return l;
}

/* Un champ « mot de passe » d'un formulaire (la connexion, 3.17) : ce qui attend s'affiche, puis la saisie sans écho. */
static char *lire_masque_pour_la_machine(void *contexte, Chaine *sortie, const char *question) {
    (void)contexte;
    if (sortie->d) {
        fputs(sortie->d, stdout);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    fflush(stdout);
    char *invite = grym_formater("%s ", question);
    char *l = lire_masque(invite);
    free(invite);
    return l;
}

/* « La base est chiffrée. » (2.37) : le mot de passe, demandé à la console ; nouveau, choisi puis confirmé. */
static char *demander_mdp_console(void *contexte, int nouveau) {
    (void)contexte;
    if (!nouveau) return lire_masque("Mot de passe de la base : ");
    fputs("La base est chiffrée : choisis son mot de passe. Sans lui, rien ne l'ouvrira.\n", stderr);
    for (int essai = 0; essai < 3; essai++) {
        char *a = lire_masque("Mot de passe : ");
        if (!a) return NULL;
        char *b = lire_masque("Le même, encore : ");
        if (!b) { free(a); return NULL; }
        const int vide = !*a, pareil = strcmp(a, b) == 0 && !vide;
        memset(b, 0, strlen(b));
        free(b);
        if (pareil) return a;
        memset(a, 0, strlen(a));
        free(a);
        fputs(vide ? "Le mot de passe est vide.\n" : "Les deux saisies diffèrent.\n", stderr);
    }
    return NULL;
}

/* La sortie standard est-elle un terminal ? Sous Windows, le mode virtuel est activé au passage :
 * sans lui, les séquences ECMA-48 s'afficheraient en clair (§ 4.3). */
static int sortie_terminal(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) return 0;
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stdout));
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 0;
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(fileno(stdout));
#endif
}

/* Ctrl+C : la machine s'arrête proprement au prochain saut arrière ou appel (docs/vm.md, § 6). */
static void sur_interruption(int signal_recu) {
    (void)signal_recu;
    grym_interruption = 1;
    signal(SIGINT, sur_interruption);   /* certains systèmes réinstallent le gestionnaire par défaut */
}

static void signaler(const char *fichier, const Diagnostic *d) {
    if (d->fichier) fichier = d->fichier;   /* l'erreur est dans un fichier utilisé (grammaire, § 21) */
    if (d->ligne) fprintf(stderr, "%s:%d:%d : erreur : %s\n", fichier, d->ligne, d->colonne, d->message);
    else          fprintf(stderr, "%s : erreur : %s\n", fichier, d->message);
}

static int est_compact(const char *chemin) {
    size_t l = strlen(chemin);
    return l > 6 && strcmp(chemin + l - 6, ".grymc") == 0;
}

/* Les avertissements d'une source (grammaire 3.20) : sur la sortie d'erreur, sans rien arrêter. */
static void avertir_source(const char *chemin, const char *source, size_t taille) {
    Avertissement *a = NULL;
    const size_t n = avertissements_source(source, taille, &a);
    for (size_t k = 0; k < n; k++)
        fprintf(stderr, "%s:%d:%d : avertissement : %s\n", chemin, a[k].ligne, a[k].colonne, a[k].message);
    avertissements_liberer(a, n);
}

/* Le programme et chacun des fichiers qu'il utilise, une seule fois par commande (« grym scenarios » charge le
 * programme une fois par scénario). La forme compacte n'en a pas. */
static void avertir_programme(const char *chemin, const char *source, size_t taille, const Programme *p) {
    static int dits = 0;
    if (dits) return;
    dits = 1;
    if (!est_compact(chemin)) avertir_source(chemin, source, taille);
    for (size_t k = 0; k < p->nb_fichiers; k++) {
        if (!p->fichiers[k] || est_compact(p->fichiers[k])) continue;
        FILE *f = grym_fopen(p->fichiers[k], "rb");
        if (!f) continue;
        size_t t = 0;
        char *d = lire_fichier(f, &t);
        fclose(f);
        if (d) avertir_source(p->fichiers[k], d, t);
        free(d);
    }
}

/* Charge un fichier : bytecode s'il commence par « GRYM », source sinon (compilée). */

static Module *charger(const char *chemin) {
    FILE *f = grym_fopen(chemin, "rb");
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
        portee_fichier(portee, chemin);
        Programme p;
        Diagnostic d;
        int lu = est_compact(chemin) ? analyser_compact(donnees, taille, portee, &p, &d)
                                     : analyser(donnees, taille, portee, 0, &p, &d);
        if (!lu) {
            signaler(chemin, &d);
            diagnostic_liberer(&d);
        } else {
            avertir_programme(chemin, donnees, taille, &p);
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

/* Dossier du programme et base à côté de lui (§ 15.2, § 16.5). */
static void situer(Machine *m, const char *chemin) {
    const char *barre = strrchr(chemin, '/');
    if (barre) {   /* les chemins de fichiers du programme partent de son dossier (§ 15.2) */
        char *dossier = grym_formater("%.*s", (int)(barre - chemin), chemin);
        machine_dossier(m, *dossier ? dossier : "/");
        free(dossier);
    }
    /* factures.grym, .grymc ou .grymb : base factures.grymd, à côté (§ 16.5) */
    size_t l = strlen(chemin);
    size_t coupe = l > 6 && (strcmp(chemin + l - 6, ".grymc") == 0 || strcmp(chemin + l - 6, ".grymb") == 0) ? l - 6
                 : l > 5 && strcmp(chemin + l - 5, ".grym") == 0 ? l - 5 : l;
    char *base = grym_formater("%.*s.grymd", (int)coupe, chemin);
    machine_base(m, base);
    free(base);
}

static int lancer(const char *chemin) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    Machine *m = machine_creer();
    machine_lecteur(m, lire_pour_la_machine, NULL);
    machine_lecteur_masque(m, lire_masque_pour_la_machine);
    machine_terminal(m, sortie_terminal());
    machine_demande_mot_de_passe(m, demander_mdp_console, NULL);
    situer(m, chemin);
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
        /* ce qui s'est affiché reste, mais rien de ce qui a été écrit (§ 3.3, § 16.6) */
        char *annule = machine_annulation(m, 0);
        if (annule) fprintf(stderr, "%s\n", annule);
        free(annule);
    }
    machine_detruire(m);
    module_detruire(b);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* grym exemples : vérifie les « Exemple : … vaut … » du programme, sans exécuter le reste (grammaire, § 9.6). La base
 * est une base vide, en mémoire : un exemple ne dépend pas des données du jour. */
static int exemples(const char *chemin) {
    const char *point = strrchr(chemin, '.');
    if (point && strcmp(point, ".grymb") == 0) {
        fprintf(stderr, "%s : erreur : les exemples se lisent dans la source (.grym ou .grymc), pas dans le bytecode.\n", chemin);
        return EXIT_FAILURE;
    }
    compilateur_mode_exemples(1);
    Module *b = charger(chemin);
    compilateur_mode_exemples(0);
    if (!b) return EXIT_FAILURE;
    Machine *m = machine_creer();
    situer(m, chemin);
    machine_base(m, ":memory:");
    Chaine sortie = {0};
    Diagnostic d;
    int ok = machine_executer(m, b, &sortie, &d);
    if (sortie.d) fputs(sortie.d, stdout);
    free(sortie.d);
    size_t rates = 0, n = machine_exemples(m, &rates);
    if (!ok) {
        fflush(stdout);
        signaler(chemin, &d);
        diagnostic_liberer(&d);
    } else if (!n) {
        printf("Aucun exemple dans « %s » : « Exemple : le carré de 7 vaut 49. »\n", chemin);
    } else {
        printf("%lu exemple%s, %lu juste%s%s.\n", (unsigned long)n, n > 1 ? "s" : "", (unsigned long)(n - rates),
               n - rates > 1 ? "s" : "", rates ? "" : " : tout est juste");
    }
    machine_detruire(m);
    module_detruire(b);
    return ok && !rates ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* grym scenarios : joue chaque « Scénario « … » : » du programme, seul, sur une base neuve en mémoire, sans exécuter le
 * reste du programme (grammaire, § 9.7). Réussit si tous réussissent. */
static int scenarios(const char *chemin) {
    const char *point = strrchr(chemin, '.');
    if (point && strcmp(point, ".grymb") == 0) {
        fprintf(stderr, "%s : erreur : les scénarios se lisent dans la source (.grym ou .grymc), pas dans le bytecode.\n", chemin);
        return EXIT_FAILURE;
    }
    int total = 0, justes = 0;
    for (int rang = 1;; rang++) {
        compilateur_mode_scenario(rang);
        Module *b = charger(chemin);
        compilateur_mode_scenario(0);
        if (!b) return EXIT_FAILURE;
        const int nb = compilateur_nb_scenarios();
        if (rang > nb) {
            module_detruire(b);
            break;
        }
        total++;
        Machine *m = machine_creer();
        situer(m, chemin);
        machine_base(m, ":memory:");
        machine_mode_scenario(m, 1);   /* l'interface simulée : console, écrans joués par les gestes (3.08) */
        Chaine sortie = {0};
        Diagnostic d;
        int ok = machine_executer(m, b, &sortie, &d);
        char *reste = NULL;
        if (ok && machine_gestes_restants(m, &reste)) {   /* un geste préparé que nul écran n'a reçu : le scénario échoue */
            ok = 0;
            memset(&d, 0, sizeof d);
            d.message = reste;
        }
        if (sortie.d && *sortie.d) fputs(sortie.d, stdout);
        free(sortie.d);
        const char *titre = compilateur_titre_scenario() ? compilateur_titre_scenario() : "";
        if (ok) { justes++; printf("✓ « %s »\n", titre); }
        else {   /* sur la sortie ordinaire, à sa place dans le bilan : où, et pourquoi */
            const char *f = d.fichier ? d.fichier : chemin;
            if (d.ligne) printf("✗ « %s » : %s:%d:%d : %s\n", titre, f, d.ligne, d.colonne, d.message);
            else printf("✗ « %s » : %s : %s\n", titre, f, d.message);
            diagnostic_liberer(&d);
        }
        machine_detruire(m);
        module_detruire(b);
    }
    compilateur_mode_scenario(-1);
    if (!total) printf("Aucun scénario dans « %s » : « Scénario « une relance se crée » : ».\n", chemin);
    else printf("%d scénario%s, %d réussi%s%s.\n", total, total > 1 ? "s" : "", justes, justes > 1 ? "s" : "",
                justes == total ? " : tout est juste" : "");
    return total && justes == total ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* grym migration : ce que le prochain lancement fera à la base, sans rien changer (docs/atelier.md, A2-c). */
static int migration(const char *chemin) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    Machine *m = machine_creer();
    situer(m, chemin);
    Chaine rapport = {0};
    machine_essai_migration(m, &rapport);
    Chaine sortie = {0};
    Diagnostic d;
    int ok = machine_executer(m, b, &sortie, &d);
    free(sortie.d);
    if (rapport.d) fputs(rapport.d, stdout);
    free(rapport.d);
    if (!ok) {
        fprintf(stderr, "Le prochain lancement sera refusé, et la base restera telle quelle :\n");
        signaler(chemin, &d);
        diagnostic_liberer(&d);
    }
    machine_detruire(m);
    module_detruire(b);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Ouvre l'adresse dans le navigateur de la machine. Elle ne contient que des chiffres, de l'hexadécimal
 * et des signes fixes : rien ne peut s'y glisser vers le shell. */
static void ouvrir_navigateur(const char *adresse) {
    for (const char *p = adresse; *p; p++)
        if (!strchr("abcdefghijklmnopqrstuvwxyz0123456789:/.?=", *p)) return;
#ifdef _WIN32
    char *commande = grym_formater("start \"\" \"%s\"", adresse);
#elif defined(__APPLE__)
    char *commande = grym_formater("open '%s'", adresse);
#else
    char *commande = grym_formater("xdg-open '%s' >/dev/null 2>&1 &", adresse);
#endif
    if (system(commande) != 0) fprintf(stderr, "Ouvre toi-même l'adresse ci-dessus dans un navigateur.\n");
    free(commande);
}

/* grym certificat dossier cabinet nom… : l'autorité du cabinet (une fois) et le certificat du serveur (docs/v2.md, § 12). */
static int certificat(const char *dossier, const char *cabinet, const char *const *noms, size_t nb) {
    char *erreur = NULL;
    if (!tls_preparer_certificats(dossier, cabinet, noms, nb, &erreur)) {
        fprintf(stderr, "%s\n", erreur);
        free(erreur);
        return EXIT_FAILURE;
    }
    printf("Dans « %s » :\n"
           "  autorite.crt  l'autorité du cabinet : à installer une fois sur chaque poste, comme autorité de confiance\n"
           "  autorite.cle  sa clé : à garder hors du serveur, en lieu sûr ; elle signe les certificats suivants\n"
           "  serveur.crt   le certificat du serveur, pour ", dossier);
    for (size_t k = 0; k < nb; k++) printf("%s%s", k ? ", " : "", noms[k]);
    printf(" (800 jours)\n  serveur.cle   sa clé, lue par « grym servir --reseau »\n");
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <wincred.h>
/* Windows : le Gestionnaire d'identification (8 novembre 2026), une identification générique de ce nom, rangée par
 * « cmdkey /generic:nom /user:grymoir /pass » (cmdkey demande le mot de passe) ; son secret, en UTF-16, devient de
 * l'UTF-8. */
static char *lire_trousseau(const char *nom, char **erreur) {
    int n = MultiByteToWideChar(CP_UTF8, 0, nom, -1, NULL, 0);
    wchar_t *w = grym_allouer((size_t)(n > 0 ? n : 1) * sizeof *w);
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, nom, -1, w, n)) w[0] = 0;
    PCREDENTIALW c = NULL;
    const BOOL ok = CredReadW(w, CRED_TYPE_GENERIC, 0, &c);
    free(w);
    char *mdp = NULL;
    if (ok && c && c->CredentialBlobSize >= 2) {
        const int nc = (int)(c->CredentialBlobSize / 2);
        const wchar_t *s = (const wchar_t *)c->CredentialBlob;
        int l = WideCharToMultiByte(CP_UTF8, 0, s, nc, NULL, 0, NULL, NULL);
        if (l > 0) {
            mdp = grym_allouer((size_t)l + 1);
            WideCharToMultiByte(CP_UTF8, 0, s, nc, mdp, l, NULL, NULL);
            mdp[l] = 0;
        }
        SecureZeroMemory(c->CredentialBlob, c->CredentialBlobSize);
    }
    if (c) CredFree(c);
    if (!mdp || !*mdp) {
        free(mdp);
        *erreur = grym_formater("Le Gestionnaire d'identification n'a pas de mot de passe « %s ». Range-le une fois : "
                                "cmdkey /generic:\"%s\" /user:grymoir /pass", nom, nom);
        return NULL;
    }
    return mdp;
}
#else
/* Le mot de passe de la base, rangé dans le trousseau du système sous ce nom (8 novembre 2026, docs/v2.md, § 11) :
 * macOS, « security find-generic-password -a $USER -s nom -w » ; ailleurs, « secret-tool lookup service nom »
 * (libsecret). Sans shell, sans écho ; rien ne s'écrit sur le disque. NULL et *erreur (à libérer) sinon. */
static char *lire_trousseau(const char *nom, char **erreur) {
    int t[2];
    if (pipe(t) != 0) { *erreur = grym_dupliquer("Le trousseau ne se lit pas : plus de tuyau disponible."); return NULL; }
    const char *compte = getenv("USER") ? getenv("USER") : "";
    pid_t p = fork();
    if (p == 0) {
        dup2(t[1], STDOUT_FILENO);
        close(t[0]);
        close(t[1]);
#ifdef __APPLE__
        execl("/usr/bin/security", "security", "find-generic-password", "-a", compte, "-s", nom, "-w", (char *)NULL);
#else
        (void)compte;
        execlp("secret-tool", "secret-tool", "lookup", "service", nom, (char *)NULL);
#endif

        _exit(127);
    }
    close(t[1]);
    Chaine c = {0};
    char b[512];
    for (ssize_t n; p > 0 && (n = read(t[0], b, sizeof b - 1)) > 0;) {
        b[n] = 0;
        chaine_ajouter(&c, b);
    }
    close(t[0]);
    int etat = 1;
    if (p > 0) waitpid(p, &etat, 0);
    char *mdp = c.d ? c.d : grym_dupliquer("");
    size_t l = strlen(mdp);
    while (l && (mdp[l - 1] == '\n' || mdp[l - 1] == '\r')) mdp[--l] = 0;
    if (p < 0 || !WIFEXITED(etat) || WEXITSTATUS(etat) != 0 || !l) {
        memset(mdp, 0, l);
        free(mdp);
#ifdef __APPLE__
        *erreur = grym_formater("Le trousseau n'a pas de mot de passe « %s » pour ce compte. Range-le une fois : "
                                "security add-generic-password -a \"$USER\" -s \"%s\" -U -w", nom, nom);
#else
        *erreur = grym_formater("Le trousseau n'a pas de mot de passe « %s » (secret-tool, libsecret). Range-le une fois : "
                                "secret-tool store --label=\"%s\" service \"%s\"", nom, nom, nom);
#endif
        return NULL;
    }
    return mdp;
}
#endif

/* Une variable d'environnement pour les sessions, sur chaque système. */
static void poser_env(const char *nom, const char *valeur) {
#ifdef _WIN32
    _putenv_s(nom, valeur);
#else
    setenv(nom, valeur, 1);
#endif
}

/* Le démarrage automatique (8 novembre et 9 novembre 2026, docs/v2.md, § 11) : « --plist » écrit l'agent launchd de macOS,
 * « --systemd » l'unité utilisateur de systemd (Linux), « --tache » la tâche planifiée de Windows. Chacun relance la même
 * commande à l'ouverture de session et la garde en vie. Les chemins deviennent absolus : aucun des trois ne part du
 * dossier courant. Sources : Apple, « Creating Launch Daemons and Agents » ; systemd.service(5), systemd.unit(5) et
 * loginctl(1) ; Microsoft, « Task Scheduler Schema » (learn.microsoft.com/windows/win32/taskschd/task-scheduler-schema). */
static void xml(const char *s) {
    for (; *s; s++) {
        if (*s == '&') fputs("&amp;", stdout);
        else if (*s == '<') fputs("&lt;", stdout);
        else if (*s == '>') fputs("&gt;", stdout);
        else if (*s == '"') fputs("&quot;", stdout);
        else putchar(*s);
    }
}

/* Le chemin absolu de grym lui-même, ou NULL. */
static char *chemin_de_grym(const char *argv0) {
#ifdef _WIN32
    char tampon[MAX_PATH];
    const DWORD n = GetModuleFileNameA(NULL, tampon, MAX_PATH);
    (void)argv0;
    return n > 0 && n < MAX_PATH ? grym_dupliquer(tampon) : NULL;
#else
    return realpath(argv0, NULL);
#endif
}

/* La valeur de l'option k est-elle un chemin (rendu absolu dans le fichier de démarrage) ? */
static int option_de_chemin(char **argv, int k) {
    return k > 3 && (strcmp(argv[k - 1], "--reseau") == 0 || strcmp(argv[k - 1], "--base") == 0
                     || strcmp(argv[k - 1], "--ecriture") == 0);
}

/* Les options de la commande à relancer, l'option de démarrage retirée, les chemins absolus : dans le tableau donné,
 * à libérer par l'appelant ; rend leur nombre. */
static int options_de_demarrage(int argc, char **argv, const char *forme, char **options) {
    int n = 0;
    for (int k = 3; k < argc; k++) {
        if (strcmp(argv[k], forme) == 0) continue;
        char *absolu = option_de_chemin(argv, k) ? chemin_absolu(argv[k]) : NULL;
        options[n++] = absolu ? absolu : grym_dupliquer(argv[k]);
    }
    options[n++] = grym_dupliquer("--sans-navigateur");
    return n;
}

/* « ch.grymoir.Praxis.grym » : l'étiquette du service, en lettres, chiffres et points. */
static char *etiquette_de(const char *nom) {
    char *etiquette = grym_formater("ch.grymoir.%s", nom);
    for (char *p = etiquette; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.')) *p = '-';
    return etiquette;
}

/* Un argument d'une ligne de commande systemd : entre guillemets doubles, « \ » et « " » protégés (systemd.service(5),
 * « Command lines »). */
static void argument_systemd(const char *s) {
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

/* Un argument d'une ligne de commande Windows, ajouté à la chaîne : entre guillemets doubles s'il contient un espace, les
 * guillemets intérieurs précédés d'une barre oblique inverse (Microsoft, « Parsing C command-line arguments »). */
static void argument_windows(Chaine *c, const char *s) {
    const int citer = strchr(s, ' ') != NULL || !*s;
    if (citer) chaine_ajouter(c, "\"");
    for (; *s; s++) {
        if (*s == '"') chaine_ajouter(c, "\\");
        { char u[2] = { *s, 0 }; chaine_ajouter(c, u); }
    }
    if (citer) chaine_ajouter(c, "\"");
}

static int demarrage(int argc, char **argv, const char *forme) {
    char *grym = chemin_de_grym(argv[0]), *prog = chemin_absolu(argv[2]);
    if (!grym || !prog) {
        fprintf(stderr, "%s : le chemin de grym ou du programme ne se résout pas.\n", forme);
        free(grym);
        free(prog);
        return EXIT_FAILURE;
    }
    const char *nom = prog;
    for (const char *p = prog; *p; p++) if (*p == '/' || *p == '\\') nom = p + 1;
    char *etiquette = etiquette_de(nom);
    char *options[64];
    const int n = argc - 3 + 1 <= 64 ? options_de_demarrage(argc, argv, forme, options) : 0;
    if (!n) {
        fprintf(stderr, "%s : trop d'options.\n", forme);
        free(etiquette); free(grym); free(prog);
        return EXIT_FAILURE;
    }
    const char *home = getenv("HOME") ? getenv("HOME") : "/tmp";
    if (strcmp(forme, "--plist") == 0) {
        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n    <key>Label</key>\n    <string>");
        xml(etiquette);
        printf("</string>\n    <key>ProgramArguments</key>\n    <array>\n        <string>");
        xml(grym);
        printf("</string>\n        <string>servir</string>\n        <string>");
        xml(prog);
        printf("</string>\n");
        for (int k = 0; k < n; k++) {
            printf("        <string>");
            xml(options[k]);
            printf("</string>\n");
        }
        printf("    </array>\n    <key>RunAtLoad</key>\n    <true/>\n    <key>KeepAlive</key>\n    <true/>\n"
               "    <key>StandardOutPath</key>\n    <string>");
        xml(home);
        printf("/Library/Logs/GrymoiR-");
        xml(nom);
        printf(".log</string>\n    <key>StandardErrorPath</key>\n    <string>");
        xml(home);
        printf("/Library/Logs/GrymoiR-");
        xml(nom);
        printf(".log</string>\n</dict>\n</plist>\n");
    } else if (strcmp(forme, "--systemd") == 0) {
        /* une unité de l'utilisateur (~/.config/systemd/user/), relancée toujours ; le journal, par journalctl --user */
        printf("[Unit]\nDescription=GrymoiR, le service %s\nAfter=network.target\n\n[Service]\nType=simple\nExecStart=", nom);
        argument_systemd(grym);
        printf(" servir ");
        argument_systemd(prog);
        for (int k = 0; k < n; k++) { putchar(' '); argument_systemd(options[k]); }
        printf("\nRestart=always\nRestartSec=5\n\n[Install]\nWantedBy=default.target\n");
    } else {
        /* une tâche planifiée, à importer par « schtasks /Create /TN nom /XML fichier » ; déclenchée à l'ouverture de
         * session, relancée sur échec (RestartOnFailure), sans limite de durée (PT0S) */
        printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
               "  <RegistrationInfo>\n    <Description>GrymoiR, le service ");
        xml(nom);
        printf("</Description>\n  </RegistrationInfo>\n  <Triggers>\n    <LogonTrigger>\n      <Enabled>true</Enabled>\n"
               "    </LogonTrigger>\n  </Triggers>\n  <Settings>\n    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
               "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
               "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
               "    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n    <StartWhenAvailable>true</StartWhenAvailable>\n"
               "    <RestartOnFailure>\n      <Interval>PT1M</Interval>\n      <Count>999</Count>\n    </RestartOnFailure>\n"
               "  </Settings>\n  <Actions Context=\"Author\">\n    <Exec>\n      <Command>");
        xml(grym);
        printf("</Command>\n      <Arguments>servir ");
        {   /* la ligne d'arguments, citée pour Windows, puis protégée pour le XML */
            Chaine c = {0};
            argument_windows(&c, prog);
            for (int k = 0; k < n; k++) { chaine_ajouter(&c, " "); argument_windows(&c, options[k]); }
            char *ligne = chaine_rendre(&c);
            xml(ligne);
            free(ligne);
        }
        printf("</Arguments>\n    </Exec>\n  </Actions>\n</Task>\n");
    }
    for (int k = 0; k < n; k++) free(options[k]);
    free(etiquette);
    free(grym);
    free(prog);
    return EXIT_SUCCESS;
}

/* grym servir : l'application dans le navigateur (docs/v2.md). */
static int servir(const char *chemin, int port, int navigateur, const char *base, const char *ecriture) {
    Module *b = charger(chemin);
    if (!b) return EXIT_FAILURE;
    const char *nom = strrchr(chemin, '/');
    char *erreur = NULL;
    Serveur *s = serveur_ouvrir(port, nom ? nom + 1 : chemin, &erreur);
    if (!s) {
        fprintf(stderr, "%s\n", erreur);
        free(erreur);
        module_detruire(b);
        return EXIT_FAILURE;
    }
    printf("« %s » est servi à l'adresse :\n  %s\nCtrl+C pour arrêter.\n", chemin, serveur_adresse(s));
    fflush(stdout);
    if (navigateur) ouvrir_navigateur(serveur_adresse(s));
    Machine *m = machine_creer();
    Interface i = serveur_interface(s);
    machine_interface(m, &i);
    machine_demande_mot_de_passe(m, serveur_demander_mot_de_passe, s);   /* une base chiffrée (2.37) */
    situer(m, chemin);
    /* --base, --ecriture (8 novembre 2026) : la base et le dossier d'une application fabriquée, pour la servir à
     * plusieurs postes sans la déplacer (docs/v2.md, § 11) */
    if (base) machine_base(m, base);
    if (ecriture) machine_dossier_ecriture(m, ecriture);
    Chaine sortie = {0};
    Diagnostic d;
    grym_interruption = 0;
    int ok = machine_executer(m, b, &sortie, &d);
    char *message = NULL, *annule = NULL;
    int interrompu = !ok && strcmp(d.message, "Interrompu (Ctrl+C).") == 0;
    if (!ok) {
        signaler(chemin, &d);
        message = d.ligne ? grym_formater("Erreur, ligne %d : %s", d.ligne, d.message) : grym_formater("Erreur : %s", d.message);
        annule = machine_annulation(m, 0);
        if (annule) fprintf(stderr, "%s\n", annule);
        diagnostic_liberer(&d);
    }
    grym_interruption = 0;
    if (!interrompu) serveur_terminer(s, &sortie, message, annule);   /* Ctrl+C : on s'arrête tout de suite */
    free(message);
    free(annule);
    free(sortie.d);
    serveur_fermer(s);
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
    FILE *f = grym_fopen(cible, "wb");
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
    FILE *f = grym_fopen(chemin, "rb");
    if (!f) {
        fprintf(stderr, "Impossible d'ouvrir « %s ».\n", chemin);
        return 0;
    }
    size_t taille = 0;
    char *source = lire_fichier(f, &taille);
    fclose(f);
    if (!source) return 0;
    Portee *portee = portee_creer();
    portee_fichier(portee, chemin);
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
    FILE *existe = grym_fopen(cible, "rb");
    if (existe) {
        fclose(existe);
        fprintf(stderr, "« %s » existe déjà : supprime-le ou renomme-le avant de traduire.\n", cible);
        free(cible);
        free(t);
        return EXIT_FAILURE;
    }
    FILE *f = grym_fopen(cible, "wb");
    int ok = f && fputs(t, f) >= 0;
    if (f && fclose(f) != 0) ok = 0;
    if (ok) {
        printf("%s\n", cible);
        /* Les deux formes côte à côte : « Utiliser » refuse ce nom tant qu'elles coexistent (§ 21). */
        fprintf(stderr, "Remarque : « %s » et « %s » sont maintenant côte à côte. Un fichier qui les utilise "
                        "(« Utiliser … ») refusera tant que les deux formes existent : garde celle que tu travailles.\n",
                chemin, cible);
    }
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
static int boucle(const char *base) {
    int tty = terminal();
    if (tty) printf("GrymoiR %s, boucle interactive. Tape « quitter » pour sortir.\n", VERSION);
    Portee *portee = portee_creer();
    Machine *m = machine_creer();
    machine_base(m, base);   /* NULL : base en mémoire, perdue à la sortie (§ 16.5) */
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
        int execute = 0;
        if (ok) {
            Module *b = compiler(&p, &d);
            ok = b != NULL;
            grym_interruption = 0;   /* un Ctrl+C tapé à l'invite ne compte pas */
            if (ok) { execute = 1; ok = machine_executer(m, b, &sortie, &d); }
            module_detruire(b);
            programme_liberer(&p);
        }
        if (ok) {
            if (sortie.d) fputs(sortie.d, stdout);
            portee_detruire(sauve);
        } else {
            if (tty && d.ligne == 1 && d.colonne > 0 && !strchr(ligne, '\n'))
                printf("%*s^\n", d.colonne + 1, "");   /* curseur sous la colonne fautive */
            if (execute && sortie.d) fputs(sortie.d, stdout);   /* ce qui s'est affiché avant l'erreur */
            printf("Erreur : %s\n", d.message);
            diagnostic_liberer(&d);
            if (execute) {
                char *annule = machine_annulation(m, 1);
                if (annule) printf("%s\n", annule);
                free(annule);
            }
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
    if (argc == 1) return boucle(NULL);
    if (argc == 3 && strcmp(argv[1], "--base") == 0) return boucle(argv[2]);
    if (argc == 2 && strcmp(argv[1], "lsp") == 0) return lsp_servir(stdin, stdout);   /* docs/lsp.md */
    if (argc == 3 && strcmp(argv[1], "lancer") == 0) return lancer(argv[2]);
    if (argc == 3 && strcmp(argv[1], "exemples") == 0) return exemples(argv[2]);
    if (argc == 3 && strcmp(argv[1], "scenarios") == 0) return scenarios(argv[2]);   /* § 9.7 */
    if (argc >= 3 && strcmp(argv[1], "servir") == 0) {   /* grym servir app.grym [--port N] [--sans-navigateur] */
        for (int k = 3; k < argc; k++)
            if (strcmp(argv[k], "--plist") == 0 || strcmp(argv[k], "--systemd") == 0 || strcmp(argv[k], "--tache") == 0)
                return demarrage(argc, argv, argv[k]);   /* le démarrage automatique (docs/v2.md, § 11) */
        int port = 0, navigateur = 1, bon = 1, sessions = 0, inactivite = 30;
        const char *reseau = NULL;   /* le dossier des certificats : sur le réseau local, en TLS (docs/v2.md, § 12) */
        const char *base = NULL, *ecriture = NULL;   /* la base, le dossier d'écriture imposés (docs/v2.md, § 11) */
        const char *trousseau = NULL;   /* le nom du mot de passe de la base dans le trousseau (démarrage automatique) */
        for (int k = 3; k < argc && bon; k++) {
            if (strcmp(argv[k], "--sans-navigateur") == 0) navigateur = 0;
            else if (strcmp(argv[k], "--sessions") == 0) sessions = 1;   /* 3.18 : une session par navigateur */
            else if (strcmp(argv[k], "--reseau") == 0 && k + 1 < argc) { reseau = argv[++k]; sessions = 1; }   /* § 12 */
            else if (strcmp(argv[k], "--base") == 0 && k + 1 < argc) base = argv[++k];
            else if (strcmp(argv[k], "--ecriture") == 0 && k + 1 < argc) ecriture = argv[++k];
            else if (strcmp(argv[k], "--trousseau") == 0 && k + 1 < argc) { trousseau = argv[++k]; sessions = 1; }
            else if (strcmp(argv[k], "--inactivite") == 0 && k + 1 < argc) {
                char *e = NULL;
                long n = strtol(argv[++k], &e, 10);
                bon = *e == '\0' && n > 0 && n <= 24 * 60;
                inactivite = (int)n;
            }
            else if (strcmp(argv[k], "--port") == 0 && k + 1 < argc) {
                char *e = NULL;
                long p = strtol(argv[++k], &e, 10);
                bon = *e == '\0' && p > 0 && p < 65536;
                port = (int)p;
            } else bon = 0;
        }
        if (bon && sessions) {
            /* le programme doit se compiler avant qu'aucune session ne parte : l'erreur se voit ici, une fois */
            Module *b = charger(argv[2]);
            if (!b) return EXIT_FAILURE;
            const int chiffree = b->base_chiffree;
            module_detruire(b);
            if (chiffree && trousseau) {   /* sans personne au clavier : le démarrage automatique (§ 11) */
                char *pb = NULL, *mdp = lire_trousseau(trousseau, &pb);
                if (!mdp) {
                    fprintf(stderr, "%s\n", pb);
                    free(pb);
                    return EXIT_FAILURE;
                }
                poser_env("GRYM_MOT_DE_PASSE_BASE", mdp);
                memset(mdp, 0, strlen(mdp));
                free(mdp);
            }
            if (chiffree && !getenv("GRYM_MOT_DE_PASSE_BASE")) {
                /* 3.18 : une base chiffrée, son mot de passe une fois ici, pour toutes les sessions ; les postes
                 * n'ont pas à le connaître (chacun se connecte comme le programme le demande) */
                char *mdp = lire_masque("Mot de passe de la base, pour toutes les sessions : ");
                if (!mdp || !*mdp) {
                    fputs("Sans le mot de passe de la base, rien ne s'ouvre.\n", stderr);
                    free(mdp);
                    return EXIT_FAILURE;
                }
                poser_env("GRYM_MOT_DE_PASSE_BASE", mdp);
                memset(mdp, 0, strlen(mdp));
                free(mdp);
            }
            return aiguilleur_servir(argv[0], argv[2], port, navigateur, inactivite, reseau, base, ecriture, ouvrir_navigateur);
        }
        if (bon) return servir(argv[2], port, navigateur, base, ecriture);
    }
    if (argc >= 5 && strcmp(argv[1], "certificat") == 0)   /* grym certificat dossier cabinet nom… (docs/v2.md, § 12) */
        return certificat(argv[2], argv[3], (const char *const *)(argv + 4), (size_t)(argc - 4));
    if (argc == 3 && strcmp(argv[1], "compiler") == 0) return compiler_fichier(argv[2]);
    if (argc == 3 && strcmp(argv[1], "migration") == 0) return migration(argv[2]);
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
            "  grym servir fichier.grym            l'application dans le navigateur\n"
            "    [--sessions] [--inactivite M]     une session par navigateur (docs/v2.md, § 11)\n"
            "    [--reseau dossier]                sur le réseau local, en TLS, avec ses certificats (§ 12)\n"
            "    [--base f.grymd] [--ecriture d]   la base et le dossier d'écriture d'une application fabriquée\n"
            "    [--trousseau nom]                 le mot de passe de la base, pris dans le trousseau du système\n"
            "    [--plist] [--systemd] [--tache]   le fichier qui démarre ce service à l'ouverture de session :\n"
            "                                      launchd (macOS), systemd (Linux), tâche planifiée (Windows)\n"
            "    [--port N] [--sans-navigateur]\n"
            "  grym certificat dossier cabinet nom…  l'autorité du cabinet et le certificat du serveur (§ 12)\n"
            "  grym compiler fichier.grym          produit fichier.grymb\n"
            "  grym migration fichier.grym         ce que le prochain lancement fera à la base, sans rien changer\n"
            "  grym exemples fichier.grym          vérifie les exemples du programme, sans exécuter le reste\n"
            "  grym scenarios fichier.grym         joue chaque scénario seul, sur une base neuve, sans exécuter le reste\n"
            "  grym desassembler fichier.grym(b)   affiche les instructions\n"
            "  grym --base fichier.grymd           boucle interactive sur une base conservée\n"
            "  grym lsp                            serveur d'aide à la saisie (protocole LSP)\n"
            "  grym formater fichier.grym(c)       affiche la forme canonique\n"
            "  grym traduire fichier.grym          produit la forme compacte fichier.grymc\n"
            "  grym traduire fichier.grymc         produit la forme littéraire fichier.grym\n", VERSION);
    return EXIT_FAILURE;
}
