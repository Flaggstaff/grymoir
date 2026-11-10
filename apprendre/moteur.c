/* GrymoiR dans une page web (9 novembre 2026) : le vrai cœur (lexeur, analyse, compilateur, machine, SQLite), compilé
 * en WebAssembly (wasm32-wasip1) par apprendre/construire. La page appelle trois fonctions :
 *   grym_web_allouer(n), grym_web_liberer(p) : la mémoire où la page écrit le programme et lit le résultat ;
 *   grym_web_lancer(programme, reponses) : analyse, compile et exécute, base en mémoire ; les réponses aux questions
 *     (« la réponse à « … » », grammaire § 17) sont données d'avance, une par ligne ;
 *   grym_web_formater(programme) : la forme canonique (grammaire § 12), ce que « grym formater » écrirait.
 * Le résultat est un texte : sa première ligne dit l'issue, « OK », ou « ERREUR<tab>ligne<tab>colonne<tab>message » ;
 * la suite est ce que le programme a affiché, questions et réponses comprises. */
#include "analyseur.h"
#include "compilateur.h"
#include "imprimeur.h"
#include "texte.h"
#include "vm.h"

#include <stdlib.h>
#include <string.h>

#define EXPORTE(nom) __attribute__((export_name(#nom)))

typedef struct { const char *suite; Chaine *transcription; } Reponses;

/* Le lecteur des questions : la ligne suivante des réponses ; plus aucune, la fin de l'entrée. */
static char *lire_reponse(void *contexte, Chaine *sortie, const char *question) {
    Reponses *r = contexte;
    if (sortie->d) {   /* ce qui attendait passe d'abord à la transcription, comme dans un terminal */
        chaine_ajouter(r->transcription, sortie->d);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    chaine_ajouter(r->transcription, question);
    chaine_ajouter(r->transcription, " ");
    if (!r->suite || !*r->suite) {
        chaine_ajouter(r->transcription, "\n");
        return NULL;
    }
    const char *fin = strchr(r->suite, '\n');
    const size_t n = fin ? (size_t)(fin - r->suite) : strlen(r->suite);
    char *ligne = grym_formater("%.*s", (int)n, r->suite);
    if (n && ligne[n - 1] == '\r') ligne[n - 1] = '\0';
    r->suite = fin ? fin + 1 : r->suite + n;
    chaine_ajouter(r->transcription, ligne);
    chaine_ajouter(r->transcription, "\n");
    return ligne;
}

EXPORTE(grym_web_allouer) void *grym_web_allouer(size_t n) { return malloc(n ? n : 1); }
EXPORTE(grym_web_liberer) void grym_web_liberer(void *p) { free(p); }

static char *issue_erreur(const Diagnostic *d, const char *sortie) {
    return grym_formater("ERREUR\t%d\t%d\t%s\n%s", d->ligne, d->colonne, d->message ? d->message : "", sortie ? sortie : "");
}

EXPORTE(grym_web_lancer) char *grym_web_lancer(const char *source, const char *reponses) {
    Portee *portee = portee_creer();
    portee_fichier(portee, "programme.grym");
    Programme p;
    Diagnostic d;
    if (!analyser(source, strlen(source), portee, 0, &p, &d)) {
        char *r = issue_erreur(&d, "");
        diagnostic_liberer(&d);
        portee_detruire(portee);
        return r;
    }
    Module *b = compiler(&p, &d);
    if (!b) {
        char *r = issue_erreur(&d, "");
        diagnostic_liberer(&d);
        programme_liberer(&p);
        portee_detruire(portee);
        return r;
    }
    Machine *m = machine_creer();   /* sans machine_base : la base vit en mémoire et disparaît avec la machine */
    Chaine transcription = { 0 };
    Reponses rep = { reponses, &transcription };
    machine_lecteur(m, lire_reponse, &rep);
    Chaine s = { 0 };
    const int ok = machine_executer(m, b, &s, &d);
    if (s.d) chaine_ajouter(&transcription, s.d);
    free(s.d);
    char *sortie = chaine_rendre(&transcription);
    if (!sortie) sortie = grym_dupliquer("");
    char *r;
    if (ok) r = grym_formater("OK\n%s", sortie);
    else {
        r = issue_erreur(&d, sortie);
        diagnostic_liberer(&d);
    }
    free(sortie);
    machine_detruire(m);
    module_detruire(b);
    programme_liberer(&p);
    portee_detruire(portee);
    return r;
}

EXPORTE(grym_web_formater) char *grym_web_formater(const char *source) {
    Portee *portee = portee_creer();
    portee_fichier(portee, "programme.grym");
    Programme p;
    Diagnostic d;
    char *r;
    if (!analyser(source, strlen(source), portee, 0, &p, &d)) {
        r = issue_erreur(&d, "");
        diagnostic_liberer(&d);
    } else {
        char *canon = imprimer_litteraire(&p);
        r = grym_formater("OK\n%s", canon);
        free(canon);
        programme_liberer(&p);
    }
    portee_detruire(portee);
    return r;
}

/* ---------- La coloration (comme l'atelier, src/atelier/coloration.cpp) ---------- */
#include "lexeur.h"

/* Le même partage des mots que l'atelier et l'extension VS Code : un fichier a les mêmes couleurs partout. Les listes
 * sont recopiées de src/atelier/coloration.cpp (9 novembre 2026) ; tests/apprendre vérifie qu'elles n'ont pas divergé. */
static const char *const CONTROLES[] = { "autrement", "répéter", "essayer", "refuser", "signaler", "utiliser", "quand", "ouvrir",
    "fermer", "chaque", "sortir", "passer", "rendre", "sinon", "selon", "échec", "fois", "pour", "tant", "cas", "que", "si" };
static const char *const MOTS_CLES[] = {
    "décroissant", "enregistrer", "inférieure", "supérieure", "conservées", "différente", "croissant",
    "conservés", "supérieur", "supprimer", "inférieur", "différent", "conserver", "conservée", "afficher",
    "positive", "négative", "nouvelle", "conservé", "positif", "négatif", "nouveau", "devient", "départ",
    "unique", "nouvel", "alors", "nulle", "égale", "dont", "dans", "vaut", "égal", "faux", "vrai", "puis",
    "non", "nul", "les", "une", "pas", "par", "est", "de", "ou", "du", "et", "au", "la", "le", "un", "a",
    "à", "des", "supprimé", "supprimée", "supprimés", "supprimées", "rétablir", "définitivement", "absent",
    "absente", "présent", "présente", "facultatif", "facultative", "parmi", "gagnent", "perdent", "suivi",
    "sur", "saisi", "saisie", "écran", "montre", "liste", "bouton", "texte", "clique", "choisit", "on", "réponse", "hui", "aujourd", "avec", "disparaît", "boucle", "tour", "suivant" };

static int parmi_les(const char *m, const char *const *l, size_t n) {
    for (size_t k = 0; k < n; k++) if (strcmp(m, l[k]) == 0) return 1;
    return 0;
}

/* Une lettre par point de code : c construction, k mot du langage, n nombre, d date, t texte, r remarque, m nom entre
 * crochets, e erreur, o opérateur, « . » rien ; les sauts de ligne restent. Chaque ligne se lit seule, comme dans
 * l'atelier. */
EXPORTE(grym_web_colorer) char *grym_web_colorer(const char *source) {
    Chaine r = { 0 };
    const char *ligne = source;
    for (;;) {
        const char *fin = strchr(ligne, '\n');
        const size_t octets = fin ? (size_t)(fin - ligne) : strlen(ligne);
        size_t points = 0;   /* les points de code de la ligne */
        for (size_t i = 0; i < octets; i++) if (((unsigned char)ligne[i] & 0xC0) != 0x80) points++;
        char *sortes = grym_allouer(points + 1);
        memset(sortes, '.', points);
        sortes[points] = '\0';
        char *erreur = NULL;
        Lexeur *lx = lexeur_creer(ligne, octets, &erreur);
        free(erreur);
        int apres_de = 0;
        while (lx) {
            Jeton j = lexeur_suivant(lx);
            if (j.type == J_FIN) { jeton_liberer(&j); break; }
            const char *m = j.valeur ? j.valeur : "";
            char s = '.';
            switch (j.type) {
            case J_MOT:
                s = parmi_les(m, CONTROLES, sizeof CONTROLES / sizeof *CONTROLES) ? 'c'
                  : parmi_les(m, MOTS_CLES, sizeof MOTS_CLES / sizeof *MOTS_CLES) ? 'k' : '.';
                if (apres_de && strcmp(m, "suivi") == 0) s = '.';   /* « tarif de suivi » (§ 4.4) */
                break;
            case J_ELISION: s = 'k'; break;
            case J_MOT_CLE: s = 'k'; break;
            case J_NOMBRE: case J_ORDINAL: s = 'n'; break;
            case J_DATE: s = 'd'; break;
            case J_TEXTE: s = 't'; break;
            case J_REMARQUE: s = 'r'; break;
            case J_CROCHETS: s = j.synthetique ? '.' : 'm'; break;
            case J_ERREUR: s = 'e'; break;
            default: s = j.type == J_ARTICLE_IMPLICITE ? '.' : 'o'; break;
            }
            apres_de = (j.type == J_MOT && (!strcmp(m, "de") || !strcmp(m, "du") || !strcmp(m, "des")))
                    || (j.type == J_ELISION && !strcmp(m, "d"));
            size_t a = j.debut, b = j.debut + j.longueur;
            if (j.type == J_ERREUR) b = points;
            if (a > points) a = points;
            if (b > points) b = points;
            if (s != '.' && !j.synthetique) memset(sortes + a, s, b > a ? b - a : 0);
            const int arret = j.type == J_ERREUR;
            jeton_liberer(&j);
            if (arret) break;
        }
        if (lx) lexeur_detruire(lx);
        chaine_ajouter(&r, sortes);
        free(sortes);
        if (!fin) break;
        chaine_ajouter(&r, "\n");
        ligne = fin + 1;
    }
    char *t = chaine_rendre(&r);
    return t ? t : grym_dupliquer("");
}
