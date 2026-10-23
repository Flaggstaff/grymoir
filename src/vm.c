/* GrymoiR : machine virtuelle à pile, v0.2
 * Spécification : docs/vm.md (révision 1.32).
 */
#include "chemins.h"
#include "vm.h"
#include "json.h"
#include "qr.h"
#include "coffre.h"
#include "teinte.h"
#include "vm_interne.h"
#include "base.h"
#include "lexeur.h"
#include "date.h"
#include "decimal.h"
#include "types.h"
#include "empreinte.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

static char *morceau_de_zone(const char *s2, char sep);   /* plus bas, avec les bornes (§ 22.6) */

/* ---------------------------------------------------------------- */
/* Valeurs (docs/vm.md, § 2)                                        */
/* ---------------------------------------------------------------- */


static const char *nom_type(TypeValeur t) {
    return t == V_NOMBRE ? "un nombre" : t == V_TEXTE ? "un texte" : t == V_BOOLEEN ? "un booléen"
         : t == V_DATE ? "une date" : t == V_HEURE ? "une heure" : t == V_ANNEE ? "une année" : t == V_FICHIER ? "un fichier" : t == V_LISTE ? "une liste"
         : t == V_ABSENT ? "absente" : t == V_LIGNE ? "une ligne de tableau" : t == V_ENSEMBLE ? "un ensemble" : "un objet";
}

static Valeur valeur_copier(const Valeur *v) {
    Valeur r;
    r.type = v->type;
    r.nombre = v->type == V_NOMBRE ? dec_copier(&v->nombre) : dec_zero();
    r.texte = (v->type == V_TEXTE || v->type == V_ABSENT || v->type == V_LIGNE) && v->texte ? grym_dupliquer(v->texte) : NULL;
    r.vrai = v->vrai;
    r.objet = v->objet;
    r.jours = v->jours;
    r.fichier = v->fichier;
    if (r.fichier) r.fichier->references++;
    r.liste = v->liste;
    if (r.liste) r.liste->references++;
    return r;
}

static void valeur_liberer(Valeur *v);

static Valeur valeur_copier(const Valeur *v);

static void valeur_liberer(Valeur *v) {
    dec_liberer(&v->nombre);
    free(v->texte);
    v->texte = NULL;
    if (v->fichier && --v->fichier->references == 0) {
        free(v->fichier->octets);
        free(v->fichier->nom);
        free(v->fichier);
    }
    v->fichier = NULL;
    if (v->liste && --v->liste->references == 0) {
        if (v->liste->valeurs) {   /* un ensemble : ses éléments (§ 13.9) */
            for (size_t k = 0; k < v->liste->n; k++) valeur_liberer(&v->liste->valeurs[k]);
            free(v->liste->valeurs);
        } else {
            for (size_t k = 0; k < v->liste->n; k++) free(v->liste->classes[k]);
            free(v->liste->classes);
            free(v->liste->ids);
        }
        free(v->liste);
    }
    v->liste = NULL;
}

static Valeur valeur_nombre(Decimal d) {
    Valeur v;
    v.type = V_NOMBRE;
    v.nombre = d;
    v.texte = NULL;
    v.vrai = 0;
    v.objet = NULL;
    v.jours = 0;
    v.fichier = NULL;
    v.liste = NULL;
    return v;
}

static Valeur valeur_date(long jours) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_DATE;
    v.jours = jours;
    return v;
}

static Valeur valeur_heure(long minutes) {   /* § 14.7 */
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_HEURE;
    v.jours = minutes;
    return v;
}

static Valeur valeur_annee(long annee) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_ANNEE;
    v.jours = annee;
    return v;
}

/* Nombre entier, dans [min, max] ; 0 sinon. */
static int dec_en_long_borne(const Decimal *d, long min, long max, long *r) {
    if (!dec_est_entier(d)) return 0;
    char *t = dec_canonique(d), *fin = NULL;
    errno = 0;
    long long n = strtoll(t, &fin, 10);
    int ok = errno == 0 && fin != t && (*fin == '\0' || *fin == '.') && n >= min && n <= max;
    free(t);
    if (ok) *r = (long)n;
    return ok;
}

/* Nombre entier de jours, borné à la largeur du calendrier ; 0 sinon. */
static int dec_en_jours(const Decimal *d, long *r) {
    if (!dec_est_entier(d)) return 0;
    Decimal borne = dec_depuis_canonique("3652059");
    Decimal inf = dec_negation(&borne);
    int dedans = dec_comparer(d, &borne) <= 0 && dec_comparer(d, &inf) >= 0;
    dec_liberer(&borne);
    dec_liberer(&inf);
    if (!dedans) return 0;
    long v = 0;
    for (size_t i = 0; i < d->n; i++) {
        long rang = (long)i + d->exp;
        if (rang < 0) continue;                 /* décimales nulles d'un entier (« 3,00 ») */
        long q = 1;
        for (long e = 0; e < rang; e++) q *= 10;
        v += d->ch[i] * q;
    }
    *r = d->negatif ? -v : v;
    return 1;
}

static Decimal dec_depuis_long(long v) {
    char t[32];
    snprintf(t, sizeof t, "%ld", v);
    return dec_depuis_canonique(t);
}

static Valeur valeur_booleen(int vrai) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_BOOLEEN;
    v.vrai = vrai != 0;
    return v;
}

/* ---------------------------------------------------------------- */
/* Machine : table globale et journal d'annulation                  */
/* ---------------------------------------------------------------- */

typedef struct {
    char *nom;
    int definie;
    Valeur valeur;
    unsigned long epoque;   /* exécution au cours de laquelle la case est entrée au journal */
} Case;

typedef struct {
    size_t c;          /* case modifiée */
    Objet *objet;      /* ou champ modifié : objet et index (objet NULL pour une case) */
    size_t index;      /* SIZE_MAX : identifiant en base de l'objet (Conserver, Supprimer) */
    long ancien_id;
    int etait_definie;
    Valeur ancienne;
} Ecriture;

typedef struct {
    char *nom;
    Bloc *bloc;
    size_t *liaison;   /* noms du bloc → cases globales */
} Formule;

/* Point de reprise d'un bloc « Essayer » (grammaire, § 18 ; docs/vm.md, § 6) : ce qu'il faut
 * rétablir si le bloc échoue. La base garde le sien sous la forme d'un SAVEPOINT. */
typedef struct {
    size_t cadre;          /* cadre qui exécute le bloc */
    size_t pile;           /* profondeur de la pile */
    size_t journal;        /* marque dans le journal */
    size_t a_ecrire;       /* écritures sur le disque déjà prévues */
    size_t cible;          /* début du bloc « En cas d'échec » */
    Valeur *locaux;        /* photographie des cases locales du cadre */
    unsigned char *definis;
    int nb_locaux;
} Essai;

/* Écriture sur le disque, différée à la fin de l'exécution (§ 15.2). */
typedef struct {
    char *chemin;      /* chemin effectif */
    char *ecrit;       /* chemin tel qu'écrit dans le programme */
    Fichier *fichier;
} Ecriture_disque;

struct Machine {
    int envoi_ouvert;       /* les valeurs d'un envoi s'évaluent : un secret s'y lit (2.42) */
    char *dossier;          /* dossier du programme : base des chemins relatifs */
    char *dossier_ecriture; /* où s'écrit un fichier au chemin relatif, s'il diffère (une application fabriquée) */
    char *chemin_base;      /* fichier de la base des entités, NULL : en mémoire (§ 16.5) */
    char *mdp_base;         /* le mot de passe de la base chiffrée (§ 16.18), en mémoire seulement, ou NULL */
    char *(*demander_mdp)(void *contexte, int nouveau);   /* l'hôte le demande (2.37) ; NULL : pas d'hôte pour cela */
    void *contexte_mdp;
    Chaine *essai_migration;   /* non NULL : préparer la base, dire ce qui changerait, tout annuler (A2-c) */
    size_t regles_verifiees;   /* vérifications complètes de règles (docs/vm.md, § 3.3), pour les essais */
    struct EcranVM *ecran;     /* l'écran ouvert (§ 22), ou NULL */
    int fermer_ecran;          /* « Fermer l'écran. » a été demandé par l'événement en cours */
    int imbrique;              /* > 0 : une exécution dans l'exécution (les règles d'une fiche, § 16.14) ; elle ne
                                  touche ni à la transaction, ni aux écrans, ni aux essais, ni au ramasse-miettes */
    int absent_permis;         /* la source d'une image calculée (§ 22.8) : un champ d'un objet absent vaut absent */
    Base *base;             /* ouverte au premier besoin */
    int base_engagee;       /* dernière exécution : une base était en jeu (message d'annulation, § 3.3) */
    int fichiers_prevus;    /* dernière exécution : des fichiers devaient être écrits */
    int style;              /* affichage des nombres : 0 suisse, 1 française, 2 sans séparateur (§ 4.1) */
    int question_posee;     /* dernière exécution : une question a validé ce qui précède (§ 17) */
    Console console;        /* l'interface par défaut, et son état (docs/vm.md, § 13) */
    Interface iface;        /* questions, formulaires, effacement : console ou autre */
    long *carte_cles;       /* carte d'identité : identifiant en base → objet en mémoire (§ 16.4) */
    Objet **carte_objets;   /* clé 0 : case vide ; clé −1 : case libérée */
    size_t carte_cap, carte_n;
    Ecriture_disque *a_ecrire;
    size_t nb_a_ecrire;
    size_t exemples, exemples_rates;   /* grym exemples (grammaire, § 9.6) */
    uint64_t hasard;                    /* état du tirage au hasard (§ 3.6) ; 0 : pas encore semé */
    Case *cases;
    size_t nb_cases, cap_cases;
    Ecriture *journal;
    size_t nb_journal, cap_journal;
    Essai *essais;          /* blocs « Essayer » en cours, du plus ancien au plus récent (§ 18) */
    size_t nb_essais, cap_essais;
    Formule *formules;
    size_t nb_formules;
    unsigned long epoque;   /* numéro de l'exécution en cours */
    ClasseVM **classes;     /* la dernière déclarée l'emporte à nom égal */
    size_t nb_classes;
    Objet *tas;             /* tous les objets vivants ou non encore ramassés */
    size_t nb_objets, depuis_ramassage, seuil;
};

#define SEUIL_RAMASSAGE 10000   /* objets créés entre deux ramassages, au minimum */

volatile sig_atomic_t grym_interruption = 0;

#define APPELS_MAX 1000   /* profondeur d'appels : au-delà, « Trop d'appels imbriqués » */

/* Tirage au hasard (grammaire, § 3.6) : xorshift64*, semé au premier tirage par l'horloge, ou par la variable
 * d'environnement GRYM_HASARD (un nombre) pour rejouer une exécution. Pas pour la cryptographie. */
static uint64_t hasard_suivant(Machine *m) {
    if (!m->hasard) {
        const char *g = getenv("GRYM_HASARD");
        uint64_t s = g && *g ? (uint64_t)strtoull(g, NULL, 10) : ((uint64_t)time(NULL) << 20) ^ (uint64_t)clock() ^ (uint64_t)(uintptr_t)m;
        s ^= 0x9E3779B97F4A7C15ULL;
        m->hasard = s ? s : 1;
    }
    m->hasard ^= m->hasard >> 12;
    m->hasard ^= m->hasard << 25;
    m->hasard ^= m->hasard >> 27;
    return m->hasard * 0x2545F4914F6CDD1DULL;
}

/* Un entier de 0 à n − 1, sans biais (rejet). */
static uint64_t hasard_jusqua(Machine *m, uint64_t n) {
    uint64_t limite = UINT64_MAX - UINT64_MAX % n, x;
    do x = hasard_suivant(m); while (x >= limite);
    return x % n;
}

size_t machine_exemples(const Machine *m, size_t *rates) {
    if (rates) *rates = m->exemples_rates;
    return m->exemples;
}

Machine *machine_creer(void) {
    Machine *m = grym_allouer(sizeof *m);
    memset(m, 0, sizeof *m);
    m->iface = console_interface(&m->console);
    return m;
}

/* ---------------------------------------------------------------- */
/* Carte d'identité : un objet conservé n'existe qu'une fois en mémoire */
/* ---------------------------------------------------------------- */

static size_t carte_case(long id, size_t cap) {
    unsigned long h = (unsigned long)id * 2654435761UL;
    return (size_t)(h % cap);
}

static Objet *carte_trouver(const Machine *m, long id) {
    if (!m->carte_cap || id <= 0) return NULL;
    for (size_t i = carte_case(id, m->carte_cap), q = 0; q < m->carte_cap; q++, i = (i + 1) % m->carte_cap) {
        if (m->carte_cles[i] == 0) return NULL;
        if (m->carte_cles[i] == id) return m->carte_objets[i];
    }
    return NULL;
}

static void carte_mettre(Machine *m, long id, Objet *o);

static void carte_agrandir(Machine *m) {
    long *cles = m->carte_cles;
    Objet **objets = m->carte_objets;
    size_t cap = m->carte_cap;
    m->carte_cap = cap ? cap * 2 : 64;
    m->carte_cles = grym_allouer(m->carte_cap * sizeof *m->carte_cles);
    m->carte_objets = grym_allouer(m->carte_cap * sizeof *m->carte_objets);
    memset(m->carte_cles, 0, m->carte_cap * sizeof *m->carte_cles);
    m->carte_n = 0;
    for (size_t i = 0; i < cap; i++) if (cles[i] > 0) carte_mettre(m, cles[i], objets[i]);
    free(cles);
    free(objets);
}

static void carte_mettre(Machine *m, long id, Objet *o) {
    if (id <= 0) return;
    if ((m->carte_n + 1) * 2 > m->carte_cap) carte_agrandir(m);
    size_t libre = (size_t)-1;
    for (size_t i = carte_case(id, m->carte_cap), q = 0; q < m->carte_cap; q++, i = (i + 1) % m->carte_cap) {
        if (m->carte_cles[i] == id) { m->carte_objets[i] = o; return; }
        if (m->carte_cles[i] == -1 && libre == (size_t)-1) libre = i;
        if (m->carte_cles[i] == 0) { if (libre == (size_t)-1) libre = i; break; }
    }
    m->carte_cles[libre] = id;
    m->carte_objets[libre] = o;
    m->carte_n++;
}

static void carte_retirer(Machine *m, long id, const Objet *o) {
    if (!m->carte_cap || id <= 0) return;
    for (size_t i = carte_case(id, m->carte_cap), q = 0; q < m->carte_cap; q++, i = (i + 1) % m->carte_cap) {
        if (m->carte_cles[i] == 0) return;
        if (m->carte_cles[i] == id) {
            if (m->carte_objets[i] == o) { m->carte_cles[i] = -1; m->carte_objets[i] = NULL; }
            return;
        }
    }
}

char *machine_annulation(const Machine *m, int interactif) {
    if (m->question_posee) {   /* une question a validé ce qui la précédait (§ 17) */
        if (!m->base_engagee && !m->fichiers_prevus) return NULL;
        return grym_dupliquer("Exécution annulée : rien n'a été conservé depuis la dernière question.");
    }
    const char *base = m->base_engagee ? "rien n'a été conservé dans la base" : NULL;
    const char *disque = m->fichiers_prevus ? "aucun fichier n'a été écrit" : NULL;
    if (interactif)
        return grym_formater("Saisie annulée : aucun nom n'a changé%s%s%s%s.", base ? ", " : "", base ? base : "",
                             disque ? ", " : "", disque ? disque : "");
    if (base && disque) return grym_dupliquer("Exécution annulée : rien n'a été conservé, ni dans la base ni sur le disque.");
    if (base) return grym_formater("Exécution annulée : %s.", base);
    if (disque) return grym_formater("Exécution annulée : %s.", disque);
    return NULL;
}

void machine_lecteur(Machine *m, char *(*lire)(void *contexte, Chaine *sortie, const char *question),
                     void *contexte) {
    m->console.lire = lire;
    m->console.contexte = contexte;
}

void machine_terminal(Machine *m, int terminal) {
    m->console.terminal = terminal;
}

void machine_interface(Machine *m, const Interface *i) {
    m->iface = i ? *i : console_interface(&m->console);
}

void machine_essai_migration(Machine *m, Chaine *rapport) { m->essai_migration = rapport; }

void machine_base(Machine *m, const char *chemin) {
    free(m->chemin_base);
    m->chemin_base = chemin ? grym_dupliquer(chemin) : NULL;
}

static void oublier_mdp(Machine *m) {
    if (m->mdp_base) { memset(m->mdp_base, 0, strlen(m->mdp_base)); free(m->mdp_base); }
    m->mdp_base = NULL;
}

void machine_mot_de_passe_base(Machine *m, const char *mdp) {
    oublier_mdp(m);
    m->mdp_base = mdp ? grym_dupliquer(mdp) : NULL;
}

void machine_demande_mot_de_passe(Machine *m, char *(*demander)(void *contexte, int nouveau), void *contexte) {
    m->demander_mdp = demander;
    m->contexte_mdp = contexte;
}

/* La base, ouverte au premier besoin ; chiffrée (« La base est chiffrée. », 2.37), avec son mot de passe : celui que
 * l'hôte a donné, sinon la variable d'environnement GRYM_MOT_DE_PASSE_BASE (les essais automatiques), sinon celui que
 * l'hôte demande (nouveau : le fichier des clés n'existe pas encore). Un mot de passe refusé est oublié. */
static Base *ouvrir_la_base(Machine *m, const Module *module, char **erreur) {
    if (!module->base_chiffree || !m->chemin_base) return base_ouvrir(m->chemin_base, erreur);
    /* un essai de migration ne crée ni clés ni chiffrement : une base encore en clair s'y lit telle quelle */
    if (m->essai_migration && !base_cles_existent(m->chemin_base)) return base_ouvrir(m->chemin_base, erreur);
    if (!m->mdp_base && getenv("GRYM_MOT_DE_PASSE_BASE")) m->mdp_base = grym_dupliquer(getenv("GRYM_MOT_DE_PASSE_BASE"));
    if (!m->mdp_base && m->demander_mdp) m->mdp_base = m->demander_mdp(m->contexte_mdp, !base_cles_existent(m->chemin_base));
    if (!m->mdp_base) { *erreur = grym_dupliquer("Cette base est chiffrée : sans son mot de passe, rien ne s'ouvre."); return NULL; }
    unsigned char publique[32];   /* étape 3 : la clé publique de secours du projet, s'il en a une */
    const int secours = base_lire_cle_publique(m->dossier, publique);
    Base *b = base_ouvrir_chiffree(m->chemin_base, m->mdp_base, secours ? publique : NULL, erreur);
    if (!b) oublier_mdp(m);
    return b;
}

void machine_dossier(Machine *m, const char *dossier) {
    free(m->dossier);
    m->dossier = dossier && *dossier ? grym_dupliquer(dossier) : NULL;
}

void machine_transcripteur(Machine *m, char *(*transcrire)(void *, const unsigned char *, size_t, char **)) {
    m->iface.transcrire = transcrire;   /* sur l'interface en place, son contexte compris (§ 15.8) */
}

void machine_dossier_ecriture(Machine *m, const char *dossier) {
    free(m->dossier_ecriture);
    m->dossier_ecriture = dossier && *dossier ? grym_dupliquer(dossier) : NULL;
}


/* L'écran ouvert (grammaire, § 22). */
typedef struct EcranVM {
    char *titre;
    int titre_calcule;  /* 2.33 : le titre vient d'une expression, relue à chaque tour (ÉCRAN_TEXTE 0xFFFF) */
    int taille_titre;   /* −1 en petit, 1 en grand, 0 celle du thème (§ 22.5) */
    size_t n;
    ElementEcran *elements;
    char **textes;         /* libellés, textes fixes, entités : possédés */
    char ***colonnes;      /* pour chaque liste : titres des colonnes (possédés) */
    size_t *nb_colonnes;
    size_t **champs;       /* pour chaque liste : l'index du champ de chaque colonne */
    Valeur *listes;        /* pour chaque liste : la dernière liste montrée (sinon type V_ABSENT) */
    Objet *objet;          /* l'écran est un objet : ses zones et la ligne choisie de chaque liste (§ 22.1) */
    long *champ;           /* pour chaque élément : zone → son champ ; liste → le champ « … choisi » ; sinon −1 */
    char **types;          /* zone : son type ; possédés */
    char **indications;    /* zone : son indication, ou NULL ; possédées (§ 22.6) */
    char **bornes;         /* zone : « min ␛ max », ou NULL ; possédées (§ 22.6) */
    char **unites;         /* zone : « <CHF » ou « >% », ou NULL ; possédées (§ 22.6) */
    char ***choix;         /* zone liée à une entité : les clés de ses objets */
    size_t *nb_choix;
    struct EcranVM *dessous;   /* l'écran qu'il recouvre (écrans empilés, § 22.3), ou NULL */
    char ***chemins;       /* liste aux colonnes choisies : le chemin de chaque colonne (« compositeur␜naissance »), sinon NULL */
    unsigned char *etats;  /* pour chaque élément : bit 1 inactif, bit 2 caché (§ 22.6) ; relus avant chaque attente */
    size_t cadre;          /* le cadre qui a ouvert l'écran : y lire une zone vide (listes, conditions) rend absent */
    Valeur *images;        /* image (§ 22.8) : le fichier lu à l'ouverture, sinon absent */
    unsigned char *calcules;   /* 1 : texte calculé (§ 22.1, 2.24), relu à chaque tour */
    int document;          /* un document (2.25) : jamais montré par l'interface, lu une fois puis rendu en PDF */
    char ***doc_cellules;  /* document : les cellules de chaque liste, ligne après ligne (possédées) */
    size_t *doc_lignes;
} EcranVM;

static void ecran_detruire(EcranVM *e);

void machine_detruire(Machine *m) {
    if (!m) return;
    while (m->ecran) {   /* les écrans empilés, du dessus au dessous */
        struct EcranVM *d = m->ecran->dessous;
        ecran_detruire(m->ecran);
        m->ecran = d;
    }
    base_fermer(m->base);
    oublier_mdp(m);
    free(m->carte_cles);
    free(m->carte_objets);
    free(m->chemin_base);
    free(m->dossier);
    free(m->dossier_ecriture);
    for (size_t i = 0; i < m->nb_cases; i++) {
        free(m->cases[i].nom);
        if (m->cases[i].definie) valeur_liberer(&m->cases[i].valeur);
    }
    free(m->cases);
    free(m->journal);
    free(m->essais);
    for (size_t i = 0; i < m->nb_formules; i++) {
        free(m->formules[i].nom);
        bloc_detruire(m->formules[i].bloc);
        free(m->formules[i].liaison);
    }
    free(m->formules);
    while (m->tas) {
        Objet *o = m->tas;
        m->tas = o->suivant;
        for (size_t k = 0; k < o->classe->nb_champs; k++)
            if (o->definis[k]) valeur_liberer(&o->champs[k]);
        free(o->champs);
        free(o->definis);
        free(o->epoques);
        free(o);
    }
    for (size_t i = 0; i < m->nb_classes; i++) {
        free(m->classes[i]->nom);
        for (size_t k = 0; k < m->classes[i]->nb_champs; k++) free(m->classes[i]->champs[k]);
        for (size_t k = 0; k < m->classes[i]->nb_champs; k++) {
            free(m->classes[i]->types[k]);
            free(m->classes[i]->departs[k]);
            free(m->classes[i]->valeurs[k]);
            if (m->classes[i]->couleurs) free(m->classes[i]->couleurs[k]);   /* une classe d'écran n'en a pas */
            if (m->classes[i]->transcrit_de) free(m->classes[i]->transcrit_de[k]);
        }
        free(m->classes[i]->valeurs);
        free(m->classes[i]->couleurs);
        free(m->classes[i]->transcrit_de);
        free(m->classes[i]->departs);
        free(m->classes[i]->types);
        free(m->classes[i]->uniques);
        free(m->classes[i]->pluriel);
        if (m->classes[i]->anciens)
            for (size_t k = 0; k < m->classes[i]->nb_champs; k++) free(m->classes[i]->anciens[k]);
        free(m->classes[i]->anciens);
        free(m->classes[i]->ancien);
        free(m->classes[i]->champs);
        free(m->classes[i]->proprietaires);
        free(m->classes[i]->aptitudes);
        free(m->classes[i]);
    }
    free(m->classes);
    free(m);
}

/* Liaison (docs/vm.md, § 5) : case de la table globale pour un nom, créée au besoin. */
static size_t case_de(Machine *m, const char *nom) {
    for (size_t i = 0; i < m->nb_cases; i++)
        if (strcmp(m->cases[i].nom, nom) == 0) return i;
    if (m->nb_cases == m->cap_cases) {
        m->cap_cases = m->cap_cases ? m->cap_cases * 2 : 16;
        Case *c = grym_allouer(m->cap_cases * sizeof *c);
        if (m->nb_cases) memcpy(c, m->cases, m->nb_cases * sizeof *c);
        free(m->cases);
        m->cases = c;
    }
    Case *c = &m->cases[m->nb_cases];
    c->nom = grym_dupliquer(nom);
    c->definie = 0;
    c->epoque = 0;
    return m->nb_cases++;
}

/* Seule la première écriture d'une case au cours d'une exécution entre au journal :
 * c'est la valeur d'avant l'exécution qu'il faut pouvoir rendre. Une boucle qui
 * modifie un nom un million de fois n'occupe ainsi qu'une entrée. */
static void ecrire(Machine *m, size_t c, Valeur v) {
    Case *k = &m->cases[c];
    if (k->epoque == m->epoque) {
        valeur_liberer(&k->valeur);
        k->valeur = v;
        return;
    }
    k->epoque = m->epoque;
    if (m->nb_journal == m->cap_journal) {
        m->cap_journal = m->cap_journal ? m->cap_journal * 2 : 16;
        Ecriture *j = grym_allouer(m->cap_journal * sizeof *j);
        if (m->nb_journal) memcpy(j, m->journal, m->nb_journal * sizeof *j);
        free(m->journal);
        m->journal = j;
    }
    Ecriture *e = &m->journal[m->nb_journal++];
    e->c = c;
    e->objet = NULL;
    e->index = 0;
    e->etait_definie = m->cases[c].definie;
    if (e->etait_definie) e->ancienne = m->cases[c].valeur;   /* la valeur passe au journal */
    m->cases[c].valeur = v;
    m->cases[c].definie = 1;
}

/* Écriture d'un champ, journalisée comme celle d'une case (première écriture seulement). */
static void ecrire_champ(Machine *m, Objet *o, size_t k, Valeur v) {
    if (o->definis[k] && o->champs[k].type == V_FICHIER && !o->champs[k].fichier->octets) {
        /* un fichier resté en base que l'objet quitte : sa ligne va partir, une copie peut encore servir */
        char *e = NULL;
        base_fichier_lire(o->champs[k].fichier, &e);
        free(e);
    }
    if (o->epoques[k] == m->epoque) {
        if (o->definis[k]) valeur_liberer(&o->champs[k]);
        o->champs[k] = v;
        o->definis[k] = 1;
        return;
    }
    o->epoques[k] = m->epoque;
    if (m->nb_journal == m->cap_journal) {
        m->cap_journal = m->cap_journal ? m->cap_journal * 2 : 16;
        Ecriture *j = grym_allouer(m->cap_journal * sizeof *j);
        if (m->nb_journal) memcpy(j, m->journal, m->nb_journal * sizeof *j);
        free(m->journal);
        m->journal = j;
    }
    Ecriture *e = &m->journal[m->nb_journal++];
    e->c = 0;
    e->objet = o;
    e->index = k;
    e->etait_definie = o->definis[k];
    if (e->etait_definie) e->ancienne = o->champs[k];
    o->champs[k] = v;
    o->definis[k] = 1;
}

static const unsigned char *octets_du_fichier(const Fichier *f, char **erreur);

/* Un son vient d'être écrit dans le champ k : les champs « transcrite du … » qui le transcrivent reçoivent son texte,
 * fabriqué par l'interface (§ 15.8). Sans transcripteur (console, navigateur), ou si la transcription échoue, ils
 * restent tels quels. Rend l'index du champ transcrit, ou −1. */
static long transcrire_si_besoin(Machine *m, Objet *o, size_t k) {
    const ClasseVM *c = o->classe;
    if (!c->transcrit_de || !o->definis[k] || o->champs[k].type != V_FICHIER || !m->iface.transcrire) return -1;
    long fait = -1;
    for (size_t q = 0; q < c->nb_champs; q++) {
        if (!c->transcrit_de[q] || strcmp(c->transcrit_de[q], c->champs[k]) != 0) continue;
        char *e = NULL;
        const unsigned char *octets = octets_du_fichier(o->champs[k].fichier, &e);
        free(e);
        if (!octets) continue;
        char *erreur = NULL;
        char *texte = m->iface.transcrire(m->iface.contexte, octets, o->champs[k].fichier->taille, &erreur);
        free(erreur);
        if (!texte) continue;
        ecrire_champ(m, o, q, vi_texte(texte));
        free(texte);
        fait = (long)q;
    }
    return fait;
}

/* Identifiant en base d'un objet, journalisé : une exécution ratée le rend tel qu'avant. */
static void ecrire_id(Machine *m, Objet *o, long id) {
    if (m->nb_journal == m->cap_journal) {
        m->cap_journal = m->cap_journal ? m->cap_journal * 2 : 16;
        Ecriture *j = grym_allouer(m->cap_journal * sizeof *j);
        if (m->nb_journal) memcpy(j, m->journal, m->nb_journal * sizeof *j);
        free(m->journal);
        m->journal = j;
    }
    Ecriture *e = &m->journal[m->nb_journal++];
    e->c = 0;
    e->objet = o;
    e->index = (size_t)-1;
    e->ancien_id = o->id;
    e->etait_definie = 0;
    carte_retirer(m, o->id, o);
    o->id = id;
    carte_mettre(m, id, o);
}

/* Rejoue le journal du plus récent au plus ancien, jusqu'à la marque (0 : tout). */
static void annuler_jusqu_a(Machine *m, size_t marque) {
    while (m->nb_journal > marque) {
        Ecriture *e = &m->journal[--m->nb_journal];
        if (e->objet && e->index == (size_t)-1) {
            carte_retirer(m, e->objet->id, e->objet);
            e->objet->id = e->ancien_id;
            carte_mettre(m, e->objet->id, e->objet);
            continue;
        }
        if (e->objet) {
            Objet *o = e->objet;
            valeur_liberer(&o->champs[e->index]);
            o->definis[e->index] = e->etait_definie;
            if (e->etait_definie) o->champs[e->index] = e->ancienne;
            continue;
        }
        Case *c = &m->cases[e->c];
        valeur_liberer(&c->valeur);
        c->definie = e->etait_definie;
        if (e->etait_definie) c->valeur = e->ancienne;
    }
}

static void annuler(Machine *m) { annuler_jusqu_a(m, 0); }

static void valider(Machine *m) {
    for (size_t i = 0; i < m->nb_journal; i++)
        if (m->journal[i].etait_definie) valeur_liberer(&m->journal[i].ancienne);
    m->nb_journal = 0;
}

/* ---------------------------------------------------------------- */
/* Exécution                                                        */
/* ---------------------------------------------------------------- */

typedef struct {
    Valeur *v;
    size_t n, cap;
} Pile;

static void empiler(Pile *p, Valeur v) {
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        Valeur *nv = grym_allouer(p->cap * sizeof *nv);
        if (p->n) memcpy(nv, p->v, p->n * sizeof *nv);
        free(p->v);
        p->v = nv;
    }
    p->v[p->n++] = v;
}

static Valeur depiler(Pile *p) { return p->v[--p->n]; }

static void vider(Pile *p) {
    while (p->n) valeur_liberer(&p->v[--p->n]);
    free(p->v);
    p->v = NULL;
    p->cap = 0;
}

static int echouer(Diagnostic *d, const Bloc *b, size_t decalage, char *message) {
    d->message = message;
    bloc_position(b, decalage, &d->ligne, &d->colonne);
    free(d->fichier);   /* une formule d'un fichier utilisé désigne son fichier (§ 21) */
    d->fichier = b->fichier ? grym_dupliquer(b->fichier) : NULL;
    return 0;
}

static char *message_statut(StatutDecimal st) {
    switch (st) {
    case DEC_DIVISION_PAR_ZERO:
        return grym_dupliquer("Division par zéro.");
    case DEC_TROP_GRAND:
        return grym_formater("Nombre trop grand : un résultat est limité à %d chiffres.", DEC_CHIFFRES_MAX);
    case DEC_EXPOSANT_NON_ENTIER:
        return grym_dupliquer("Exposant non entier : en v0.1, la puissance n'accepte qu'un exposant "
                              "entier (2 ^ 3, 2 ^ −1).");
    default:
        return grym_dupliquer("Erreur interne de calcul.");
    }
}

static size_t *lier(Machine *m, const Bloc *b) {
    size_t *liaison = grym_allouer((b->nb_noms ? b->nb_noms : 1) * sizeof *liaison);
    for (size_t i = 0; i < b->nb_noms; i++) liaison[i] = case_de(m, b->noms[i]);
    return liaison;
}

static int meme_classe(const char *a, const char *b) {
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

/* Version exacte d'une formule : même nom, même classe (NULL pour une formule sans classe). */
static Formule *formule_de(Machine *m, const char *nom, const char *classe) {
    for (size_t i = 0; i < m->nb_formules; i++)
        if (strcmp(m->formules[i].nom, nom) == 0 && meme_classe(m->formules[i].bloc->classe, classe))
            return &m->formules[i];
    return NULL;
}

/* Remplacements effectués par un module, pour revenir en arrière en cas d'échec. */
typedef struct {
    size_t index;
    Bloc *bloc;
    size_t *liaison;
} Remplacement;

/* Cadre d'appel : le bloc exécuté, sa position, ses cases locales. */
typedef struct {
    const Bloc *b;
    const size_t *liaison;
    size_t ip;
    Valeur *locaux;
    unsigned char *definis;
} Cadre;

static void liberer_cadre(Cadre *c) {
    if (c->locaux)
        for (int k = 0; k < c->b->nb_locaux; k++)
            if (c->definis[k]) valeur_liberer(&c->locaux[k]);
    free(c->locaux);
    free(c->definis);
}

/* ---------------------------------------------------------------- */
/* Ramasse-miettes : marquage et balayage (docs/vm.md, principe 4)  */
/* ---------------------------------------------------------------- */

typedef struct {
    Objet **o;
    size_t n, cap;
} Pile_objets;

static void marquer(Pile_objets *p, const Valeur *v) {
    if (v->type == V_ENSEMBLE && v->liste) {   /* les objets d'un ensemble restent vivants (§ 13.9) */
        for (size_t k = 0; k < v->liste->n; k++) marquer(p, &v->liste->valeurs[k]);
        return;
    }
    if (v->type != V_OBJET || !v->objet || v->objet->marque) return;
    v->objet->marque = 1;
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        Objet **t = grym_allouer(p->cap * sizeof *t);
        if (p->n) memcpy(t, p->o, p->n * sizeof *t);
        free(p->o);
        p->o = t;
    }
    p->o[p->n++] = v->objet;
}

/* Racines : cases globales, pile, cases locales des cadres, anciennes valeurs du journal.
 * Le marquage suit les champs avec une pile explicite : une longue chaîne d'objets
 * ne fait pas déborder la pile du C. */


static void ramasser(Machine *m, const Pile *pile, const Cadre *cadres, size_t nb_cadres) {
    Pile_objets p = { NULL, 0, 0 };
    for (size_t i = 0; i < m->nb_cases; i++)
        if (m->cases[i].definie) marquer(&p, &m->cases[i].valeur);
    for (size_t i = 0; pile && i < pile->n; i++) marquer(&p, &pile->v[i]);
    for (size_t c = 0; c < nb_cadres; c++)
        for (int k = 0; k < cadres[c].b->nb_locaux; k++)
            if (cadres[c].definis[k]) marquer(&p, &cadres[c].locaux[k]);
    for (size_t i = 0; i < m->nb_essais; i++)   /* cases locales d'avant chaque essai (§ 18) */
        for (int k = 0; k < m->essais[i].nb_locaux; k++)
            if (m->essais[i].definis[k]) marquer(&p, &m->essais[i].locaux[k]);
    for (size_t i = 0; i < m->nb_journal; i++) {
        if (m->journal[i].etait_definie) marquer(&p, &m->journal[i].ancienne);
        if (m->journal[i].objet) {
            Valeur v = valeur_nombre(dec_zero());
            v.type = V_OBJET;
            v.objet = m->journal[i].objet;
            marquer(&p, &v);
            dec_liberer(&v.nombre);
        }
    }
    for (EcranVM *e = m->ecran; e; e = e->dessous) {   /* l'objet de chaque écran ouvert (§ 22.1), empilés compris */
        if (!e->objet) continue;
        Valeur v = valeur_nombre(dec_zero());
        v.type = V_OBJET;
        v.objet = e->objet;
        marquer(&p, &v);
        dec_liberer(&v.nombre);
    }
    while (p.n) {
        Objet *o = p.o[--p.n];
        for (size_t k = 0; k < o->classe->nb_champs; k++)
            if (o->definis[k]) marquer(&p, &o->champs[k]);
    }
    free(p.o);
    /* balayage */
    Objet **lien = &m->tas;
    size_t vivants = 0;
    while (*lien) {
        Objet *o = *lien;
        if (o->marque) {
            o->marque = 0;
            vivants++;
            lien = &o->suivant;
            continue;
        }
        *lien = o->suivant;
        carte_retirer(m, o->id, o);
        for (size_t k = 0; k < o->classe->nb_champs; k++)
            if (o->definis[k]) valeur_liberer(&o->champs[k]);
        free(o->champs);
        free(o->definis);
        free(o->epoques);
        free(o);
    }
    m->nb_objets = vivants;
    m->depuis_ramassage = 0;
    m->seuil = vivants * 2 > SEUIL_RAMASSAGE ? vivants * 2 : SEUIL_RAMASSAGE;
}

/* ---------------------------------------------------------------- */
/* Fichiers (grammaire, § 15)                                       */
/* ---------------------------------------------------------------- */

#define FICHIER_TAILLE_MAX 1000000000UL   /* SQLITE_MAX_LENGTH, pour ranger le contenu en base */

/* Format d'image reconnu à sa signature, ou NULL. Signatures : PNG (89 50 4E 47 0D 0A 1A 0A),
 * JPEG (FF D8 FF), GIF (« GIF87a », « GIF89a »), WebP (« RIFF », 4 octets, « WEBP »). */
static const char *format_image(const unsigned char *o, size_t n) {
    static const unsigned char PNG[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n >= 8 && memcmp(o, PNG, 8) == 0) return "PNG";
    if (n >= 3 && o[0] == 0xFF && o[1] == 0xD8 && o[2] == 0xFF) return "JPEG";
    if (n >= 6 && (memcmp(o, "GIF87a", 6) == 0 || memcmp(o, "GIF89a", 6) == 0)) return "GIF";
    if (n >= 12 && memcmp(o, "RIFF", 4) == 0 && memcmp(o + 8, "WEBP", 4) == 0) return "WebP";
    return NULL;
}

/* Le format d'un son, à sa signature (§ 15.7) ; NULL si ce n'en est pas un. M4A : le conteneur MP4 (« ftyp »), qui
 * peut aussi porter une vidéo, dont on ne garderait que le son à l'écoute. */
static const char *format_son(const unsigned char *o, size_t n) {
    if (n >= 12 && memcmp(o, "RIFF", 4) == 0 && memcmp(o + 8, "WAVE", 4) == 0) return "WAV";
    if (n >= 3 && memcmp(o, "ID3", 3) == 0) return "MP3";
    if (n >= 2 && o[0] == 0xFF && (o[1] & 0xF6) == 0xF2) return "MP3";   /* une trame MPEG audio, couche III */
    if (n >= 2 && o[0] == 0xFF && (o[1] & 0xF6) == 0xF0) return "AAC";   /* ADTS */
    if (n >= 12 && memcmp(o + 4, "ftyp", 4) == 0) return "M4A";
    if (n >= 4 && memcmp(o, "OggS", 4) == 0) return "Ogg";
    if (n >= 4 && o[0] == 0x1A && o[1] == 0x45 && o[2] == 0xDF && o[3] == 0xA3) return "WebM";
    if (n >= 4 && memcmp(o, "fLaC", 4) == 0) return "FLAC";
    return NULL;
}

static const unsigned char *octets_du_fichier(const Fichier *f, char **erreur);

/* Ce qu'une interface sait montrer d'un fichier : le format d'une image, ou celui d'un son (§ 15.7), ou NULL. */
static const char *format_montre(const Fichier *f, char **illisible) {
    if (f->format) return f->format;
    const unsigned char *o = octets_du_fichier(f, illisible);
    return o ? format_son(o, f->taille) : NULL;
}

/* « ~/Documents/x » : le dossier de l'utilisateur (2.30) ; un chemin absolu reste tel quel ; sinon, à côté du programme. */
static int chemin_absolu_ecrit(const char *ecrit) {
    return ecrit[0] == '/' || ecrit[0] == '\\' || (ecrit[0] == '~' && (ecrit[1] == '/' || ecrit[1] == '\\'))
        || (ecrit[0] && ecrit[1] == ':' && (ecrit[2] == '/' || ecrit[2] == '\\'));
}

static char *chemin_effectif(const Machine *m, const char *ecrit) {
    if (chemin_absolu_ecrit(ecrit) || !m->dossier) return grym_chemin_utilisateur(ecrit);
    return grym_formater("%s/%s", m->dossier, ecrit);
}

/* Où s'écrit un fichier : le dossier d'écriture s'il est fixé (une application fabriquée, dont le dossier du programme
 * n'est pas modifiable), sinon à côté du programme (§ 15.2). */
static char *chemin_ecriture(const Machine *m, const char *ecrit) {
    if (chemin_absolu_ecrit(ecrit) || !m->dossier_ecriture) return chemin_effectif(m, ecrit);
    return grym_formater("%s/%s", m->dossier_ecriture, ecrit);
}

/* Lit un fichier du disque ; NULL et *erreur sinon. */
static Fichier *lire_fichier(const Machine *m, const char *ecrit, char **erreur) {
    if (!*ecrit) { *erreur = grym_dupliquer("Chemin de fichier vide."); return NULL; }
    char *chemin = chemin_effectif(m, ecrit);
    FILE *f = grym_fopen(chemin, "rb");
    free(chemin);
    if (!f) { *erreur = grym_formater("Fichier « %s » introuvable ou illisible.", ecrit); return NULL; }
    Fichier *x = grym_allouer(sizeof *x);
    x->base = NULL;
    x->cle = 0;
    size_t cap = 65536;
    x->octets = grym_allouer(cap);
    x->taille = 0;
    for (;;) {
        if (x->taille == cap) {
            if (cap > FICHIER_TAILLE_MAX) break;
            cap *= 2;
            unsigned char *t = grym_allouer(cap);
            memcpy(t, x->octets, x->taille);
            free(x->octets);
            x->octets = t;
        }
        size_t lu = fread(x->octets + x->taille, 1, cap - x->taille, f);
        x->taille += lu;
        if (lu == 0) break;
    }
    int illisible = ferror(f);
    fclose(f);
    if (illisible || x->taille > FICHIER_TAILLE_MAX) {
        *erreur = illisible ? grym_formater("Fichier « %s » illisible.", ecrit)
                            : grym_formater("Fichier « %s » trop grand : 1'000'000'000 octets au plus.", ecrit);
        free(x->octets);
        free(x);
        return NULL;
    }
    const char *nom = strrchr(ecrit, '/');
    x->nom = grym_dupliquer(nom ? nom + 1 : ecrit);
    x->format = format_image(x->octets, x->taille);
    x->references = 1;
    return x;
}

/* « 2,3 Mo » : puissances de 1000, une décimale au plus, arrondie au plus proche. */
static char *taille_lisible(size_t n) {
    static const char *const U[] = { "Ko", "Mo", "Go" };
    if (n < 1000) return grym_formater("%lu octet%s", (unsigned long)n, n > 1 ? "s" : "");
    size_t u = 0, d = 1000;
    while (u < 2 && n >= d * 1000) { d *= 1000; u++; }
    unsigned long dixiemes = (unsigned long)((n * 10 + d / 2) / d);
    if (dixiemes % 10 == 0) return grym_formater("%lu %s", dixiemes / 10, U[u]);
    return grym_formater("%lu,%lu %s", dixiemes / 10, dixiemes % 10, U[u]);
}

static char *decrire_fichier(const Fichier *f) {
    char *t = taille_lisible(f->taille);
    char *r = f->format ? grym_formater("une image %s de %s", f->format, t) : grym_formater("un fichier de %s", t);
    free(t);
    return r;
}

static void vider_ecritures(Machine *m) {
    for (size_t k = 0; k < m->nb_a_ecrire; k++) {
        Valeur v = valeur_nombre(dec_zero());
        v.fichier = m->a_ecrire[k].fichier;
        valeur_liberer(&v);
        free(m->a_ecrire[k].chemin);
        free(m->a_ecrire[k].ecrit);
    }
    free(m->a_ecrire);
    m->a_ecrire = NULL;
    m->nb_a_ecrire = 0;
}

/* Écritures différées : toutes ou aucune. Un fichier déjà écrit est retiré si une suivante échoue. */
static char *ecrire_sur_le_disque(Machine *m) {
    size_t k;
    char *erreur = NULL;
    for (k = 0; k < m->nb_a_ecrire && !erreur; k++) {
        const Ecriture_disque *e = &m->a_ecrire[k];
        FILE *existe = grym_fopen(e->chemin, "rb");
        if (existe) {
            fclose(existe);
            erreur = grym_formater("« %s » existe déjà : il n'est jamais écrasé.", e->ecrit);
            break;
        }
        grym_creer_dossiers(e->chemin);   /* un dossier qui manque se crée (2.30) : un dossier synchronisé, une clé neuve */
        FILE *f = grym_fopen(e->chemin, "wb");
        int ok = f && fwrite(e->fichier->octets, 1, e->fichier->taille, f) == e->fichier->taille;
        if (f && fclose(f) != 0) ok = 0;
        if (!ok) {
            if (f) grym_supprimer(e->chemin);
            erreur = grym_formater("Écriture de « %s » impossible.", e->ecrit);
            break;
        }
    }
    if (erreur) while (k > 0) grym_supprimer(m->a_ecrire[--k].chemin);
    return erreur;
}

static char *decrire_valeur_pour_type(const Valeur *v) {
    if (v->type == V_NOMBRE) {
        char *n = dec_afficher(&v->nombre);
        char *r = grym_formater("%s", n);
        free(n);
        return r;
    }
    if (v->type == V_OBJET) {
        return grym_formater("%s %s", v->objet->classe->feminin ? "une" : "un", v->objet->classe->nom);
    }
    return grym_dupliquer(nom_type(v->type));
}

static char *article_classe(const ClasseVM *c);

/* Un nombre au style demandé (§ 4.1) : l'apostrophe des milliers devient une espace insécable
 * (française, U+202F) ou disparaît (sans séparateur). */
static char *nombre_au_style(const Decimal *d, int style) {
    char *s = dec_afficher(d);
    if (!style) return s;
    Chaine c = {0};
    for (const char *p = s; *p; p++) {
        if (*p == '\'') { if (style == 1) chaine_ajouter(&c, "\u202f"); continue; }
        char m[2] = { *p, 0 };
        chaine_ajouter(&c, m);
    }
    free(s);
    return chaine_rendre(&c);
}

/* Le texte d'une valeur, tel que « Afficher » l'écrit (grammaire, § 4). */
static char *texte_valeur(const Machine *m, const Valeur *v) {
    switch (v->type) {
    case V_TEXTE:   return grym_dupliquer(v->texte);
    case V_BOOLEEN: return grym_dupliquer(v->vrai ? "vrai" : "faux");
    case V_ABSENT:  return grym_dupliquer("absent");
    case V_FICHIER: return decrire_fichier(v->fichier);
    case V_ANNEE:   return grym_formater("%ld", v->jours);   /* « 1747 », jamais « 1'747 » (§ 14.5) */
    case V_DATE:    return date_suisse(v->jours);
    case V_HEURE:   return grym_formater("%02ld:%02ld", v->jours / 60, v->jours % 60);   /* « 08:05 » (§ 14.7) */
    case V_OBJET:   return article_classe(v->objet->classe);
    case V_ENSEMBLE: {   /* ses éléments, séparés par « , » (§ 13.9) */
        Chaine c = {0};
        for (size_t k = 0; v->liste && k < v->liste->n; k++) {
            if (k) chaine_ajouter(&c, ", ");
            char *t = texte_valeur(m, &v->liste->valeurs[k]);
            chaine_ajouter(&c, t);
            free(t);
        }
        return chaine_rendre(&c);
    }
    case V_LIGNE: {   /* ses cellules, séparées par « ; » (§ 15.5) */
        const char *p = v->texte ? strchr(v->texte, '\x1e') : NULL;
        char *t = grym_dupliquer(p ? p + 1 : "");
        Chaine c = {0};
        char un[2] = { 0, 0 };
        for (const char *q = t; *q; q++) {
            if (*q == '\x1f') chaine_ajouter(&c, " ; ");
            else if (*q == '\x1e') chaine_ajouter(&c, " / ");
            else { un[0] = *q; chaine_ajouter(&c, un); }
        }
        free(t);
        return chaine_rendre(&c);
    }
    default:        return nombre_au_style(&v->nombre, m->style);
    }
}

/* Réponse de l'utilisateur (grammaire, § 17) : la ligne tapée, lue selon le type demandé.
 * *probleme reçoit le message de relance ; la valeur n'est pas rendue. */
static int lire_reponse(const char *type, const char *ligne, Valeur *v, char **probleme) {
    while (*ligne == ' ' || *ligne == '\t') ligne++;
    size_t n = strlen(ligne);
    while (n && (ligne[n - 1] == ' ' || ligne[n - 1] == '\t')) n--;
    char *t = grym_formater("%.*s", (int)n, ligne);
    if (type_textuel(type)) {
        *v = vi_texte(t);
        free(t);
        return 1;
    }
    if (!n) {
        free(t);
        *probleme = grym_dupliquer("Une réponse est attendue.");
        return 0;
    }
    if (strcmp(type, "vrai ou faux") == 0) {
        for (char *p = t; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
        int oui = strcmp(t, "oui") == 0 || strcmp(t, "vrai") == 0;
        int non = strcmp(t, "non") == 0 || strcmp(t, "faux") == 0;
        free(t);
        if (!oui && !non) {
            *probleme = grym_dupliquer("Réponds par oui ou non.");
            return 0;
        }
        *v = vi_booleen(oui);
        return 1;
    }
    if (strcmp(type, "heure") == 0) {   /* « 14:30 », « 8:05 » (§ 14.7) */
        long mi = 0;
        int bon = heure_lire(t, &mi);
        if (!bon) *probleme = grym_formater("« %s » n'est pas une heure : écris heures:minutes (14:30).", t);
        else *v = valeur_heure(mi);
        free(t);
        return bon;
    }
    /* nombres et dates : les règles du lexeur (§ 1.2, § 14.1) */
    char *erreur = NULL;
    Lexeur *lx = lexeur_creer(t, strlen(t), &erreur);
    free(erreur);
    int date = strcmp(type, "date") == 0;
    int negatif = 0, bon = lx != NULL;
    Jeton j = { 0 }, f = { 0 };
    if (bon) {
        j = lexeur_suivant(lx);
        if (!date && j.type == J_MOINS) { negatif = 1; jeton_liberer(&j); j = lexeur_suivant(lx); }
        f = lexeur_suivant(lx);
        bon = j.type == (date ? J_DATE : J_NOMBRE) && f.type == J_FIN;
    }
    char *valeur = bon ? grym_dupliquer(j.valeur) : NULL;
    if (lx) { jeton_liberer(&j); jeton_liberer(&f); lexeur_detruire(lx); }
    if (!bon) {
        *probleme = grym_formater(date ? "« %s » n'est pas une date : écris jour.mois.année (21.09.2026)."
                                       : "« %s » n'est pas un nombre.", t);
        free(t);
        free(valeur);
        return 0;
    }
    if (date) {
        long jours = 0;
        date_lire_iso(valeur, &jours);
        *v = vi_date(jours);
    } else {
        char *canonique = negatif ? grym_formater("-%s", valeur) : grym_dupliquer(valeur);
        Decimal d = dec_depuis_canonique(canonique);
        free(canonique);
        long a = 0;
        int entier = strcmp(type, "nombre entier") == 0, an = strcmp(type, "année") == 0;
        if ((entier || an) && !dec_est_entier(&d)) {
            *probleme = grym_formater("« %s » n'est pas un nombre entier.", t);
            dec_liberer(&d);
            free(t);
            free(valeur);
            return 0;
        }
        if (an && !dec_en_long_borne(&d, 1, 9999, &a)) {
            *probleme = grym_formater("« %s » n'est pas une année : de 1 à 9999.", t);
            dec_liberer(&d);
            free(t);
            free(valeur);
            return 0;
        }
        if (an) { dec_liberer(&d); *v = vi_annee(a); }
        else *v = valeur_nombre(d);
    }
    free(t);
    free(valeur);
    return 1;
}

/* Une question valide ce qui la précède (grammaire, § 17) : fichiers en attente, puis base, puis
 * le journal se vide. Le verrou de la base reprend après la réponse. */
static char *valider_jusqu_ici(Machine *m, Chaine *sortie) {
    (void)sortie;
    if (m->nb_a_ecrire) {
        char *erreur = ecrire_sur_le_disque(m);
        if (erreur) return erreur;
        m->fichiers_prevus = 1;
        vider_ecritures(m);
    }
    if (m->base) {
        char *erreur = NULL;
        if (!base_valider(m->base, &erreur)) return erreur;
    }
    valider(m);
    m->epoque++;   /* ce qui s'écrit ensuite repasse au journal : une erreur l'annulera */
    return NULL;
}


/* ---------------------------------------------------------------- */
/* Envoi à un service web (2.42)                                    */
/* ---------------------------------------------------------------- */

/* https://, ou http:// vers la machine elle-même (127.0.0.1, localhost : un service local, les essais). */
static Valeur vi_texte_pris(char *t) {
    Valeur v = vi_texte(t);
    free(t);
    return v;
}

static int adresse_envoyable(const char *u) {
    if (!strncmp(u, "https://", 8)) return 1;
    return !strncmp(u, "http://127.0.0.1", 16) || !strncmp(u, "http://localhost", 16) || !strncmp(u, "http://[::1]", 12);
}

/* Une valeur envoyée, en JSON : texte, nombre, vrai ou faux, date (AAAA-MM-JJ), absent (null). */
static char *valeur_en_json(const Machine *m, const Valeur *v, Chaine *c) {
    switch (v->type) {
    case V_ABSENT: chaine_ajouter(c, "null"); return NULL;
    case V_BOOLEEN: chaine_ajouter(c, v->vrai ? "true" : "false"); return NULL;
    case V_NOMBRE: {
        char *t = dec_canonique(&v->nombre);
        chaine_ajouter(c, t);
        free(t);
        return NULL;
    }
    case V_DATE: {
        int an, mo, jo;
        date_civile(v->jours, &an, &mo, &jo);
        char *t = grym_formater("\"%04d-%02d-%02d\"", an, mo, jo);
        chaine_ajouter(c, t);
        free(t);
        return NULL;
    }
    case V_TEXTE: json_ecrire_texte(c, v->texte); return NULL;
    default: {
        (void)m;
        return grym_dupliquer("Un envoi porte des textes, des nombres, des dates, vrai ou faux.");
    }
    }
}

static void pourcent_encoder(Chaine *c, const char *t) {
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            char x[2] = { (char)*p, 0 };
            chaine_ajouter(c, x);
        } else {
            char x[4];
            snprintf(x, sizeof x, "%%%02X", *p);
            chaine_ajouter(c, x);
        }
    }
}

/* « le champ « a.b.0 » de r » : les objets par leur clé, les tableaux par leur rang (0 le premier). */
static const Json *json_suivre(const Json *j, const char *chemin) {
    char *c = grym_dupliquer(chemin);
    const Json *r = j;
    char *reste = c;
    for (char *p = reste; r && p; ) {
        char *point = strchr(p, '.');
        if (point) *point = '\0';
        if (r->type == JSON_TABLEAU) {
            char *fin = NULL;
            long k = strtol(p, &fin, 10);
            r = (*p && fin && !*fin && k >= 0 && (size_t)k < r->nb) ? r->elements[k] : NULL;
        } else r = json_champ(r, p);
        p = point ? point + 1 : NULL;
    }
    free(c);
    return r;
}

/* ---------------------------------------------------------------- */
/* Essais (grammaire, § 18)                                         */
/* ---------------------------------------------------------------- */

static void photographier(Essai *e, const Cadre *c) {
    int nl = c->b->nb_locaux;
    e->nb_locaux = nl;
    e->locaux = grym_allouer((nl ? (size_t)nl : 1) * sizeof *e->locaux);
    e->definis = grym_allouer(nl ? (size_t)nl : 1);
    for (int k = 0; k < nl; k++) {
        e->definis[k] = c->definis[k];
        if (c->definis[k]) e->locaux[k] = valeur_copier(&c->locaux[k]);
    }
}

static void oublier_photo(Essai *e) {
    for (int k = 0; k < e->nb_locaux; k++)
        if (e->definis[k]) valeur_liberer(&e->locaux[k]);
    free(e->locaux);
    free(e->definis);
    e->locaux = NULL;
    e->definis = NULL;
    e->nb_locaux = 0;
}

/* Rend au cadre ses cases locales d'avant l'essai. */
static void restaurer_photo(const Essai *e, Cadre *c) {
    for (int k = 0; k < e->nb_locaux; k++) {
        if (c->definis[k]) valeur_liberer(&c->locaux[k]);
        c->definis[k] = e->definis[k];
        if (e->definis[k]) c->locaux[k] = valeur_copier(&e->locaux[k]);
    }
}

/* Retire les écritures sur le disque prévues après la marque. */
static void retirer_ecritures(Machine *m, size_t marque) {
    while (m->nb_a_ecrire > marque) {
        Ecriture_disque *w = &m->a_ecrire[--m->nb_a_ecrire];
        Valeur v = valeur_nombre(dec_zero());
        v.fichier = w->fichier;
        valeur_liberer(&v);
        free(w->chemin);
        free(w->ecrit);
    }
}

/* Nouvel objet du tas ; epoque : exécution où ses champs comptent comme déjà journalisés. */
static Objet *creer_objet(Machine *m, const ClasseVM *cl, unsigned long epoque) {
    Objet *o = grym_allouer(sizeof *o);
    size_t nc = cl->nb_champs ? cl->nb_champs : 1;
    o->classe = cl;
    o->champs = grym_allouer(nc * sizeof *o->champs);
    o->definis = grym_allouer(nc);
    memset(o->definis, 0, nc);
    o->epoques = grym_allouer(nc * sizeof *o->epoques);
    for (size_t q = 0; q < nc; q++) o->epoques[q] = epoque;
    o->marque = 0;
    o->id = 0;
    o->a_charger = 0;
    o->suivant = m->tas;
    m->tas = o;
    m->nb_objets++;
    m->depuis_ramassage++;
    return o;
}

/* ---------------------------------------------------------------- */
/* Services pour la base (vm_interne.h)                             */
/* ---------------------------------------------------------------- */

Valeur vi_nombre_canonique(const char *texte) { return valeur_nombre(dec_depuis_canonique(texte)); }

Valeur vi_texte(const char *texte) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_TEXTE;
    v.texte = grym_dupliquer(texte);
    return v;
}

Valeur vi_booleen(int vrai) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_BOOLEEN;
    v.vrai = vrai != 0;
    return v;
}

Valeur vi_date(long jours) { return valeur_date(jours); }
Valeur vi_annee(long annee) { return valeur_annee(annee); }
Valeur vi_heure(long minutes) { return valeur_heure(minutes); }

int heure_lire(const char *t, long *minutes) {
    int h = 0, m = 0, n = 0;
    if (!t || sscanf(t, "%d:%2d%n", &h, &m, &n) != 2 || t[n] || h < 0 || h > 23 || m < 0 || m > 59) return 0;
    const char *d = strchr(t, ':');
    if (!d || strlen(d + 1) != 2 || d - t < 1 || d - t > 2) return 0;
    *minutes = h * 60L + m;
    return 1;
}

Valeur vi_fichier(const void *octets, size_t taille, const char *nom) {
    Fichier *f = grym_allouer(sizeof *f);
    f->octets = grym_allouer(taille ? taille : 1);
    if (taille) memcpy(f->octets, octets, taille);
    f->taille = taille;
    f->nom = grym_dupliquer(nom);
    f->format = format_image(f->octets, taille);
    f->references = 1;
    f->base = NULL;
    f->cle = 0;
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_FICHIER;
    v.fichier = f;
    return v;
}

/* Les octets d'un fichier, lus en base au premier besoin (docs/vm.md, § 8) ; NULL et *erreur s'ils manquent. */
static const unsigned char *octets_du_fichier(const Fichier *f, char **erreur) {
    if (!f->octets && !base_fichier_lire((Fichier *)f, erreur)) return NULL;
    return f->octets;
}

/* Deux fichiers égaux : même contenu (§ 15) ; la même ligne de la base l'est sans rien lire. */
static int fichiers_egaux(const Fichier *a, const Fichier *b) {
    if (a == b) return 1;
    if (a->taille != b->taille) return 0;
    if (a->base && a->base == b->base && a->cle == b->cle) return 1;
    char *e = NULL;
    const unsigned char *x = octets_du_fichier(a, &e), *y = x ? octets_du_fichier(b, &e) : NULL;
    free(e);
    return x && y && memcmp(x, y, a->taille) == 0;
}

Valeur vi_fichier_en_base(struct Base *b, long cle, size_t taille, const unsigned char *debut, size_t n, const char *nom) {
    Fichier *f = grym_allouer(sizeof *f);
    f->octets = NULL;
    f->taille = taille;
    f->nom = grym_dupliquer(nom);
    f->format = debut ? format_image(debut, n) : NULL;
    f->references = 1;
    f->base = b;
    f->cle = cle;
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_FICHIER;
    v.fichier = f;
    return v;
}

Valeur vi_absent(const char *champ) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_ABSENT;
    v.texte = champ ? grym_dupliquer(champ) : NULL;
    return v;
}

/* « Le champ « date » est absent. » : une valeur absente ne se laisse pas utiliser par mégarde (§ 16.9). */
static char *message_absent(const Valeur *v) {
    return v->texte ? grym_formater("Le champ « %s » est absent : vérifie-le d'abord avec « est présent ».", v->texte)
                    : grym_dupliquer("La valeur est absente : vérifie-la d'abord avec « est présent ».");
}

Valeur vi_objet(Objet *o) {
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_OBJET;
    v.objet = o;
    return v;
}

static const ClasseVM *classe_vm(const Machine *m, const char *nom);

const ClasseVM *machine_classe(const Machine *m, const char *nom) { return classe_vm(m, nom); }

void vi_liberer(Valeur *v) { valeur_liberer(v); }

Valeur vi_liste(long *ids, char **classes, size_t n) {
    Liste *l = grym_allouer(sizeof *l);
    l->references = 1;
    l->ids = ids;
    l->classes = classes;
    l->n = n;
    l->valeurs = NULL;
    l->cap = 0;
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_LISTE;
    v.liste = l;
    return v;
}

Objet *machine_objet_en_base(Machine *m, long id, const char *classe, char **erreur) {
    Objet *o = carte_trouver(m, id);
    if (o && o->id == id) return o;
    const ClasseVM *c = classe_vm(m, classe);
    if (!c || !c->conserve) {
        *erreur = grym_formater("La base contient un objet « %s », que ce programme ne déclare pas comme entité.", classe);
        return NULL;
    }
    o = creer_objet(m, c, 0);   /* ses champs, une fois lus, entreront au journal à la première écriture */
    o->id = id;
    o->a_charger = 1;
    carte_mettre(m, id, o);
    return o;
}

/* Lit les champs d'un objet retrouvé, au premier accès. */
static int charger(Machine *m, Objet *o, char **erreur);

void machine_objet_efface(Machine *m, long id) {
    Objet *o = carte_trouver(m, id);
    if (!o || o->id != id) return;
    char *e = NULL;
    charger(m, o, &e);   /* ses valeurs restent en mémoire, octets des fichiers compris : leurs lignes partent */
    for (size_t k = 0; !e && k < o->classe->nb_champs; k++)
        if (o->definis[k] && o->champs[k].type == V_FICHIER) octets_du_fichier(o->champs[k].fichier, &e);
    free(e);
    ecrire_id(m, o, 0);
}

static int charger(Machine *m, Objet *o, char **erreur) {
    if (!o->a_charger) return 1;
    if (!base_charger(m->base, m, o, erreur)) return 0;
    o->a_charger = 0;
    return 1;
}

size_t machine_regles_verifiees(const Machine *m) { return m->regles_verifiees; }

size_t machine_objets_vivants(const Machine *m) {
    return m->nb_objets;
}

static const ClasseVM *classe_vm(const Machine *m, const char *nom) {
    for (size_t i = m->nb_classes; i > 0; i--)
        if (!m->classes[i - 1]->aptitude && strcmp(m->classes[i - 1]->nom, nom) == 0) return m->classes[i - 1];
    return NULL;
}

static const ClasseVM *aptitude_vm(const Machine *m, const char *nom) {
    for (size_t i = m->nb_classes; i > 0; i--)
        if (m->classes[i - 1]->aptitude && strcmp(m->classes[i - 1]->nom, nom) == 0) return m->classes[i - 1];
    return NULL;
}

static long index_champ(const ClasseVM *c, const char *champ) {
    for (size_t k = 0; k < c->nb_champs; k++)
        if (strcmp(c->champs[k], champ) == 0) return (long)k;
    return -1;
}

static char *decrire_valeur_pour_type(const Valeur *v);

/* Typage strict (grammaire, § 16.2) : la valeur convient-elle au type du champ ? NULL si oui, sinon le message. */
/* « « à faire », « en cours » ou « fini » » : la liste des valeurs permises, pour les messages (§ 16.15). */
static char *lister_valeurs(const char *valeurs) {
    Chaine c = {0};
    size_t nb = 1, k = 0;
    for (const char *p = valeurs; *p; p++) nb += *p == '\x1e';
    for (const char *p = valeurs; ; k++) {
        const char *q = strchr(p, '\x1e');
        if (k) chaine_ajouter(&c, k + 1 == nb ? " ou " : ", ");
        char *x = q ? grym_formater("« %.*s »", (int)(q - p), p) : grym_formater("« %s »", p);
        chaine_ajouter(&c, x);
        free(x);
        if (!q) break;
        p = q + 1;
    }
    return chaine_rendre(&c);
}

/* Les valeurs permises, une par case (pour un menu) ; *n reçoit leur nombre. */
static char **decouper_valeurs(const char *valeurs, size_t *n) {
    size_t nb = 1;
    for (const char *p = valeurs; *p; p++) nb += *p == '\x1e';
    char **r = grym_allouer(nb * sizeof *r);
    size_t k = 0;
    for (const char *p = valeurs; p; k++) {
        const char *q = strchr(p, '\x1e');
        r[k] = q ? grym_formater("%.*s", (int)(q - p), p) : grym_dupliquer(p);
        p = q ? q + 1 : NULL;
    }
    *n = nb;
    return r;
}

static int valeur_permise(const char *valeurs, const char *t) {
    size_t l = strlen(t);
    for (const char *p = valeurs; p; ) {
        const char *q = strchr(p, '\x1e');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        if (n == l && strncmp(p, t, l) == 0) return 1;
        p = q ? q + 1 : NULL;
    }
    return 0;
}

static char *verifier_type_champ(const Machine *m, const char *champ, const char *type, const Valeur *v, int facultatif,
                                 const char *valeurs) {
    if (v->type == V_ABSENT)
        return facultatif ? NULL : grym_formater("Le champ « %s » n'est pas facultatif : il ne devient pas absent.", champ);
    if (!type) return NULL;
    if (valeurs && v->type == V_TEXTE && !valeur_permise(valeurs, v->texte ? v->texte : "")) {
        char *l = lister_valeurs(valeurs);
        char *r = grym_formater("« %s » n'est pas une valeur permise de « %s » : %s.", v->texte ? v->texte : "", champ, l);
        free(l);
        return r;
    }
    int ok = 0;
    if (type_textuel(type)) {   /* un courriel, un téléphone, une adresse web : un texte, à la forme contrôlée */
        ok = v->type == V_TEXTE;
        const char *attendu = ok ? forme_refusee(type, v->texte) : NULL;
        if (attendu) return grym_formater("« %s » n'est pas %s.", v->texte, attendu);
    }
    else if (strcmp(type, "nombre") == 0) ok = v->type == V_NOMBRE;
    else if (strcmp(type, "nombre entier") == 0) ok = v->type == V_NOMBRE && dec_est_entier(&v->nombre);
    else if (strcmp(type, "vrai ou faux") == 0) ok = v->type == V_BOOLEEN;
    else if (strcmp(type, "date") == 0) ok = v->type == V_DATE;
    else if (strcmp(type, "heure") == 0) ok = v->type == V_HEURE;
    else if (strcmp(type, "année") == 0) {   /* une année, ou un nombre entier de 1 à 9999 (§ 14.5) */
        long a;
        ok = v->type == V_ANNEE || (v->type == V_NOMBRE && dec_en_long_borne(&v->nombre, 1, 9999, &a));
        if (!ok && v->type == V_NOMBRE) {
            char *n = dec_afficher(&v->nombre);
            char *r = grym_formater("Le champ « %s » attend une année (de 1 à 9999), pas %s.", champ, n);
            free(n);
            return r;
        }
    }
    else if (strcmp(type, "fichier") == 0) ok = v->type == V_FICHIER;
    else if (strcmp(type, "image") == 0) {
        if (v->type == V_FICHIER && !v->fichier->format)
            return grym_formater("« %s » n'est pas une image (PNG, JPEG, GIF ou WebP).", v->fichier->nom);
        ok = v->type == V_FICHIER;
    } else if (strcmp(type, "son") == 0) {   /* § 15.7 : la signature, lue dans les octets */
        if (v->type == V_FICHIER) {
            char *e = NULL;
            const unsigned char *o = octets_du_fichier(v->fichier, &e);
            if (!o) return e ? e : grym_formater("« %s » ne se lit pas.", v->fichier->nom);
            if (!format_son(o, v->fichier->taille))
                return grym_formater("« %s » n'est pas un son (WAV, MP3, M4A, Ogg, WebM ou FLAC).", v->fichier->nom);
        }
        ok = v->type == V_FICHIER;
    } else if (v->type == V_OBJET) {
        for (const ClasseVM *c = v->objet->classe; c && !ok; c = c->parent) ok = strcmp(c->nom, type) == 0;
    }
    if (ok) return NULL;
    const ClasseVM *e = classe_vm(m, type);
    char *attendu = strcmp(type, "vrai ou faux") == 0 ? grym_dupliquer("vrai ou faux")
                  : strcmp(type, "date") == 0 || strcmp(type, "image") == 0 || strcmp(type, "année") == 0
                    || strcmp(type, "heure") == 0 ? grym_formater("une %s", type)
                  : e ? grym_formater("%s %s", e->feminin ? "une" : "un", type)
                  : grym_formater("un %s", type);
    char *vu = decrire_valeur_pour_type(v);
    char *r = grym_formater("Le champ « %s » attend %s, pas %s.", champ, attendu, vu);
    free(attendu);
    free(vu);
    return r;
}

static char *article_classe(const ClasseVM *c) {
    return grym_formater("%s %s", c->feminin ? "une" : "un", c->nom);
}

/* Version appelée (grammaire, § 13.6) : la formule sans classe si elle existe ; sinon, la version
 * dont la classe est la plus proche de celle du premier argument, en remontant sa lignée. */
static Formule *choisir_version_depuis(Machine *m, const char *nom, const Valeur *premier, const char *depuis,
                                       char **pourquoi);

static Formule *choisir_version(Machine *m, const char *nom, const Valeur *premier, char **pourquoi) {
    const char *sep = strchr(nom, '\x1e');   /* « saluer␞personne » : « Saluer m comme une personne. » (§ 13.8) */
    if (!sep) return choisir_version_depuis(m, nom, premier, NULL, pourquoi);
    char *base = grym_formater("%.*s", (int)(sep - nom), nom);
    Formule *f = choisir_version_depuis(m, base, premier, sep + 1, pourquoi);
    free(base);
    return f;
}

static Formule *choisir_version_depuis(Machine *m, const char *nom, const Valeur *premier, const char *depuis,
                                       char **pourquoi) {
    Formule *f = depuis ? NULL : formule_de(m, nom, NULL);
    if (f) return f;
    int versions = 0;
    for (size_t i = 0; i < m->nb_formules; i++) if (strcmp(m->formules[i].nom, nom) == 0) versions++;
    if (!versions) {
        *pourquoi = grym_formater("Formule « %s » inconnue.", nom);
        return NULL;
    }
    if (premier && premier->type == V_ABSENT) {
        *pourquoi = message_absent(premier);
        return NULL;
    }
    if (!premier || premier->type != V_OBJET) {
        *pourquoi = grym_formater("« %s » choisit sa version selon la classe de son premier argument : "
                                  "celui-ci n'est pas un objet, c'est %s.", nom,
                                  premier ? nom_type(premier->type) : "absent");
        return NULL;
    }
    const ClasseVM *depart = premier->objet->classe;
    if (depuis) {   /* l'objet doit être de cette classe ; la recherche part d'elle, plus de sa classe réelle */
        const ClasseVM *c = depart;
        while (c && strcmp(c->nom, depuis) != 0) c = c->parent;
        if (!c) {
            char *qui = article_classe(depart);
            const ClasseVM *cd = classe_vm(m, depuis);
            *pourquoi = grym_formater("« %s » comme %s %s : %s n'en est pas un%s.", nom, cd && cd->feminin ? "une" : "un",
                                      depuis, qui, cd && cd->feminin ? "e" : "");
            free(qui);
            return NULL;
        }
        depart = c;
    }
    for (const ClasseVM *c = depart; c; c = c->parent) {
        f = formule_de(m, nom, c->nom);
        if (f) return f;
        /* sinon, une version d'aptitude : deux aptitudes à égalité, c'est un conflit (charte, art. 6) */
        Formule *trouvee = NULL;
        const ClasseVM *source = NULL;
        for (size_t k = 0; k < c->nb_aptitudes; k++) {
            Formule *g = formule_de(m, nom, c->aptitudes[k]->nom);
            if (!g) continue;
            if (trouvee) {
                *pourquoi = grym_formater("« %s » est défini par les aptitudes « %s » et « %s » de « %s ».",
                                          nom, source->nom, c->aptitudes[k]->nom, c->nom);
                return NULL;
            }
            trouvee = g;
            source = c->aptitudes[k];
        }
        if (trouvee) return trouvee;
    }
    char *qui = article_classe(depart);
    *pourquoi = grym_formater("Aucune version de « %s » pour %s.", nom, qui);
    free(qui);
    return NULL;
}

/* ---------------------------------------------------------------- */
/* Formulaire : « un nouveau client saisi » (grammaire, § 19)       */
/* ---------------------------------------------------------------- */

/* Avant de lire une ligne : ce qui précède est validé, la base est rendue, chaque essai repart d'ici (§ 17, § 18). */
static char *ouvrir_attente(Machine *m, Chaine *sortie, Cadre *cadres) {
    char *probleme = valider_jusqu_ici(m, sortie);
    if (probleme) return probleme;
    m->question_posee = 1;
    for (size_t k = 0; k < m->nb_essais; k++) {
        Essai *e = &m->essais[k];
        e->journal = 0;
        e->a_ecrire = 0;
        oublier_photo(e);
        photographier(e, &cadres[e->cadre]);
    }
    return NULL;
}

/* Après la ligne lue : le verrou de la base reprend, avec les points de reprise des essais. */
static char *fermer_attente(Machine *m) {
    char *probleme = NULL;
    if (m->base && !base_commencer(m->base, &probleme)) return probleme;
    if (base_changee_ailleurs(m->base) && !m->nb_essais) {
        /* un autre programme (l'atelier, une autre fenêtre) a écrit pendant l'attente : les objets déjà lus se
         * relisent au prochain accès, au lieu de montrer ce que la base ne contient plus (docs/vm.md, § 8) */
        for (Objet *o = m->tas; o; o = o->suivant) {
            if (o->id <= 0 || o->a_charger) continue;
            for (size_t k = 0; k < o->classe->nb_champs; k++)
                if (o->definis[k]) { valeur_liberer(&o->champs[k]); o->definis[k] = 0; }
            o->a_charger = 1;
        }
    }
    for (size_t k = 0; m->base && k < m->nb_essais && !probleme; k++) base_point(m->base, k + 1, &probleme);
    return probleme;
}

/* « date d'inscription » → « Date d'inscription » : première lettre en capitale (ASCII, Latin-1, œ) ;
 * les sigles du programme en capitales (§ 2.4) : « numéro rcc » → « Numéro RCC ». */
static char *capitale(const char *s) {
    char *r = texte_avec_sigles(s);
    unsigned char *u = (unsigned char *)r;
    if (u[0] >= 'a' && u[0] <= 'z') u[0] = (unsigned char)(u[0] - 32);
    else if (u[0] == 0xC3 && u[1] >= 0xA0 && u[1] <= 0xBE && u[1] != 0xB7) u[1] = (unsigned char)(u[1] - 0x20);
    else if (u[0] == 0xC5 && u[1] == 0x93) u[1] = 0x92;
    return r;
}

/* Valeur de départ d'un champ (§ 16.7), depuis sa forme canonique. */
static Valeur valeur_de_depart(const char *type, const char *canonique) {
    long j = 0;
    if (strcmp(type, "nombre") == 0 || strcmp(type, "nombre entier") == 0) return vi_nombre_canonique(canonique);
    if (strcmp(type, "vrai ou faux") == 0) return vi_booleen(strcmp(canonique, "vrai") == 0);
    if (strcmp(type, "date") == 0 && date_lire_iso(canonique, &j)) return vi_date(j);
    if (strcmp(type, "année") == 0) return vi_annee(atol(canonique));
    if (strcmp(type, "heure") == 0 && heure_lire(canonique, &j)) return valeur_heure(j);
    return vi_texte(canonique);
}

/* Champ texte unique qui désigne un objet de l'entité (le premier), ou NULL (§ 19). */
static const char *champ_cle(const ClasseVM *c) {
    for (size_t k = 0; k < c->nb_champs; k++)
        if (c->types[k] && type_textuel(c->types[k]) && (c->uniques[k] & 1)) return c->champs[k];
    return NULL;
}

/* Objets conservés (mode 0 : liste ; 2 : nombre ; 5 : nombre dans la corbeille) dont le champ vaut v. */
static int chercher_valeur(Machine *m, const char *entite, int mode, const char *champ, const Valeur *v,
                           Valeur *r, char **erreur) {
    char *d = grym_formater("%s\x1f%d\x1f\x1f" "0\x1f(=[%s]?1)", entite, mode, champ);
    int ok = base_chercher(m->base, m, d, v, 1, r, erreur);
    free(d);
    return ok;
}

static long nombre_de(const Valeur *v) {
    long n = 0;
    if (v->type == V_NOMBRE) dec_en_long_borne(&v->nombre, 0, LONG_MAX, &n);
    return n;
}

/* Le champ saute-t-il le formulaire ? liste : champs initialisés dans le bloc, séparés par « , ». */
static int initialise(const char *liste, const char *champ) {
    size_t n = strlen(champ);
    for (const char *p = liste; *p; ) {
        const char *f = strstr(p, ", ");
        size_t l = f ? (size_t)(f - p) : strlen(p);
        if (l == n && strncmp(p, champ, n) == 0) return 1;
        p += l + (f ? 2 : 0);
    }
    return 0;
}

/* Valeur actuelle d'un champ, telle que le formulaire de modification la montre : un lien par sa clé,
 * un fichier par son nom d'origine. NULL : rien à montrer. */
static char *valeur_montree(Machine *m, const Valeur *v, const char *cle) {
    if (v->type == V_ABSENT) return NULL;
    if (v->type == V_FICHIER) return grym_dupliquer(v->fichier->nom);
    if (v->type == V_OBJET && cle) {
        char *erreur = NULL;
        if (!charger(m, v->objet, &erreur)) { free(erreur); return NULL; }
        long q = index_champ(v->objet->classe, cle);
        if (q < 0 || !v->objet->definis[q]) return NULL;
        return texte_valeur(m, &v->objet->champs[q]);
    }
    return texte_valeur(m, v);
}

typedef struct { size_t k; Valeur v; } Modification;

static void oublier_modifications(Modification *t, size_t n) {
    for (size_t q = 0; q < n; q++) valeur_liberer(&t[q].v);
    free(t);
}

/* État commun des validations : le motif d'un arrêt, et s'il est rattrapable. */
typedef struct {
    Machine *m;
    int fatal;
    char *motif;
} Attente;

/* Pose des champs par l'interface, avec la discipline des questions (§ 17) : ce qui précède est validé
 * avant, le verrou de la base repris après. NULL : tout est répondu ; sinon le motif, et a->fatal dit
 * s'il échappe aux essais. */
static char *poser(Machine *m, Chaine *sortie, Cadre *cadres, Champ *champs, size_t n,
                   Validation valider, void *vcontexte, Attente *a) {
    if (!m->iface.disponible(m->iface.contexte)) {
        a->fatal = 1;
        return grym_dupliquer("Aucune entrée : la question ne peut pas être posée ici.");
    }
    char *probleme = ouvrir_attente(m, sortie, cadres);
    if (probleme) { a->fatal = 1; return probleme; }
    size_t k = 0;
    Issue issue = m->iface.formulaire(m->iface.contexte, sortie, champs, n, &k, valider, vcontexte);
    if (issue == ISSUE_ARRET) {
        probleme = a->motif;
        a->motif = NULL;
        return probleme ? probleme : grym_dupliquer("Saisie interrompue.");
    }
    if (issue == ISSUE_INTERROMPU) {
        grym_interruption = 0;
        a->fatal = 1;
        return grym_dupliquer("Interrompu (Ctrl+C).");
    }
    if (issue == ISSUE_FIN) {
        char *invite = champ_invite(&champs[k]);
        probleme = grym_formater("Plus rien à lire : la réponse à « %s » manque.", invite);
        free(invite);
        a->fatal = 1;
        return probleme;
    }
    probleme = fermer_attente(m);   /* le verrou reprend : l'essai qui rattrape retrouve une transaction */
    if (probleme) { a->fatal = 1; return probleme; }
    if (issue == ISSUE_ANNULE) return grym_dupliquer("Saisie annulée.");
    return NULL;
}

/* Une réponse refusée pendant un Ctrl+C : l'arrêt l'emporte sur la relance. */
static int interrompu(Attente *a, char **message) {
    if (!grym_interruption) return 0;
    grym_interruption = 0;
    free(*message);
    *message = NULL;
    a->motif = grym_dupliquer("Interrompu (Ctrl+C).");
    a->fatal = 1;
    return 1;
}

/* « la réponse à » : un champ, lu selon son type (§ 17). */
typedef struct {
    Attente a;
    Champ *champ;
    const char *type;
    Valeur r;
} Demande;

static int valider_demande(void *vcontexte, size_t k, char **message) {
    (void)k;
    Demande *d = vcontexte;
    char *ligne = d->champ->ligne;
    d->champ->ligne = NULL;
    Valeur v;
    char *pb = NULL;
    int lu = lire_reponse(d->type, ligne, &v, &pb);
    free(ligne);
    if (lu) {
        valeur_liberer(&d->r);
        d->r = v;
        return 0;
    }
    *message = pb;
    return interrompu(&d->a, message) ? -1 : 1;
}

/* Un champ du formulaire, vu par la machine. */
typedef struct {
    size_t k;               /* rang du champ dans la classe */
    const ClasseVM *lie;    /* lien : l'entité liée ; sa clé */
    const char *cle;
    int fichier, facultatif, unique;
    char *actuel;           /* modification : la valeur actuelle montrée ; NULL sinon */
    int a_depart;
    Valeur depart;
} ChampMachine;

typedef struct {
    Attente a;
    Objet *o;
    int modifier;
    Chaine *sortie;
    Cadre *cadres;
    Champ *champs;
    ChampMachine *infos;
    Modification *attente;   /* modification : écrite après la dernière réponse (tout ou rien) */
    size_t nb_attente;
} Formulaire;

static void en_attente(Formulaire *f, size_t k, Valeur v) {
    f->attente[f->nb_attente].k = k;
    f->attente[f->nb_attente++].v = v;
}

/* Lit la réponse au champ q : 0 acceptée, 1 relance (*message), -1 arrêt (f->a.motif). */
static int lire_champ(Formulaire *f, size_t q, char **message) {
    Machine *m = f->a.m;
    const ChampMachine *ci = &f->infos[q];
    const ClasseVM *cl = f->o->classe;
    size_t k = ci->k;
    const char *champ = cl->champs[k], *type = cl->types[k];
    char *ligne = f->champs[q].ligne ? f->champs[q].ligne : grym_dupliquer("");
    f->champs[q].ligne = NULL;
    char *t = ligne;
    while (*t == ' ' || *t == '\t') t++;
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t')) t[--n] = '\0';
    Fichier *recu = NULL;   /* un fichier téléversé par l'interface (docs/v2.md, § 10), au lieu d'un chemin */
    if (f->champs[q].fichier_recu) {
        recu = grym_allouer(sizeof *recu);
        recu->base = NULL;
        recu->cle = 0;
        recu->octets = f->champs[q].octets;
        recu->taille = f->champs[q].taille;
        recu->nom = f->champs[q].nom_fichier ? f->champs[q].nom_fichier : grym_dupliquer("fichier");
        recu->format = format_image(recu->octets, recu->taille);
        recu->references = 1;
        f->champs[q].octets = NULL;
        f->champs[q].nom_fichier = NULL;
        f->champs[q].fichier_recu = 0;
        n = 1;   /* une réponse, même si la ligne est vide */
    }
    if (f->champs[q].vider) {   /* « - » : le champ devient absent (§ 19) */
        if (recu) { Valeur x = valeur_nombre(dec_zero()); x.fichier = recu; x.type = V_FICHIER; valeur_liberer(&x); }
        free(ligne);
        en_attente(f, k, vi_absent(champ));
        return 0;
    }
    if (ci->actuel && !n) { free(ligne); return 0; }   /* ligne vide : la valeur actuelle reste */
    Valeur v;
    int pris = 0;
    char *relance = NULL;
    if (!n) {
        if (ci->facultatif) { free(ligne); return 0; }   /* reste absent */
        if (ci->a_depart) { v = valeur_copier(&ci->depart); pris = 1; }
        else relance = grym_dupliquer("Une réponse est attendue.");
    } else if (ci->lie) {
        Valeur cherche = vi_texte(t), l;
        char *erreur = NULL;
        if (!chercher_valeur(m, ci->lie->nom, 0, ci->cle, &cherche, &l, &erreur)) {
            valeur_liberer(&cherche);
            free(ligne);
            f->a.motif = erreur;
            return -1;
        }
        if (l.type == V_LISTE && l.liste->n) {
            Objet *x = machine_objet_en_base(m, l.liste->ids[0], l.liste->classes[0], &erreur);
            if (x) { v = vi_objet(x); pris = 1; }
            else relance = erreur;
        } else {
            relance = ci->lie->feminin
                    ? grym_formater("Aucune %s conservée n'a « %s » pour %s.", ci->lie->nom, t, ci->cle)
                    : grym_formater("Aucun %s conservé n'a « %s » pour %s.", ci->lie->nom, t, ci->cle);
        }
        valeur_liberer(&l);
        valeur_liberer(&cherche);
    } else if (ci->fichier) {
        char *erreur = NULL;
        Fichier *fi = recu ? recu : lire_fichier(m, t, &erreur);
        recu = NULL;
        if (fi) {
            v = valeur_nombre(dec_zero());
            v.type = V_FICHIER;
            v.fichier = fi;
            pris = 1;
        } else {
            relance = erreur;
        }
    } else {
        pris = lire_reponse(type, t, &v, &relance);
    }
    free(ligne);
    if (pris) relance = verifier_type_champ(m, champ, type, &v, ci->facultatif, cl->valeurs[k]);
    if (pris && !relance && ci->unique && m->base && ci->actuel) {
        char *x = valeur_montree(m, &v, ci->cle);   /* garder sa propre valeur n'est pas un doublon */
        int meme = x && strcmp(x, ci->actuel) == 0;
        free(x);
        if (meme) { en_attente(f, k, v); return 0; }
    }
    if (pris && !relance && ci->unique && m->base) {
        /* une valeur unique déjà prise, même dans la corbeille, se refuse dès la réponse */
        const char *table = cl->proprietaires[k]->nom;
        Valeur r1, r2;
        char *erreur = NULL;
        if (!chercher_valeur(m, table, 2, champ, &v, &r1, &erreur)) {
            valeur_liberer(&v);
            f->a.motif = erreur;
            return -1;
        }
        if (!chercher_valeur(m, table, 5, champ, &v, &r2, &erreur)) {
            valeur_liberer(&r1);
            valeur_liberer(&v);
            f->a.motif = erreur;
            return -1;
        }
        if (nombre_de(&r1) + nombre_de(&r2) > 0) {
            char *x = texte_valeur(m, &v);
            relance = grym_formater("« %s » est déjà pris.", x);
            free(x);
        }
        valeur_liberer(&r1);
        valeur_liberer(&r2);
    }
    if (pris && !relance) {
        if (f->modifier) en_attente(f, k, v);
        else ecrire_champ(m, f->o, k, v);
        return 0;
    }
    if (pris) valeur_liberer(&v);
    *message = relance;
    return interrompu(&f->a, message) ? -1 : 1;
}

/* Validation d'un champ du formulaire : le verrou reprend le temps de la vérifier (liens, unicité),
 * puis se rend avant la question suivante (§ 17). */
static int valider_champ(void *vcontexte, size_t q, char **message) {
    Formulaire *f = vcontexte;
    char *probleme = fermer_attente(f->a.m);
    if (probleme) {
        free(f->champs[q].ligne);
        f->champs[q].ligne = NULL;
        f->a.motif = probleme;
        f->a.fatal = 1;
        return -1;
    }
    int r = lire_champ(f, q, message);
    if (r < 0) return -1;
    probleme = ouvrir_attente(f->a.m, f->sortie, f->cadres);
    if (probleme) {
        free(*message);
        *message = NULL;
        f->a.motif = probleme;
        f->a.fatal = 1;
        return -1;
    }
    return r;
}

/* Remplit l'objet par un formulaire (grammaire, § 19). modifier : « Saisir à nouveau », chaque valeur
 * actuelle montrée, les champs écrits seulement après la dernière réponse (tout ou rien).
 * *fatal : erreur qu'aucun essai ne rattrape (entrée épuisée, base perdue, Ctrl+C). */
static char *saisir(Machine *m, Objet *o, const char *deja, Chaine *sortie, Cadre *cadres, int *fatal, int modifier) {
    const ClasseVM *cl = o->classe;
    if (modifier) {
        char *erreur = NULL;
        if (o->id && !charger(m, o, &erreur)) return erreur;
        if (o->id && m->base && base_est_supprime(m->base, o->id)) {
            char *qui = article_classe(cl);
            char *r = grym_formater("Cet objet (%s) est dans la corbeille : rétablis-le d'abord.", qui);
            free(qui);
            return r;
        }
    }
    size_t cap = cl->nb_champs ? cl->nb_champs : 1;
    Formulaire f = { { m, 0, NULL }, o, modifier, sortie, cadres,
                     grym_allouer(cap * sizeof(Champ)), grym_allouer(cap * sizeof(ChampMachine)),
                     grym_allouer(cap * sizeof(Modification)), 0 };
    size_t n = 0;
    char *probleme = NULL;
    /* les champs à demander, décrits d'avance : un lien impossible à demander se signale avant toute question */
    for (size_t k = 0; k < cl->nb_champs && !probleme; k++) {
        const char *champ = cl->champs[k], *type = cl->types[k];
        if ((cl->uniques[k] & 8) || initialise(deja, champ) || !type) continue;
        ChampMachine ci = { k, NULL, NULL, 0, (cl->uniques[k] & 2) != 0, (cl->uniques[k] & 1) != 0, NULL, 0, vi_absent(champ) };
        ci.fichier = type_fichier(type);
        static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "année", "heure" };
        int de_base = ci.fichier;
        for (size_t q = 0; q < sizeof BASE / sizeof *BASE; q++) de_base |= strcmp(type, BASE[q]) == 0;
        /* courriel, téléphone, adresse web : des textes ; un mot de passe ne passe pas par le formulaire, qui ne le hacherait pas,
         * ni un secret, qu'il montrerait en clair */
        de_base |= type_textuel(type) && !type_cache(type);
        if (!de_base) {
            ci.lie = classe_vm(m, type);
            ci.cle = ci.lie ? champ_cle(ci.lie) : NULL;
            if (!ci.cle) {
                if (ci.facultatif) { valeur_liberer(&ci.depart); continue; }
                valeur_liberer(&ci.depart);
                probleme = grym_formater("Le champ « %s » ne se demande pas : « %s » n'a aucun champ texte unique qui "
                                         "désigne un objet. Donne-lui sa valeur dans le bloc du nouvel objet.", champ, type);
                break;
            }
        }
        /* un mot de passe ne se propose pas en valeur actuelle : vide, il est gardé ; tapé, il est remplacé (§ 16.1) */
        ci.actuel = modifier && o->definis[k] && !type_cache(o->classe->types[k]) ? valeur_montree(m, &o->champs[k], ci.cle) : NULL;
        ci.a_depart = !modifier && cl->departs[k] != NULL;
        if (ci.a_depart) {
            valeur_liberer(&ci.depart);
            ci.depart = valeur_de_depart(type, cl->departs[k]);
        }
        Champ c;
        memset(&c, 0, sizeof c);
        c.libelle = capitale(champ);
        c.type = type;
        if (cl->valeurs[k]) {   /* valeurs permises (§ 16.15) : un menu complet ; en console, la liste dans la question */
            c.suggestions = (const char *const *)decouper_valeurs(cl->valeurs[k], &c.nb_suggestions);
            c.suggestions_completes = 1;
            if (!m->iface.riche) {
                char *l = lister_valeurs(cl->valeurs[k]);
                char *x = grym_formater("%s (%s)", c.libelle, l);
                free(l);
                free((char *)c.libelle);
                c.libelle = x;
            }
        }
        c.videable = ci.actuel && ci.facultatif;
        c.facultatif = ci.facultatif;
        if (ci.lie && m->iface.riche && m->base) {   /* un lien : les clés existantes en suggestions (§ 10) */
            char **vals = NULL;
            size_t nv = 0;
            char *erreur = NULL;
            long kc = index_champ(ci.lie, ci.cle);
            /* 1001 demandées : la millième et unième dit que la liste n'est pas complète (docs/v2.md, § 10) */
            if (kc >= 0 && base_valeurs(m->base, ci.lie->nom, ci.lie->proprietaires[kc]->nom, ci.cle, 1001, &vals, &nv, &erreur)) {
                c.suggestions_completes = nv <= 1000;
                if (nv > 1000) { free(vals[1000]); nv = 1000; }
                c.suggestions = (const char *const *)vals;
                c.nb_suggestions = nv;
            } else {
                free(erreur);
            }
        }
        c.valeur = ci.actuel ? grym_dupliquer(ci.actuel) : ci.a_depart ? texte_valeur(m, &ci.depart) : NULL;
        f.champs[n] = c;
        f.infos[n++] = ci;
    }
    if (!probleme && n) {
        probleme = poser(m, sortie, cadres, f.champs, n, valider_champ, &f, &f.a);
        if (f.a.fatal) *fatal = 1;
    }
    if (!probleme && modifier) {
        /* toutes les réponses sont là : les champs s'écrivent maintenant, et la base les suit (§ 16.3) */
        for (size_t q = 0; q < f.nb_attente && !probleme; q++) {
            ecrire_champ(m, o, f.attente[q].k, f.attente[q].v);
            f.attente[q].v = vi_absent("");
            if (o->id) base_ecrire_champ(m->base, o, f.attente[q].k, &probleme);
        }
    }
    for (size_t q = 0; q < n; q++) {
        for (size_t k2 = 0; k2 < f.champs[q].nb_suggestions; k2++) free((char *)f.champs[q].suggestions[k2]);
        free((void *)f.champs[q].suggestions);
        free(f.champs[q].octets);
        free(f.champs[q].nom_fichier);
        free((char *)f.champs[q].libelle);
        free((char *)f.champs[q].valeur);
        free(f.champs[q].ligne);
        free(f.infos[q].actuel);
        valeur_liberer(&f.infos[q].depart);
    }
    free(f.champs);
    free(f.infos);
    oublier_modifications(f.attente, f.nb_attente);
    free(f.a.motif);
    return probleme;
}

/* ---------------------------------------------------------------- */
/* Fiche d'un objet (grammaire, § 20)                               */
/* ---------------------------------------------------------------- */

static size_t largeur_utf8(const char *s) {
    size_t n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

/* Un objet tel qu'une fiche le nomme : par sa clé, sinon « un compositeur ». */
static char *nom_objet(Machine *m, Objet *x) {
    const char *cle = champ_cle(x->classe);
    Valeur v = vi_objet(x);
    char *r = cle ? valeur_montree(m, &v, cle) : NULL;
    valeur_liberer(&v);
    return r ? r : article_classe(x->classe);
}

/* « Afficher la fiche de p. » : chaque champ et sa valeur, un lien par sa clé, un multiple par ses objets. */
/* La couleur d'étiquette d'une valeur permise (§ 16.15) : sa lettre, ou 0 si elle n'en a pas. */
static char lettre_etiquette(const ClasseVM *cl, size_t q, const char *t) {
    if (!cl->couleurs || !cl->couleurs[q] || !cl->valeurs || !cl->valeurs[q] || !t || !*t) return 0;
    size_t rang = 0;
    for (const char *p = cl->valeurs[q]; p;) {
        const char *f = strchr(p, '\x1e');
        const size_t l = f ? (size_t)(f - p) : strlen(p);
        if (l == strlen(t) && strncmp(p, t, l) == 0) {
            const char lettre = rang < strlen(cl->couleurs[q]) ? cl->couleurs[q][rang] : '-';
            return lettre == '-' ? 0 : lettre;
        }
        rang++;
        p = f ? f + 1 : NULL;
    }
    return 0;
}

/* Un courriel s'écrit, une adresse web s'ouvre (§ 22.13) : la sorte de lien qu'en fait une fiche. */
static int lien_du_type(const char *type) {
    if (!type) return 0;
    if (strcmp(type, "courriel") == 0) return LIEN_COURRIEL;
    if (strcmp(type, "adresse web") == 0) return LIEN_WEB;
    return 0;
}

static char *afficher_fiche(Machine *m, Objet *o, Chaine *sortie) {
    char *erreur = NULL;
    if (o->id && !charger(m, o, &erreur)) return erreur;
    const ClasseVM *cl = o->classe;
    size_t n = cl->nb_champs;
    LigneFiche *l = grym_allouer((n ? n : 1) * sizeof *l);
    char **textes = grym_allouer((n ? n : 1) * sizeof *textes);
    char **libelles = grym_allouer((n ? n : 1) * sizeof *libelles);
    for (size_t k = 0; k < n && !erreur; k++) {
        memset(&l[k], 0, sizeof l[k]);
        libelles[k] = capitale(cl->champs[k]);
        textes[k] = NULL;
        if (cl->uniques[k] & 8) {   /* un champ multiple : ses objets, dans l'ordre où ils ont été gagnés */
            Chaine c = {0};
            long *ids = NULL;
            char **classes = NULL;
            size_t nm = 0;
            if (o->id && m->base && base_membres(m->base, cl, k, o->id, &ids, &classes, &nm, &erreur)) {
                for (size_t q = 0; q < nm && !erreur; q++) {
                    Objet *x = machine_objet_en_base(m, ids[q], classes[q], &erreur);
                    if (!x) break;
                    char *t = nom_objet(m, x);
                    if (q) chaine_ajouter(&c, ", ");
                    chaine_ajouter(&c, t);
                    free(t);
                }
            }
            for (size_t q = 0; q < nm; q++) free(classes[q]);
            free(classes);
            free(ids);
            textes[k] = c.n ? chaine_rendre(&c) : (free(c.d), grym_dupliquer("aucun"));
        } else if (!o->definis[k] || o->champs[k].type == V_ABSENT) {
            textes[k] = grym_dupliquer("absent");
        } else if (o->champs[k].type == V_OBJET) {
            textes[k] = nom_objet(m, o->champs[k].objet);
        } else if (type_cache(o->classe->types[k])) {
            textes[k] = grym_dupliquer("••••••");   /* un mot de passe, un secret ne se montrent pas (§ 16.1, § 22.17) */
        } else {
            textes[k] = texte_valeur(m, &o->champs[k]);
            l[k].etiquette = lettre_etiquette(cl, k, textes[k]);   /* « payée » en vert (§ 16.15) */
            l[k].lien = o->champs[k].type == V_TEXTE ? lien_du_type(cl->types[k]) : 0;
            char *illisible = NULL;
            const char *montre = o->champs[k].type == V_FICHIER ? format_montre(o->champs[k].fichier, &illisible) : NULL;
            if (montre && octets_du_fichier(o->champs[k].fichier, &illisible)) {   /* une image, ou un son (§ 15.7) */
                l[k].octets = o->champs[k].fichier->octets;
                l[k].taille = o->champs[k].fichier->taille;
                l[k].format = montre;
            }
            free(illisible);
        }
        l[k].libelle = libelles[k];
        l[k].texte = textes[k];
    }
    if (!erreur) {
        const char *cle = champ_cle(cl);
        char *nom = capitale(cl->nom);
        char *clef = NULL;
        if (cle) { long kc = index_champ(cl, cle); if (kc >= 0 && o->definis[kc]) clef = texte_valeur(m, &o->champs[kc]); }
        char *titre = clef ? grym_formater("%s %s", nom, clef) : grym_dupliquer(nom);
        if (m->iface.afficher_fiche) {
            m->iface.afficher_fiche(m->iface.contexte, sortie, titre, l, n);
        } else {   /* la console : le titre, puis une colonne de libellés alignés */
            size_t large = 0;
            for (size_t k = 0; k < n; k++) if (largeur_utf8(l[k].libelle) > large) large = largeur_utf8(l[k].libelle);
            chaine_ajouter(sortie, titre);
            chaine_ajouter(sortie, "\n");
            for (size_t k = 0; k < n; k++) {
                chaine_ajouter(sortie, "  ");
                chaine_ajouter(sortie, l[k].libelle);
                for (size_t e = largeur_utf8(l[k].libelle); e < large + 2; e++) chaine_ajouter(sortie, " ");
                chaine_ajouter(sortie, l[k].texte);
                chaine_ajouter(sortie, "\n");
            }
        }
        free(titre);
        free(clef);
        free(nom);
    }
    for (size_t k = 0; k < n; k++) { free(textes[k]); free(libelles[k]); }
    free(textes);
    free(libelles);
    free(l);
    return erreur;
}

/* ---------------------------------------------------------------- */
/* Écrans (grammaire, § 22)                                          */
/* ---------------------------------------------------------------- */



static void html_echapper(Chaine *c, const char *t);

/* Les octets en base64, pour une image dans la page (data:). */
static void ajouter_base64(Chaine *c, const unsigned char *o, size_t n) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char q[5] = {0};
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (unsigned)o[i] << 16 | (i + 1 < n ? (unsigned)o[i + 1] << 8 : 0) | (i + 2 < n ? o[i + 2] : 0);
        q[0] = T[v >> 18 & 63];
        q[1] = T[v >> 12 & 63];
        q[2] = i + 1 < n ? T[v >> 6 & 63] : '=';
        q[3] = i + 2 < n ? T[v & 63] : '=';
        chaine_ajouter(c, q);
    }
}

/* Les éléments de k jusqu'à la fin du bloc qui les contient ; rend l'indice qui suit. Un « côte à côte » est un tableau
 * d'une ligne, une cellule par élément ou par bloc qu'il contient (2.25). */
static size_t document_elements(const EcranVM *e, size_t k, Chaine *c, int cote);

static size_t document_un(const EcranVM *e, size_t k, Chaine *c) {
    const ElementEcran *el = &e->elements[k];
    switch (el->sorte) {
    case ELEMENT_TEXTE: {
        const char *t = e->textes[k] ? e->textes[k] : (el->texte ? el->texte : "");
        if (el->capitales == 2) {
            /* la section paiement (2.28) : les lignes du Swiss QR Code, que la mise en page dessine en bas de la
             * dernière page ; rien sans elles (un cabinet sans IBAN) */
            if (*t && strcmp(t, "…") != 0) {
                chaine_ajouter(c, "<p class=\"grym-paiement\" title=\"");
                ajouter_base64(c, (const unsigned char *)t, strlen(t));
                chaine_ajouter(c, "\"></p>\n");
            }
            return k + 1;
        }
        if (el->capitales) {
            char *m = texte_majuscules(t);
            chaine_ajouter(c, "<p class=\"capitales\">");
            html_echapper(c, m);
            free(m);
        } else {
            chaine_ajouter(c, "<p>");
            for (const char *p = t; *p; p++) {   /* un retour à la ligne reste un retour à la ligne */
                if (*p == '\n') { chaine_ajouter(c, "<br>"); continue; }
                char u[2] = { *p, 0 };
                html_echapper(c, u);
            }
        }
        chaine_ajouter(c, "</p>\n");
        return k + 1;
    }
    case ELEMENT_IMAGE:
        if (el->octets && el->taille) {
            char *a = el->largeur > 0 ? grym_formater(" width=\"%d\"", el->largeur) : grym_dupliquer("");
            char *h = el->hauteur > 0 ? grym_formater(" height=\"%d\"", el->hauteur) : grym_dupliquer("");
            const char *mime = el->format && strcmp(el->format, "JPEG") == 0 ? "image/jpeg" : el->format && strcmp(el->format, "GIF") == 0
                             ? "image/gif" : el->format && strcmp(el->format, "WebP") == 0 ? "image/webp" : "image/png";
            chaine_ajouter(c, "<p><img");
            chaine_ajouter(c, a);
            chaine_ajouter(c, h);
            chaine_ajouter(c, " src=\"data:");
            chaine_ajouter(c, mime);
            chaine_ajouter(c, ";base64,");
            ajouter_base64(c, el->octets, el->taille);
            chaine_ajouter(c, "\"></p>\n");
            free(a);
            free(h);
        }
        return k + 1;
    case ELEMENT_LISTE: {
        const size_t nc = e->nb_colonnes[k], nl = e->doc_lignes ? e->doc_lignes[k] : 0;
        chaine_ajouter(c, "<table class=\"liste\" width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><thead><tr>");
        for (size_t q = 0; q < nc; q++) {
            chaine_ajouter(c, "<th align=\"left\">");
            html_echapper(c, e->colonnes[k][q]);
            chaine_ajouter(c, "</th>");
        }
        chaine_ajouter(c, "</tr></thead><tbody>\n");
        for (size_t r = 0; r < nl; r++) {
            chaine_ajouter(c, "<tr>");
            for (size_t q = 0; q < nc; q++) {
                chaine_ajouter(c, "<td>");
                html_echapper(c, e->doc_cellules[k][r * nc + q]);
                chaine_ajouter(c, "</td>");
            }
            chaine_ajouter(c, "</tr>\n");
        }
        chaine_ajouter(c, "</tbody></table>\n");
        return k + 1;
    }
    case ELEMENT_COTE_A_COTE:
        chaine_ajouter(c, "<table class=\"cote\" width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>");
        k = document_elements(e, k + 1, c, 1);
        chaine_ajouter(c, "</tr></table>\n");
        return k;
    case ELEMENT_L_UN_SOUS_L_AUTRE: case ELEMENT_LIBRE: case ELEMENT_ONGLETS: case ELEMENT_ONGLET:
        chaine_ajouter(c, "<div>");
        k = document_elements(e, k + 1, c, 0);
        chaine_ajouter(c, "</div>\n");
        return k;
    case ELEMENT_GROUPE:
        chaine_ajouter(c, "<div class=\"groupe\"><p class=\"capitales\">");
        if (el->texte) { char *m = texte_majuscules(el->texte); html_echapper(c, m); free(m); }
        chaine_ajouter(c, "</p>");
        k = document_elements(e, k + 1, c, 0);
        chaine_ajouter(c, "</div>\n");
        return k;
    default:   /* bouton, zone, graphique, menu : rien sur le papier */
        if (el->sorte == ELEMENT_MENU) {
            size_t p = 1;
            for (k++; k < e->n && p; k++) {
                const int s = e->elements[k].sorte;
                if (s == ELEMENT_FIN_DE_BLOC) p--;
                else if (s == ELEMENT_COTE_A_COTE || s == ELEMENT_L_UN_SOUS_L_AUTRE || s == ELEMENT_GROUPE || s == ELEMENT_LIBRE
                         || s == ELEMENT_ONGLETS || s == ELEMENT_ONGLET || s == ELEMENT_MENU) p++;
            }
            return k;
        }
        return k + 1;
    }
}

static size_t document_elements(const EcranVM *e, size_t k, Chaine *c, int cote) {
    while (k < e->n && e->elements[k].sorte != ELEMENT_FIN_DE_BLOC) {
        if (cote) chaine_ajouter(c, "<td valign=\"top\">");
        k = document_un(e, k, c);
        if (cote) chaine_ajouter(c, "</td>");
    }
    return k < e->n ? k + 1 : k;   /* la fin du bloc */
}

/* La page d'un document (2.25) : A4, le titre, puis les éléments dans l'ordre ; les listes en tableaux. */
static char *document_html(const EcranVM *e) {
    Chaine c = {0};
    chaine_ajouter(&c, "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>");
    html_echapper(&c, e->titre ? e->titre : "Document");
    chaine_ajouter(&c, "</title><style>@page{size:A4;margin:18mm 15mm}"
                       "body{font-family:Helvetica,Arial,sans-serif;font-size:10pt;color:#111}"
                       "h1{font-size:18pt;margin:0 0 12px}p{margin:0 0 6px}"
                       "p.capitales{font-size:8pt;letter-spacing:1px;color:#555;margin-top:10px}"
                       "table.liste{border-collapse:collapse;width:100%;margin:6px 0 10px}"
                       "table.liste th{text-align:left;border-bottom:1.5pt solid #333;padding:4px 6px}"
                       "table.liste td{border-bottom:0.5pt solid #bbb;padding:4px 6px}"
                       "table.cote td{vertical-align:top;padding-right:12px}"
                       "div.groupe{margin:8px 0}</style></head><body>\n");
    if (e->titre && *e->titre) {   /* le titre, en style écrit sur la balise : QTextDocument ne lit pas toujours h1{…} */
        chaine_ajouter(&c, "<p style=\"font-size:20pt;font-weight:bold;margin:4px 0 14px\">");
        html_echapper(&c, e->titre);
        chaine_ajouter(&c, "</p>\n");
    }
    size_t k = 0;
    while (k < e->n) k = document_un(e, k, &c);
    chaine_ajouter(&c, "</body></html>\n");
    return chaine_rendre(&c);
}

static void ecran_detruire(EcranVM *e) {
    if (!e) return;
    for (size_t k = 0; k < e->n; k++) {
        if (e->images) valeur_liberer(&e->images[k]);
        free(e->textes[k]);
        for (size_t c = 0; c < e->nb_colonnes[k]; c++) free(e->colonnes[k][c]);
        free(e->colonnes[k]);
        free(e->champs[k]);
        valeur_liberer(&e->listes[k]);
        free(e->types[k]);
        free(e->indications[k]);
        free(e->bornes[k]);
        free(e->unites[k]);
        if (e->chemins[k]) {
            for (size_t c = 0; c < e->nb_colonnes[k]; c++) free(e->chemins[k][c]);
            free(e->chemins[k]);
        }
        for (size_t c = 0; c < e->nb_choix[k]; c++) free(e->choix[k][c]);
        free(e->choix[k]);
    }
    free(e->types);
    free(e->indications);
    free(e->bornes);
    free(e->unites);
    free(e->chemins);
    free(e->choix);
    free(e->nb_choix);
    free(e->champ);
    free(e->etats);
    free(e->images);
    free(e->calcules);
    for (size_t k = 0; e->doc_cellules && k < e->n; k++) {
        for (size_t x = 0; e->doc_cellules[k] && x < e->doc_lignes[k] * e->nb_colonnes[k]; x++) free(e->doc_cellules[k][x]);
        free(e->doc_cellules[k]);
    }
    free(e->doc_cellules);
    free(e->doc_lignes);
    free(e->titre);
    free(e->elements);
    free(e->textes);
    free(e->colonnes);
    free(e->nb_colonnes);
    free(e->champs);
    free(e->listes);
    free(e);
}

/* « des compositeurs » → « Compositeurs » ; « d'accueil » → « Accueil » (grammaire, § 22.1). */
static char *titre_d_ecran(const char *nom) {
    const char *p = nom;
    static const char *const debuts[] = { "des ", "du ", "de la ", "de l'", "de ", "d'" };
    for (size_t k = 0; k < sizeof debuts / sizeof *debuts; k++)
        if (strncmp(p, debuts[k], strlen(debuts[k])) == 0 && p[strlen(debuts[k])]) { p += strlen(debuts[k]); break; }
    return capitale(p);
}

/* La description compilée (« nom ␟ titre ␟ Lentité ␟ Bbouton ␟ Ttexte ») devient un écran prêt à montrer. */
static EcranVM *ecran_lire(const Machine *m, const char *desc, char **erreur) {
    size_t nb = 1;
    for (const char *p = desc; *p; p++) nb += *p == '\x1f';
    char **parts = grym_allouer(nb * sizeof *parts);
    size_t np = 0;
    const char *debut = desc;
    for (const char *p = desc;; p++) {
        if (*p == '\x1f' || !*p) {
            parts[np++] = grym_formater("%.*s", (int)(p - debut), debut);
            if (!*p) break;
            debut = p + 1;
        }
    }
    EcranVM *e = grym_allouer(sizeof *e);
    memset(e, 0, sizeof *e);
    if (np > 1 && strchr(parts[1], '\x1d')) {   /* « Recherche␝g » : la taille du titre (§ 22.5) */
        char *t = strchr(parts[1], '\x1d');
        e->taille_titre = t[1] == 'g' ? 1 : t[1] == 'p' ? -1 : 0;
        *t = 0;
    }
    e->titre = np > 1 && parts[1][0] ? grym_dupliquer(parts[1]) : titre_d_ecran(parts[0]);
    e->n = np > 2 ? np - 2 : 0;
    size_t t = e->n ? e->n : 1;
    e->elements = grym_allouer(t * sizeof *e->elements);
    memset(e->elements, 0, t * sizeof *e->elements);   /* type, choix : seulement pour une zone */
    e->textes = grym_allouer(t * sizeof *e->textes);
    e->colonnes = grym_allouer(t * sizeof *e->colonnes);
    e->nb_colonnes = grym_allouer(t * sizeof *e->nb_colonnes);
    e->champs = grym_allouer(t * sizeof *e->champs);
    e->listes = grym_allouer(t * sizeof *e->listes);
    e->champ = grym_allouer(t * sizeof *e->champ);
    e->types = grym_allouer(t * sizeof *e->types);
    e->indications = grym_allouer(t * sizeof *e->indications);
    e->bornes = grym_allouer(t * sizeof *e->bornes);
    e->unites = grym_allouer(t * sizeof *e->unites);
    e->choix = grym_allouer(t * sizeof *e->choix);
    e->nb_choix = grym_allouer(t * sizeof *e->nb_choix);
    e->chemins = grym_allouer(t * sizeof *e->chemins);
    e->etats = grym_allouer(t);
    memset(e->etats, 0, t);
    e->images = grym_allouer(t * sizeof *e->images);
    for (size_t k = 0; k < t; k++) e->images[k] = vi_absent(NULL);
    e->calcules = grym_allouer(t);
    memset(e->calcules, 0, t);
    char *nom_classe = grym_formater("écran %s", parts[0]);
    const ClasseVM *ce = classe_vm(m, nom_classe);
    free(nom_classe);
    for (size_t k = 0; k < e->n; k++) {
        char *nak = strchr(parts[k + 2], '\x15');   /* « … ␕ c » : une liste en cartes (2.19), lue puis retirée */
        const int cartes = nak && nak[1] == 'c';
        if (nak) *nak = '\0';
        const char *x = parts[k + 2];
        if (x[0] == '\x1d') {   /* « ␝96,16,240,0␝ » : la place d'un élément en position libre (§ 22.9) */
            int g = 0, h = 0, l = 0, ht = 0;
            if (sscanf(x + 1, "%d,%d,%d,%d", &g, &h, &l, &ht) == 4) {
                e->elements[k].place = 1;
                e->elements[k].gauche = g;
                e->elements[k].haut = h;
                e->elements[k].largeur = l;
                e->elements[k].hauteur = ht;
            }
            const char *f2 = strchr(x + 1, '\x1d');
            x = f2 ? f2 + 1 : x + strlen(x);
        }
        e->champ[k] = -1;
        e->types[k] = NULL;
        e->indications[k] = NULL;
        e->bornes[k] = NULL;
        e->unites[k] = NULL;
        e->chemins[k] = NULL;
        e->choix[k] = NULL;
        e->nb_choix[k] = 0;
        if (x[0] == 'H' || x[0] == 'V' || x[0] == 'F' || x[0] == 'G' || x[0] == 'P' || x[0] == 'O' || x[0] == 'Q' || x[0] == 'M') {
            /* un bloc de disposition (§ 22.1), un groupe (§ 22.7), un bloc en position libre (§ 22.9), les onglets et
             * un onglet (§ 22.10) */
            e->textes[k] = grym_dupliquer(x[0] == 'G' || x[0] == 'Q' || x[0] == 'M' ? x + 1 : "");
            if (x[0] == 'G') {   /* « Gprix ␞ i » : en couleur inversée (2.18) */
                char *o = strchr(e->textes[k], '\x1e');
                if (o) { e->elements[k].inverse = strchr(o + 1, 'i') != NULL; *o = '\0'; }
            }
            e->elements[k].sorte = x[0] == 'H' ? ELEMENT_COTE_A_COTE : x[0] == 'V' ? ELEMENT_L_UN_SOUS_L_AUTRE
                                 : x[0] == 'G' ? ELEMENT_GROUPE : x[0] == 'P' ? ELEMENT_LIBRE : x[0] == 'O' ? ELEMENT_ONGLETS
                                 : x[0] == 'Q' ? ELEMENT_ONGLET : x[0] == 'M' ? ELEMENT_MENU : ELEMENT_FIN_DE_BLOC;
            e->elements[k].texte = e->textes[k];
            e->listes[k] = vi_absent(NULL);
            e->colonnes[k] = NULL;
            e->nb_colonnes[k] = 0;
            e->champs[k] = NULL;
            continue;
        }
        if (x[0] == 'Z') {   /* « Zpays ␞ texte ␞ f » : une zone de saisie */
            const char *s1 = strchr(x, '\x1e'), *s2 = s1 ? strchr(s1 + 1, '\x1e') : NULL;
            char *nomz = grym_formater("%.*s", (int)(s1 ? s1 - x - 1 : (long)strlen(x + 1)), x + 1);
            e->types[k] = s1 ? grym_formater("%.*s", (int)(s2 ? s2 - s1 - 1 : (long)strlen(s1 + 1)), s1 + 1) : grym_dupliquer("texte");
            e->textes[k] = capitale(nomz);
            e->champ[k] = ce ? index_champ(ce, nomz) : -1;
            e->elements[k].sorte = ELEMENT_ZONE;
            e->elements[k].texte = e->textes[k];
            e->elements[k].colonnes = NULL;
            e->elements[k].nb_colonnes = 0;
            e->elements[k].type = e->types[k];
            /* « Zpays ␞ texte ␞ fms ␝ 4 chiffres » : les lettres des options, puis l'indication (§ 22.6) */
            /* après les lettres des options, des morceaux, chacun derrière son séparateur : « ␜ 1 ␛ 99 » les bornes,
             * « ␚ < CHF » l'unité, « ␝ 4 chiffres » l'indication (§ 22.6) */
            const size_t nopt = s2 ? strcspn(s2 + 1, "\x1c\x1a\x1d") : 0;
            e->indications[k] = morceau_de_zone(s2, '\x1d');
            e->elements[k].indication = e->indications[k];
            e->unites[k] = morceau_de_zone(s2, '\x1a');
            e->elements[k].unite = e->unites[k] && e->unites[k][0] ? e->unites[k] + 1 : NULL;
            e->elements[k].unite_devant = e->unites[k] && e->unites[k][0] == '<';
            e->bornes[k] = morceau_de_zone(s2, '\x1c');   /* coupées en deux ci-dessous (« 1 », « 99 ») */
            e->elements[k].borne_min = e->elements[k].borne_max = NULL;
            if (e->bornes[k]) {
                char *sep = strchr(e->bornes[k], '\x1b');
                if (sep) *sep = 0;
                e->elements[k].borne_min = *e->bornes[k] ? e->bornes[k] : NULL;
                e->elements[k].borne_max = sep && sep[1] ? sep + 1 : NULL;
            }
            e->elements[k].facultatif = s2 && s2[1] == 'f';
            e->elements[k].plusieurs_lignes = s2 && memchr(s2 + 1, 'm', nopt) != NULL;
            e->elements[k].lecture_seule = s2 && memchr(s2 + 1, 's', nopt) != NULL;
            e->elements[k].interrupteur = s2 && memchr(s2 + 1, 'i', nopt) != NULL;   /* § 22.6 */
            e->elements[k].multiple = s2 && memchr(s2 + 1, 'p', nopt) != NULL;   /* « des genres (genre) » */
            e->elements[k].recherche = s2 && memchr(s2 + 1, 'r', nopt) != NULL;   /* « avec recherche » */
            e->elements[k].masque = (s2 && memchr(s2 + 1, 'c', nopt) != NULL) || type_cache(e->types[k]);   /* « masqué », ou un mot de passe */
            e->elements[k].dossier = s2 && memchr(s2 + 1, 'd', nopt) != NULL;   /* « avec le choix d'un dossier » (2.32) */
            {   /* « large de 12 caractères » (2.34) : « ␙12 » parmi les options */
                const char *w = s2 ? memchr(s2 + 1, '\x19', nopt) : NULL;
                e->elements[k].largeur_caracteres = w ? atoi(w + 1) : 0;
            }
            e->elements[k].choix = NULL;
            e->elements[k].nb_choix = 0;
            const ClasseVM *lie = classe_vm(m, e->types[k]);
            if (ce && e->champ[k] >= 0 && ce->valeurs[e->champ[k]]) {   /* valeurs permises : leur menu (§ 16.15) */
                size_t nv = 0;
                char **vals = decouper_valeurs(ce->valeurs[e->champ[k]], &nv);
                e->choix[k] = vals;
                e->nb_choix[k] = nv;
                e->elements[k].choix = (const char *const *)vals;
                e->elements[k].nb_choix = nv;
            } else if (lie && lie->conserve && m->base) {   /* un menu des clés, comme les formulaires (docs/v2.md, § 10) */
                const char *cle = champ_cle(lie);
                long kc = cle ? index_champ(lie, cle) : -1;
                char **vals = NULL;
                size_t nv = 0;
                char *err = NULL;
                if (kc >= 0 && base_valeurs(m->base, lie->nom, lie->proprietaires[kc]->nom, cle, 1000, &vals, &nv, &err)) {
                    e->choix[k] = vals;
                    e->nb_choix[k] = nv;
                    e->elements[k].choix = (const char *const *)vals;
                    e->elements[k].nb_choix = nv;
                } else free(err);
            }
            free(nomz);
            e->listes[k] = vi_absent(NULL);
            e->colonnes[k] = NULL;
            e->nb_colonnes[k] = 0;
            e->champs[k] = NULL;
            continue;
        }
        const char *colonnes_choisies = strchr(x, '\x1b');
        const char *options_bouton = x[0] == 'B' ? strchr(x, '\x1e') : NULL;   /* « Bfermer ␞ p » : principal (2.14) */
        if (options_bouton) colonnes_choisies = options_bouton;
        const char *options_texte = x[0] == 'T' ? strchr(x, '\x1e') : NULL;   /* « Taujourd'hui ␞ s » (2.20) */
        if (options_texte) colonnes_choisies = options_texte;
        /* « Rb+ » : la forme et la période dans la deuxième lettre (§ 22.11) */
        const char *pf = x[0] == 'R' ? strchr("bcdestuv", x[1]) : NULL;
        const int rang_f = pf ? (int)(pf - "bcdestuv") : 0;
        const int graphe = x[0] == 'R' ? 1 | (rang_f >= 4 ? 2 : 0) | (x[2] == '+' ? 4 : 0) | ((rang_f % 4) ? 4 << (rang_f % 4) : 0) : 0;
        const char *nom_l = x + 1 + (graphe ? 2 : 0);
        e->textes[k] = colonnes_choisies ? grym_formater("%.*s", (int)(colonnes_choisies - nom_l), nom_l) : grym_dupliquer(nom_l);
        e->colonnes[k] = NULL;
        e->nb_colonnes[k] = 0;
        e->champs[k] = NULL;
        e->listes[k] = vi_absent(NULL);
        e->elements[k].texte = e->textes[k];
        e->elements[k].colonnes = NULL;
        e->elements[k].nb_colonnes = 0;
        e->elements[k].graphique = graphe;
        e->elements[k].capitales = options_texte && strchr(options_texte + 1, 's') != NULL;
        if (options_texte && strchr(options_texte + 1, 'q')) e->elements[k].capitales = 2;   /* la section paiement (2.28) */
        if (options_bouton) {   /* « ␞ pt ␝ description ␚ image » (2.14, 2.15) */
            const size_t nopt = strcspn(options_bouton + 1, "\x1c\x1a\x1d");
            e->elements[k].principal = memchr(options_bouton + 1, 'p', nopt) != NULL;
            e->elements[k].tuile = memchr(options_bouton + 1, 't', nopt) != NULL;
            e->elements[k].a_cote_du_titre = memchr(options_bouton + 1, 'e', nopt) != NULL;   /* 2.17 */
            e->indications[k] = morceau_de_zone(options_bouton, '\x1d');
            e->elements[k].indication = e->indications[k];
            char *image = morceau_de_zone(options_bouton, '\x1a');
            if (image) {   /* relue à chaque ouverture, comme « l'image « … » » (§ 22.8) */
                Fichier *f = lire_fichier(m, image, erreur);
                free(image);
                if (!f) break;
                e->images[k].type = V_FICHIER;
                e->images[k].fichier = f;
                char *illisible = NULL;
                if (octets_du_fichier(f, &illisible)) {
                    const char *o = (const char *)f->octets;
                    size_t d = 0;
                    while (d < f->taille && (o[d] == ' ' || o[d] == '\n' || o[d] == '\r' || o[d] == '\t')) d++;
                    e->elements[k].octets = f->octets;
                    e->elements[k].taille = f->taille;
                    e->elements[k].format = f->format ? f->format : "SVG";
                }
                free(illisible);
            }
        }
        e->elements[k].cartes = cartes && x[0] == 'L';
        e->elements[k].sorte = graphe ? ELEMENT_GRAPHIQUE : x[0] == 'L' ? ELEMENT_LISTE : x[0] == 'B' ? ELEMENT_BOUTON
                             : x[0] == 'I' || x[0] == 'J' ? ELEMENT_IMAGE : ELEMENT_TEXTE;
        if (x[0] == 'J') { e->elements[k].calculee = 1; continue; }   /* image calculée : ÉCRAN_IMAGE la donne (§ 22.8) */
        if (x[0] == 'I') {   /* « l'image « plan.png » » (§ 22.8) : relue à chaque ouverture, elle a pu changer depuis l'analyse */
            Fichier *f = lire_fichier(m, x + 1, erreur);
            if (!f) break;
            e->images[k].type = V_FICHIER;
            e->images[k].fichier = f;
            if (!f->format) { *erreur = grym_formater("« %s » n'est pas une image (PNG, JPEG, GIF ou WebP).", x + 1); break; }
            e->elements[k].octets = f->octets;
            e->elements[k].taille = f->taille;
            e->elements[k].format = f->format;
            continue;
        }
        if (x[0] != 'L' && !graphe) continue;
        /* Colonnes par défaut : les champs simples, dans l'ordre ; ni fichiers, ni images, ni « plusieurs » (§ 22.1) */
        const ClasseVM *c = classe_vm(m, e->textes[k]);
        if (!c) { *erreur = grym_formater("Entité « %s » inconnue.", e->textes[k]); break; }
        if (ce) {   /* le champ « … choisi » de la classe de l'écran */
            char *choisi = grym_formater("%s %s", c->nom, c->feminin ? "choisie" : "choisi");
            e->champ[k] = index_champ(ce, choisi);
            free(choisi);
        }
        if (colonnes_choisies) {   /* « avec le titre et la naissance du compositeur » : les colonnes écrites (§ 22.1) */
            size_t nb = 1;
            for (const char *q = colonnes_choisies + 1; *q; q++) nb += *q == '\x1e';
            e->colonnes[k] = grym_allouer(nb * sizeof(char *));
            e->chemins[k] = grym_allouer(nb * sizeof(char *));
            e->champs[k] = grym_allouer(nb * sizeof(size_t));
            const char *q = colonnes_choisies + 1;
            for (size_t col = 0; col < nb; col++) {
                const char *fin = strchr(q, '\x1e');
                if (!fin) fin = q + strlen(q);
                const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
                char *ecrit = grym_formater("%.*s", (int)((sep ? sep : fin) - q), q);
                const char *sans = ecrit;   /* le titre : l'écrit sans son article */
                if (!strncmp(sans, "le ", 3) || !strncmp(sans, "la ", 3)) sans += 3;
                else if (!strncmp(sans, "l'", 2)) sans += 2;
                e->colonnes[k][col] = capitale(sans);
                e->chemins[k][col] = sep ? grym_formater("%.*s", (int)(fin - sep - 1), sep + 1) : grym_dupliquer("");
                e->champs[k][col] = 0;
                free(ecrit);
                q = *fin ? fin + 1 : fin;
            }
            e->nb_colonnes[k] = nb;
            if (graphe && nb == 1) {   /* compté : une seconde colonne, « Nombre », pour l'interface (§ 22.11) */
                char **deux = grym_allouer(2 * sizeof(char *));
                deux[0] = e->colonnes[k][0];
                deux[1] = (char *)"Nombre";   /* une constante : la libération s'arrête à nb_colonnes */
                free(e->colonnes[k]);
                e->colonnes[k] = deux;
            }
            e->elements[k].colonnes = (const char *const *)e->colonnes[k];
            e->elements[k].nb_colonnes = graphe ? 2 : nb;
            if (ce) {
                char *choisi = grym_formater("%s %s", c->nom, c->feminin ? "choisie" : "choisi");
                e->champ[k] = index_champ(ce, choisi);
                free(choisi);
            }
            continue;
        }
        e->colonnes[k] = grym_allouer((c->nb_champs ? c->nb_champs : 1) * sizeof(char *));
        e->champs[k] = grym_allouer((c->nb_champs ? c->nb_champs : 1) * sizeof(size_t));
        for (size_t q = 0; q < c->nb_champs; q++) {
            const char *ty = c->types[q];
            if ((c->uniques[q] & 8) || (ty && (type_fichier(ty)))) continue;
            e->champs[k][e->nb_colonnes[k]] = q;
            e->colonnes[k][e->nb_colonnes[k]++] = capitale(c->champs[q]);
        }
        e->elements[k].colonnes = (const char *const *)e->colonnes[k];
        e->elements[k].nb_colonnes = e->nb_colonnes[k];
    }
    for (size_t k = 0; k < np; k++) free(parts[k]);
    free(parts);
    if (*erreur) { ecran_detruire(e); return NULL; }
    return e;
}

/* Les cellules d'une liste, ligne par ligne : chaque objet est lu, un lien montre la clé de l'objet désigné,
 * un champ absent reste vide (§ 22.1). */
/* Un fichier à écrire à la fin de l'exécution réussie (§ 15.2) : jamais par-dessus un fichier existant, jamais deux
 * fois le même chemin. NULL si accepté (le fichier passe à l'écriture), sinon le message. */
static char *planifier_ecriture(Machine *m, const char *ecrit, Fichier *f) {
    char *probleme = NULL;
    if (!octets_du_fichier(f, &probleme)) return probleme;   /* lu maintenant : la base peut changer d'ici la fin */
    char *chemin = chemin_ecriture(m, ecrit);
    FILE *existe = grym_fopen(chemin, "rb");
    if (existe) {
        fclose(existe);
        probleme = grym_formater("« %s » existe déjà : il n'est jamais écrasé.", ecrit);
    }
    for (size_t k = 0; k < m->nb_a_ecrire && !probleme; k++)
        if (strcmp(m->a_ecrire[k].chemin, chemin) == 0)
            probleme = grym_formater("« %s » est déjà enregistré par cette exécution.", ecrit);
    if (probleme) { free(chemin); return probleme; }
    Ecriture_disque *t = grym_allouer((m->nb_a_ecrire + 1) * sizeof *t);
    if (m->nb_a_ecrire) memcpy(t, m->a_ecrire, m->nb_a_ecrire * sizeof *t);
    free(m->a_ecrire);
    m->a_ecrire = t;
    t[m->nb_a_ecrire].chemin = chemin;
    t[m->nb_a_ecrire].ecrit = grym_dupliquer(ecrit);
    t[m->nb_a_ecrire].fichier = f;
    m->nb_a_ecrire++;
    return NULL;
}

/* La valeur d'une cellule de tableau (§ 15.4) : un nombre en forme canonique à virgule, sans séparateur de milliers ;
 * une date jour.mois.année ; vrai, faux ; un lien par sa clé ; absent : vide. */
static char *cellule_csv(Machine *m, const Valeur *v) {
    if (v->type == V_NOMBRE) {
        char *x = dec_canonique(&v->nombre);
        for (char *p = x; *p; p++) if (*p == '.') *p = ',';
        return x;
    }
    if (v->type == V_BOOLEEN) return grym_dupliquer(v->vrai ? "vrai" : "faux");
    if (v->type == V_OBJET) return nom_objet(m, v->objet);
    if (v->type == V_FICHIER) return grym_dupliquer(v->fichier->nom ? v->fichier->nom : "");
    /* les marques invisibles de relecture d'un texte transcrit (U+2062 à U+2064, § 15.8) n'ont rien à faire dans un
     * tableur : retirées */
    char *t = texte_valeur(m, v), *w = t;
    for (const char *r = t; *r;) {
        if ((unsigned char)r[0] == 0xE2 && (unsigned char)r[1] == 0x81 && (unsigned char)r[2] >= 0xA2 && (unsigned char)r[2] <= 0xA4) r += 3;
        else *w++ = *r++;
    }
    *w = 0;
    return t;
}

/* RFC 4180 : entre guillemets si la cellule contient le séparateur, un guillemet, un retour à la ligne ou une espace
 * au bord ; le guillemet se double. */
static void ajouter_cellule(Chaine *c, const char *t) {
    size_t l = strlen(t);
    int citer = l && (t[0] == ' ' || t[l - 1] == ' ');
    for (const char *p = t; *p && !citer; p++) citer = *p == ';' || *p == '"' || *p == '\n' || *p == '\r';
    if (!citer) { chaine_ajouter(c, t); return; }
    chaine_ajouter(c, "\"");
    char un[2] = { 0, 0 };
    for (const char *p = t; *p; p++) {
        if (*p == '"') chaine_ajouter(c, "\"");
        un[0] = *p;
        chaine_ajouter(c, un);
    }
    chaine_ajouter(c, "\"");
}

/* Le tableau d'un export (§ 15.4) : « entité ␛ écrit ␝ a ␟ b ␞ … » ; sans colonnes, tous les champs simples de
 * l'entité. UTF-8 avec marque d'ordre (pour les tableurs), séparateur « ; », lignes CRLF. */
/* Le format d'un export, par l'extension du fichier (§ 15.4, § 15.6) : 0 CSV, 1 page HTML, 2 PDF. */
static int format_export(const char *nom) {
    const char *p = strrchr(nom, '.');
    if (!p) return 0;
    char x[8] = "";
    for (int i = 0; i < 7 && p[i + 1]; i++) x[i] = (char)(p[i + 1] >= 'A' && p[i + 1] <= 'Z' ? p[i + 1] + 32 : p[i + 1]);
    return !strcmp(x, "html") || !strcmp(x, "htm") ? 1 : !strcmp(x, "pdf") ? 2 : 0;
}

static void html_echapper(Chaine *c, const char *t) {
    for (; *t; t++) {
        if (*t == '&') chaine_ajouter(c, "&amp;");
        else if (*t == '<') chaine_ajouter(c, "&lt;");
        else if (*t == '>') chaine_ajouter(c, "&gt;");
        else if (*t == '"') chaine_ajouter(c, "&quot;");
        else { char u[2] = {*t, 0}; chaine_ajouter(c, u); }
    }
}

/* Un export (§ 15.4) : un tableau CSV, ou un rapport (§ 15.6), page HTML prête à imprimer ou PDF fabriqué par
 * l'interface. « entité ␛ colonnes [␚ titre] [␙ totaux] ». */
static char *tableau_csv(Machine *m, const char *descripteur, const Valeur *l, Fichier *f) {
    const char *esc = strchr(descripteur, '\x1b');
    if (!esc) return grym_dupliquer("Export invalide.");
    char *entite = grym_formater("%.*s", (int)(esc - descripteur), descripteur);
    const ClasseVM *e = classe_vm(m, entite);
    free(entite);
    if (!e) return grym_dupliquer("Export invalide : entité inconnue.");
    const int format = format_export(f->nom);
    const char *ptitre = strchr(esc, '\x1a'), *ptot = strchr(esc, '\x19');
    const size_t lcols = strcspn(esc + 1, "\x1a\x19");
    char *cols_desc = grym_formater("%.*s", (int)lcols, esc + 1);
    char *titre_rapport = ptitre ? grym_formater("%.*s", (int)strcspn(ptitre + 1, "\x19"), ptitre + 1) : NULL;
    char *totaux_desc = ptot ? grym_dupliquer(ptot + 1) : NULL;
    size_t nc = 0;
    char **titres = NULL, **chemins = NULL;
    if (*cols_desc) {
        for (const char *q = cols_desc; *q; ) {
            const char *fin = strchr(q, '\x1e');
            if (!fin) fin = q + strlen(q);
            const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
            titres = realloc(titres, (nc + 1) * sizeof *titres);
            chemins = realloc(chemins, (nc + 1) * sizeof *chemins);
            const char *t = q, *tf = sep ? sep : fin;
            /* le titre : tel qu'écrit, sans l'article du début (« le nom » → « Nom ») */
            if ((size_t)(tf - t) > 3 && (strncmp(t, "le ", 3) == 0 || strncmp(t, "la ", 3) == 0)) t += 3;
            else if ((size_t)(tf - t) > 4 && strncmp(t, "les ", 4) == 0) t += 4;
            else if ((size_t)(tf - t) > 2 && strncmp(t, "l'", 2) == 0) t += 2;
            char *brut = grym_formater("%.*s", (int)(tf - t), t);
            titres[nc] = capitale(brut);
            free(brut);
            chemins[nc] = sep ? grym_formater("%.*s", (int)(fin - sep - 1), sep + 1) : grym_dupliquer("");
            nc++;
            q = *fin ? fin + 1 : fin;
        }
    } else {
        for (size_t k = 0; k < e->nb_champs; k++) {
            if ((e->uniques[k] & 8) || !e->types[k] || type_fichier(e->types[k]))
                continue;
            titres = realloc(titres, (nc + 1) * sizeof *titres);
            chemins = realloc(chemins, (nc + 1) * sizeof *chemins);
            titres[nc] = capitale(e->champs[k]);
            chemins[nc] = grym_dupliquer(e->champs[k]);
            nc++;
        }
    }
    /* les colonnes totalisées (§ 15.6) : leur somme, sur une dernière ligne « Total » */
    int *totalise = grym_allouer((nc ? nc : 1) * sizeof *totalise);
    Decimal *sommes = grym_allouer((nc ? nc : 1) * sizeof *sommes);
    int numerique_vu = 0;
    for (size_t k = 0; k < nc; k++) {
        totalise[k] = 0;
        sommes[k] = dec_zero();
        for (const char *q = totaux_desc; q && *q && !totalise[k];) {
            const char *fin = strchr(q, '\x1e');
            if (!fin) fin = q + strlen(q);
            const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
            totalise[k] = sep && strlen(chemins[k]) == (size_t)(fin - sep - 1) && strncmp(sep + 1, chemins[k], strlen(chemins[k])) == 0;
            q = *fin ? fin + 1 : fin;
        }
    }
    Chaine c = {0};
    if (format == 0) {
        chaine_ajouter(&c, "\xEF\xBB\xBF");   /* marque d'ordre : les tableurs lisent alors l'UTF-8 */
        for (size_t k = 0; k < nc; k++) {
            if (k) chaine_ajouter(&c, ";");
            ajouter_cellule(&c, titres[k]);
        }
        chaine_ajouter(&c, "\r\n");
    } else {   /* une page A4 : l'en-tête du tableau se répète à chaque page, la date d'édition en pied */
        chaine_ajouter(&c, "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>");
        html_echapper(&c, titre_rapport ? titre_rapport : f->nom);
        chaine_ajouter(&c, "</title><style>@page{size:A4;margin:18mm 15mm}"
                           "body{font-family:Helvetica,Arial,sans-serif;font-size:10pt;color:#111}"
                           "h1{font-size:16pt;margin:0 0 12px}table{border-collapse:collapse;width:100%}"
                           "thead{display:table-header-group}th{text-align:left;border-bottom:1.5pt solid #333;padding:4px 6px}"
                           "td{border-bottom:0.5pt solid #bbb;padding:4px 6px}.n{text-align:right}"
                           "tr.total td{border-top:1.5pt solid #333;border-bottom:none;font-weight:bold}"
                           ".edite{margin-top:16px;font-size:8pt;color:#555}</style></head><body>\n");
        if (titre_rapport) { chaine_ajouter(&c, "<h1>"); html_echapper(&c, titre_rapport); chaine_ajouter(&c, "</h1>\n"); }
        chaine_ajouter(&c, "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><thead><tr>");
        for (size_t k = 0; k < nc; k++) {
            chaine_ajouter(&c, totalise[k] ? "<th class=\"n\" align=\"right\">" : "<th align=\"left\">");
            html_echapper(&c, titres[k]);
            chaine_ajouter(&c, "</th>");
        }
        chaine_ajouter(&c, "</tr></thead><tbody>\n");
    }
    char *erreur = NULL;
    for (size_t r = 0; r < l->liste->n && !erreur; r++) {
        Objet *o = machine_objet_en_base(m, l->liste->ids[r], l->liste->classes[r], &erreur);
        if (!o || (o->id && !charger(m, o, &erreur))) break;
        if (format) chaine_ajouter(&c, "<tr>");
        for (size_t k = 0; k < nc && !erreur; k++) {
            Objet *x = o;
            long q = -1;
            for (const char *p = chemins[k];;) {   /* « compositeur␟naissance » : suivre les liens (§ 22.1) */
                const char *fin = strchr(p, '\x1f');
                char *champ = grym_formater("%.*s", (int)(fin ? fin - p : (long)strlen(p)), p);
                q = x ? index_champ(x->classe, champ) : -1;
                free(champ);
                if (!fin || q < 0) break;
                if (!x->definis[q] || x->champs[q].type != V_OBJET) { x = NULL; break; }
                x = x->champs[q].objet;
                if (x->id && !charger(m, x, &erreur)) { x = NULL; break; }
                p = fin + 1;
            }
            const int present = x && q >= 0 && (size_t)q < x->classe->nb_champs && x->definis[q] && x->champs[q].type != V_ABSENT;
            const Valeur *v = present ? &x->champs[q] : NULL;
            if (v && totalise[k] && v->type == V_NOMBRE) {
                Decimal s2 = dec_zero();
                dec_addition(&sommes[k], &v->nombre, &s2);
                dec_liberer(&sommes[k]);
                sommes[k] = s2;
                numerique_vu = 1;
            }
            if (format == 0) {
                if (k) chaine_ajouter(&c, ";");
                if (v) {
                    char *t = type_cache(x->classe->types[q]) ? grym_dupliquer("") : cellule_csv(m, v);
                    ajouter_cellule(&c, t);
                    free(t);
                }
            } else {
                chaine_ajouter(&c, v && v->type == V_NOMBRE ? "<td class=\"n\" align=\"right\">" : "<td>");
                if (v) {
                    char *t = type_cache(x->classe->types[q]) ? grym_dupliquer("••••••")
                            : v->type == V_OBJET ? nom_objet(m, v->objet) : texte_valeur(m, v);
                    html_echapper(&c, t);
                    free(t);
                }
                chaine_ajouter(&c, "</td>");
            }
        }
        chaine_ajouter(&c, format ? "</tr>\n" : "\r\n");
    }
    (void)numerique_vu;
    if (!erreur && totaux_desc) {   /* la ligne des totaux : « Total » dans la première colonne qui n'en est pas un */
        int libelle = 0;
        if (format) chaine_ajouter(&c, "<tr class=\"total\">");
        for (size_t k = 0; k < nc; k++) {
            char *t = NULL;
            if (totalise[k]) {
                Valeur vs = valeur_nombre(dec_copier(&sommes[k]));
                t = format ? texte_valeur(m, &vs) : cellule_csv(m, &vs);
                valeur_liberer(&vs);
            } else if (!libelle) { t = grym_dupliquer("Total"); libelle = 1; }
            if (format == 0) {
                if (k) chaine_ajouter(&c, ";");
                if (t) ajouter_cellule(&c, t);
            } else {
                chaine_ajouter(&c, totalise[k] ? "<td class=\"n\" align=\"right\"><b>" : "<td><b>");
                if (t) html_echapper(&c, t);
                chaine_ajouter(&c, "</b></td>");
            }
            free(t);
        }
        chaine_ajouter(&c, format ? "</tr>\n" : "\r\n");
    }
    if (format) {
        char *jour = date_suisse(date_aujourdhui());
        chaine_ajouter(&c, "</tbody></table>\n<p class=\"edite\">Édité le ");
        chaine_ajouter(&c, jour);
        chaine_ajouter(&c, "</p>\n</body></html>\n");
        free(jour);
    }
    for (size_t k = 0; k < nc; k++) { free(titres[k]); free(chemins[k]); dec_liberer(&sommes[k]); }
    free(titres);
    free(chemins);
    free(totalise);
    free(sommes);
    free(cols_desc);
    free(titre_rapport);
    free(totaux_desc);
    char *t = chaine_rendre(&c);
    if (erreur) { free(t); return erreur; }
    if (format == 2) {   /* le PDF : la page, mise en page par l'interface (l'atelier, une application fabriquée) */
        if (!m->iface.rendre_pdf) {
            free(t);
            return grym_formater("Un PDF se fabrique dans l'atelier ou dans une application fabriquée ; ici, exporte en "
                                 "« .html » et imprime la page depuis le navigateur.");
        }
        size_t taille = 0;
        char *probleme = NULL;
        unsigned char *pdf = m->iface.rendre_pdf(m->iface.contexte, t, &taille, &probleme);
        free(t);
        if (!pdf) return probleme ? probleme : grym_dupliquer("Le PDF n'a pas pu être fabriqué.");
        f->taille = taille;
        f->octets = pdf;
        return NULL;
    }
    f->taille = strlen(t);
    f->octets = (unsigned char *)t;
    return NULL;
}

/* Un tableau CSV lu (§ 15.5) : « titres ␞ cellules ␞ … », cellules séparées par ␟. Marque d'ordre ignorée ;
 * séparateur deviné sur la ligne des titres (« ; », « , » ou tabulation, le plus fréquent hors guillemets) ;
 * guillemets RFC 4180 ; lignes vides ignorées. NULL et *probleme si le fichier n'est pas un tableau. */
static char *lire_csv(const Fichier *f, char **probleme) {
    const char *s = (const char *)f->octets, *fin = s + f->taille;
    if (f->taille >= 3 && memcmp(s, "\xEF\xBB\xBF", 3) == 0) s += 3;
    for (const char *p = s; p < fin; p++)
        if (*p == '\0') { *probleme = grym_formater("« %s » n'est pas un tableau : il contient des données binaires.", f->nom); return NULL; }
    char sep = ';';
    {
        size_t nb[3] = { 0, 0, 0 };
        int g = 0;
        for (const char *p = s; p < fin && (g || (*p != '\n' && *p != '\r')); p++) {
            if (*p == '"') g = !g;
            else if (!g) nb[0] += *p == ';', nb[1] += *p == ',', nb[2] += *p == '\t';
        }
        sep = nb[1] > nb[0] && nb[1] >= nb[2] ? ',' : nb[2] > nb[0] && nb[2] > nb[1] ? '\t' : ';';
    }
    Chaine c = {0};
    Chaine ligne = {0};
    int vide = 1, premiere = 1;
    size_t colonnes = 0, cellules = 0, numero = 1;
    char un[2] = { 0, 0 };
    const char *p = s;
    while (p <= fin) {
        int fin_ligne = p == fin || *p == '\n' || *p == '\r';
        if (fin_ligne) {
            if (!vide) {
                cellules++;
                if (premiere) colonnes = cellules;
                else if (cellules != colonnes) {
                    free(chaine_rendre(&c));
                    free(chaine_rendre(&ligne));
                    *probleme = grym_formater("« %s », ligne %lu : %lu cellule%s pour %lu colonne%s.", f->nom,
                                              (unsigned long)numero, (unsigned long)cellules, cellules > 1 ? "s" : "",
                                              (unsigned long)colonnes, colonnes > 1 ? "s" : "");
                    return NULL;
                }
                char *l = chaine_rendre(&ligne);
                if (!premiere) chaine_ajouter(&c, "\x1e");
                chaine_ajouter(&c, l);
                free(l);
                premiere = 0;
            } else free(chaine_rendre(&ligne));
            ligne.d = NULL; ligne.n = ligne.cap = 0;
            vide = 1;
            cellules = 0;
            if (p < fin && *p == '\r' && p + 1 < fin && p[1] == '\n') p++;
            if (p < fin && (*p == '\n' || *p == '\r')) numero++;
            p++;
            continue;
        }
        vide = 0;
        if (*p == '"') {   /* cellule entre guillemets : « "" » vaut un guillemet */
            p++;
            while (p < fin && !(*p == '"' && (p + 1 >= fin || p[1] != '"'))) {
                if (*p == '"') p++;
                if (*p == '\n') numero++;
                un[0] = (*p == '\x1e' || *p == '\x1f') ? ' ' : *p;
                chaine_ajouter(&ligne, un);
                p++;
            }
            if (p >= fin) {
                free(chaine_rendre(&c));
                free(chaine_rendre(&ligne));
                *probleme = grym_formater("« %s » : un guillemet n'est pas refermé.", f->nom);
                return NULL;
            }
            p++;
            continue;
        }
        if (*p == sep) { chaine_ajouter(&ligne, "\x1f"); cellules++; p++; continue; }
        un[0] = (*p == '\x1e' || *p == '\x1f') ? ' ' : *p;
        chaine_ajouter(&ligne, un);
        p++;
    }
    if (premiere) {
        free(chaine_rendre(&c));
        *probleme = grym_formater("« %s » est vide : un tableau commence par la ligne des titres.", f->nom);
        return NULL;
    }
    return chaine_rendre(&c);
}

/* Ensembles (§ 13.9) : partagés et comptés ; une modification copie d'abord un ensemble partagé. */
static int valeurs_identiques(const Valeur *a, const Valeur *b) {
    if (a->type != b->type) return 0;
    switch (a->type) {
    case V_NOMBRE: return dec_comparer(&a->nombre, &b->nombre) == 0;
    case V_TEXTE: return strcmp(a->texte ? a->texte : "", b->texte ? b->texte : "") == 0;
    case V_BOOLEEN: return a->vrai == b->vrai;
    case V_DATE: case V_ANNEE: case V_HEURE: return a->jours == b->jours;
    case V_OBJET: return a->objet == b->objet;
    case V_FICHIER: return a->fichier == b->fichier;
    case V_ENSEMBLE: {
        if (a->liste->n != b->liste->n) return 0;
        for (size_t k = 0; k < a->liste->n; k++) {
            int trouve = 0;
            for (size_t q = 0; q < b->liste->n && !trouve; q++) trouve = valeurs_identiques(&a->liste->valeurs[k], &b->liste->valeurs[q]);
            if (!trouve) return 0;
        }
        return 1;
    }
    default: return 0;
    }
}

static Valeur ensemble_vide(void) {
    Liste *l = grym_allouer(sizeof *l);
    l->references = 1;
    l->ids = NULL;
    l->classes = NULL;
    l->n = 0;
    l->cap = 4;
    l->valeurs = grym_allouer(l->cap * sizeof *l->valeurs);
    Valeur v = valeur_nombre(dec_zero());
    v.type = V_ENSEMBLE;
    v.liste = l;
    return v;
}

static long ensemble_rang(const Valeur *e, const Valeur *x) {
    for (size_t k = 0; k < e->liste->n; k++) if (valeurs_identiques(&e->liste->valeurs[k], x)) return (long)k;
    return -1;
}

/* Rend un ensemble que l'appelant peut modifier : le même s'il est seul à le tenir, sinon une copie. */
static void ensemble_seul(Valeur *e) {
    if (e->liste->references == 1) return;
    Valeur c = ensemble_vide();
    for (size_t k = 0; k < e->liste->n; k++) {
        if (c.liste->n == c.liste->cap) {
            c.liste->cap *= 2;
            Valeur *t = grym_allouer(c.liste->cap * sizeof *t);
            memcpy(t, c.liste->valeurs, c.liste->n * sizeof *t);
            free(c.liste->valeurs);
            c.liste->valeurs = t;
        }
        c.liste->valeurs[c.liste->n++] = valeur_copier(&e->liste->valeurs[k]);
    }
    valeur_liberer(e);
    *e = c;
}

/* Ajoute x (dont il prend possession), sauf s'il y est déjà. */
static void ensemble_gagner(Valeur *e, Valeur x) {
    if (ensemble_rang(e, &x) >= 0) { valeur_liberer(&x); return; }
    ensemble_seul(e);
    Liste *l = e->liste;
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        Valeur *t = grym_allouer(l->cap * sizeof *t);
        if (l->n) memcpy(t, l->valeurs, l->n * sizeof *t);
        free(l->valeurs);
        l->valeurs = t;
    }
    l->valeurs[l->n++] = x;
}

/* Le jour de la semaine d'une date : 0 lundi … 6 dimanche ; le 1er janvier 2024 était un lundi (§ 14.8). */
static const char *const JOURS_SEMAINE[] = { "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche" };
static int jour_semaine(long jours) {
    const long r = (jours - date_jours(2024, 1, 1)) % 7;
    return (int)(r < 0 ? r + 7 : r);
}

/* Le morceau d'une description de zone qui suit le séparateur sep, jusqu'au séparateur suivant ; NULL s'il n'y est pas. */
static char *morceau_de_zone(const char *s2, char sep) {
    const char *p = s2 ? strchr(s2, sep) : NULL;
    if (!p) return NULL;
    p++;
    return grym_formater("%.*s", (int)strcspn(p, "\x1c\x1a\x1d"), p);
}

/* La saisie d'une zone qui a une unité, l'unité ôtée si on l'a tapée (« 12 CHF », « CHF 12 ») ; à libérer. */
static char *sans_unite(const char *t, const char *unite) {
    size_t d = 0, f = strlen(t), u = unite ? strlen(unite) : 0;
    while (d < f && t[d] == ' ') d++;
    while (f > d && t[f - 1] == ' ') f--;
    if (u && f - d >= u && strncmp(t + f - u, unite, u) == 0) f -= u;
    else if (u && f - d >= u && strncmp(t + d, unite, u) == 0) d += u;
    while (d < f && t[d] == ' ') d++;
    while (f > d && t[f - 1] == ' ') f--;
    return grym_formater("%.*s", (int)(f - d), t + d);
}

/* Une valeur hors des bornes d'une zone : le message qui dit la règle ; NULL si elle les respecte (§ 22.6). Un texte
 * se mesure en caractères, un nombre se compare. */
static char *ecrire_borne(const char *canon) {
    const int neg = canon[0] == '-';
    Decimal d = dec_depuis_canonique(neg ? canon + 1 : canon);
    char *s = dec_formater(&d);
    dec_liberer(&d);
    if (!neg) return s;
    char *t = grym_formater("−%s", s);
    free(s);
    return t;
}

static char *hors_bornes(const ElementEcran *el, const Valeur *v) {
    if (!el->borne_min && !el->borne_max) return NULL;
    int dessous = 0, dessus = 0;
    if (v->type == V_TEXTE) {
        size_t n = 0;
        for (const unsigned char *p = (const unsigned char *)v->texte; *p; p++) n += (*p & 0xC0) != 0x80;
        if (el->borne_min && n < (size_t)atol(el->borne_min)) dessous = 1;
        if (el->borne_max && n > (size_t)atol(el->borne_max)) dessus = 1;
    } else if (v->type == V_NOMBRE) {
        for (int i = 0; i < 2; i++) {
            const char *b = i ? el->borne_max : el->borne_min;
            if (!b) continue;
            Decimal d = b[0] == '-' ? dec_depuis_canonique(b + 1) : dec_depuis_canonique(b);
            if (b[0] == '-') { Decimal z = dec_zero(), r = dec_zero(); dec_soustraction(&z, &d, &r); dec_liberer(&d); dec_liberer(&z); d = r; }
            const int c = dec_comparer(&v->nombre, &d);
            if (!i && c < 0) dessous = 1;
            if (i && c > 0) dessus = 1;
            dec_liberer(&d);
        }
    } else return NULL;
    if (!dessous && !dessus) return NULL;
    const int car = v->type == V_TEXTE;
    char *a = el->borne_min ? ecrire_borne(el->borne_min) : NULL, *b = el->borne_max ? ecrire_borne(el->borne_max) : NULL, *r;
    if (a && b && car && strcmp(a, b) == 0) r = grym_formater("« %s » a %s caractères.", el->texte, a);
    else if (a && b) r = grym_formater(car ? "« %s » a de %s à %s caractères." : "« %s » va de %s à %s.", el->texte, a, b);
    else if (a) r = grym_formater(car ? "« %s » a au moins %s caractères." : "« %s » vaut au moins %s.", el->texte, a);
    else r = grym_formater(car ? "« %s » a au plus %s caractères." : "« %s » vaut au plus %s.", el->texte, b);
    free(a);
    free(b);
    return r;
}

static char *ecran_montrer_liste(Machine *m, EcranVM *e, size_t k, Valeur *l) {
    char *erreur = NULL;
    const size_t nc = e->nb_colonnes[k], nl = l->liste->n;
    char **cellules = grym_allouer((nc * nl > 0 ? nc * nl : 1) * sizeof *cellules);
    const int graphe = e->elements[k].graphique;
    Decimal *valeurs = graphe ? grym_allouer((nl ? nl : 1) * sizeof *valeurs) : NULL;   /* la valeur additionnée de chaque ligne */
    for (size_t r = 0; graphe && r < nl; r++) valeurs[r] = dec_zero();
    size_t faites = 0;
    for (size_t r = 0; r < nl && !erreur; r++) {
        Objet *o = machine_objet_en_base(m, l->liste->ids[r], l->liste->classes[r], &erreur);
        if (!o || (o->id && !charger(m, o, &erreur))) break;
        for (size_t c = 0; c < nc; c++) {
            Objet *x = o;
            long q = e->chemins[k] ? -1 : (long)e->champs[k][c];
            if (e->chemins[k]) {   /* suivre le chemin : « compositeur␜naissance » (§ 22.1) */
                const char *p = e->chemins[k][c];
                for (;;) {
                    const char *fin = strchr(p, '\x1c');
                    char *champ = grym_formater("%.*s", (int)(fin ? fin - p : (long)strlen(p)), p);
                    q = x ? index_champ(x->classe, champ) : -1;
                    free(champ);
                    if (!fin || q < 0) break;
                    if (!x->definis[q] || x->champs[q].type != V_OBJET) { x = NULL; break; }   /* lien absent : vide */
                    x = x->champs[q].objet;
                    if (x->id && !charger(m, x, &erreur)) { x = NULL; break; }
                    p = fin + 1;
                }
            }
            char *t;
            if ((graphe & 4) && c == 1 && x && q >= 0 && (size_t)q < x->classe->nb_champs && x->definis[q]) {
                if (x->champs[q].type == V_NOMBRE) { dec_liberer(&valeurs[r]); valeurs[r] = dec_copier(&x->champs[q].nombre); }
                else if (x->champs[q].type != V_ABSENT)
                    erreur = grym_dupliquer("« avec la somme de » additionne un champ nombre (§ 22.11).");
            }
            if (!x || q < 0 || (size_t)q >= x->classe->nb_champs || !x->definis[q] || x->champs[q].type == V_ABSENT)
                t = grym_dupliquer("");
            else if (x->champs[q].type == V_OBJET) t = nom_objet(m, x->champs[q].objet);
            else if (type_cache(x->classe->types[q])) t = grym_dupliquer("••••••");   /* § 16.1 */
            else t = texte_valeur(m, &x->champs[q]);
            if ((graphe & 56) && faites % nc == 0 && x && q >= 0 && x->definis[q] && x->champs[q].type == V_DATE) {
                /* la clé d'un graphique par période : triable dans l'ordre du calendrier, lisible plus tard (§ 22.11) */
                int an, mo, jo;
                date_civile(x->champs[q].jours, &an, &mo, &jo);
                free(t);
                if (graphe & 8) t = grym_formater("%04d-%02d", an, mo);
                else if (graphe & 32) t = grym_formater("%04d", an);
                else {   /* semaine ISO 8601 : celle de son jeudi */
                    const long jeudi = x->champs[q].jours - jour_semaine(x->champs[q].jours) + 3;
                    int aj, mj, jj;
                    date_civile(jeudi, &aj, &mj, &jj);
                    t = grym_formater("%04d-%02ld", aj, (jeudi - date_jours(aj, 1, 1)) / 7 + 1);
                }
            }
            if (!graphe && x && q >= 0) {
                /* une valeur permise colorée : « ␝v payée », une étiquette verte dans l'interface (§ 16.15) */
                const char lettre = lettre_etiquette(x->classe, (size_t)q, t);
                if (lettre) {
                    char *e2 = grym_formater("\x1d%c%s", lettre, t);
                    free(t);
                    t = e2;
                }
            }
            cellules[faites++] = t;
        }
    }
    if (!erreur && graphe) {
        /* un graphique : une ligne par valeur de la clé, dans l'ordre où elle apparaît ; « — » pour une clé vide ; le
         * nombre de lignes, ou la somme de leurs valeurs (§ 22.11) */
        char **cles = grym_allouer((nl ? nl : 1) * sizeof *cles);
        Decimal *totaux = grym_allouer((nl ? nl : 1) * sizeof *totaux);
        size_t ng = 0;
        for (size_t r = 0; r < faites / (nc ? nc : 1); r++) {
            const char *cle = *cellules[r * nc] ? cellules[r * nc] : "—";
            size_t g = 0;
            while (g < ng && strcmp(cles[g], cle) != 0) g++;
            if (g == ng) { cles[ng] = grym_dupliquer(cle); totaux[ng] = dec_zero(); ng++; }
            Decimal un = graphe & 4 ? dec_copier(&valeurs[r]) : dec_depuis_canonique("1"), s = dec_zero();
            dec_addition(&totaux[g], &un, &s);
            dec_liberer(&totaux[g]);
            dec_liberer(&un);
            totaux[g] = s;
        }
        if (graphe & 56) {   /* l'ordre du calendrier, les dates absentes à la fin ; puis « octobre 2026 » (§ 22.11) */
            for (size_t i = 1; i < ng; i++)
                for (size_t j = i; j > 0; j--) {
                    const int a0 = strcmp(cles[j - 1], "—") == 0, b0 = strcmp(cles[j], "—") == 0;
                    if (!(a0 && !b0) && (a0 || b0 || strcmp(cles[j - 1], cles[j]) <= 0)) break;
                    char *tc = cles[j]; cles[j] = cles[j - 1]; cles[j - 1] = tc;
                    Decimal td = totaux[j]; totaux[j] = totaux[j - 1]; totaux[j - 1] = td;
                }
            static const char *const MOIS[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
                                                "septembre", "octobre", "novembre", "décembre" };
            for (size_t g = 0; g < ng; g++) {
                int an = 0, n = 0;
                if (strcmp(cles[g], "—") == 0 || sscanf(cles[g], "%d-%d", &an, &n) < 1) continue;
                char *lisible = (graphe & 8) && n >= 1 && n <= 12 ? grym_formater("%s %d", MOIS[n - 1], an)
                              : (graphe & 16) ? grym_formater("semaine %d, %d", n, an) : grym_formater("%d", an);
                free(cles[g]);
                cles[g] = lisible;
            }
        }
        char **lignes = grym_allouer((ng ? 2 * ng : 1) * sizeof *lignes);
        for (size_t g = 0; g < ng; g++) {
            Valeur v = valeur_nombre(totaux[g]);
            lignes[2 * g] = cles[g];
            lignes[2 * g + 1] = texte_valeur(m, &v);
            valeur_liberer(&v);
        }
        m->iface.ecran_lignes(m->iface.contexte, k, (const char *const *)lignes, ng);
        for (size_t g = 0; g < 2 * ng; g++) free(lignes[g]);
        free(lignes);
        free(cles);
        free(totaux);
        valeur_liberer(&e->listes[k]);
        e->listes[k] = valeur_copier(l);
    } else if (!erreur && e->document) {   /* un document (2.25) : les cellules restent, pour la page */
        for (size_t x = 0; e->doc_cellules[k] && x < e->doc_lignes[k] * nc; x++) free(e->doc_cellules[k][x]);
        free(e->doc_cellules[k]);
        e->doc_cellules[k] = grym_allouer((faites ? faites : 1) * sizeof **e->doc_cellules);
        for (size_t x = 0; x < faites; x++) {   /* une étiquette colorée (« ␝v faite ») devient son texte */
            const char *t = cellules[x];
            e->doc_cellules[k][x] = grym_dupliquer(t[0] == '\x1d' && t[1] ? t + 2 : t);
        }
        e->doc_lignes[k] = nc ? faites / nc : 0;
    } else if (!erreur) {
        /* seulement les lignes remplies : un objet qui ne se charge plus (une ligne coupée) laissait des cellules au hasard */
        m->iface.ecran_lignes(m->iface.contexte, k, (const char *const *)cellules, nc ? faites / nc : 0);
        valeur_liberer(&e->listes[k]);
        e->listes[k] = valeur_copier(l);
    }
    for (size_t r = 0; graphe && r < nl; r++) dec_liberer(&valeurs[r]);
    free(valeurs);
    for (size_t x = 0; x < faites; x++) free(cellules[x]);
    free(cellules);
    return erreur;
}

/* Les tableaux d'une fiche (grammaire, § 22.3) : pour chaque lien d'une entité qui peut désigner l'objet de la fiche,
 * les objets qui le désignent, en liste, sous ses champs. Une entité « hors des fiches » n'en a pas. */
typedef struct {
    const ClasseVM *entite;
    size_t lien;          /* le champ qui désigne l'objet de la fiche */
    size_t element;       /* rang de la liste parmi les éléments de la fiche */
    size_t bouton;        /* rang du bouton « Nouvelle … » */
    size_t *champs;       /* les colonnes : des champs de l'entité */
    char **titres;
    size_t nb_colonnes;
    size_t nb_objets;     /* lignes d'objets ; la ligne « Total », s'il y en a une, les suit */
    Valeur objets;        /* les objets montrés, une liste ; absent avant la première lecture */
    char *titre;          /* « Séances », « Séances (client) » */
    char *nouveau;        /* « Nouvelle séance » */
} TableauFiche;

/* Vrai si un champ de ce type désigne un objet de la classe c (la sienne, ou celle d'un ancêtre). */
static int lien_vers(const Machine *m, const char *type, const ClasseVM *c) {
    const ClasseVM *cible = type ? classe_vm(m, type) : NULL;
    for (; cible && c; c = c->parent)
        if (c == cible) return 1;
    return 0;
}

/* « séance » → « séances », « ligne de commande » → « lignes de commande » : le pluriel déclaré, sinon un s au premier
 * mot, sauf s'il finit par s, x ou z (même règle que « la somme des débits », § 16.4). À libérer. */
static char *pluriel_classe(const ClasseVM *c) {
    if (c->pluriel) return grym_dupliquer(c->pluriel);
    const size_t l = strcspn(c->nom, " ");
    const char der = l ? c->nom[l - 1] : 0;
    if (der == 's' || der == 'x' || der == 'z') return grym_dupliquer(c->nom);
    return grym_formater("%.*ss%s", (int)l, c->nom, c->nom + l);
}

/* « Nouvelle séance », « Nouveau client », « Nouvel achat » : le libellé du bouton qui crée un objet. À libérer. */
static char *libelle_nouveau(const ClasseVM *c) {
    const unsigned char *u = (const unsigned char *)c->nom;
    int voyelle = strchr("aeiouyh", u[0]) != NULL
                  || (u[0] == 0xC3 && (u[1] == 0xA0 || u[1] == 0xA2 || (u[1] >= 0xA8 && u[1] <= 0xAA) || u[1] == 0xAE
                                       || u[1] == 0xB4 || u[1] == 0xBB))   /* à â è é ê î ô û */
                  || (u[0] == 0xC5 && u[1] == 0x93);                       /* œ */
    return grym_formater("%s %s", c->feminin ? "Nouvelle" : voyelle ? "Nouvel" : "Nouveau", c->nom);
}

/* Les tableaux de la fiche de o : un par lien qui peut le désigner, dans l'ordre des déclarations. */
static TableauFiche *tableaux_fiche(Machine *m, const Objet *o, size_t *nb) {
    *nb = 0;
    if (!o->id || !m->base) return NULL;
    TableauFiche *t = NULL;
    size_t cap = 0;
    for (size_t i = 0; i < m->nb_classes; i++) {
        const ClasseVM *c = m->classes[i];
        if (!c->conserve || c->aptitude || c->hors_fiches || classe_vm(m, c->nom) != c) continue;
        size_t herites = c->parent ? c->parent->nb_champs : 0;   /* un lien hérité : le tableau du parent les montre */
        size_t liens = 0;
        for (size_t q = herites; q < c->nb_champs; q++)
            if (!(c->uniques[q] & 8) && lien_vers(m, c->types[q], o->classe)) liens++;
        for (size_t q = herites; q < c->nb_champs; q++) {
            if ((c->uniques[q] & 8) || !lien_vers(m, c->types[q], o->classe)) continue;
            if (*nb == cap) {
                cap = cap ? cap * 2 : 4;
                TableauFiche *x = grym_allouer(cap * sizeof *x);
                if (*nb) memcpy(x, t, *nb * sizeof *x);
                free(t);
                t = x;
            }
            TableauFiche *f = &t[(*nb)++];
            memset(f, 0, sizeof *f);
            f->entite = c;
            f->lien = q;
            f->objets = vi_absent(NULL);
            f->champs = grym_allouer((c->nb_champs ? c->nb_champs : 1) * sizeof *f->champs);
            f->titres = grym_allouer((c->nb_champs ? c->nb_champs : 1) * sizeof *f->titres);
            for (size_t r = 0; r < c->nb_champs; r++) {
                const char *ty = c->types[r];
                if (r == q || (c->uniques[r] & 8) || !ty || type_fichier(ty) || type_cache(ty)) continue;
                f->champs[f->nb_colonnes] = r;
                f->titres[f->nb_colonnes++] = capitale(c->champs[r]);
            }
            char *pl = pluriel_classe(c), *grand = capitale(pl), *nv = libelle_nouveau(c);
            f->titre = liens > 1 ? grym_formater("%s (%s)", grand, c->champs[q]) : grym_dupliquer(grand);
            f->nouveau = liens > 1 ? grym_formater("%s (%s)", nv, c->champs[q]) : grym_dupliquer(nv);
            free(pl);
            free(grand);
            free(nv);
        }
    }
    return t;
}

static void tableaux_liberer(TableauFiche *t, size_t nb) {
    for (size_t k = 0; k < nb; k++) {
        for (size_t c = 0; c < t[k].nb_colonnes; c++) free(t[k].titres[c]);
        free(t[k].titres);
        free(t[k].champs);
        free(t[k].titre);
        free(t[k].nouveau);
        valeur_liberer(&t[k].objets);
    }
    free(t);
}

/* Relit les objets d'un tableau et les montre : tri par la première date, la plus récente en tête, sinon par la clé ;
 * une ligne « Total » additionne chaque colonne (nombre), s'il y en a une. NULL, ou l'erreur (à libérer). */
static char *tableau_montrer(Machine *m, Objet *o, TableauFiche *t) {
    const ClasseVM *c = t->entite;
    const char *tri = NULL;
    for (size_t k = 0; k < t->nb_colonnes && !tri; k++)
        if (strcmp(c->types[t->champs[k]], "date") == 0) tri = c->champs[t->champs[k]];
    const int decroissant = tri != NULL;
    if (!tri) tri = champ_cle(c);
    char *d = grym_formater("%s\x1f" "0\x1f%s\x1f%c\x1f(=[%s]?1)", c->nom, tri ? tri : "", decroissant ? '1' : '0',
                            c->champs[t->lien]);
    Valeur soi = vi_objet(o), liste;
    char *erreur = NULL;
    int ok = base_chercher(m->base, m, d, &soi, 1, &liste, &erreur);
    valeur_liberer(&soi);
    free(d);
    if (!ok) return erreur;
    valeur_liberer(&t->objets);
    t->objets = liste;
    const size_t nc = t->nb_colonnes, nl = liste.type == V_LISTE ? liste.liste->n : 0;
    int total = 0;
    for (size_t k = 0; k < nc; k++) total |= strcmp(c->types[t->champs[k]], "nombre") == 0;
    total = total && nl > 0 && nc > 0;
    Decimal *sommes = grym_allouer((nc ? nc : 1) * sizeof *sommes);
    for (size_t k = 0; k < nc; k++) sommes[k] = dec_zero();
    char **cellules = grym_allouer(((nl + 1) * nc != 0 ? (nl + 1) * nc : 1) * sizeof *cellules);
    size_t faites = 0, montres = 0;
    for (size_t r = 0; r < nl && !erreur; r++) {
        Objet *x = machine_objet_en_base(m, liste.liste->ids[r], liste.liste->classes[r], &erreur);
        if (!x || (x->id && !charger(m, x, &erreur))) break;
        for (size_t k = 0; k < nc; k++) {
            const size_t q = t->champs[k];
            const Valeur *v = q < x->classe->nb_champs && x->definis[q] ? &x->champs[q] : NULL;
            char *s;
            if (!v || v->type == V_ABSENT) s = grym_dupliquer("");
            else if (v->type == V_OBJET) s = nom_objet(m, v->objet);
            else s = texte_valeur(m, v);
            if (v && v->type == V_NOMBRE) {
                Decimal somme = dec_zero();
                dec_addition(&sommes[k], &v->nombre, &somme);
                dec_liberer(&sommes[k]);
                sommes[k] = somme;
            }
            const char lettre = v && v->type == V_TEXTE ? lettre_etiquette(x->classe, q, s) : 0;
            if (lettre) {   /* une valeur permise colorée, comme dans une liste d'écran (§ 16.15) */
                char *e2 = grym_formater("\x1d%c%s", lettre, s);
                free(s);
                s = e2;
            }
            cellules[faites++] = s;
        }
        montres++;
    }
    t->nb_objets = montres;
    if (!erreur && total && montres) {
        for (size_t k = 0; k < nc; k++) {
            if (strcmp(c->types[t->champs[k]], "nombre") == 0) {
                Valeur v = valeur_nombre(dec_copier(&sommes[k]));
                cellules[faites++] = texte_valeur(m, &v);
                valeur_liberer(&v);
            } else cellules[faites++] = grym_dupliquer(k == 0 ? "Total" : "");
        }
        char *marquee = grym_formater("\x1c%s", cellules[faites - nc]);   /* « ␜ » : une ligne de total (interface.h) */
        free(cellules[faites - nc]);
        cellules[faites - nc] = marquee;
    }
    if (!erreur && m->iface.ecran_lignes)
        m->iface.ecran_lignes(m->iface.contexte, t->element, (const char *const *)cellules, faites / (nc ? nc : 1));
    for (size_t x = 0; x < faites; x++) free(cellules[x]);
    free(cellules);
    for (size_t k = 0; k < nc; k++) dec_liberer(&sommes[k]);
    free(sommes);
    return erreur;
}

/* « Nouvelle séance » depuis la fiche de o : le formulaire de l'entité, son lien vers o déjà rempli, puis l'objet
 * conservé. NULL, ou ce qui l'a empêché (« Saisie annulée », une valeur refusée), à libérer. */
static char *tableau_nouveau(Machine *m, Objet *o, const TableauFiche *t, Chaine *sortie, Cadre *cadres, int *fatal) {
    Objet *x = creer_objet(m, t->entite, m->epoque);
    x->champs[t->lien] = vi_objet(o);
    x->definis[t->lien] = 1;
    char *r = saisir(m, x, t->entite->champs[t->lien], sortie, cadres, fatal, 0);
    if (r) return r;
    long id = 0;
    if (base_conserver(m->base, x, &id, &r)) ecrire_id(m, x, id);
    return r;
}

/* La fiche d'un objet, en écran (grammaire, § 22.3) : ses champs, un bouton par lien vers la fiche de l'objet
 * désigné (empilée par-dessus), un tableau par lien d'une entité qui le désigne (ses objets, « Nouvelle … »),
 * « Modifier » (le formulaire de « Saisir à nouveau »), « Fermer ».
 * Chaque attente valide ce qui précède : chaque geste est une transaction, comme un événement. */
/* Les règles du programme (§ 16.14), vérifiées par la machine elle-même, là où elle valide une saisie hors du code
 * compilé : « Modifier » et « Nouvelle … » d'une fiche déduite. NULL si elles tiennent (ou s'il n'y en a pas),
 * sinon le motif. L'action NOM_REGLES s'exécute dans l'exécution en cours, sans rien valider ni annuler. */
static char *verifier_regles_machine(Machine *m, Chaine *sortie) {
    if (!formule_de(m, NOM_REGLES, NULL)) return NULL;
    Module *mod = module_creer();
    Bloc *p = bloc_creer();
    long k = bloc_nom(p, NOM_REGLES);
    bloc_emettre_appel(p, (uint16_t)k, 0, 0, 0, 0);
    bloc_emettre(p, I_RETOUR, 0, 0, 0);
    module_ajouter(mod, p);
    Diagnostic d = { NULL, 0, 0, NULL, 0, 0 };
    m->imbrique++;
    const int ok = machine_executer(m, mod, sortie, &d);
    m->imbrique--;
    module_detruire(mod);
    free(d.fichier);
    if (ok) { free(d.message); return NULL; }
    return d.message ? d.message : grym_dupliquer("Une règle n'est pas vérifiée.");
}

static char *fiche_ecran(Machine *m, Objet *o, Chaine *sortie, Cadre *cadres, int *fatal, int profondeur) {
    if (profondeur > 50) return grym_dupliquer("Trop de fiches empilées : plus de 50.");
    /* Les éléments montrés restent valides tant que la fiche est ouverte (src/interface.h) : ils ne se
       refont qu'à la relecture, après « Modifier ». */
    ElementEcran *el = NULL;
    char **textes = NULL;
    Objet **liens = NULL;
    TableauFiche *tableaux = NULL;
    size_t n = 0, modifier = 0, fermer = 0, nb_tableaux = 0;
    int relire = 1, ouverte = 0;
    char *resultat = NULL, *a_dire = NULL;
    for (;;) {
        if (relire) {
            for (size_t k = 0; k < n; k++) free(textes[k]);
            free(textes);
            free(el);
            free(liens);
            tableaux_liberer(tableaux, nb_tableaux);
            n = 0;
            char *erreur = NULL;
            tableaux = tableaux_fiche(m, o, &nb_tableaux);
            if (o->id && !charger(m, o, &erreur)) { resultat = erreur; textes = NULL; el = NULL; liens = NULL; break; }
            const ClasseVM *c = o->classe;
            /* les champs « plusieurs » (§ 16.13) : leurs éléments, lus d'abord, pour dimensionner la fiche */
            Valeur *plusieurs = grym_allouer((c->nb_champs ? c->nb_champs : 1) * sizeof *plusieurs);
            for (size_t q = 0; q < c->nb_champs; q++) plusieurs[q] = vi_absent(NULL);   /* toutes, avant tout échec */
            size_t en_plus = 0;
            for (size_t q = 0; q < c->nb_champs && !erreur; q++) {
                if (!(c->uniques[q] & 8) || !o->id || !m->base) continue;
                char *d = grym_formater("%s\x1f" "0\x1f\x1f" "0\x1f(M?1[%s])", c->types[q], c->champs[q]);
                Valeur soi = vi_objet(o), liste;
                if (base_chercher(m->base, m, d, &soi, 1, &liste, &erreur)) {   /* la recherche écrit une valeur neuve */
                    valeur_liberer(&plusieurs[q]);
                    plusieurs[q] = liste;
                    if (liste.type == V_LISTE) en_plus += liste.liste->n + 2;
                }
                valeur_liberer(&soi);   /* le décimal de la valeur ; l'objet reste au ramasse-miettes */
                free(d);
            }
            if (erreur) {
                for (size_t q = 0; q < c->nb_champs; q++) valeur_liberer(&plusieurs[q]);
                free(plusieurs);
                resultat = erreur; textes = NULL; el = NULL; liens = NULL;
                break;
            }
            size_t cap = c->nb_champs + 3 + en_plus + 3 * nb_tableaux;
            el = grym_allouer(cap * sizeof *el);
            textes = grym_allouer(cap * sizeof *textes);
            liens = grym_allouer(cap * sizeof *liens);
            memset(el, 0, cap * sizeof *el);
            for (size_t q = 0; q < c->nb_champs; q++) {
                if (c->uniques[q] & 8) {   /* « plusieurs » : le libellé, puis un bouton par élément, vers sa fiche */
                    char *libelle = capitale(c->champs[q]);
                    const Liste *l = plusieurs[q].type == V_LISTE ? plusieurs[q].liste : NULL;
                    liens[n] = NULL;
                    el[n].sorte = ELEMENT_TEXTE;
                    textes[n] = l && l->n ? grym_formater("%s :", libelle) : grym_formater("%s : aucun", libelle);
                    el[n].texte = textes[n];
                    n++;
                    free(libelle);
                    if (!l || !l->n) continue;
                    liens[n] = NULL;
                    textes[n] = NULL;
                    el[n++].sorte = ELEMENT_COTE_A_COTE;
                    for (size_t i = 0; i < l->n; i++) {
                        char *err = NULL;
                        Objet *x = machine_objet_en_base(m, l->ids[i], l->classes[i], &err);
                        free(err);
                        if (!x) continue;
                        textes[n] = nom_objet(m, x);
                        el[n].sorte = ELEMENT_BOUTON;
                        el[n].texte = textes[n];
                        liens[n] = x;
                        n++;
                    }
                    liens[n] = NULL;
                    textes[n] = NULL;
                    el[n++].sorte = ELEMENT_FIN_DE_BLOC;
                    continue;
                }
                char *libelle = capitale(c->champs[q]);
                const Valeur *v = &o->champs[q];
                liens[n] = NULL;
                if (o->definis[q] && v->type == V_OBJET) {   /* un lien : un bouton vers sa fiche */
                    char *cle = nom_objet(m, v->objet);
                    textes[n] = grym_formater("%s : %s", libelle, cle);
                    free(cle);
                    el[n].sorte = ELEMENT_BOUTON;
                    liens[n] = v->objet;
                } else {
                    char *t = !o->definis[q] || v->type == V_ABSENT ? grym_dupliquer("") : texte_valeur(m, v);
                    textes[n] = grym_formater("%s : %s", libelle, t);
                    el[n].sorte = ELEMENT_TEXTE;
                    if (*t) {   /* la valeur en étiquette (§ 16.15), le courriel à écrire, l'adresse à ouvrir (§ 22.13) */
                        el[n].etiquette = lettre_etiquette(c, q, t);
                        el[n].lien = v->type == V_TEXTE ? lien_du_type(c->types[q]) : 0;
                        if (o->definis[q] && v->type == V_FICHIER) {   /* une image, un son (§ 15.7) : ses octets, montrés */
                            char *illisible = NULL;
                            const char *montre = format_montre(v->fichier, &illisible);
                            if (montre && octets_du_fichier(v->fichier, &illisible)) {
                                el[n].octets = v->fichier->octets;
                                el[n].taille = v->fichier->taille;
                                el[n].format = montre;
                            }
                            free(illisible);
                        }
                        if (el[n].etiquette || el[n].lien || el[n].octets) el[n].valeur_depuis = strlen(libelle) + 3;
                    }
                    free(t);
                }
                free(libelle);
                el[n].texte = textes[n];
                n++;
            }
            for (size_t q = 0; q < c->nb_champs; q++) valeur_liberer(&plusieurs[q]);
            free(plusieurs);
            for (size_t k = 0; k < nb_tableaux; k++) {   /* « Séances », la liste, « Nouvelle séance » */
                TableauFiche *t = &tableaux[k];
                textes[n] = grym_dupliquer(t->titre);
                el[n].sorte = ELEMENT_TEXTE; el[n].texte = textes[n]; liens[n] = NULL; n++;
                t->element = n;
                textes[n] = grym_dupliquer(t->entite->nom);
                el[n].sorte = ELEMENT_LISTE; el[n].texte = textes[n]; liens[n] = NULL;
                el[n].colonnes = (const char *const *)t->titres;
                el[n].nb_colonnes = t->nb_colonnes;
                n++;
                t->bouton = n;
                textes[n] = grym_dupliquer(t->nouveau);
                el[n].sorte = ELEMENT_BOUTON; el[n].texte = textes[n]; liens[n] = NULL; n++;
            }
            modifier = n;
            fermer = n + 1;
            textes[n] = grym_dupliquer("Modifier");
            el[n].sorte = ELEMENT_BOUTON; el[n].texte = textes[n]; liens[n] = NULL; n++;
            textes[n] = grym_dupliquer("Fermer");
            el[n].sorte = ELEMENT_BOUTON; el[n].texte = textes[n]; liens[n] = NULL; n++;
            char *titre = nom_objet(m, o);
            if (ouverte) m->iface.ecran_fermer(m->iface.contexte, sortie);
            m->iface.ecran_ouvrir(m->iface.contexte, sortie, titre, el, n);
            free(titre);
            ouverte = 1;
            relire = 0;
            for (size_t k = 0; k < nb_tableaux && !erreur; k++) erreur = tableau_montrer(m, o, &tableaux[k]);
            if (erreur) { resultat = erreur; break; }
        }
        if (a_dire) {   /* le motif d'une saisie refusée, sur la fiche relue */
            if (m->iface.ecran_erreur) m->iface.ecran_erreur(m->iface.contexte, sortie, a_dire);
            free(a_dire);
            a_dire = NULL;
        }
        char *probleme = ouvrir_attente(m, sortie, cadres);
        Evenement ev = { EVENEMENT_FERMETURE, 0, 0, NULL, NULL, 0, NULL, 0 };
        Issue issue = ISSUE_REPONDU;
        if (!probleme) issue = m->iface.ecran_attendre(m->iface.contexte, sortie, &ev);
        char *reprise = probleme ? NULL : fermer_attente(m);
        if (!probleme) probleme = reprise;
        if (!probleme && issue != ISSUE_REPONDU) {
            probleme = grym_dupliquer("Interrompu (Ctrl+C).");
            *fatal = 1;
        }
        if (probleme || ev.sorte == EVENEMENT_FERMETURE || (ev.sorte == EVENEMENT_CLIC && ev.element == fermer)) {
            resultat = probleme;
            break;
        }
        char *r = NULL;
        int retableau = 0;   /* un geste qui a pu changer ce que montrent les tableaux */
        if (ev.sorte == EVENEMENT_CLIC && ev.element < n && liens[ev.element]) {
            r = fiche_ecran(m, liens[ev.element], sortie, cadres, fatal, profondeur + 1);   /* la fiche du lien, par-dessus */
            retableau = 1;
        } else if (ev.sorte == EVENEMENT_CLIC && ev.element == modifier) {
            r = saisir(m, o, "", sortie, cadres, fatal, 1);
            relire = 1;
        } else {
            for (size_t k = 0; k < nb_tableaux; k++) {
                TableauFiche *t = &tableaux[k];
                if (ev.sorte == EVENEMENT_CLIC && ev.element == t->bouton) {
                    r = tableau_nouveau(m, o, t, sortie, cadres, fatal);
                    retableau = 1;
                } else if (ev.sorte == EVENEMENT_CHOIX && ev.element == t->element && ev.ligne < t->nb_objets
                           && t->objets.type == V_LISTE && ev.ligne < t->objets.liste->n) {
                    /* la ligne « Total » ne mène nulle part ; un objet, à sa fiche, par-dessus */
                    char *err = NULL;
                    Objet *x = machine_objet_en_base(m, t->objets.liste->ids[ev.ligne], t->objets.liste->classes[ev.ligne], &err);
                    if (x) r = fiche_ecran(m, x, sortie, cadres, fatal, profondeur + 1);
                    else r = err;
                    retableau = 1;
                }
            }
        }
        if (retableau && !relire && !*fatal && ouverte) {
            /* ce qui a changé par-dessus (une séance modifiée, ajoutée) se voit au retour ; une erreur de lecture se dit */
            for (size_t k = 0; k < nb_tableaux; k++) {
                char *e2 = tableau_montrer(m, o, &tableaux[k]);
                if (e2 && !r) r = e2;
                else free(e2);
            }
        }
        if (!r && !*fatal && m->base && base_modifiee(m->base)) {
            /* une saisie de la fiche a écrit : les règles se vérifient avant la validation, comme à la fin d'un
             * événement ; refusée, la saisie s'annule tout entière, en base comme en mémoire, et la fiche le dit */
            r = verifier_regles_machine(m, sortie);
            if (r) {
                char *e = NULL;
                base_annuler(m->base);
                annuler(m);
                if (!base_commencer(m->base, &e)) { free(r); resultat = e; *fatal = 1; break; }
                relire = 1;
                retableau = 1;
            }
        }
        if (r && *fatal) { resultat = r; break; }
        if (r && relire) { a_dire = r; continue; }   /* dit après la relecture, qui referait l'écran par-dessus */
        if (r) {   /* « Saisie annulée », une valeur refusée : la fiche le dit, et reste ouverte */
            if (m->iface.ecran_erreur) m->iface.ecran_erreur(m->iface.contexte, sortie, r);
            free(r);
        }
    }
    if (ouverte) m->iface.ecran_fermer(m->iface.contexte, sortie);
    free(a_dire);
    for (size_t k = 0; k < n; k++) free(textes[k]);
    free(textes);
    free(el);
    free(liens);
    tableaux_liberer(tableaux, nb_tableaux);
    return resultat;
}

/* Vrai si un bloc du module contient cette instruction. */
static int module_contient(const Module *module, CodeInstruction cherchee) {
    for (size_t b = 0; b < module->nb; b++) {
        const Bloc *bl = module->blocs[b];
        for (size_t d = 0; d < bl->taille_code;) {
            CodeInstruction c = (CodeInstruction)bl->code[d];
            if (c == cherchee) return 1;
            d += instruction_taille(c);
        }
    }
    return 0;
}

int machine_executer(Machine *m, Module *module, Chaine *sortie, Diagnostic *diag) {
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    diag->fichier = NULL;
    diag->origine_ligne = diag->origine_colonne = 0;

    char *erreur = NULL;
    if (!module_verifier(module, &erreur)) {
        diag->message = grym_formater("Bytecode invalide : %s", erreur);
        free(erreur);
        return 0;
    }
    /* un essai de migration n'exécute rien (A2-c) : qu'il y ait des écrans ne l'empêche pas */
    if (!m->essai_migration && !m->iface.ecran_ouvrir
        && (module_contient(module, I_ECRAN_OUVRIR) || module_contient(module, I_FICHE_ECRAN))) {
        diag->message = grym_dupliquer("Ce programme ouvre des écrans ou des documents : lance-le dans une fenêtre, avec grym-atelier.");
        return 0;
    }

    /* Classes du module : ajoutées à la machine ; une redéclaration l'emporte pour la suite.
     * Les champs hérités sont recopiés en tête : un champ garde le même rang dans toute la lignée. */
    for (size_t k = 0; k < module->nb_classes; k++) {
        const ClasseModule *cm = &module->classes[k];
        const ClasseVM *parent = NULL;
        char *probleme = NULL;
        if (cm->parent) {
            parent = classe_vm(m, cm->parent);
            if (!parent) probleme = grym_formater("Classe parente « %s » inconnue.", cm->parent);
        }
        const ClasseVM **aptitudes = grym_allouer((cm->nb_aptitudes ? cm->nb_aptitudes : 1) * sizeof *aptitudes);
        for (size_t q = 0; q < cm->nb_aptitudes && !probleme; q++) {
            aptitudes[q] = aptitude_vm(m, cm->aptitudes[q]);
            if (!aptitudes[q]) probleme = grym_formater("Aptitude « %s » inconnue.", cm->aptitudes[q]);
        }
        /* Champs, dans l'ordre : hérités, apportés par les aptitudes, propres. Aucun nom en double. */
        size_t total = (parent ? parent->nb_champs : 0) + cm->nb_champs;
        for (size_t q = 0; q < cm->nb_aptitudes && !probleme; q++) total += aptitudes[q]->nb_champs;
        char **champs = grym_allouer((total ? total : 1) * sizeof *champs);
        char **types = grym_allouer((total ? total : 1) * sizeof *types);
        char **departs = grym_allouer((total ? total : 1) * sizeof *departs);
        char **valeurs = grym_allouer((total ? total : 1) * sizeof *valeurs);
        char **types_libres = grym_allouer((total ? total : 1) * sizeof *types_libres);   /* « texte », découpé */
        char **valeurs_libres = grym_allouer((total ? total : 1) * sizeof *valeurs_libres);
        char **couleurs = grym_allouer((total ? total : 1) * sizeof *couleurs);   /* leurs étiquettes (§ 16.15) */
        char **transcrits = grym_allouer((total ? total : 1) * sizeof *transcrits);   /* « transcrite du mémo » (§ 15.8) */
        unsigned char *uniques = grym_allouer(total ? total : 1);
        size_t n = 0;
        for (size_t q = 0; q < total; q++) { valeurs[q] = NULL; types_libres[q] = NULL; valeurs_libres[q] = NULL; couleurs[q] = NULL; transcrits[q] = NULL; }
        for (size_t q = 0; parent && q < parent->nb_champs; q++) {
            couleurs[n] = parent->couleurs ? parent->couleurs[q] : NULL;
            transcrits[n] = parent->transcrit_de ? parent->transcrit_de[q] : NULL;
            valeurs[n] = parent->valeurs[q];
            types[n] = parent->types[q];
            departs[n] = parent->departs[q];
            uniques[n] = parent->uniques[q];
            champs[n++] = parent->champs[q];
        }
        for (size_t q = 0; q < cm->nb_aptitudes && !probleme; q++)
            for (size_t r = 0; r < aptitudes[q]->nb_champs; r++) {
                types[n] = aptitudes[q]->types[r];
                valeurs[n] = aptitudes[q]->valeurs[r];
                couleurs[n] = aptitudes[q]->couleurs ? aptitudes[q]->couleurs[r] : NULL;
                departs[n] = aptitudes[q]->departs[r];
                uniques[n] = aptitudes[q]->uniques[r] & 2;   /* une aptitude n'a pas de champ unique */
                champs[n++] = aptitudes[q]->champs[r];
            }
        const size_t debut_propres = n;   /* les champs propres, ceux qui peuvent avoir un nom d'avant (§ 16.7) */
        for (size_t q = 0; q < cm->nb_champs && !probleme; q++) {
            types[n] = cm->types ? cm->types[q] : NULL;
            if (types[n] && strchr(types[n], '\x1c')) {   /* « texte␜mémo » : transcrit d'un champ (son) (§ 15.8) */
                const char *s = strchr(types[n], '\x1c');
                types_libres[n] = grym_formater("%.*s", (int)(s - types[n]), types[n]);
                transcrits[n] = (char *)(s + 1);
                types[n] = types_libres[n];
            } else if (types[n] && strchr(types[n], '\x1e')) {   /* « texte␞à faire␞fini » : valeurs permises (§ 16.15) */
                const char *s = strchr(types[n], '\x1e');
                types_libres[n] = grym_formater("%.*s", (int)(s - types[n]), types[n]);
                const char *c2 = strchr(s + 1, '\x1d');   /* « ␝vr » : les couleurs, une lettre par valeur */
                valeurs_libres[n] = grym_formater("%.*s", (int)(c2 ? c2 - s - 1 : (long)strlen(s + 1)), s + 1);
                valeurs[n] = valeurs_libres[n];
                couleurs[n] = c2 ? (char *)(c2 + 1) : NULL;
                types[n] = types_libres[n];
                if (strcmp(types[n], "texte") != 0 || !s[1])
                    probleme = grym_formater("« %s » : des valeurs permises sur un champ %s.", cm->champs[q], types[n]);
            }
            departs[n] = cm->departs ? cm->departs[q] : NULL;
            uniques[n] = cm->uniques ? cm->uniques[q] : 0;
            champs[n++] = cm->champs[q];
        }
        /* Un type désigne un type de base ou une entité connue (ou la classe elle-même). */
        static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image", "son",
                                            "année", "heure" };
        for (size_t q = 0; q < n && !probleme; q++) {
            if (!types[q]) continue;
            int ok_type = strcmp(types[q], cm->nom) == 0 && cm->conserve;
            for (size_t r = 0; r < sizeof BASE / sizeof *BASE; r++) ok_type |= strcmp(types[q], BASE[r]) == 0;
            ok_type |= type_textuel(types[q]);   /* courriel, téléphone, adresse web (types.h) */
            const ClasseVM *e = classe_vm(m, types[q]);
            if (!ok_type && !(e && e->conserve))
                probleme = grym_formater("« %s » : type « %s » inconnu.", champs[q], types[q]);
        }
        for (size_t q = 0; q < n && !probleme; q++)
            for (size_t r = 0; r < q && !probleme; r++)
                if (strcmp(champs[q], champs[r]) == 0)
                    probleme = grym_formater("« %s » : champ fourni deux fois dans « %s ».", champs[q], cm->nom);
        if (probleme) {
            diag->message = grym_formater("Bytecode invalide : %s", probleme);
            free(probleme);
            free(champs);
            for (size_t q = 0; q < total; q++) { free(types_libres[q]); free(valeurs_libres[q]); }
            free(types_libres);
            free(valeurs_libres);
            free(couleurs);
            free(transcrits);
            free(valeurs);
            free(types);
            free(departs);
            free(uniques);
            free(aptitudes);
            return 0;   /* rien n'est encore enregistré : aucune formule, aucun cadre */
        }
        ClasseVM *c = grym_allouer(sizeof *c);
        c->nom = grym_dupliquer(cm->nom);
        c->feminin = cm->feminin;
        c->aptitude = cm->aptitude;
        c->parent = parent;
        c->aptitudes = aptitudes;
        c->nb_aptitudes = cm->nb_aptitudes;
        c->nb_champs = n;
        c->conserve = cm->conserve;
        c->hors_fiches = cm->hors_fiches;
        c->pluriel = cm->pluriel ? grym_dupliquer(cm->pluriel) : NULL;
        c->ancien = cm->ancien ? grym_dupliquer(cm->ancien) : NULL;
        c->anciens = NULL;
        for (size_t q = 0; cm->anciens && q < cm->nb_champs; q++) {
            if (!cm->anciens[q]) continue;
            if (!c->anciens) {
                c->anciens = grym_allouer((n ? n : 1) * sizeof *c->anciens);
                for (size_t r = 0; r < n; r++) c->anciens[r] = NULL;
            }
            if (debut_propres + q < n) c->anciens[debut_propres + q] = grym_dupliquer(cm->anciens[q]);
        }
        c->champs = grym_allouer((n ? n : 1) * sizeof *c->champs);
        c->types = grym_allouer((n ? n : 1) * sizeof *c->types);
        c->departs = grym_allouer((n ? n : 1) * sizeof *c->departs);
        c->valeurs = grym_allouer((n ? n : 1) * sizeof *c->valeurs);
        c->couleurs = grym_allouer((n ? n : 1) * sizeof *c->couleurs);
        c->transcrit_de = grym_allouer((n ? n : 1) * sizeof *c->transcrit_de);
        c->uniques = uniques;
        for (size_t q = 0; q < n; q++) {
            c->champs[q] = grym_dupliquer(champs[q]);
            c->types[q] = types[q] ? grym_dupliquer(types[q]) : NULL;
            c->departs[q] = departs[q] ? grym_dupliquer(departs[q]) : NULL;
            c->valeurs[q] = valeurs[q] ? grym_dupliquer(valeurs[q]) : NULL;
            c->couleurs[q] = couleurs[q] ? grym_dupliquer(couleurs[q]) : NULL;
            c->transcrit_de[q] = transcrits[q] ? grym_dupliquer(transcrits[q]) : NULL;
        }
        free(champs);
        for (size_t q = 0; q < total; q++) { free(types_libres[q]); free(valeurs_libres[q]); }
        free(types_libres);
        free(valeurs_libres);
        free(couleurs);
        free(transcrits);
        free(valeurs);
        free(types);
        free(departs);
        /* Table qui porte chaque champ : celle de la classe parente pour un champ hérité, la sienne sinon. */
        c->proprietaires = grym_allouer((n ? n : 1) * sizeof *c->proprietaires);
        for (size_t q = 0; q < n; q++)
            c->proprietaires[q] = parent && q < parent->nb_champs ? parent->proprietaires[q] : c;
        ClasseVM **t = grym_allouer((m->nb_classes + 1) * sizeof *t);
        if (m->nb_classes) memcpy(t, m->classes, m->nb_classes * sizeof *t);
        free(m->classes);
        m->classes = t;
        m->classes[m->nb_classes++] = c;
    }
    /* Base des entités (§ 16.5, § 16.6) : ouverte au premier besoin, une transaction par exécution. */
    int entites = 0;
    for (size_t i = 0; i < m->nb_classes; i++) entites |= m->classes[i]->conserve;
    if (m->essai_migration) {   /* essai : la base est préparée puis rendue telle quelle, rien ne s'exécute */
        Chaine *r = m->essai_migration;
        m->essai_migration = NULL;
        if (!entites) { chaine_ajouter(r, "Aucune entité conservée : aucune base.\n"); return 1; }
        FILE *existe = m->chemin_base ? grym_fopen(m->chemin_base, "rb") : NULL;
        if (m->chemin_base && !existe) {   /* ne pas créer la base pour un essai */
            size_t n = 0;
            for (size_t i = m->nb_classes - module->nb_classes; i < m->nb_classes; i++) n += m->classes[i]->conserve;
            char *l = grym_formater("La base n'existe pas encore : le premier lancement la créera, avec %lu table%s.\n",
                                    (unsigned long)n, n > 1 ? "s" : "");
            chaine_ajouter(r, l);
            free(l);
            return 1;
        }
        if (existe) fclose(existe);
        char *erreur = NULL;
        if (!m->base) m->base = ouvrir_la_base(m, module, &erreur);
        int pret = m->base && base_commencer(m->base, &erreur);
        if (pret) base_rapport(m->base, r);
        if (pret) pret = base_renommer_entites(m->base, m->classes, m->nb_classes, &erreur);   /* § 16.7 */
        for (size_t i = m->nb_classes - module->nb_classes; pret && i < m->nb_classes; i++)
            pret = base_preparer(m->base, m->classes[i], &erreur);
        if (m->base) { base_rapport(m->base, NULL); base_annuler(m->base); }
        if (!pret) { diag->message = erreur; return 0; }
        if (!r->n) chaine_ajouter(r, "La base est à jour : rien ne changera.\n");
        return 1;
    }
    const int imbrique = m->imbrique > 0;
    const size_t nb_essais_dehors = m->nb_essais;
    const size_t seuil_dehors = m->seuil;
    if (imbrique) {
        /* le ramasse-miettes ne voit que la pile de cette exécution : les objets de celle qui l'appelle
         * (la fiche ouverte, sa pile) ne doivent pas être pris pour des déchets */
        m->seuil = (size_t)-1;
        entites = 0;
    } else {
        m->base_engagee = entites;
        m->fichiers_prevus = 0;
        m->question_posee = 0;
    }
    if (entites) {
        char *erreur = NULL;
        if (!m->base) m->base = ouvrir_la_base(m, module, &erreur);
        int pret = m->base && base_commencer(m->base, &erreur);
        if (pret) pret = base_renommer_entites(m->base, m->classes, m->nb_classes, &erreur);   /* § 16.7 */
        for (size_t i = m->nb_classes - module->nb_classes; pret && i < m->nb_classes; i++)
            pret = base_preparer(m->base, m->classes[i], &erreur);
        if (!pret) {
            if (m->base) base_annuler(m->base);
            diag->message = erreur;
            return 0;
        }
    }
    /* Enregistrement des formules (docs/vm.md, § 5) : une formule du même nom est remplacée. */
    size_t nb_avant = m->nb_formules;
    Remplacement *remplaces = grym_allouer(module->nb * sizeof *remplaces);
    size_t nb_remplaces = 0;
    for (size_t k = 1; k < module->nb; k++) {
        Bloc *f = module->blocs[k];
        module->blocs[k] = NULL;
        Formule *ex = formule_de(m, f->nom, f->classe);
        if (ex) {
            remplaces[nb_remplaces].index = (size_t)(ex - m->formules);
            remplaces[nb_remplaces].bloc = ex->bloc;
            remplaces[nb_remplaces].liaison = ex->liaison;
            nb_remplaces++;
            ex->bloc = f;
            ex->liaison = lier(m, f);
        } else {
            Formule *t = grym_allouer((m->nb_formules + 1) * sizeof *t);
            if (m->nb_formules) memcpy(t, m->formules, m->nb_formules * sizeof *t);
            free(m->formules);
            m->formules = t;
            Formule *nf = &m->formules[m->nb_formules++];
            nf->nom = grym_dupliquer(f->nom);
            nf->bloc = f;
            nf->liaison = lier(m, f);
        }
    }

    if (m->seuil == 0) m->seuil = SEUIL_RAMASSAGE;
    if (imbrique) m->seuil = (size_t)-1;

    m->epoque++;
    long aujourdhui = date_aujourdhui();
    long maintenant;
    {
        time_t t = time(NULL);
        struct tm *l = localtime(&t);
        maintenant = l ? l->tm_hour * 60L + l->tm_min : 0;
    }
    const Bloc *principal = module->blocs[0];
    size_t *liaison_principale = lier(m, principal);
    Cadre *cadres = grym_allouer(8 * sizeof *cadres);
    size_t nb_cadres = 1, cap_cadres = 8;
    cadres[0].b = principal;
    cadres[0].liaison = liaison_principale;
    cadres[0].ip = 0;
    {
        int nl = principal->nb_locaux;
        cadres[0].locaux = grym_allouer((nl ? (size_t)nl : 1) * sizeof *cadres[0].locaux);
        cadres[0].definis = grym_allouer(nl ? (size_t)nl : 1);
        memset(cadres[0].definis, 0, nl ? (size_t)nl : 1);
    }

    Pile pile = { NULL, 0, 0 };
    int ok = 1;
    int fatal = 0;   /* erreur qu'aucun « Essayer » ne rattrape : Ctrl+C, entrée épuisée, base perdue (§ 18) */
    for (;;) {
        Cadre *cadre = &cadres[nb_cadres - 1];
        const Bloc *b = cadre->b;
        const size_t *liaison = cadre->liaison;
        size_t ip = cadre->ip;
        size_t debut = ip;
        CodeInstruction code = (CodeInstruction)b->code[ip];
        unsigned op = 0;
        size_t cible = 0;
        if (instruction_a_operande(code))
            op = (unsigned)b->code[ip + 1] | ((unsigned)b->code[ip + 2] << 8);
        if (code == I_SAUTER || code == I_SAUTER_SI_FAUX || code == I_ESSAYER)
            cible = (size_t)b->code[ip + 1] | ((size_t)b->code[ip + 2] << 8)
                  | ((size_t)b->code[ip + 3] << 16) | ((size_t)b->code[ip + 4] << 24);
        ip += instruction_taille(code);
        cadre->ip = ip;

        if (code == I_RETOUR || code == I_RENDRE) {
            /* un essai du cadre qui se termine se referme sans échec (bytecode écrit à la main) */
            while (m->nb_essais && m->essais[m->nb_essais - 1].cadre >= nb_cadres - 1) {
                Essai *e = &m->essais[--m->nb_essais];
                oublier_photo(e);
                char *probleme = NULL;
                if (m->base && !base_lacher(m->base, m->nb_essais + 1, &probleme)) {
                    ok = echouer(diag, b, debut, probleme);
                    fatal = 1;
                }
            }
            if (!ok) break;
            if (nb_cadres == 1) break;                  /* fin du programme */
            liberer_cadre(cadre);
            nb_cadres--;                                /* la valeur rendue reste au sommet de la pile */
            continue;
        }

        switch (code) {
        case I_CONSTANTE: {
            const Constante *k = &b->constantes[op];
            Valeur v;
            if (k->type == C_NOMBRE) {
                v = valeur_nombre(dec_depuis_canonique(k->texte));
            } else if (k->type == C_DATE) {
                long j = 0;
                date_lire_iso(k->texte, &j);   /* vérifiée au chargement */
                v = valeur_date(j);
            } else if (k->type == C_BOOLEEN) {
                v = valeur_booleen(strcmp(k->texte, "vrai") == 0);
            } else {
                v = valeur_nombre(dec_zero());
                v.type = V_TEXTE;
                v.texte = grym_dupliquer(k->texte);
            }
            empiler(&pile, v);
            break;
        }
        case I_LIRE: {
            Case *c = &m->cases[liaison[op]];
            if (!c->definie) {
                ok = echouer(diag, b, debut, grym_formater("« %s » n'a pas de valeur.", c->nom));
                break;
            }
            empiler(&pile, valeur_copier(&c->valeur));
            break;
        }
        case I_ECRIRE:
            ecrire(m, liaison[op], depiler(&pile));
            break;
        case I_NEGATION: {
            Valeur *x = &pile.v[pile.n - 1];
            if (x->type == V_ABSENT) { ok = echouer(diag, b, debut, message_absent(x)); break; }
            if (x->type == V_ANNEE) { ok = echouer(diag, b, debut, grym_dupliquer("On ne prend pas l'opposé d'une année.")); break; }
            if (x->type != V_NOMBRE) {
                ok = echouer(diag, b, debut, grym_formater("Opposé impossible : la valeur est %s.",
                                                           nom_type(x->type)));
                break;
            }
            Decimal d = dec_negation(&x->nombre);
            dec_liberer(&x->nombre);
            x->nombre = d;
            break;
        }
        case I_ADDITION: case I_SOUSTRACTION: case I_MULTIPLICATION:
        case I_DIVISION: case I_PUISSANCE: {
            Valeur vb = depiler(&pile), va = depiler(&pile);
            if (va.type == V_ABSENT || vb.type == V_ABSENT) {
                char *m = message_absent(va.type == V_ABSENT ? &va : &vb);
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, m);
                break;
            }
            if (va.type == V_ANNEE || vb.type == V_ANNEE) {
                /* année ± nombre entier, nombre entier + année, année − année (§ 14.5) */
                char *probleme = NULL;
                Valeur r = valeur_nombre(dec_zero());
                long n = 0;
                const Valeur *an = va.type == V_ANNEE ? &va : &vb, *x = va.type == V_ANNEE ? &vb : &va;
                if (code == I_MULTIPLICATION) probleme = grym_dupliquer("On ne multiplie pas une année.");
                else if (code == I_DIVISION) probleme = grym_dupliquer("On ne divise pas une année.");
                else if (code == I_PUISSANCE) probleme = grym_dupliquer("On n'élève pas une année à une puissance.");
                else if (va.type == V_ANNEE && vb.type == V_ANNEE) {
                    if (code == I_ADDITION) probleme = grym_dupliquer("On n'additionne pas deux années.");
                    else { dec_liberer(&r.nombre); r = valeur_nombre(dec_depuis_long(va.jours - vb.jours)); }
                } else if (x->type != V_NOMBRE)
                    probleme = grym_formater("%s impossible entre une année et %s.", instruction_nom(code), nom_type(x->type));
                else if (code == I_SOUSTRACTION && vb.type == V_ANNEE)
                    probleme = grym_dupliquer("On ne soustrait pas une année d'un nombre.");
                else if (!dec_en_long_borne(&x->nombre, -99999, 99999, &n))
                    probleme = grym_dupliquer("Une année se décale d'un nombre entier d'années.");
                else {
                    long a = code == I_ADDITION ? an->jours + n : an->jours - n;
                    if (a < 1 || a > 9999) probleme = grym_dupliquer("Année hors du calendrier : de 1 à 9999.");
                    else { valeur_liberer(&r); r = valeur_annee(a); }
                }
                valeur_liberer(&va);
                valeur_liberer(&vb);
                if (probleme) {
                    valeur_liberer(&r);
                    ok = echouer(diag, b, debut, probleme);
                    break;
                }
                empiler(&pile, r);
                break;
            }
            if ((va.type == V_HEURE || vb.type == V_HEURE) && code == I_SOUSTRACTION && va.type == V_HEURE && vb.type == V_HEURE) {
                /* h2 − h1 : des minutes, négatives si h2 précède (§ 14.7) */
                Valeur r = valeur_nombre(dec_depuis_long(va.jours - vb.jours));
                valeur_liberer(&va);
                valeur_liberer(&vb);
                empiler(&pile, r);
                break;
            }
            if (va.type == V_HEURE || vb.type == V_HEURE) {
                char *mp = grym_formater("%s impossible avec une heure : écris « h + 15 minutes », « h2 − h1 ».", instruction_nom(code));
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, mp);
                break;
            }
            if ((va.type == V_DATE || vb.type == V_DATE) && (code == I_ADDITION || code == I_SOUSTRACTION)) {
                /* date ± jours, jours + date, date − date (§ 14.2) */
                char *probleme = NULL;
                Valeur r = valeur_nombre(dec_zero());
                long j = 0;
                if (va.type == V_DATE && vb.type == V_DATE) {
                    if (code == I_ADDITION) probleme = grym_dupliquer("On n'additionne pas deux dates.");
                    else { dec_liberer(&r.nombre); r = valeur_nombre(dec_depuis_long(va.jours - vb.jours)); }
                } else {
                    const Valeur *d = va.type == V_DATE ? &va : &vb, *x = va.type == V_DATE ? &vb : &va;
                    if (x->type != V_NOMBRE)
                        probleme = grym_formater("%s impossible entre une date et %s.", instruction_nom(code),
                                                 nom_type(x->type));
                    else if (code == I_SOUSTRACTION && vb.type == V_DATE)
                        probleme = grym_dupliquer("On ne soustrait pas une date d'un nombre.");
                    else if (!dec_en_jours(&x->nombre, &j))
                        probleme = grym_dupliquer("Une date se décale d'un nombre entier de jours.");
                    else {
                        long n = code == I_ADDITION ? d->jours + j : d->jours - j;
                        if (n < DATE_MIN || n > DATE_MAX)
                            probleme = grym_dupliquer("Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
                        else { valeur_liberer(&r); r = valeur_date(n); }
                    }
                }
                valeur_liberer(&va);
                valeur_liberer(&vb);
                if (probleme) {
                    valeur_liberer(&r);
                    ok = echouer(diag, b, debut, probleme);
                    break;
                }
                empiler(&pile, r);
                break;
            }
            if (va.type != V_NOMBRE || vb.type != V_NOMBRE) {
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, grym_formater(
                    "%s impossible : un des opérandes est %s.", instruction_nom(code),
                    nom_type(va.type != V_NOMBRE ? va.type : vb.type)));
                break;
            }
            Decimal r;
            StatutDecimal st =
                code == I_ADDITION       ? dec_addition(&va.nombre, &vb.nombre, &r) :
                code == I_SOUSTRACTION   ? dec_soustraction(&va.nombre, &vb.nombre, &r) :
                code == I_MULTIPLICATION ? dec_multiplication(&va.nombre, &vb.nombre, &r) :
                code == I_DIVISION       ? dec_division(&va.nombre, &vb.nombre, &r) :
                                           dec_puissance(&va.nombre, &vb.nombre, &r);
            valeur_liberer(&va);
            valeur_liberer(&vb);
            if (st != DEC_OK) {
                dec_liberer(&r);
                ok = echouer(diag, b, debut, message_statut(st));
                break;
            }
            empiler(&pile, valeur_nombre(r));
            break;
        }
        case I_ESSAYER: {
            /* « Essayer : » : point de reprise (grammaire, § 18 ; docs/vm.md, § 6) */
            if (m->nb_essais == m->cap_essais) {
                m->cap_essais = m->cap_essais ? m->cap_essais * 2 : 8;
                Essai *t = grym_allouer(m->cap_essais * sizeof *t);
                if (m->nb_essais) memcpy(t, m->essais, m->nb_essais * sizeof *t);
                free(m->essais);
                m->essais = t;
            }
            Essai *e = &m->essais[m->nb_essais];
            e->cadre = nb_cadres - 1;
            e->pile = pile.n;
            e->journal = m->nb_journal;
            e->a_ecrire = m->nb_a_ecrire;
            e->cible = cible;
            photographier(e, cadre);
            char *probleme = NULL;
            if (m->base && !base_point(m->base, m->nb_essais + 1, &probleme)) {
                oublier_photo(e);
                ok = echouer(diag, b, debut, probleme);
                fatal = 1;
                break;
            }
            m->nb_essais++;
            m->epoque++;   /* une case déjà écrite repasse au journal : l'échec saura la rendre */
            break;
        }
        case I_FIN_ESSAI: {
            /* fin du bloc « Essayer » sans échec : ce qu'il a fait est gardé */
            if (!m->nb_essais || m->essais[m->nb_essais - 1].cadre != nb_cadres - 1) {
                ok = echouer(diag, b, debut, grym_dupliquer("FIN_ESSAI sans essai ouvert."));
                fatal = 1;
                break;
            }
            Essai *e = &m->essais[--m->nb_essais];
            oublier_photo(e);
            char *probleme = NULL;
            if (m->base && !base_lacher(m->base, m->nb_essais + 1, &probleme)) {
                ok = echouer(diag, b, debut, probleme);
                fatal = 1;
            }
            break;
        }
        case I_EFFACER:   /* « Effacer l'écran. » (§ 4.3) : hors d'un terminal, rien n'est écrit */
            m->iface.effacer(m->iface.contexte, sortie);   /* hors d'un terminal, la console n'écrit rien */
            break;
        case I_STYLE:   /* « Les nombres s'affichent à la française. » (§ 4.1) */
            m->style = (int)op;
            break;
        case I_COLLER:
        case I_ELIDER: {
            /* « a suivi de b » : les deux textes collés ; « de x » : « de x » ou « d'x » (§ 4.4) */
            Valeur vb = depiler(&pile), va = code == I_COLLER ? depiler(&pile) : vi_texte("");
            if (va.type == V_ABSENT || vb.type == V_ABSENT) {
                char *probleme = message_absent(va.type == V_ABSENT ? &va : &vb);
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            char *ta = texte_valeur(m, &va), *tb = texte_valeur(m, &vb), *r;
            if (code == I_COLLER) {
                r = grym_formater("%s%s", ta, tb);
            } else {
                /* voyelle, accentuée ou non, œ, æ, y : jamais le h (muet ou aspiré, l'orthographe ne le dit pas) */
                static const char *const V[] = { "a", "e", "i", "o", "u", "y", "A", "E", "I", "O", "U", "Y",
                    "à", "â", "ä", "é", "è", "ê", "ë", "î", "ï", "ô", "ö", "ù", "û", "ü", "ÿ", "œ", "æ",
                    "À", "Â", "Ä", "É", "È", "Ê", "Ë", "Î", "Ï", "Ô", "Ö", "Ù", "Û", "Ü", "Œ", "Æ" };
                int voy = 0;
                for (size_t k = 0; k < sizeof V / sizeof *V && !voy; k++) voy = strncmp(tb, V[k], strlen(V[k])) == 0;
                const char *prep = op ? (voy ? "qu'" : "que ") : (voy ? "d'" : "de ");
                r = grym_formater("%s%s", prep, tb);
            }
            free(ta);
            free(tb);
            valeur_liberer(&va);
            valeur_liberer(&vb);
            empiler(&pile, vi_texte(r));
            free(r);
            break;
        }
        case I_CADRER: {
            /* « le nom sur 20 », « le solde sur 10 à droite » (§ 4.2) */
            Valeur vl = depiler(&pile), vv = depiler(&pile);
            long largeur = 0;
            char *probleme = NULL;
            if (vl.type == V_ABSENT || vv.type == V_ABSENT)
                probleme = message_absent(vl.type == V_ABSENT ? &vl : &vv);
            else if (vl.type != V_NOMBRE || !dec_en_long_borne(&vl.nombre, 1, 1000, &largeur))
                probleme = grym_dupliquer("Largeur invalide : un nombre entier de 1 à 1000 est attendu.");
            if (probleme) {
                valeur_liberer(&vl);
                valeur_liberer(&vv);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            int droite = op == 2 || (op == 0 && (vv.type == V_NOMBRE || vv.type == V_ANNEE));
            char *t = texte_valeur(m, &vv);
            size_t n = 0, octets = 0;
            for (const char *p = t; *p; p++) {   /* caractères, pas octets : « é » compte pour un */
                if (((unsigned char)*p & 0xC0) == 0x80) continue;
                if (n == (size_t)largeur) break;
                n++;
                octets = (size_t)(p - t) + 1;
                while (((unsigned char)p[1] & 0xC0) == 0x80) { p++; octets++; }
            }
            Chaine c = {0};
            if (droite) for (size_t k = n; k < (size_t)largeur; k++) chaine_ajouter(&c, " ");
            char *coupe = grym_formater("%.*s", (int)octets, t);
            chaine_ajouter(&c, coupe);
            free(coupe);
            if (!droite) for (size_t k = n; k < (size_t)largeur; k++) chaine_ajouter(&c, " ");
            free(t);
            valeur_liberer(&vl);
            valeur_liberer(&vv);
            char *r = chaine_rendre(&c);
            Valeur v = vi_texte(r);
            free(r);
            empiler(&pile, v);
            break;
        }
        case I_AFFICHER:
        case I_AFFICHER_SANS_LIGNE: {
            /* Les éléments sont dans la pile, du premier au dernier. */
            size_t base = pile.n - op;
            for (size_t k = 0; k < op; k++) {
                if (k) chaine_ajouter(sortie, " ");
                const Valeur *v = &pile.v[base + k];
                char *t = texte_valeur(m, v);
                char *illisible = NULL;
                const char *montre = v->type == V_FICHIER && m->iface.afficher_image ? format_montre(v->fichier, &illisible) : NULL;
                if (v->type == V_FICHIER && m->iface.afficher_image && (illisible || (montre && !octets_du_fichier(v->fichier, &illisible)))) {
                    chaine_ajouter(sortie, illisible);   /* une image disparue se dit, au lieu d'arrêter l'affichage */
                    free(illisible);
                } else if (montre)   /* une image, ou un son à écouter (docs/v2.md, § 10 ; § 15.7) */
                    m->iface.afficher_image(m->iface.contexte, sortie, v->fichier->octets, v->fichier->taille, montre, t);
                else
                    chaine_ajouter(sortie, t);
                free(t);
            }
            if (code == I_AFFICHER) chaine_ajouter(sortie, "\n");
            while (pile.n > base) valeur_liberer(&pile.v[--pile.n]);
            break;
        }
        case I_EGAL: case I_DIFFERENT: case I_INFERIEUR: case I_SUPERIEUR:
        case I_INFERIEUR_OU_EGAL: case I_SUPERIEUR_OU_EGAL: {
            Valeur vb = depiler(&pile), va = depiler(&pile);
            if (va.type == V_ABSENT || vb.type == V_ABSENT) {
                char *m = message_absent(va.type == V_ABSENT ? &va : &vb);
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, m);
                break;
            }
            int egalite = code == I_EGAL || code == I_DIFFERENT;
            int resultat = 0;
            if ((va.type == V_ANNEE && vb.type == V_DATE) || (va.type == V_DATE && vb.type == V_ANNEE)) {
                valeur_liberer(&va);
                valeur_liberer(&vb);
                ok = echouer(diag, b, debut, grym_dupliquer("Une année ne se compare pas à une date : "
                                                            "compare l'année de la date, « l'année de d »."));
                break;
            }
            /* une année se compare à une année ou à un nombre, par valeur (§ 14.5) */
            if (va.type == V_ANNEE && (vb.type == V_ANNEE || vb.type == V_NOMBRE)) {
                va.type = V_NOMBRE;
                dec_liberer(&va.nombre);
                va.nombre = dec_depuis_long(va.jours);
            }
            if (vb.type == V_ANNEE && va.type == V_NOMBRE) {
                vb.type = V_NOMBRE;
                dec_liberer(&vb.nombre);
                vb.nombre = dec_depuis_long(vb.jours);
            }
            if (va.type != vb.type || (!egalite && va.type != V_NOMBRE && va.type != V_DATE && va.type != V_HEURE)) {
                ok = echouer(diag, b, debut, va.type != vb.type
                    ? grym_formater("Comparaison impossible entre %s et %s.",
                                    nom_type(va.type), nom_type(vb.type))
                    : grym_formater("Seuls deux nombres ou deux dates se comparent par ordre : la valeur est %s.",
                                    nom_type(va.type)));
            } else if (va.type == V_NOMBRE || va.type == V_DATE || va.type == V_HEURE) {
                int c = va.type != V_NOMBRE ? (va.jours > vb.jours) - (va.jours < vb.jours)
                                          : dec_comparer(&va.nombre, &vb.nombre);
                resultat = code == I_EGAL ? c == 0 : code == I_DIFFERENT ? c != 0
                         : code == I_INFERIEUR ? c < 0 : code == I_SUPERIEUR ? c > 0
                         : code == I_INFERIEUR_OU_EGAL ? c <= 0 : c >= 0;
            } else {
                int egaux = va.type == V_BOOLEEN ? va.vrai == vb.vrai
                          : va.type == V_OBJET  ? va.objet == vb.objet       /* identité */
                          : va.type == V_FICHIER ? fichiers_egaux(va.fichier, vb.fichier)
                          : strcmp(va.texte, vb.texte) == 0;
                resultat = code == I_EGAL ? egaux : !egaux;
            }
            valeur_liberer(&va);
            valeur_liberer(&vb);
            if (ok) empiler(&pile, valeur_booleen(resultat));
            break;
        }
        case I_NON: {
            Valeur *x = &pile.v[pile.n - 1];
            if (x->type != V_BOOLEEN) {
                ok = echouer(diag, b, debut, grym_formater("Négation impossible : la valeur est %s, "
                                                           "ni vraie ni fausse.", nom_type(x->type)));
                break;
            }
            x->vrai = !x->vrai;
            break;
        }
        case I_SAUTER:
            if (cible <= debut && grym_interruption) {
                grym_interruption = 0;
                ok = echouer(diag, b, debut, grym_dupliquer("Interrompu (Ctrl+C)."));
                fatal = 1;
                break;
            }
            cadre->ip = cible;
            break;
        case I_ECHOUER:
            ok = echouer(diag, b, debut, grym_dupliquer(b->constantes[op].texte));
            break;
        case I_EXIGER_ENTIER_NATUREL: {
            Valeur *x = &pile.v[pile.n - 1];
            if (x->type != V_NOMBRE || x->nombre.negatif || !dec_est_entier(&x->nombre)) {
                char *v = x->type == V_NOMBRE ? dec_afficher(&x->nombre) : grym_dupliquer(nom_type(x->type));
                ok = echouer(diag, b, debut, grym_formater(
                    "Nombre de tours invalide : un entier positif ou nul est attendu, pas %s.", v));
                free(v);
            }
            break;
        }
        case I_SAUTER_SI_FAUX: {
            Valeur v = depiler(&pile);
            if (v.type != V_BOOLEEN) {
                ok = echouer(diag, b, debut, grym_formater(
                    "Condition ni vraie ni fausse : la valeur est %s.", nom_type(v.type)));
            } else if (!v.vrai) {
                cadre->ip = cible;
            }
            valeur_liberer(&v);
            break;
        }
        case I_LIRE_FICHIER: {
            Valeur c = depiler(&pile);
            if (c.type != V_TEXTE) {
                ok = echouer(diag, b, debut, grym_formater("Le chemin d'un fichier est un texte, pas %s.",
                                                           nom_type(c.type)));
                valeur_liberer(&c);
                break;
            }
            char *erreur = NULL;
            Fichier *f = lire_fichier(m, c.texte, &erreur);
            valeur_liberer(&c);
            if (!f) { ok = echouer(diag, b, debut, erreur); break; }
            Valeur v = valeur_nombre(dec_zero());
            v.type = V_FICHIER;
            v.fichier = f;
            empiler(&pile, v);
            break;
        }
        case I_ENREGISTRER: {
            Valeur c = depiler(&pile), v = depiler(&pile);
            char *probleme = NULL;
            if (v.type != V_FICHIER) probleme = grym_formater("Seul un fichier s'enregistre : la valeur est %s.",
                                                              nom_type(v.type));
            else if (c.type != V_TEXTE) probleme = grym_formater("Le chemin d'un fichier est un texte, pas %s.",
                                                                  nom_type(c.type));
            else if (!*c.texte) probleme = grym_dupliquer("Chemin de fichier vide.");
            if (!probleme) probleme = planifier_ecriture(m, c.texte, v.fichier);
            if (!probleme) v.fichier = NULL;   /* la référence passe à l'écriture */
            valeur_liberer(&c);
            valeur_liberer(&v);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_SAUVEGARDER:
        case I_SAUVEGARDER_COFFRE: {   /* « Sauvegarder la base dans « Cabinet.grymd » [avec le mot de passe m]. » (2.29, 2.30) */
            Valeur mdp = vi_absent(NULL);
            if (code == I_SAUVEGARDER_COFFRE) mdp = depiler(&pile);
            Valeur c = depiler(&pile);
            char *probleme = NULL;
            if (code == I_SAUVEGARDER_COFFRE && (mdp.type != V_TEXTE || !mdp.texte || !*mdp.texte))
                probleme = grym_dupliquer("Le mot de passe d'une sauvegarde est un texte, et pas vide.");
            if (c.type != V_TEXTE) probleme = grym_formater("Le chemin d'un fichier est un texte, pas %s.", nom_type(c.type));
            else if (!*c.texte) probleme = grym_dupliquer("Chemin de fichier vide.");
            else if (!m->base) probleme = grym_dupliquer("Ce programme n'a pas de base : il ne conserve aucune entité.");
            Fichier *f = NULL;
            char *libre = NULL;
            if (!probleme) {
                /* jamais par-dessus un fichier, ni deux fois le même chemin : « copie (2).grymd », « copie (3).grymd »… */
                const char *point = strrchr(c.texte, '.'), *barre = strrchr(c.texte, '/');
                const size_t radical = point && (!barre || point > barre) ? (size_t)(point - c.texte) : strlen(c.texte);
                for (int rang = 1; !libre && rang < 1000; rang++) {
                    char *essai = rang == 1 ? grym_dupliquer(c.texte)
                                            : grym_formater("%.*s (%d)%s", (int)radical, c.texte, rang, c.texte + radical);
                    char *chemin = chemin_ecriture(m, essai);
                    FILE *existe = grym_fopen(chemin, "rb");
                    int pris = existe != NULL;
                    if (existe) fclose(existe);
                    for (size_t k = 0; k < m->nb_a_ecrire && !pris; k++) pris = strcmp(m->a_ecrire[k].chemin, chemin) == 0;
                    free(chemin);
                    if (pris) free(essai);
                    else libre = essai;
                }
                if (!libre) probleme = grym_formater("« %s » : plus de mille sauvegardes de ce nom.", c.texte);
                else {
                    /* le dossier se crée et s'essaie dès maintenant : une sauvegarde impossible (un dossier synchronisé
                     * absent, un disque plein) se dit ici, où un « Essayer » la rattrape, pas à la fin de l'exécution */
                    char *chemin = chemin_ecriture(m, libre), *essai = grym_formater("%s.essai", chemin);
                    grym_creer_dossiers(chemin);
                    FILE *t = grym_fopen(essai, "wb");
                    if (t) { fclose(t); grym_supprimer(essai); }
                    else probleme = grym_formater("Sauvegarde impossible dans « %s » : le dossier ne s'écrit pas.", libre);
                    free(essai);
                    free(chemin);
                }
            }
            if (!probleme) {
                f = grym_allouer(sizeof *f);
                f->references = 1;
                f->format = NULL;
                f->base = NULL;
                f->cle = 0;
                f->octets = NULL;
                f->taille = 0;
                if (base_est_chiffree(m->base)) {   /* 2.37 : le coffre de format 2, avec les clés de la base */
                    if (code == I_SAUVEGARDER_COFFRE)
                        probleme = grym_dupliquer("La base est chiffrée : sa sauvegarde l'est d'office, avec ses clés. "
                                                  "Écris « Sauvegarder la base dans … » sans mot de passe.");
                    else f->octets = base_coffre(m->base, &f->taille, &probleme);
                } else f->octets = base_copie(m->base, &f->taille, &probleme);
                if (!probleme && code == I_SAUVEGARDER_COFFRE) {   /* le coffre : rien ne s'ouvre sans le mot de passe */
                    size_t n = 0;
                    unsigned char *ferme = coffre_fermer(f->octets, f->taille, mdp.texte, &n, &probleme);
                    memset(f->octets, 0, f->taille);
                    free(f->octets);
                    f->octets = ferme;
                    f->taille = ferme ? n : 0;
                }
                const char *barre = strrchr(libre, '/');
                f->nom = grym_dupliquer(barre ? barre + 1 : libre);
                if (!probleme) probleme = planifier_ecriture(m, libre, f);
                if (probleme) { free(f->octets); free(f->nom); free(f); }
            }
            free(libre);
            valeur_liberer(&c);
            valeur_liberer(&mdp);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_RESTAURER: {   /* « Restaurer la base depuis f [avec le mot de passe m]. » (2.30) */
            Valeur mdp = vi_absent(NULL);
            if (op) mdp = depiler(&pile);
            Valeur src = depiler(&pile);
            char *probleme = NULL;
            const unsigned char *o = NULL;
            size_t n = 0;
            Fichier *lu = NULL;
            if (!m->base) probleme = grym_dupliquer("Ce programme n'a pas de base : rien à restaurer.");
            else if (src.type == V_TEXTE) {
                lu = lire_fichier(m, src.texte ? src.texte : "", &probleme);
                if (lu) o = octets_du_fichier(lu, &probleme), n = lu->taille;
            } else if (src.type == V_FICHIER && src.fichier) {
                o = octets_du_fichier(src.fichier, &probleme);
                n = src.fichier->taille;
            } else if (src.type == V_ABSENT) probleme = grym_dupliquer("Choisis d'abord la sauvegarde à restaurer.");
            else probleme = grym_formater("On restaure depuis un chemin ou un fichier, pas %s.", nom_type(src.type));
            unsigned char *clair = NULL;
            size_t nc = 0;
            if (!probleme && o) {
                if (coffre_est(o, n)) {
                    if (!op || mdp.type != V_TEXTE) probleme = grym_dupliquer("Cette sauvegarde est chiffrée : il faut son mot de passe.");
                    else clair = coffre_ouvrir(o, n, mdp.texte, &nc, &probleme);
                } else if (coffre2_est(o, n)) {   /* 2.37 : une sauvegarde d'une base chiffrée, par ses propres clés */
                    const char *secret = op && mdp.type == V_TEXTE ? mdp.texte : m->mdp_base;
                    if (!secret) probleme = grym_dupliquer("Cette sauvegarde est chiffrée : il faut son mot de passe.");
                    else clair = coffre2_ouvrir(o, n, secret, &nc, NULL, NULL, NULL, &probleme);
                } else {
                    clair = malloc(n ? n : 1);
                    if (clair) { memcpy(clair, o, n); nc = n; }
                    else probleme = grym_dupliquer("Mémoire insuffisante pour lire la sauvegarde.");
                }
            }
            if (!probleme && clair && !base_verifier_copie(clair, nc, &probleme)) { /* probleme rempli */ }
            if (!probleme && clair && !m->chemin_base) probleme = grym_dupliquer("Cette base vit en mémoire : rien à remplacer.");
            if (!probleme && clair && base_est_chiffree(m->base)) {
                /* 2.37 : rien en clair sur le disque ; l'attente est un coffre de format 2, celui qu'on a choisi tel quel,
                 * sinon la copie enfermée sous les clés de la base ; elle s'ouvrira avec le mot de passe de sa copie */
                size_t nf = 0;
                unsigned char *ferme = coffre2_est(o, n) ? malloc(n ? n : 1) : base_coffre_depuis(m->base, clair, nc, &nf, &probleme);
                if (ferme && coffre2_est(o, n)) { memcpy(ferme, o, n); nf = n; }
                memset(clair, 0, nc);
                free(clair);
                clair = ferme;
                nc = nf;
                if (!clair && !probleme) probleme = grym_dupliquer("Mémoire insuffisante pour la restauration.");
            }
            if (!probleme && clair) {
                /* à côté de la base, en attente : elle prend sa place à la prochaine ouverture, l'ancienne gardée */
                char *absolu = chemin_absolu(m->chemin_base);
                char *attente = grym_formater("%s.restaurer", absolu ? absolu : m->chemin_base);
                free(absolu);
                grym_supprimer(attente);   /* une restauration demandée plus tôt est remplacée */
                Fichier *f = grym_allouer(sizeof *f);
                f->references = 1;
                f->format = NULL;
                f->base = NULL;
                f->cle = 0;
                f->octets = clair;
                f->taille = nc;
                const char *barre = strrchr(attente, '/');
                f->nom = grym_dupliquer(barre ? barre + 1 : attente);
                clair = NULL;
                probleme = planifier_ecriture(m, attente, f);
                if (probleme) { free(f->octets); free(f->nom); free(f); }
                free(attente);
            }
            if (clair) { memset(clair, 0, nc); free(clair); }
            if (lu) { free(lu->octets); free(lu->nom); free(lu); }
            valeur_liberer(&src);
            valeur_liberer(&mdp);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_CHANGER_MDP_BASE: {   /* « Changer le mot de passe de la base pour n, l'ancien étant a. » (2.37) */
            Valeur nouveau = depiler(&pile), ancien = depiler(&pile);
            char *probleme = NULL;
            if (!m->base) probleme = grym_dupliquer("Ce programme n'a pas de base : il ne conserve aucune entité.");
            else if (!base_est_chiffree(m->base))
                probleme = grym_dupliquer("Cette base n'est pas chiffrée : écris « La base est chiffrée. » en tête du programme.");
            else if (ancien.type != V_TEXTE || !ancien.texte || !*ancien.texte)
                probleme = grym_dupliquer("Donne d'abord le mot de passe actuel.");
            else if (nouveau.type != V_TEXTE || !nouveau.texte || !*nouveau.texte)
                probleme = grym_dupliquer("Le nouveau mot de passe est un texte, et pas vide.");
            else if (base_changer_mot_de_passe(m->base, ancien.texte, nouveau.texte, &probleme))
                machine_mot_de_passe_base(m, nouveau.texte);
            valeur_liberer(&nouveau);
            valeur_liberer(&ancien);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_EXPORTER: {   /* « Exporter les membres conservés … dans « m.csv ». » (§ 15.4) */
            Valeur c = depiler(&pile), l = depiler(&pile);
            char *probleme = NULL;
            if (c.type != V_TEXTE) probleme = grym_formater("Le chemin d'un fichier est un texte, pas %s.", nom_type(c.type));
            else if (!*c.texte) probleme = grym_dupliquer("Chemin de fichier vide.");
            else if (l.type != V_LISTE) probleme = grym_dupliquer("Export invalide.");
            Fichier *f = NULL;
            if (!probleme) {
                f = grym_allouer(sizeof *f);
                f->references = 1;
                f->format = NULL;
                f->base = NULL;
                f->cle = 0;
                f->octets = NULL;
                const char *barre = strrchr(c.texte, '/');
                f->nom = grym_dupliquer(barre ? barre + 1 : c.texte);
                probleme = tableau_csv(m, b->constantes[op].texte, &l, f);
                if (!probleme) probleme = planifier_ecriture(m, c.texte, f);
                if (probleme) { free(f->octets); free(f->nom); free(f); }
            }
            valeur_liberer(&c);
            valeur_liberer(&l);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_CHERCHER: {
            /* recherche dans la base (§ 16.4) : les valeurs comparées sont au sommet de la pile */
            long np = requete_parametres(b->constantes[op].texte);
            {   /* « Pour chaque genre de l'écran », « le nombre de genres de l'écran » : « parmi les genres » d'un objet en
                 * mémoire, jamais conservé (un écran) ; sa liste est dans son champ, pas en base (§ 22.1) */
                const char *rq = b->constantes[op].texte, *cond = strrchr(rq, '\x1f');   /* champs séparés par U+001F */
                const Valeur *p1 = np == 1 ? &pile.v[pile.n - 1] : NULL;
                char champ[256] = "";
                if (cond) {   /* « (M…1[genres]) », seule condition : le champ entre crochets */
                    const char *o = cond + 1;
                    while (*o == ' ') o++;
                    const char *g = o[0] == '(' && o[1] == 'M' ? strchr(o, '[') : NULL, *d = g ? strchr(g, ']') : NULL;
                    if (g && d && d[1] == ')' && !d[2] && (size_t)(d - g - 1) < sizeof champ) {
                        memcpy(champ, g + 1, (size_t)(d - g - 1));
                        champ[d - g - 1] = 0;
                    }
                }
                if (p1 && p1->type == V_OBJET && !p1->objet->id && *champ) {
                    const char *mode = strchr(rq, '\x1f');
                    const int compte = mode && atoi(mode + 1) == 2;
                    const long q = index_champ(p1->objet->classe, champ);
                    Valeur r;
                    if (q >= 0 && p1->objet->definis[q] && p1->objet->champs[q].type == V_LISTE) r = valeur_copier(&p1->objet->champs[q]);
                    else r = vi_liste(NULL, NULL, 0);
                    if (compte) {
                        char t[32];
                        snprintf(t, sizeof t, "%lu", (unsigned long)r.liste->n);
                        valeur_liberer(&r);
                        r = valeur_nombre(dec_depuis_canonique(t));
                    }
                    Valeur x = depiler(&pile);
                    valeur_liberer(&x);
                    empiler(&pile, r);
                    break;
                }
            }
            Valeur r;
            char *erreur = NULL;
            /* sans paramètre, la pile peut être vide : aucune adresse calculée sur un pointeur nul */
            int trouve = base_chercher(m->base, m, b->constantes[op].texte, np ? &pile.v[pile.n - (size_t)np] : NULL,
                                       (size_t)np, &r, &erreur);
            for (long q = 0; q < np; q++) {
                Valeur x = depiler(&pile);
                valeur_liberer(&x);
            }
            if (!trouve) { ok = echouer(diag, b, debut, erreur); break; }
            empiler(&pile, r);
            break;
        }
        case I_TAILLE_LISTE: {
            Valeur l = depiler(&pile);
            char t[32];
            size_t nb = l.type == V_LISTE || l.type == V_ENSEMBLE ? l.liste->n : 0;
            if (l.type == V_LIGNE && l.texte) for (const char *p = l.texte; *p; p++) nb += *p == '\x1e';   /* ses lignes */
            snprintf(t, sizeof t, "%lu", (unsigned long)nb);
            valeur_liberer(&l);
            empiler(&pile, valeur_nombre(dec_depuis_canonique(t)));
            break;
        }
        case I_ELEMENT: {
            /* un parcours de la base crée une coquille par objet : il ramasse aussi, sinon une boucle qui ne fait
             * que lire garderait en mémoire tout ce qu'elle a lu jusqu'à la fin de l'exécution (docs/vm.md, § 9) */
            if (m->depuis_ramassage >= m->seuil) ramasser(m, &pile, cadres, nb_cadres);
            if (pile.v[pile.n - 2].type == V_ENSEMBLE) {   /* « Pour chaque couleur parmi les couleurs » (§ 13.9) */
                Valeur rang = depiler(&pile), l = depiler(&pile);
                long k = -1;
                if (rang.type == V_NOMBRE) dec_en_jours(&rang.nombre, &k);
                int dedans = k >= 0 && (size_t)k < l.liste->n;
                Valeur r = dedans ? valeur_copier(&l.liste->valeurs[k]) : vi_absent(NULL);
                valeur_liberer(&rang);
                valeur_liberer(&l);
                if (!dedans) { valeur_liberer(&r); ok = echouer(diag, b, debut, grym_dupliquer("Rang hors de l'ensemble.")); break; }
                empiler(&pile, r);
                break;
            }
            if (pile.v[pile.n - 2].type == V_LIGNE) {   /* la ligne r d'un tableau : ses titres, puis ses cellules (§ 15.5) */
                Valeur rang = depiler(&pile), l = depiler(&pile);
                long k = -1;
                if (rang.type == V_NOMBRE) dec_en_jours(&rang.nombre, &k);
                const char *p = l.texte ? strchr(l.texte, '\x1e') : NULL;
                for (long q = 0; p && q < k; q++) p = strchr(p + 1, '\x1e');
                const char *fin = p ? strchr(p + 1, '\x1e') : NULL;
                Valeur r = vi_texte("");
                if (p && l.texte) {
                    free(r.texte);
                    r.texte = grym_formater("%.*s%.*s", (int)(strchr(l.texte, '\x1e') - l.texte), l.texte,
                                            (int)(fin ? fin - p : (long)strlen(p)), p);
                    r.type = V_LIGNE;
                }
                valeur_liberer(&rang);
                valeur_liberer(&l);
                if (r.type != V_LIGNE) { valeur_liberer(&r); ok = echouer(diag, b, debut, grym_dupliquer("Rang hors du tableau.")); break; }
                empiler(&pile, r);
                break;
            }
            Valeur rang = depiler(&pile), l = depiler(&pile);
            long k = -1;
            if (l.type == V_LISTE && rang.type == V_NOMBRE && dec_en_jours(&rang.nombre, &k)) {}
            char *erreur = NULL;
            Objet *o = k >= 0 && l.type == V_LISTE && (size_t)k < l.liste->n
                     ? machine_objet_en_base(m, l.liste->ids[k], l.liste->classes[k], &erreur) : NULL;
            valeur_liberer(&rang);
            valeur_liberer(&l);
            if (!o) {
                ok = echouer(diag, b, debut, erreur ? erreur : grym_dupliquer("Rang hors de la liste."));
                break;
            }
            empiler(&pile, vi_objet(o));
            break;
        }
        case I_CONSERVER:
        case I_SUPPRIMER:
        case I_SUPPRIMER_DEFINITIVEMENT:
        case I_RETABLIR: {
            Valeur v = depiler(&pile);
            const char *verbe = code == I_CONSERVER ? "conserve" : code == I_RETABLIR ? "rétablit" : "supprime";
            char *probleme = NULL;
            if (v.type != V_OBJET)
                probleme = grym_formater("Seul un objet se %s : la valeur est %s.", verbe, nom_type(v.type));
            else if (!v.objet->classe->conserve)
                probleme = grym_formater("« %s » n'est pas une entité : ses objets ne se %s pas. "
                                         "Déclare « %s %s, %s, a : ».", v.objet->classe->nom,
                                         code == I_CONSERVER ? "conservent" : code == I_RETABLIR ? "rétablissent" : "suppriment",
                                         v.objet->classe->feminin ? "Une" : "Un", v.objet->classe->nom,
                                         v.objet->classe->feminin ? "conservée" : "conservé");
            else if (code == I_CONSERVER && v.objet->id) {
                char *qui = article_classe(v.objet->classe);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s déjà conservé%s ne se conserve pas deux fois.", qui,
                                         v.objet->classe->feminin ? "e" : "");
                free(qui);
            } else if (code != I_CONSERVER && !v.objet->id) {
                char *qui = article_classe(v.objet->classe);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s qui n'est pas conservé%s ne se %s pas.", qui,
                                         v.objet->classe->feminin ? "e" : "", verbe);
                free(qui);
            } else if (code == I_CONSERVER) {
                /* un secret donné avant « Conserver » : la même règle qu'à l'écriture du champ (§ 22.17) */
                for (size_t q = 0; q < v.objet->classe->nb_champs && !probleme && !module->base_chiffree; q++)
                    if (type_secret(v.objet->classe->types[q]) && v.objet->definis[q] && v.objet->champs[q].type == V_TEXTE)
                        probleme = grym_formater("« %s » est un secret : il se range dans une base chiffrée. Écris « La base "
                                                 "est chiffrée. » dans le programme.", v.objet->classe->champs[q]);
                long id = 0;
                if (!probleme && base_conserver(m->base, v.objet, &id, &probleme)) ecrire_id(m, v.objet, id);
            } else if (code == I_RETABLIR) {
                if (charger(m, v.objet, &probleme)) base_retablir(m->base, v.objet, m->classes, m->nb_classes, &probleme);
            } else if (charger(m, v.objet, &probleme)) {   /* l'objet reste en mémoire, avec ses valeurs */
                /* simple : mis de côté ; définitive : effacé, et ceux qui disparaissent avec lui (§ 16.12) */
                base_supprimer(m->base, m, v.objet, m->classes, m->nb_classes, code == I_SUPPRIMER_DEFINITIVEMENT,
                               &probleme);
            }
            valeur_liberer(&v);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_DEMANDER: {
            /* « la réponse en nombre à « Âge ? » » (grammaire, § 17) : un formulaire d'un seul champ */
            Valeur q = depiler(&pile);
            if (q.type != V_TEXTE) {
                char *m2 = grym_formater("La question se pose en texte : la valeur est %s.", nom_type(q.type));
                valeur_liberer(&q);
                ok = echouer(diag, b, debut, m2);
                break;
            }
            Champ c;
            memset(&c, 0, sizeof c);
            c.libelle = q.texte;
            c.question = 1;
            c.type = b->noms[op];
            Demande d = { { m, 0, NULL }, &c, b->noms[op], vi_absent("réponse") };
            char *probleme = poser(m, sortie, cadres, &c, 1, valider_demande, &d, &d.a);
            free(c.ligne);
            valeur_liberer(&q);
            if (probleme) {
                valeur_liberer(&d.r);
                if (d.a.fatal) fatal = 1;
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, d.r);
            break;
        }
        case I_SAISIR: {
            /* « un nouveau client saisi » : l'objet neuf est au sommet, il y reste (grammaire, § 19) */
            Valeur *vo = &pile.v[pile.n - 1];
            if (vo->type != V_OBJET || !vo->objet->classe->conserve) {
                ok = echouer(diag, b, debut, grym_dupliquer("Seul un objet d'une entité se saisit."));
                break;
            }
            char *probleme = saisir(m, vo->objet, b->constantes[op].texte, sortie, cadres, &fatal, 0);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ECRAN_OUVRIR: {
            /* « Ouvrir l'écran X. » (grammaire, § 22.3) : l'écran se montre ; la boucle compilée attend ensuite */
            char *probleme = NULL;
            EcranVM *e = ecran_lire(m, b->constantes[op].texte, &probleme);
            if (e) {
                /* l'objet de l'écran : ses zones vivent le temps de l'écran, jamais conservées */
                const char *fin_nom = strchr(b->constantes[op].texte, '\x1f');
                char *nc = grym_formater("écran %.*s", (int)(fin_nom ? fin_nom - b->constantes[op].texte
                                                                   : (long)strlen(b->constantes[op].texte)),
                                         b->constantes[op].texte);
                const ClasseVM *ce = classe_vm(m, nc);
                free(nc);
                if (m->depuis_ramassage >= m->seuil) ramasser(m, &pile, cadres, nb_cadres);
                e->objet = ce ? creer_objet(m, ce, m->epoque) : NULL;
                e->dessous = m->ecran;   /* par-dessus l'écran ouvert, s'il y en a un (§ 22.3) */
                e->cadre = nb_cadres;
                m->ecran = e;
                /* un document (2.25) : son nom commence par « document » ; l'interface ne le voit jamais */
                e->document = strncmp(b->constantes[op].texte, "document ", 9) == 0;
                if (e->document) {
                    e->doc_cellules = grym_allouer((e->n ? e->n : 1) * sizeof *e->doc_cellules);
                    e->doc_lignes = grym_allouer((e->n ? e->n : 1) * sizeof *e->doc_lignes);
                    for (size_t k = 0; k < e->n; k++) { e->doc_cellules[k] = NULL; e->doc_lignes[k] = 0; }
                } else {
                    m->fermer_ecran = 0;
                    if (m->iface.ecran_taille_titre) m->iface.ecran_taille_titre(m->iface.contexte, e->taille_titre);
                    m->iface.ecran_ouvrir(m->iface.contexte, sortie, e->titre, e->elements, e->n);
                }
            }
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ECRAN_LISTE: {
            Valeur l = depiler(&pile);
            char *probleme = NULL;
            if (!m->ecran || op >= m->ecran->n || l.type != V_LISTE)
                probleme = grym_dupliquer("Liste d'écran invalide.");
            else
                probleme = ecran_montrer_liste(m, m->ecran, op, &l);
            valeur_liberer(&l);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ECRAN_ETAT: {
            /* « actif si … », « visible si … » (§ 22.6) : la condition, relue à chaque tour de l'écran */
            Valeur v = depiler(&pile);
            const size_t k = op / 2;
            char *probleme = NULL;
            if (!m->ecran || k >= m->ecran->n) probleme = grym_dupliquer("État d'écran invalide.");
            else if (v.type != V_BOOLEEN)
                probleme = grym_formater("« %s si » : la condition n'est ni vraie ni fausse.", op % 2 ? "visible" : "actif");
            else {
                const unsigned char bit = op % 2 ? 2 : 1;
                if (v.vrai) m->ecran->etats[k] &= (unsigned char)~bit;
                else m->ecran->etats[k] |= bit;
            }
            valeur_liberer(&v);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_SIGLES: {   /* les sigles du programme (§ 2.4), retenus pour les libellés */
            const char *t = b->constantes[op].texte;
            for (const char *p = t; *p; ) {
                const char *f = strchr(p, ' ');
                char *mot = grym_formater("%.*s", (int)(f ? f - p : (long)strlen(p)), p);
                if (*mot) sigle_retenir(mot);
                free(mot);
                p = f ? f + 1 : p + strlen(p);
            }
            break;
        }
        case I_ECRAN_EXIGER: {
            /* Les zones qu'un événement lit (§ 22.6) : toutes les obligatoires vides l'arrêtent d'un coup, chacune avec
             * son message, plutôt qu'une à une à leur lecture. */
            EcranVM *e = m->ecran;
            if (!e || !e->objet) break;
            const char *noms = b->constantes[op].texte;
            size_t *els = grym_allouer((e->n ? e->n : 1) * sizeof *els), nb = 0;
            char **msgs = grym_allouer((e->n ? e->n : 1) * sizeof *msgs);
            for (const char *p = noms; *p && nb < e->n;) {
                const char *f = strchr(p, '\x1e');
                char *nom = grym_formater("%.*s", (int)(f ? f - p : (long)strlen(p)), p);
                const long q = index_champ(e->objet->classe, nom);
                if (q >= 0 && !(e->objet->classe->uniques[q] & 2) && !(e->objet->classe->uniques[q] & 8) && !e->objet->definis[q])
                    for (size_t k = 0; k < e->n; k++)
                        if (e->elements[k].sorte == ELEMENT_ZONE && e->champ[k] == q && !e->elements[k].lecture_seule) {
                            els[nb] = k;
                            msgs[nb++] = grym_formater("Remplis « %s » : cette zone est obligatoire.", e->textes[k]);
                            break;
                        }
                free(nom);
                p = f ? f + 1 : p + strlen(p);
            }
            if (nb) {
                if (m->iface.ecran_erreurs_zones) m->iface.ecran_erreurs_zones(m->iface.contexte, els, (const char *const *)msgs, nb);
                char *resume;
                if (nb == 1) resume = grym_dupliquer(msgs[0]);
                else {
                    Chaine c = {0};
                    chaine_ajouter(&c, "Remplis les zones obligatoires : ");
                    for (size_t k = 0; k < nb; k++) {
                        char *x = grym_formater("%s« %s »", k ? (k + 1 == nb ? " et " : ", ") : "", e->textes[els[k]]);
                        chaine_ajouter(&c, x);
                        free(x);
                    }
                    chaine_ajouter(&c, ".");
                    resume = chaine_rendre(&c);
                }
                for (size_t k = 0; k < nb; k++) free(msgs[k]);
                free(msgs);
                free(els);
                ok = echouer(diag, b, debut, resume);
                break;
            }
            free(msgs);
            free(els);
            break;
        }
        case I_ENVOI_DEBUT:
            m->envoi_ouvert++;
            break;
        case I_ENVOI: {   /* « l'envoi à … avec : … et de : / du tableau : », « la lecture de … avec : » (2.42, 2.43) */
            const int lecture = (int)(op & 1), corps_t = (int)((op >> 1) & 3);
            const size_t nq = (op >> 3) & 63, nb = op >> 9;
            const size_t nc = corps_t == 1 ? 2 * nb : corps_t == 2 ? nb : 0;
            Valeur *qv = grym_allouer((2 * nq ? 2 * nq : 1) * sizeof *qv);
            Valeur *cv = grym_allouer((nc ? nc : 1) * sizeof *cv);
            for (size_t q = nc; q-- > 0;) cv[q] = depiler(&pile);
            for (size_t q = 2 * nq; q-- > 0;) qv[q] = depiler(&pile);
            Valeur u = depiler(&pile);
            if (m->envoi_ouvert > 0) m->envoi_ouvert--;
            char *probleme = NULL;
            Chaine corps = {0}, adresse = {0};
            if (u.type != V_TEXTE) probleme = grym_dupliquer("L'adresse d'un envoi est un texte : « https://… ».");
            else if (!adresse_envoyable(u.texte))
                probleme = grym_formater("Un envoi passe par https:// : « %s » enverrait en clair.", u.texte);
            if (!probleme) {
                chaine_ajouter(&adresse, u.texte);
                int premiere = !strchr(u.texte, '?');
                for (size_t q = 0; q < nq && !probleme; q++) {   /* les valeurs de l'adresse, encodées */
                    const Valeur *cle = &qv[2 * q], *v = &qv[2 * q + 1];
                    if (v->type == V_ABSENT) continue;
                    if (v->type != V_TEXTE && v->type != V_NOMBRE && v->type != V_BOOLEEN && v->type != V_DATE) {
                        probleme = grym_dupliquer("Une adresse porte des textes, des nombres, des dates, vrai ou faux.");
                        break;
                    }
                    chaine_ajouter(&adresse, premiere ? "?" : "&");
                    premiere = 0;
                    pourcent_encoder(&adresse, cle->texte);
                    chaine_ajouter(&adresse, "=");
                    char *t;
                    if (v->type == V_BOOLEEN) t = grym_dupliquer(v->vrai ? "true" : "false");
                    else if (v->type == V_NOMBRE) t = dec_canonique(&v->nombre);
                    else if (v->type == V_DATE) {
                        int an, mo, jo;
                        date_civile(v->jours, &an, &mo, &jo);
                        t = grym_formater("%04d-%02d-%02d", an, mo, jo);
                    } else t = grym_dupliquer(v->texte);
                    pourcent_encoder(&adresse, t);
                    free(t);
                }
                if (corps_t == 1) {   /* un objet JSON */
                    chaine_ajouter(&corps, "{");
                    for (size_t q = 0; q < nb && !probleme; q++) {
                        if (q) chaine_ajouter(&corps, ",");
                        json_ecrire_texte(&corps, cv[2 * q].texte);
                        chaine_ajouter(&corps, ":");
                        probleme = valeur_en_json(m, &cv[2 * q + 1], &corps);
                    }
                    chaine_ajouter(&corps, "}");
                } else if (corps_t == 2) {   /* une liste JSON */
                    chaine_ajouter(&corps, "[");
                    for (size_t q = 0; q < nb && !probleme; q++) {
                        if (q) chaine_ajouter(&corps, ",");
                        probleme = valeur_en_json(m, &cv[q], &corps);
                    }
                    chaine_ajouter(&corps, "]");
                }
            }
            for (size_t q = 0; q < 2 * nq; q++) valeur_liberer(&qv[q]);
            for (size_t q = 0; q < nc; q++) valeur_liberer(&cv[q]);
            free(qv);
            free(cv);
            valeur_liberer(&u);
            char *url = chaine_rendre(&adresse), *json = chaine_rendre(&corps);
            if (!probleme && !m->iface.envoyer)
                probleme = grym_dupliquer("Un envoi sur internet se fait dans l'atelier ou dans une application fabriquée.");
            if (!probleme) probleme = valider_jusqu_ici(m, sortie);   /* l'envoi valide ce qui précède, comme « Ouvrir » */
            long statut = 0;
            char *reponse = NULL;
            if (!probleme && !m->iface.envoyer(m->iface.contexte, lecture, url, lecture ? NULL : (json ? json : ""), &statut,
                                               &reponse, &probleme) && !probleme)
                probleme = grym_dupliquer("Le service n'a pas répondu.");
            if (!probleme && statut >= 400)
                probleme = grym_formater("Le service a répondu %ld : %.200s", statut, reponse ? reponse : "");
            free(url);
            free(json);
            if (probleme) { free(reponse); ok = echouer(diag, b, debut, probleme); break; }
            empiler(&pile, vi_texte_pris(reponse ? reponse : grym_dupliquer("")));
            break;
        }
        case I_CHAMP_JSON: {   /* « le champ « a.b » de r » (2.42) */
            Valeur r = depiler(&pile);
            const char *chemin = b->constantes[op].texte;
            if (r.type == V_ABSENT) { empiler(&pile, r); break; }
            if (r.type != V_TEXTE) {
                valeur_liberer(&r);
                ok = echouer(diag, b, debut, grym_dupliquer("« le champ « … » de » lit la réponse d'un envoi, un texte."));
                break;
            }
            char *illisible = NULL;
            Json *j = json_lire(r.texte, strlen(r.texte), &illisible);
            valeur_liberer(&r);
            if (!j) {
                free(illisible);
                ok = echouer(diag, b, debut, grym_formater("La réponse n'est pas du JSON : « %s » ne s'y lit pas.", chemin));
                break;
            }
            const Json *x = json_suivre(j, chemin);
            Valeur v;
            if (!x || x->type == JSON_NUL) v = vi_absent(NULL);
            else if (x->type == JSON_TEXTE || x->type == JSON_NOMBRE) v = vi_texte_pris(grym_dupliquer(x->texte ? x->texte : ""));
            else if (x->type == JSON_BOOLEEN) v = vi_texte_pris(grym_dupliquer(x->vrai ? "vrai" : "faux"));
            else {
                Chaine c = {0};
                json_ecrire(&c, x);
                v = vi_texte_pris(chaine_rendre(&c));
            }
            json_liberer(j);
            empiler(&pile, v);
            break;
        }
        case I_CODE_QR: {   /* « le code QR suisse de t » (2.27) : le texte, écrit comme Afficher, en image PNG */
            Valeur v = depiler(&pile);
            char *probleme = NULL;
            if (v.type == V_ABSENT) probleme = grym_dupliquer("Un code QR se fait d'un texte : celui-ci est absent.");
            unsigned char *png = NULL;
            size_t taille = 0;
            if (!probleme) {
                char *t = texte_valeur(m, &v);
                png = qr_png(t, (int)op, &taille, &probleme);
                free(t);
            }
            valeur_liberer(&v);
            if (probleme) { ok = echouer(diag, b, debut, probleme); break; }
            Valeur img = vi_fichier(png, taille, op ? "qr-facture.png" : "code-qr.png");
            free(png);
            empiler(&pile, img);
            break;
        }
        case I_DOCUMENT: {
            if (op == 2) {   /* « Ouvrir le justificatif du frais. » (2.26) : le fichier tel qu'il est rangé */
                Valeur v = depiler(&pile);
                char *probleme = NULL;
                if (v.type == V_ABSENT) probleme = grym_dupliquer("Rien à ouvrir : ce fichier est absent.");
                else if (v.type == V_TEXTE) {   /* une adresse web, dans le navigateur du système (2.41) */
                    if (strncmp(v.texte, "https://", 8) != 0 && strncmp(v.texte, "http://", 7) != 0)
                        probleme = grym_formater("« Ouvrir » montre une adresse web qui commence par https:// : « %s » n'en est pas une.",
                                                 v.texte);
                    else if (!m->iface.ouvrir_adresse)
                        probleme = grym_dupliquer("Une adresse web s'ouvre depuis l'atelier ou une application fabriquée.");
                    else m->iface.ouvrir_adresse(m->iface.contexte, v.texte);
                }
                else if (v.type != V_FICHIER) probleme = grym_dupliquer("« Ouvrir » montre un fichier ou une adresse web.");
                else if (!m->iface.montrer_document)
                    probleme = grym_dupliquer("Un fichier s'ouvre dans l'atelier ou dans une application fabriquée.");
                else {
                    const unsigned char *o = octets_du_fichier(v.fichier, &probleme);
                    if (o) m->iface.montrer_document(m->iface.contexte, o, v.fichier->taille, v.fichier->nom);
                }
                valeur_liberer(&v);
                if (probleme) ok = echouer(diag, b, debut, probleme);
                break;
            }
            /* « le document de quittance », « Ouvrir le document de quittance. » (2.25) : la page, puis le PDF */
            EcranVM *e = m->ecran;
            char *probleme = NULL;
            if (!e || !e->document) probleme = grym_dupliquer("Document invalide.");
            char *html = probleme ? NULL : document_html(e);
            if (e && e->document) { m->ecran = e->dessous; ecran_detruire(e); }
            unsigned char *pdf = NULL;
            size_t taille = 0;
            if (!probleme && !m->iface.rendre_pdf)
                probleme = grym_dupliquer("Un document se fabrique dans l'atelier ou dans une application fabriquée.");
            if (!probleme) {
                pdf = m->iface.rendre_pdf(m->iface.contexte, html, &taille, &probleme);
                if (!pdf && !probleme) probleme = grym_dupliquer("Le PDF du document n'a pas pu être fabriqué.");
            }
            free(html);
            if (!probleme && op == 1) {
                if (m->iface.montrer_document) m->iface.montrer_document(m->iface.contexte, pdf, taille, "document.pdf");
                else probleme = grym_dupliquer("Un document se montre dans l'atelier ou dans une application fabriquée.");
                free(pdf);
            } else if (!probleme) {
                Valeur v = vi_fichier(pdf, taille, "document.pdf");
                free(pdf);
                empiler(&pile, v);
            }
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ECRAN_TEXTE: {
            /* « le texte « Reçu de » suivi du nom du patient de la quittance » (§ 22.1, 2.24) : relu à chaque tour */
            Valeur v = depiler(&pile);
            if (m->ecran && op == 0xFFFF) {   /* le titre calculé (2.33) */
                char *t = texte_valeur(m, &v);
                valeur_liberer(&v);
                free(m->ecran->titre);
                m->ecran->titre = t;
                m->ecran->titre_calcule = 1;
                break;
            }
            if (!m->ecran || op >= m->ecran->n) {
                valeur_liberer(&v);
                ok = echouer(diag, b, debut, grym_dupliquer("Texte d'écran invalide."));
                break;
            }
            EcranVM *e = m->ecran;
            char *t = texte_valeur(m, &v);
            valeur_liberer(&v);
            if (e->elements[op].capitales == 2 && *t) {
                /* la section paiement (2.28) : des lignes de Swiss QR Code justes, ou rien (2.29) */
                char *probleme = qr_suisse_verifier(t);
                if (probleme) { free(t); ok = echouer(diag, b, debut, probleme); break; }
            }
            free(e->textes[op]);
            e->textes[op] = t;
            e->elements[op].texte = t;
            e->calcules[op] = 1;
            break;
        }
        case I_ECRAN_IMAGE: {
            /* « l'image du portrait du compositeur choisi de l'écran » (§ 22.8) : la valeur, relue à chaque tour */
            if (op % 2) { m->absent_permis = 1; break; }   /* élément × 2 + 1 : la source commence ; un objet absent la rend absente */
            m->absent_permis = 0;
            Valeur v = depiler(&pile);
            const size_t k = op / 2;
            char *probleme = NULL;
            if (!m->ecran || k >= m->ecran->n || !m->ecran->elements[k].calculee) probleme = grym_dupliquer("Image d'écran invalide.");
            else if (v.type != V_ABSENT && (v.type != V_FICHIER || !v.fichier || !v.fichier->format))
                probleme = grym_dupliquer("« l'image de » attend une image : « l'image de la photo du compositeur choisi de l'écran ».");
            else {
                EcranVM *e = m->ecran;
                valeur_liberer(&e->images[k]);
                e->images[k] = v;   /* l'écran la garde : ses octets vivent jusqu'au prochain tour */
                v = vi_absent(NULL);
                e->elements[k].octets = e->images[k].type == V_FICHIER ? e->images[k].fichier->octets : NULL;
                e->elements[k].taille = e->images[k].type == V_FICHIER ? e->images[k].fichier->taille : 0;
                e->elements[k].format = e->images[k].type == V_FICHIER ? e->images[k].fichier->format : NULL;
            }
            valeur_liberer(&v);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ECRAN_ATTENDRE: {
            /* Attendre valide ce qui précède : chaque événement sera sa propre transaction (charte, art. 7). */
            char *probleme = ouvrir_attente(m, sortie, cadres);
            if (!probleme && m->ecran && m->ecran->objet && m->iface.ecran_valeurs) {   /* les zones, telles qu'elles sont */
                EcranVM *e = m->ecran;
                char **v = grym_allouer((e->n ? e->n : 1) * sizeof *v);
                for (size_t k = 0; k < e->n; k++) {
                    v[k] = NULL;
                    if (e->elements[k].sorte != ELEMENT_ZONE || e->champ[k] < 0) continue;
                    const Objet *o = e->objet;
                    const size_t q = (size_t)e->champ[k];
                    if (!o->definis[q] || o->champs[q].type == V_ABSENT) v[k] = grym_dupliquer("");
                    else if (o->champs[q].type == V_OBJET) v[k] = nom_objet(m, o->champs[q].objet);
                    else if (o->champs[q].type == V_LISTE) {   /* des choix multiples : les clés cochées, U+001E entre elles */
                        Chaine c = {0};
                        const Liste *l = o->champs[q].liste;
                        for (size_t r = 0; r < l->n; r++) {
                            char *err = NULL;
                            Objet *x = machine_objet_en_base(m, l->ids[r], l->classes[r], &err);
                            free(err);
                            if (!x) continue;
                            char *nom = nom_objet(m, x);
                            if (c.n) chaine_ajouter(&c, "\x1e");
                            chaine_ajouter(&c, nom);
                            free(nom);
                        }
                        v[k] = chaine_rendre(&c);
                    }
                    else if (o->champs[q].type == V_BOOLEEN) v[k] = grym_dupliquer(o->champs[q].vrai ? "oui" : "non");
                    else v[k] = texte_valeur(m, &o->champs[q]);
                }
                m->iface.ecran_valeurs(m->iface.contexte, (const char *const *)v, e->n);
                for (size_t k = 0; k < e->n; k++) free(v[k]);
                free(v);
            }
            if (!probleme && m->ecran && m->iface.ecran_etats) m->iface.ecran_etats(m->iface.contexte, m->ecran->etats, m->ecran->n);
            for (size_t k = 0; !probleme && m->ecran && m->iface.ecran_texte && k < m->ecran->n; k++)   /* 2.24 */
                if (m->ecran->calcules[k]) m->iface.ecran_texte(m->iface.contexte, k, m->ecran->textes[k]);
            if (!probleme && m->ecran && m->ecran->titre_calcule && m->iface.ecran_texte)   /* le titre calculé (2.33) */
                m->iface.ecran_texte(m->iface.contexte, (size_t)-1, m->ecran->titre ? m->ecran->titre : "");
            for (size_t k = 0; !probleme && m->ecran && m->iface.ecran_image && k < m->ecran->n; k++)   /* § 22.8 */
                if (m->ecran->elements[k].calculee)
                    m->iface.ecran_image(m->iface.contexte, k, m->ecran->elements[k].octets, m->ecran->elements[k].taille,
                                         m->ecran->elements[k].format);
            Evenement ev = { EVENEMENT_FERMETURE, 0, 0, NULL, NULL, 0, NULL, 0 };
            Issue issue = ISSUE_REPONDU;
            if (!probleme && !m->fermer_ecran)
                for (;;) {   /* un geste sur un élément inactif, caché ou en lecture seule ne compte pas (§ 22.6) */
                    issue = m->iface.ecran_attendre(m->iface.contexte, sortie, &ev);
                    if (issue != ISSUE_REPONDU || ev.sorte == EVENEMENT_FERMETURE || !m->ecran || ev.element >= m->ecran->n)
                        break;
                    const int refuse = m->ecran->etats[ev.element]
                                    || (ev.sorte == EVENEMENT_CHANGEMENT && m->ecran->elements[ev.element].lecture_seule);
                    if (!refuse) break;
                }
            m->fermer_ecran = 0;
            char *reprise = probleme ? NULL : fermer_attente(m);
            if (!probleme) probleme = reprise;
            if (!probleme && issue != ISSUE_REPONDU) {
                probleme = grym_dupliquer("Interrompu (Ctrl+C).");
                fatal = 1;
            }
            /* la ligne choisie de chaque liste : « le compositeur choisi de l'écran » */
            for (size_t k = 0; !probleme && m->ecran && m->ecran->objet && k < m->ecran->n; k++) {
                EcranVM *e = m->ecran;
                if (e->elements[k].sorte != ELEMENT_LISTE || e->champ[k] < 0) continue;
                long r = ev.choisies && k < ev.nb_choisies ? ev.choisies[k] : -1;
                Valeur v = vi_absent(NULL);
                if (r >= 0 && e->listes[k].type == V_LISTE && (size_t)r < e->listes[k].liste->n) {
                    Objet *o = machine_objet_en_base(m, e->listes[k].liste->ids[r], e->listes[k].liste->classes[r], &probleme);
                    if (o) { valeur_liberer(&v); v = vi_objet(o); }
                }
                ecrire_champ(m, e->objet, (size_t)e->champ[k], v);
            }
            Valeur objet = vi_absent(NULL);
            long code_ev = 0;
            if (!probleme && ev.sorte == EVENEMENT_CHANGEMENT && ev.element < m->ecran->n
                && m->ecran->elements[ev.element].sorte == ELEMENT_ZONE && m->ecran->champ[ev.element] >= 0 && m->ecran->objet) {
                /* une zone validée : sa valeur, lue selon son type ; refusée, l'écran le dit et rien ne s'exécute */
                EcranVM *e = m->ecran;
                const char *type = e->types[ev.element], *t = ev.texte ? ev.texte : "";
                const ClasseVM *lie = classe_vm(m, type);
                Valeur v;
                char *refus = NULL;
                int lu = 0;
                while (*t == ' ') t++;
                /* une unité tapée avec la valeur (« 12 CHF ») est ôtée : la zone l'affiche déjà (§ 22.6) */
                char *t_sans = e->elements[ev.element].unite ? sans_unite(t, e->elements[ev.element].unite) : NULL;
                if (t_sans) t = t_sans;
                if (e->elements[ev.element].multiple && lie && lie->conserve) {
                    /* des choix multiples : chaque clé cochée désigne un objet ; ensemble, la valeur de la zone (§ 22.1) */
                    const char *cle = champ_cle(lie);
                    long *ids = grym_allouer(64 * sizeof *ids);
                    char **cls = grym_allouer(64 * sizeof *cls);
                    size_t n = 0, cap = 64;
                    for (const char *p = t; *p && !probleme && !refus;) {
                        const char *f = strchr(p, '\x1e');
                        if (!f) f = p + strlen(p);
                        char *une = grym_formater("%.*s", (int)(f - p), p);
                        Valeur cherche = vi_texte(une), l;
                        if (*une && cle && chercher_valeur(m, lie->nom, 0, cle, &cherche, &l, &probleme)) {
                            if (l.type == V_LISTE && l.liste->n) {
                                if (n == cap) { cap *= 2; ids = realloc(ids, cap * sizeof *ids); cls = realloc(cls, cap * sizeof *cls); }
                                ids[n] = l.liste->ids[0];
                                cls[n++] = grym_dupliquer(l.liste->classes[0]);
                            } else refus = grym_formater("Aucun %s conservé n'a « %s » pour %s.", lie->nom, une, cle);
                            valeur_liberer(&l);
                        }
                        valeur_liberer(&cherche);
                        free(une);
                        p = *f ? f + 1 : f;
                    }
                    if (refus || probleme) { for (size_t r = 0; r < n; r++) free(cls[r]); free(ids); free(cls); }
                    else { v = vi_liste(ids, cls, n); lu = 1; }
                    if (lu) {   /* pas de contrôle de type : une liste n'est pas une valeur de champ ordinaire */
                        ecrire_champ(m, e->objet, (size_t)e->champ[ev.element], v);
                        code_ev = (long)ev.element + 1;
                        goto zone_lue;
                    }
                }
                else if (!*t && e->elements[ev.element].facultatif) { v = vi_absent(NULL); lu = 1; }
                else if (type_fichier(type)) {
                    /* une zone de fichier ou d'image : le fichier choisi, ses octets et son nom ; une image se vérifie
                     * ensuite, comme pour un champ (« … n'est pas une image ») */
                    if (ev.octets) { v = vi_fichier(ev.octets, ev.taille, t); lu = 1; }
                    else refus = grym_dupliquer("Choisis un fichier.");
                } else if (lie && lie->conserve) {
                    Valeur cherche = vi_texte(t), l;
                    const char *cle = champ_cle(lie);
                    if (cle && chercher_valeur(m, lie->nom, 0, cle, &cherche, &l, &probleme)) {
                        if (l.type == V_LISTE && l.liste->n) {
                            Objet *x = machine_objet_en_base(m, l.liste->ids[0], l.liste->classes[0], &probleme);
                            if (x) { v = vi_objet(x); lu = 1; }
                        } else {
                            refus = grym_formater("Aucun %s conservé n'a « %s » pour %s.", lie->nom, t, cle);
                        }
                        valeur_liberer(&l);
                    } else if (!cle) refus = grym_formater("« %s » n'a pas de champ texte unique pour le désigner.", lie->nom);
                    valeur_liberer(&cherche);
                } else {
                    lu = lire_reponse(type, t, &v, &refus);
                }
                if (lu && !refus) {
                    char *mauvais = verifier_type_champ(m, e->objet->classe->champs[e->champ[ev.element]], type, &v,
                                                        e->elements[ev.element].facultatif,
                                                        e->objet->classe->valeurs[e->champ[ev.element]]);
                    if (mauvais) { refus = mauvais; valeur_liberer(&v); lu = 0; }
                }
                if (!probleme && lu && !refus && v.type != V_ABSENT)   /* les bornes de la zone (§ 22.6) */
                    refus = hors_bornes(&e->elements[ev.element], &v);
                if (refus && lu) { valeur_liberer(&v); lu = 0; }
                if (probleme) {}
                else if (!lu) {
                    const char *msg = refus ? refus : "Valeur refusée.";
                    if (m->iface.ecran_erreurs_zones) {   /* la zone en faute porte aussi le message (§ 22.6) */
                        const size_t el1 = ev.element;
                        m->iface.ecran_erreurs_zones(m->iface.contexte, &el1, &msg, 1);
                    }
                    if (m->iface.ecran_erreur) m->iface.ecran_erreur(m->iface.contexte, sortie, msg);
                    free(refus);
                    code_ev = -1;   /* aucun événement ne s'exécute */
                } else {
                    ecrire_champ(m, e->objet, (size_t)e->champ[ev.element], v);
                    transcrire_si_besoin(m, e->objet, (size_t)e->champ[ev.element]);   /* « transcrite du mémo » (§ 15.8) */
                    code_ev = (long)ev.element + 1;
                }
            zone_lue:
                free(t_sans);
            }
            if (!probleme && code_ev == 0 && ev.sorte == EVENEMENT_CLIC && ev.element < m->ecran->n) {
                code_ev = (long)ev.element + 1;
            } else if (!probleme && ev.sorte == EVENEMENT_CHOIX && ev.element < m->ecran->n
                       && m->ecran->listes[ev.element].type == V_LISTE && ev.ligne < m->ecran->listes[ev.element].liste->n) {
                const Liste *l = m->ecran->listes[ev.element].liste;
                Objet *o = machine_objet_en_base(m, l->ids[ev.ligne], l->classes[ev.ligne], &probleme);
                if (o) { valeur_liberer(&objet); objet = vi_objet(o); code_ev = (long)ev.element + 1; }
            } else if (!probleme && code_ev == 0 && ev.sorte != EVENEMENT_FERMETURE) {
                code_ev = -1;   /* un événement qui ne désigne rien (ligne disparue entre-temps) : rien ne s'exécute */
            }
            if (probleme) { valeur_liberer(&objet); ok = echouer(diag, b, debut, probleme); break; }
            char num[24];
            snprintf(num, sizeof num, "%ld", code_ev);
            if (code_ev == -1) snprintf(num, sizeof num, "-1");
            empiler(&pile, objet);
            empiler(&pile, valeur_nombre(dec_depuis_canonique(num)));
            break;
        }
        case I_ECRAN_ERREUR: {
            /* un événement raté : rien de ce qu'il a fait n'est écrit ; l'écran reste ouvert, avec le motif,
             * même si l'événement avait demandé « Fermer l'écran. » avant d'échouer (une règle violée, § 16.14) */
            m->fermer_ecran = 0;
            Valeur motif = depiler(&pile);
            char *t = texte_valeur(m, &motif);
            if (m->iface.ecran_erreur) m->iface.ecran_erreur(m->iface.contexte, sortie, t);
            free(t);
            valeur_liberer(&motif);
            break;
        }
        case I_ECRAN_FERMER:
            if (m->ecran) {   /* l'écran du dessus se ferme ; celui du dessous reprend (§ 22.3) */
                EcranVM *d = m->ecran->dessous;
                m->iface.ecran_fermer(m->iface.contexte, sortie);
                ecran_detruire(m->ecran);
                m->ecran = d;
            }
            break;
        case I_FICHE_ECRAN: {
            /* « Ouvrir la fiche de c. » : l'écran déduit de l'entité (grammaire, § 22.3) */
            Valeur vo = depiler(&pile);
            char *probleme = NULL;
            if (vo.type == V_ABSENT) probleme = message_absent(&vo);
            else if (vo.type != V_OBJET || !vo.objet->classe->conserve)
                probleme = grym_dupliquer("Seul un objet d'une entité a une fiche.");
            else
                probleme = fiche_ecran(m, vo.objet, sortie, cadres, &fatal, 0);
            valeur_liberer(&vo);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_CORRESPOND: {   /* « … correspond au code du membre » : le clair contre l'empreinte (§ 16.1) */
            Valeur e = depiler(&pile), x = depiler(&pile);
            const int vrai = e.type == V_TEXTE && x.type == V_TEXTE && empreinte_verifier(x.texte, e.texte);
            valeur_liberer(&e);
            valeur_liberer(&x);
            empiler(&pile, valeur_booleen(vrai));
            break;
        }
        case I_CONTIENT: {   /* « le nom contient « mül » » (§ 4.5) */
            static const char *const VERBES[] = { "contient", "commence par", "finit par" };
            Valeur vp = depiler(&pile), vt = depiler(&pile);
            char *probleme = NULL;
            if (vt.type == V_ABSENT || vp.type == V_ABSENT)
                probleme = message_absent(vt.type == V_ABSENT ? &vt : &vp);
            else if (vt.type != V_TEXTE || vp.type != V_TEXTE)
                probleme = grym_formater("« %s » compare deux textes, pas %s.", VERBES[op < 3 ? op : 0],
                                         nom_type(vt.type != V_TEXTE ? vt.type : vp.type));
            int r = probleme ? 0 : texte_contient(vt.texte ? vt.texte : "", vp.texte ? vp.texte : "", "cdf"[op < 3 ? op : 0]);
            valeur_liberer(&vt);
            valeur_liberer(&vp);
            if (probleme) {
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, vi_booleen(r));
            break;
        }
        case I_CONVERTIR: {   /* « le nom en majuscules », « le code en nombre » (§ 4.5) */
            static const char *const CIBLES[] = { "majuscules", "minuscules", "nombre entier", "nombre", "date", "année",
                                                  "vrai ou faux", "heure", "toutes lettres", "format technique", "IBAN" };
            static const char *const NOMS[] = { "", "", "un nombre entier", "un nombre", "une date", "une année",
                                                "vrai ou faux", "une heure", "", "", "" };
            (void)CIBLES;
            if (op == 10) {
                /* « l'IBAN du cabinet en IBAN » (2.29) : sans espaces, en capitales, et vérifié (ISO 13616) : deux
                 * lettres de pays, deux chiffres de clé, de 15 à 34 lettres et chiffres en tout, la clé mod 97 = 1 */
                Valeur vt = depiler(&pile);
                if (vt.type != V_TEXTE) {
                    char *mp = vt.type == V_ABSENT ? message_absent(&vt)
                             : grym_formater("« en IBAN » s'applique à un texte, pas à %s.", nom_type(vt.type));
                    valeur_liberer(&vt);
                    ok = echouer(diag, b, debut, mp);
                    break;
                }
                char x[64];
                size_t n = 0;
                int mauvais = 0;
                for (const unsigned char *p = (const unsigned char *)(vt.texte ? vt.texte : ""); *p && !mauvais; p++) {
                    if (*p == ' ') continue;
                    if (p[0] == 0xC2 && p[1] == 0xA0) { p++; continue; }                         /* espace insécable */
                    if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xAF) { p += 2; continue; }      /* fine insécable */
                    unsigned char ch = *p >= 'a' && *p <= 'z' ? (unsigned char)(*p - 32) : *p;
                    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) || n >= 40) mauvais = 1;
                    else x[n++] = (char)ch;
                }
                x[n] = 0;
                char *probleme = NULL;
                if (mauvais || n < 15 || n > 34)
                    probleme = grym_formater("« %s » n'est pas un IBAN : de 15 à 34 lettres et chiffres, espaces permises.", vt.texte);
                else if (!(x[0] >= 'A' && x[0] <= 'Z' && x[1] >= 'A' && x[1] <= 'Z' && x[2] >= '0' && x[2] <= '9' && x[3] >= '0' && x[3] <= '9'))
                    probleme = grym_formater("« %s » n'est pas un IBAN : il commence par les deux lettres du pays et les "
                                             "deux chiffres de sa clé (CH52…).", vt.texte);
                else {
                    unsigned reste = 0;
                    for (size_t q = 0; q < n; q++) {
                        const char ch = x[(q + 4) % n];   /* le pays et la clé passent à la fin */
                        if (ch >= 'A') reste = (reste * 100 + (unsigned)(ch - 'A' + 10)) % 97;
                        else reste = (reste * 10 + (unsigned)(ch - '0')) % 97;
                    }
                    if (reste != 1)
                        probleme = grym_formater("« %s » n'est pas un IBAN : sa clé de contrôle ne correspond pas "
                                                 "(une faute de frappe ?).", vt.texte);
                }
                valeur_liberer(&vt);
                if (probleme) { ok = echouer(diag, b, debut, probleme); break; }
                empiler(&pile, vi_texte(x));
                break;
            }
            if (op == 9) {
                /* « le montant en format technique » (2.27) : l'écriture qu'attend une machine (code QR suisse, XML) ;
                 * un nombre : « 1590.00 », sans séparateur de milliers, le point décimal, les décimales gardées ; une
                 * date : « 2026-10-20 » ; un texte : sans ses espaces (« CH44 3199 … » devient « CH443199… ») */
                Valeur vt = depiler(&pile);
                if (vt.type == V_ABSENT) {
                    char *mp = message_absent(&vt);
                    valeur_liberer(&vt);
                    ok = echouer(diag, b, debut, mp);
                    break;
                }
                char *t = texte_valeur(m, &vt);
                Chaine c = {0};
                if (vt.type == V_DATE && strlen(t) == 10) {
                    char iso[11];
                    snprintf(iso, sizeof iso, "%.4s-%.2s-%.2s", t + 6, t + 3, t);
                    chaine_ajouter(&c, iso);
                } else {
                    const int nombre = vt.type == V_NOMBRE;
                    for (const char *p = t; *p; p++) {
                        if (*p == ' ' && !nombre) continue;
                        if (nombre && *p == '\'') continue;
                        if (nombre && (unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x88 && (unsigned char)p[2] == 0x92) {
                            chaine_ajouter(&c, "-");   /* le signe moins typographique */
                            p += 2;
                            continue;
                        }
                        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0 && !nombre) { p++; continue; }
                        char u[2] = { nombre && *p == ',' ? '.' : *p, 0 };
                        chaine_ajouter(&c, u);
                    }
                }
                free(t);
                valeur_liberer(&vt);
                char *rendu = chaine_rendre(&c);
                empiler(&pile, vi_texte(rendu ? rendu : ""));
                free(rendu);
                break;
            }
            if (op == 8) {   /* « l'échéance en toutes lettres » : « vendredi 16 octobre 2026 », « 1er » le premier (§ 14.8) */
                Valeur vd = depiler(&pile);
                if (vd.type != V_DATE) {
                    char *mp = vd.type == V_ABSENT ? message_absent(&vd)
                             : grym_formater("« en toutes lettres » s'applique à une date, pas à %s.", nom_type(vd.type));
                    valeur_liberer(&vd);
                    ok = echouer(diag, b, debut, mp);
                    break;
                }
                static const char *const MOIS[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
                                                    "septembre", "octobre", "novembre", "décembre" };
                int a, mo, j;
                date_civile(vd.jours, &a, &mo, &j);
                char *t = j == 1 ? grym_formater("%s 1er %s %d", JOURS_SEMAINE[jour_semaine(vd.jours)], MOIS[mo - 1], a)
                                 : grym_formater("%s %d %s %d", JOURS_SEMAINE[jour_semaine(vd.jours)], j, MOIS[mo - 1], a);
                valeur_liberer(&vd);
                empiler(&pile, vi_texte(t));
                free(t);
                break;
            }
            Valeur vx = depiler(&pile);
            char *probleme = NULL;
            Valeur r = valeur_nombre(dec_zero());
            if (op > 7) op = 0;
            if (vx.type == V_ABSENT)
                probleme = message_absent(&vx);
            else if (vx.type != V_TEXTE)
                probleme = grym_formater("« en %s » s'applique à un texte, pas à %s.", CIBLES[op], nom_type(vx.type));
            else if (op <= 1) {
                valeur_liberer(&r);
                char *t = op == 0 ? texte_majuscules(vx.texte ? vx.texte : "") : texte_minuscules(vx.texte ? vx.texte : "");
                r = vi_texte(t);
                free(t);
            } else {
                const char *s = vx.texte ? vx.texte : "";
                const char *p = s;
                while (*p == ' ' || *p == '\t') p++;
                if (!*p) probleme = grym_formater("Un texte vide n'est pas %s.", NOMS[op]);
                else {
                    valeur_liberer(&r);
                    if (!lire_reponse(CIBLES[op], s, &r, &probleme)) r = valeur_nombre(dec_zero());
                }
            }
            valeur_liberer(&vx);
            if (probleme) {
                valeur_liberer(&r);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, r);
            break;
        }
        case I_RESTE: {   /* « le reste de 17 par 5 » (§ 3.5) */
            Valeur vb = depiler(&pile), va = depiler(&pile);
            char *probleme = NULL;
            Decimal r = dec_zero();
            if (va.type == V_ABSENT || vb.type == V_ABSENT)
                probleme = message_absent(va.type == V_ABSENT ? &va : &vb);
            else if (va.type != V_NOMBRE || vb.type != V_NOMBRE)
                probleme = grym_formater("Le reste se calcule entre deux nombres, pas avec %s.",
                                         nom_type(va.type != V_NOMBRE ? va.type : vb.type));
            else {
                dec_liberer(&r);
                StatutDecimal st = dec_reste(&va.nombre, &vb.nombre, &r);
                if (st != DEC_OK) probleme = message_statut(st);
            }
            valeur_liberer(&va);
            valeur_liberer(&vb);
            if (probleme) {
                dec_liberer(&r);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, valeur_nombre(r));
            break;
        }
        case I_ECART: {   /* « le nombre de mois entre d1 et d2 » (§ 14.6) */
            if (op == 6) {   /* jours ouvrables : du lundi au vendredi, d1 exclu, d2 compris ; négatif à rebours */
                Valeur w2 = depiler(&pile), w1 = depiler(&pile);
                if (w1.type != V_DATE || w2.type != V_DATE) {
                    char *mp = w1.type == V_ABSENT || w2.type == V_ABSENT ? message_absent(w1.type == V_ABSENT ? &w1 : &w2)
                             : grym_formater("Un écart se mesure entre deux dates, pas avec %s.", nom_type(w1.type != V_DATE ? w1.type : w2.type));
                    valeur_liberer(&w1);
                    valeur_liberer(&w2);
                    ok = echouer(diag, b, debut, mp);
                    break;
                }
                const long d1 = w1.jours < w2.jours ? w1.jours : w2.jours, d2 = w1.jours < w2.jours ? w2.jours : w1.jours;
                long n = 0;
                for (long d = d1 + 1; d <= d2; d++) n += jour_semaine(d) < 5;
                empiler(&pile, valeur_nombre(dec_depuis_long(w1.jours <= w2.jours ? n : -n)));
                break;
            }
            Valeur v2 = depiler(&pile), v1 = depiler(&pile);
            char *mp = NULL;
            long r = 0;
            if (v1.type == V_ABSENT || v2.type == V_ABSENT) mp = message_absent(v1.type == V_ABSENT ? &v1 : &v2);
            else if (op >= 4) {   /* minutes, heures entre deux heures (§ 14.7) */
                if (v1.type != V_HEURE || v2.type != V_HEURE)
                    mp = grym_formater("Des %s se comptent entre deux heures, pas avec %s.", op == 4 ? "minutes" : "heures",
                                       nom_type(v1.type != V_HEURE ? v1.type : v2.type));
                else r = op == 4 ? v2.jours - v1.jours : (v2.jours - v1.jours) / 60;
            }
            else if (v1.type != V_DATE || v2.type != V_DATE)
                mp = grym_formater("Un écart se mesure entre deux dates, pas avec %s.", nom_type(v1.type != V_DATE ? v1.type : v2.type));
            else if (op <= 1) {
                long j = v2.jours - v1.jours;
                r = op == 0 ? j : j / 7;   /* semaines entières, vers zéro */
            } else {
                /* mois entiers : le plus grand n tel que d1 + n mois ne dépasse pas d2 (ou, vers le passé, l'inverse) */
                int a1, m1, j1, a2, m2, j2;
                date_civile(v1.jours, &a1, &m1, &j1);
                date_civile(v2.jours, &a2, &m2, &j2);
                long n = (long)(a2 - a1) * 12 + (m2 - m1), d = 0;
                if (n > 0 && date_ajouter_mois(v1.jours, n, &d) && d > v2.jours) n--;
                else if (n < 0 && date_ajouter_mois(v1.jours, n, &d) && d < v2.jours) n++;
                r = op == 2 ? n : n / 12;
            }
            valeur_liberer(&v1);
            valeur_liberer(&v2);
            if (mp) { ok = echouer(diag, b, debut, mp); break; }
            empiler(&pile, valeur_nombre(dec_depuis_long(r)));
            break;
        }
        case I_HASARD: {   /* « un nombre au hasard de 1 à 6 », « un élément au hasard parmi les invités » (§ 3.6) */
            if (op) {
                Valeur e = depiler(&pile);
                char *mp = NULL;
                if (e.type != V_ENSEMBLE) mp = grym_formater("Un élément au hasard se tire d'un ensemble, pas de %s.", nom_type(e.type));
                else if (!e.liste->n) mp = grym_dupliquer("Un élément au hasard ne se tire pas d'un ensemble vide.");
                Valeur r = mp ? vi_absent(NULL) : valeur_copier(&e.liste->valeurs[hasard_jusqua(m, e.liste->n)]);
                valeur_liberer(&e);
                if (mp) { valeur_liberer(&r); ok = echouer(diag, b, debut, mp); break; }
                empiler(&pile, r);
                break;
            }
            Valeur vh = depiler(&pile), vb = depiler(&pile);
            char *mp = NULL;
            long bas = 0, haut = 0;
            if (vb.type == V_ABSENT || vh.type == V_ABSENT) mp = message_absent(vb.type == V_ABSENT ? &vb : &vh);
            else if (vb.type != V_NOMBRE || vh.type != V_NOMBRE || !dec_en_long_borne(&vb.nombre, -1000000000L, 1000000000L, &bas)
                     || !dec_en_long_borne(&vh.nombre, -1000000000L, 1000000000L, &haut))
                mp = grym_dupliquer("Un nombre au hasard se tire entre deux nombres entiers (au plus un milliard).");
            valeur_liberer(&vb);
            valeur_liberer(&vh);
            if (mp) { ok = echouer(diag, b, debut, mp); break; }
            if (bas > haut) { long x = bas; bas = haut; haut = x; }
            long r = bas + (long)hasard_jusqua(m, (uint64_t)(haut - bas) + 1);
            empiler(&pile, valeur_nombre(dec_depuis_long(r)));
            break;
        }
        case I_EXEMPLE: {   /* « Exemple : le carré de 7 vaut 49. » (§ 9.6) */
            Valeur attendu = depiler(&pile), obtenu = depiler(&pile);
            int juste = valeurs_identiques(&obtenu, &attendu);
            if (!juste && obtenu.type == V_ANNEE && attendu.type == V_NOMBRE) {   /* une année et son nombre */
                long n = 0;
                juste = dec_en_long_borne(&attendu.nombre, 1, 9999, &n) && n == obtenu.jours;
            }
            m->exemples++;
            chaine_ajouter(sortie, juste ? "✓ " : "✗ ");
            chaine_ajouter(sortie, b->constantes[op].texte);
            if (!juste) {
                m->exemples_rates++;
                char *x = texte_valeur(m, &obtenu);
                chaine_ajouter(sortie, " ; obtenu : ");
                chaine_ajouter(sortie, obtenu.type == V_TEXTE ? "« " : "");
                chaine_ajouter(sortie, x);
                chaine_ajouter(sortie, obtenu.type == V_TEXTE ? " »" : "");
                free(x);
            }
            chaine_ajouter(sortie, "\n");
            valeur_liberer(&attendu);
            valeur_liberer(&obtenu);
            break;
        }
        case I_EXEMPLE_RATE: {
            Valeur motif = depiler(&pile);
            m->exemples++;
            m->exemples_rates++;
            chaine_ajouter(sortie, "✗ ");
            chaine_ajouter(sortie, b->constantes[op].texte);
            chaine_ajouter(sortie, " ; erreur : ");
            chaine_ajouter(sortie, motif.type == V_TEXTE && motif.texte ? motif.texte : "");
            chaine_ajouter(sortie, "\n");
            valeur_liberer(&motif);
            break;
        }
        case I_ENSEMBLE: {   /* « « rouge », « vert » et « bleu » » (§ 13.9) : les doublons tombent, l'ordre reste */
            Valeur e = ensemble_vide();
            size_t n = op, base_pile = pile.n - n;
            for (size_t k = 0; k < n; k++) {
                Valeur x = pile.v[base_pile + k];
                if (x.type == V_ABSENT) {
                    char *mp = message_absent(&x);
                    for (size_t q = k; q < n; q++) valeur_liberer(&pile.v[base_pile + q]);
                    pile.n = base_pile;
                    valeur_liberer(&e);
                    ok = echouer(diag, b, debut, mp);
                    break;
                }
                ensemble_gagner(&e, x);
            }
            if (!ok) break;
            pile.n = base_pile;
            empiler(&pile, e);
            break;
        }
        case I_AJOUTER: {   /* « Les élus gagnent m. », « perdent » */
            Valeur x = depiler(&pile), e = depiler(&pile);
            if (e.type != V_ENSEMBLE || x.type == V_ABSENT) {
                char *mp = x.type == V_ABSENT ? message_absent(&x) : grym_formater("Seul un ensemble gagne ou perd un élément, pas %s.", nom_type(e.type));
                valeur_liberer(&x);
                valeur_liberer(&e);
                ok = echouer(diag, b, debut, mp);
                break;
            }
            if (op == 0) ensemble_gagner(&e, x);
            else {
                long k = ensemble_rang(&e, &x);
                valeur_liberer(&x);
                if (k >= 0) {
                    ensemble_seul(&e);
                    valeur_liberer(&e.liste->valeurs[k]);
                    memmove(&e.liste->valeurs[k], &e.liste->valeurs[k + 1], (e.liste->n - (size_t)k - 1) * sizeof *e.liste->valeurs);
                    e.liste->n--;
                }
            }
            empiler(&pile, e);
            break;
        }
        case I_PARMI: {   /* « m est parmi les élus » */
            Valeur e = depiler(&pile), x = depiler(&pile);
            char *mp = NULL;
            if (x.type == V_ABSENT) mp = message_absent(&x);
            else if (e.type != V_ENSEMBLE) mp = grym_formater("« parmi » cherche dans un ensemble, pas dans %s.", nom_type(e.type));
            int r = mp ? 0 : ensemble_rang(&e, &x) >= 0;
            valeur_liberer(&x);
            valeur_liberer(&e);
            if (mp) { ok = echouer(diag, b, debut, mp); break; }
            empiler(&pile, vi_booleen(r));
            break;
        }
        case I_TAILLE_ENSEMBLE: {   /* « le nombre de couleurs » */
            Valeur e = depiler(&pile);
            if (e.type != V_ENSEMBLE) {
                char *mp = grym_formater("« le nombre de » compte un ensemble, pas %s.", nom_type(e.type));
                valeur_liberer(&e);
                ok = echouer(diag, b, debut, mp);
                break;
            }
            Valeur r = valeur_nombre(dec_depuis_long((long)e.liste->n));
            valeur_liberer(&e);
            empiler(&pile, r);
            break;
        }
        case I_JOINDRE: {   /* « les couleurs jointes par « , » » */
            Valeur s = depiler(&pile), e = depiler(&pile);
            char *mp = NULL;
            if (e.type != V_ENSEMBLE) mp = grym_formater("Seul un ensemble se joint, pas %s.", nom_type(e.type));
            else if (s.type != V_TEXTE) mp = grym_formater("Le séparateur est un texte, pas %s.", nom_type(s.type));
            Chaine c = {0};
            for (size_t k = 0; !mp && k < e.liste->n; k++) {
                if (k) chaine_ajouter(&c, s.texte ? s.texte : "");
                char *t = e.liste->valeurs[k].type == V_OBJET ? nom_objet(m, e.liste->valeurs[k].objet)
                                                                : texte_valeur(m, &e.liste->valeurs[k]);
                chaine_ajouter(&c, t);
                free(t);
            }
            char *t = chaine_rendre(&c);
            valeur_liberer(&s);
            valeur_liberer(&e);
            if (mp) { free(t); ok = echouer(diag, b, debut, mp); break; }
            Valeur r = vi_texte(t);
            free(t);
            empiler(&pile, r);
            break;
        }
        case I_TABLEAU: {   /* « Pour chaque ligne du tableau « f.csv » » : le fichier lu devient un tableau (§ 15.5) */
            Valeur f = depiler(&pile);
            char *probleme = NULL;
            Valeur r = vi_texte("");
            if (f.type != V_FICHIER) probleme = grym_formater("Un tableau se lit dans un fichier, pas dans %s.", nom_type(f.type));
            else {
                free(r.texte);
                r.texte = octets_du_fichier(f.fichier, &probleme) ? lire_csv(f.fichier, &probleme) : NULL;
                r.type = V_LIGNE;
            }
            valeur_liberer(&f);
            if (probleme) { valeur_liberer(&r); ok = echouer(diag, b, debut, probleme); break; }
            empiler(&pile, r);
            break;
        }
        case I_COLONNE: {   /* « la colonne « Nom » de la ligne » (§ 15.5) */
            Valeur titre = depiler(&pile), l = depiler(&pile);
            char *probleme = NULL;
            Valeur r = vi_absent(NULL);
            if (l.type == V_ABSENT) probleme = message_absent(&l);
            else if (l.type != V_LIGNE) probleme = grym_formater("« la colonne » se lit dans une ligne de tableau, pas dans %s.",
                                                                 nom_type(l.type));
            else if (titre.type != V_TEXTE) probleme = grym_formater("Le titre d'une colonne est un texte, pas %s.",
                                                                     nom_type(titre.type));
            else {
                const char *sep = strchr(l.texte, '\x1e');
                if (!sep || strchr(sep + 1, '\x1e'))
                    probleme = grym_dupliquer("« la colonne » se lit dans une seule ligne : « Pour chaque ligne du tableau … ».");
                else {
                    char *cherche = texte_plier(titre.texte);
                    long k = -1, q = 0;
                    Chaine titres = {0};
                    for (const char *p = l.texte; p < sep && k < 0; q++) {
                        const char *f2 = memchr(p, '\x1f', (size_t)(sep - p));
                        if (!f2) f2 = sep;
                        char *t = grym_formater("%.*s", (int)(f2 - p), p);
                        char *pl = texte_plier(t);
                        if (strcmp(pl, cherche) == 0) k = q;
                        if (q) chaine_ajouter(&titres, ", ");
                        chaine_ajouter(&titres, t);
                        free(pl);
                        free(t);
                        p = f2 + 1;
                    }
                    free(cherche);
                    char *liste = chaine_rendre(&titres);
                    if (k < 0) probleme = grym_formater("Aucune colonne « %s » dans ce tableau : %s.", titre.texte, liste);
                    else {
                        const char *p = sep + 1;
                        for (long z = 0; z < k && p; z++) { p = strchr(p, '\x1f'); if (p) p++; }
                        const char *f2 = p ? strchr(p, '\x1f') : NULL;
                        size_t n = p ? (f2 ? (size_t)(f2 - p) : strlen(p)) : 0;
                        if (n) { valeur_liberer(&r); r = vi_texte(""); free(r.texte); r.texte = grym_formater("%.*s", (int)n, p); }
                        else { valeur_liberer(&r); r = vi_absent(titre.texte); }
                    }
                    free(liste);
                }
            }
            valeur_liberer(&titre);
            valeur_liberer(&l);
            if (probleme) { valeur_liberer(&r); ok = echouer(diag, b, debut, probleme); break; }
            empiler(&pile, r);
            break;
        }
        case I_ARRONDIR: {   /* « l'arrondi de x à 0,05 » (§ 3.4) */
            Valeur vp = depiler(&pile), vx = depiler(&pile);
            char *probleme = NULL;
            Decimal r = dec_zero();
            if (vx.type == V_ABSENT || vp.type == V_ABSENT)
                probleme = message_absent(vx.type == V_ABSENT ? &vx : &vp);
            else if (vx.type != V_NOMBRE)
                probleme = grym_formater("On n'arrondit qu'un nombre, pas %s.", nom_type(vx.type));
            else if (vp.type != V_NOMBRE)
                probleme = grym_formater("Le pas d'un arrondi est un nombre, pas %s.", nom_type(vp.type));
            else if (!vp.nombre.n || vp.nombre.negatif) {
                char *x = dec_afficher(&vp.nombre);
                probleme = grym_formater("Un arrondi se fait à un pas positif (0,05, 1, 10…), pas à %s.", x);
                free(x);
            } else {
                dec_liberer(&r);
                ModeArrondi mode = op == 1 ? DEC_VERS_LE_BAS : op == 2 ? DEC_VERS_LE_HAUT : op == 3 ? DEC_PAIR : DEC_PROCHE;
                StatutDecimal st = dec_arrondir(&vx.nombre, &vp.nombre, mode, &r);
                if (st != DEC_OK) probleme = message_statut(st);
            }
            valeur_liberer(&vx);
            valeur_liberer(&vp);
            if (probleme) {
                dec_liberer(&r);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, valeur_nombre(r));
            break;
        }
        case I_REFUSER: {
            /* « Refuser « … » puis x. » (grammaire, § 18.1) : une erreur voulue, dont le message s'écrit comme
             * « Afficher » l'écrirait ; Essayer la rattrape, la transaction annule */
            size_t n = (size_t)op;
            Valeur *v = grym_allouer((n ? n : 1) * sizeof *v);
            for (size_t k = n; k-- > 0;) v[k] = depiler(&pile);
            char *probleme = NULL;
            Chaine msg = { 0 };
            for (size_t k = 0; k < n && !probleme; k++) {
                if (v[k].type == V_ABSENT) { probleme = message_absent(&v[k]); break; }
                char *t = texte_valeur(m, &v[k]);
                if (k) chaine_ajouter(&msg, " ");
                chaine_ajouter(&msg, t);
                free(t);
            }
            for (size_t k = 0; k < n; k++) valeur_liberer(&v[k]);
            free(v);
            if (!probleme) {
                char *t = chaine_rendre(&msg);
                probleme = t ? t : grym_dupliquer("");
            } else {
                free(chaine_rendre(&msg));
            }
            ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_APPARENCE: {
            /* « Les écrans ont la couleur verte et le logo « … ». » (grammaire, § 22.5) : l'interface l'applique ;
             * sans écrans (console, navigateur), rien. Le logo est relu ici : il a pu changer depuis l'analyse.
             * Avec 0x100 (2.13) : la police, la couleur libre et le fond, dépilés d'abord (le fond au sommet). */
            const int etendue = (op & 0x100) != 0;
            Valeur vsig = (op & 0x200) ? depiler(&pile) : vi_absent(NULL);   /* la signature (2.16), au sommet */
            Valeur vfond = etendue ? depiler(&pile) : vi_absent(NULL);
            Valeur vcoul = etendue ? depiler(&pile) : vi_absent(NULL);
            Valeur vpol = etendue ? depiler(&pile) : vi_absent(NULL);
            Valeur vl = depiler(&pile);
            const int couleur = (int)(op & 0xFF) - 1;
            const char *format_signature = NULL;
            if (vsig.type == V_FICHIER) {   /* une image, ou un SVG reconnu à son texte, comme le logo */
                char *illisible = NULL;
                if (octets_du_fichier(vsig.fichier, &illisible)) {
                    format_signature = vsig.fichier->format;
                    const char *o = (const char *)vsig.fichier->octets;
                    for (size_t q = 0; !format_signature && q + 4 <= vsig.fichier->taille && q < 1024; q++)
                        if (!memcmp(o + q, "<svg", 4)) format_signature = "SVG";
                }
                free(illisible);
            }
            char *probleme = NULL;
            if (vl.type == V_FICHIER && !vl.fichier->format) {   /* 2.33 : une image lue en base, ses octets pas encore lus */
                char *illisible = NULL;
                const unsigned char *o = octets_du_fichier(vl.fichier, &illisible);
                if (o) vl.fichier->format = format_image(o, vl.fichier->taille);
                free(illisible);
            }
            const char *format_logo = vl.type == V_FICHIER ? vl.fichier->format : NULL;
            if (couleur < -1 || couleur > 4) probleme = grym_formater("Couleur inconnue : %d.", couleur);
            if (!probleme && vl.type == V_FICHIER && !format_logo) {
                /* une image SVG (2.13) : du texte qui commence par « <svg », ou une déclaration XML qui en contient */
                char *illisible = NULL;
                if (etendue && octets_du_fichier(vl.fichier, &illisible)) {
                    const char *o = (const char *)vl.fichier->octets;
                    size_t n = vl.fichier->taille, d = 0;
                    if (n >= 3 && (unsigned char)o[0] == 0xEF && (unsigned char)o[1] == 0xBB && (unsigned char)o[2] == 0xBF) d = 3;
                    while (d < n && (o[d] == ' ' || o[d] == '\t' || o[d] == '\r' || o[d] == '\n')) d++;
                    int svg = n - d >= 4 && !memcmp(o + d, "<svg", 4);
                    for (size_t q = d; !svg && q + 4 <= n && q < d + 1024; q++) svg = !memcmp(o + q, "<svg", 4);
                    if (svg) format_logo = "SVG";
                }
                free(illisible);
                if (!format_logo)
                    probleme = grym_formater("« %s » n'est pas une image (PNG, JPEG, GIF, WebP%s).", vl.fichier->nom,
                                             etendue ? " ou SVG" : "");
            }
            TeinteTheme th;
            memset(&th, 0, sizeof th);
            const char *cl = vcoul.type == V_TEXTE ? vcoul.texte : NULL, *fo = vfond.type == V_TEXTE ? vfond.texte : NULL;
            if (!probleme && (cl || fo)) teinte_theme(cl, fo, &th, &probleme);
            if (!probleme && vpol.type == V_FICHIER) {
                char *illisible = NULL;
                if (!octets_du_fichier(vpol.fichier, &illisible)) probleme = illisible;
                else {
                    const unsigned char *o = vpol.fichier->octets;
                    if (vpol.fichier->taille < 4 || !(!memcmp(o, "\x00\x01\x00\x00", 4) || !memcmp(o, "OTTO", 4) || !memcmp(o, "true", 4)))
                        probleme = grym_formater("« %s » n'est pas une police (TrueType ou OpenType : .ttf, .otf).", vpol.fichier->nom);
                }
            }
            if (!probleme && m->iface.ecran_apparence) {
                char *illisible = NULL;
                if (vl.type == V_FICHIER && !octets_du_fichier(vl.fichier, &illisible)) probleme = illisible;
                else if (vl.type == V_FICHIER)
                    m->iface.ecran_apparence(m->iface.contexte, couleur, vl.fichier->octets, vl.fichier->taille, format_logo);
                else
                    m->iface.ecran_apparence(m->iface.contexte, couleur, NULL, 0, NULL);
            }
            if (!probleme && etendue && m->iface.ecran_theme) {
                const char *noms[TEINTE_JETONS_MAX], *clairs[TEINTE_JETONS_MAX], *sombres[TEINTE_JETONS_MAX];
                for (size_t k = 0; k < th.n; k++) {
                    noms[k] = th.noms[k];
                    clairs[k] = th.clair[k];
                    sombres[k] = th.sombre[k][0] ? th.sombre[k] : NULL;
                }
                m->iface.ecran_theme(m->iface.contexte, noms, clairs, sombres, th.n, th.clair_impose,
                                     vpol.type == V_FICHIER ? vpol.fichier->octets : NULL,
                                     vpol.type == V_FICHIER ? vpol.fichier->taille : 0);
            }
            if (!probleme && vsig.type == V_FICHIER && !format_signature)
                probleme = grym_formater("« %s » n'est pas une image (PNG, JPEG, GIF, WebP ou SVG).", vsig.fichier->nom);
            if (!probleme && format_signature && m->iface.ecran_signature)
                m->iface.ecran_signature(m->iface.contexte, vsig.fichier->octets, vsig.fichier->taille, format_signature);
            valeur_liberer(&vsig);
            valeur_liberer(&vl);
            valeur_liberer(&vpol);
            valeur_liberer(&vcoul);
            valeur_liberer(&vfond);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_FERMER_ECRAN:
            m->fermer_ecran = 1;
            break;
        case I_ECRAN_OBJET:
            if (!m->ecran || !m->ecran->objet) {
                ok = echouer(diag, b, debut, grym_dupliquer("Aucun écran n'est ouvert."));
                break;
            }
            empiler(&pile, vi_objet(m->ecran->objet));
            break;
        case I_FICHE: {
            /* « Afficher la fiche de p. » (grammaire, § 20) */
            Valeur vo = depiler(&pile);
            char *probleme = NULL;
            if (vo.type == V_ABSENT) probleme = message_absent(&vo);
            else if (vo.type != V_OBJET)
                probleme = grym_formater("Une fiche montre un objet : la valeur est %s.", nom_type(vo.type));
            else
                probleme = afficher_fiche(m, vo.objet, sortie);
            valeur_liberer(&vo);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_RESAISIR: {
            /* « Saisir à nouveau p. » : le formulaire de modification (grammaire, § 19) */
            Valeur vo = depiler(&pile);
            char *probleme = NULL;
            if (vo.type != V_OBJET || !vo.objet->classe->conserve)
                probleme = grym_dupliquer("Seul un objet d'une entité se saisit.");
            else
                probleme = saisir(m, vo.objet, "", sortie, cadres, &fatal, 1);
            valeur_liberer(&vo);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_GAGNER:
        case I_PERDRE: {
            /* « Les genres de o gagnent baroque. » : objet, valeur → rien (grammaire, § 16.13) */
            const char *champ = b->noms[op];
            Valeur v = depiler(&pile);
            Valeur ob = depiler(&pile);
            char *probleme = NULL;
            long k = ob.type == V_OBJET ? index_champ(ob.objet->classe, champ) : -1;
            if (ob.type == V_ABSENT) probleme = message_absent(&ob);
            else if (ob.type != V_OBJET)
                probleme = grym_formater("« %s » : la valeur n'est pas un objet, c'est %s.", champ, nom_type(ob.type));
            else if (k < 0 || !(ob.objet->classe->uniques[k] & 8)) {
                char *qui = article_classe(ob.objet->classe);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s n'a pas de champ multiple « %s ».", qui, champ);
                free(qui);
            } else if (!ob.objet->id) {
                const ClasseVM *c = ob.objet->classe;
                char *qui = article_classe(c);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s qui n'est pas conservé%s ne %s rien : ses « %s » vivent dans la base. "
                                         "Conserve-%s d'abord.", qui, c->feminin ? "e" : "",
                                         code == I_GAGNER ? "gagne" : "perd", champ, c->feminin ? "la" : "le");
                free(qui);
            } else if (v.type == V_ABSENT) {
                probleme = message_absent(&v);
            } else if ((probleme = verifier_type_champ(m, champ, ob.objet->classe->types[k], &v, 0, NULL)) == NULL) {
                base_gagner(m->base, ob.objet, (size_t)k, &v, code == I_PERDRE, &probleme);
            }
            valeur_liberer(&v);
            valeur_liberer(&ob);
            if (probleme) ok = echouer(diag, b, debut, probleme);
            break;
        }
        case I_ABSENT:
            empiler(&pile, vi_absent(NULL));
            break;
        case I_EST_ABSENT: {
            Valeur v = depiler(&pile);
            int absent = v.type == V_ABSENT;
            valeur_liberer(&v);
            Valeur r = valeur_nombre(dec_zero());
            r.type = V_BOOLEEN;
            r.vrai = absent;
            empiler(&pile, r);
            break;
        }
        case I_DECALER: {
            /* date ± jours, semaines, mois, années ; année ± années (§ 14.2) */
            static const char *const UNITE[] = { "jours", "semaines", "mois", "années", "minutes", "heures" };
            if (op > 5) op = 0;
            if (op >= 4 || pile.v[pile.n - 2].type == V_HEURE) {   /* « h + 15 minutes » : l'heure tourne comme une horloge (§ 14.7) */
                Valeur vn = depiler(&pile), vh = depiler(&pile);
                char *mp = NULL;
                long n = 0;
                if (vh.type == V_ABSENT || vn.type == V_ABSENT) mp = message_absent(vh.type == V_ABSENT ? &vh : &vn);
                else if (vh.type != V_HEURE)
                    mp = grym_formater("Des %s s'ajoutent à une heure, pas à %s.", UNITE[op], nom_type(vh.type));
                else if (op < 4)
                    mp = grym_formater("Une heure se décale de minutes ou d'heures, pas de %s.", UNITE[op]);
                else if (vn.type != V_NOMBRE || !dec_en_long_borne(&vn.nombre, -1000000000L, 1000000000L, &n))
                    mp = grym_formater("Une heure se décale d'un nombre entier de %s.", UNITE[op]);
                long r = 0;
                if (!mp) {
                    long d = op == 5 ? (n % 24) * 60 : n % 1440;
                    r = ((vh.jours + d) % 1440 + 1440) % 1440;
                }
                valeur_liberer(&vn);
                valeur_liberer(&vh);
                if (mp) { ok = echouer(diag, b, debut, mp); break; }
                empiler(&pile, valeur_heure(r));
                break;
            }
            Valeur vn = depiler(&pile), vd = depiler(&pile);
            char *probleme = NULL;
            Valeur r = valeur_nombre(dec_zero());
            long n = 0;
            if (vd.type == V_ABSENT || vn.type == V_ABSENT)
                probleme = message_absent(vd.type == V_ABSENT ? &vd : &vn);
            else if (vd.type != V_DATE && !(vd.type == V_ANNEE && op == 3))
                probleme = vd.type == V_ANNEE
                    ? grym_formater("Une année se décale d'années, pas de %s.", UNITE[op])
                    : grym_formater("Des %s s'ajoutent à une date, pas à %s.", UNITE[op], nom_type(vd.type));
            else if (vn.type != V_NOMBRE || !dec_est_entier(&vn.nombre))
                probleme = grym_formater("%s se décale d'un nombre entier de %s.",
                                         vd.type == V_ANNEE ? "Une année" : "Une date", UNITE[op]);
            else if (!dec_en_long_borne(&vn.nombre, -9999999L, 9999999L, &n))
                probleme = grym_dupliquer(vd.type == V_ANNEE ? "Année hors du calendrier : de 1 à 9999."
                                                             : "Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
            else if (vd.type == V_ANNEE) {
                long a = vd.jours + n;
                if (a < 1 || a > 9999) probleme = grym_dupliquer("Année hors du calendrier : de 1 à 9999.");
                else { valeur_liberer(&r); r = valeur_annee(a); }
            } else {
                long j = 0;
                int dedans = op <= 1 ? (j = vd.jours + (op ? 7 * n : n), j >= DATE_MIN && j <= DATE_MAX)
                                     : date_ajouter_mois(vd.jours, op == 3 ? 12 * n : n, &j);
                if (!dedans) probleme = grym_dupliquer("Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
                else { valeur_liberer(&r); r = valeur_date(j); }
            }
            valeur_liberer(&vn);
            valeur_liberer(&vd);
            if (probleme) {
                valeur_liberer(&r);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            empiler(&pile, r);
            break;
        }
        case I_REGLE_UTILE:   /* une règle dont rien de ce qu'elle a lu n'a changé ne se revérifie pas (docs/vm.md, § 3.3) */
        {
            int utile = base_regle_utile(m->base, b->constantes[op].texte);
            m->regles_verifiees += utile;
            empiler(&pile, valeur_booleen(utile));
            break;
        }
        case I_REGLE_FAITE:
            base_regle_faite(m->base, b->constantes[op].texte);
            break;
        case I_BASE_MODIFIEE:   /* les règles ne se revérifient que si la transaction a écrit (§ 16.14) */
            empiler(&pile, valeur_booleen(base_modifiee(m->base)));
            break;
        case I_HEURE:
            empiler(&pile, valeur_heure((long)op));
            break;
        case I_MAINTENANT:
            empiler(&pile, valeur_heure(maintenant));   /* lue une fois, au début de l'exécution (§ 14.7) */
            break;
        case I_AUJOURDHUI:
            empiler(&pile, valeur_date(aujourdhui));   /* lue une fois, au début de l'exécution (§ 14.3) */
            break;
        case I_NOUVEAU: {
            const ClasseVM *cl = classe_vm(m, b->noms[op]);
            if (!cl) {
                ok = echouer(diag, b, debut, grym_formater("Classe « %s » inconnue.", b->noms[op]));
                break;
            }
            if (m->depuis_ramassage >= m->seuil) ramasser(m, &pile, cadres, nb_cadres);
            Objet *o = creer_objet(m, cl, m->epoque);   /* objet neuf : rien à journaliser */
            Valeur v = valeur_nombre(dec_zero());
            v.type = V_OBJET;
            v.objet = o;
            empiler(&pile, v);
            break;
        }
        case I_INITIALISER_CHAMP:
        case I_ECRIRE_CHAMP:
        case I_LIRE_CHAMP: {
            /* INITIALISER : objet, valeur → objet ; ÉCRIRE : objet, valeur → rien ; LIRE : objet → valeur */
            Valeur *vo = &pile.v[pile.n - (code == I_LIRE_CHAMP ? 1 : 2)];
            const char *champ = b->noms[op];
            if (vo->type == V_FICHIER) {
                /* taille, format, nom de fichier (§ 15.3) ; un fichier ne se modifie pas */
                const Fichier *f = vo->fichier;
                Valeur r;
                if (code != I_LIRE_CHAMP) {
                    ok = echouer(diag, b, debut, grym_formater("Un fichier ne se modifie pas : « %s ».", champ));
                    break;
                } else if (strcmp(champ, "taille") == 0) {
                    char t[32];
                    snprintf(t, sizeof t, "%lu", (unsigned long)f->taille);
                    r = valeur_nombre(dec_depuis_canonique(t));
                } else if (strcmp(champ, "format") == 0 || strcmp(champ, "nom de fichier") == 0) {
                    r = valeur_nombre(dec_zero());
                    r.type = V_TEXTE;
                    char *il = NULL;   /* le format d'une image, ou d'un son (§ 15.7) */
                    const char *fm = strcmp(champ, "format") == 0 ? format_montre(f, &il) : NULL;
                    free(il);
                    r.texte = grym_dupliquer(strcmp(champ, "format") == 0 ? (fm ? fm : "inconnu") : f->nom);
                } else {
                    ok = echouer(diag, b, debut, grym_formater(
                        "Un fichier n'a pas de champ « %s » : taille, format ou nom de fichier.", champ));
                    break;
                }
                valeur_liberer(vo);
                *vo = r;
                break;
            }
            if (vo->type == V_NOMBRE && code == I_LIRE_CHAMP
                && (strcmp(champ, "valeur absolue") == 0 || strcmp(champ, "partie entière") == 0)) {
                /* « la valeur absolue de x », « la partie entière de x » (§ 3.5) ; un autre champ : le message des objets */
                int absolue = strcmp(champ, "valeur absolue") == 0;
                Decimal r;
                if (absolue) {
                    r = dec_copier(&vo->nombre);
                    r.negatif = 0;
                } else {
                    Decimal un = dec_depuis_canonique("1");
                    dec_arrondir(&vo->nombre, &un, DEC_VERS_ZERO, &r);
                    dec_liberer(&un);
                }
                valeur_liberer(vo);
                *vo = valeur_nombre(r);
                break;
            }
            if (vo->type == V_TEXTE && code == I_LIRE_CHAMP && strcmp(champ, "longueur") == 0) {
                /* « la longueur du nom » (§ 4.5) ; un autre champ : le message des objets */
                Valeur r = valeur_nombre(dec_depuis_long((long)longueur_utf8(vo->texte ? vo->texte : "")));
                valeur_liberer(vo);
                *vo = r;
                break;
            }
            if (vo->type == V_DATE) {   /* « l'année de d » (§ 14.5) ; « le mois », « le jour », « le jour de la semaine » (§ 14.8) */
                const int an = strcmp(champ, "année") == 0, mo = strcmp(champ, "mois") == 0, jo = strcmp(champ, "jour") == 0,
                          se = strcmp(champ, "jour de la semaine") == 0;
                if (code != I_LIRE_CHAMP || !(an || mo || jo || se)) {
                    ok = echouer(diag, b, debut, code != I_LIRE_CHAMP
                        ? grym_formater("Une date ne se modifie pas : « %s ».", champ)
                        : grym_formater("Une date n'a pas de champ « %s » : « année », « mois », « jour » ou « jour de la semaine ».", champ));
                    break;
                }
                int jour, mois, annee;
                date_civile(vo->jours, &annee, &mois, &jour);
                const long dd = vo->jours;
                valeur_liberer(vo);
                if (an) *vo = valeur_annee(annee);
                else if (mo || jo) *vo = valeur_nombre(dec_depuis_long(mo ? mois : jour));
                else *vo = vi_texte(JOURS_SEMAINE[jour_semaine(dd)]);
                break;
            }
            if (vo->type == V_ABSENT && m->absent_permis && code == I_LIRE_CHAMP) break;   /* reste absent (§ 22.8) */
            if (vo->type == V_ABSENT) { ok = echouer(diag, b, debut, message_absent(vo)); break; }
            if (vo->type != V_OBJET) {
                ok = echouer(diag, b, debut, grym_formater(
                    "« %s » : la valeur n'est pas un objet, c'est %s.", champ, nom_type(vo->type)));
                break;
            }
            Objet *o = vo->objet;
            {
                char *erreur = NULL;
                if (!charger(m, o, &erreur)) { ok = echouer(diag, b, debut, erreur); break; }
            }
            long k = index_champ(o->classe, champ);
            if (k < 0) {
                char *qui = article_classe(o->classe);
                qui[0] = (char)(qui[0] - 32);
                ok = echouer(diag, b, debut, grym_formater("%s n'a pas de champ « %s ».", qui, champ));
                free(qui);
                break;
            }
            if (o->classe->uniques[k] & 8) {   /* champ multiple : un ensemble, pas une valeur (§ 16.13) */
                ok = echouer(diag, b, debut, grym_formater(
                    "« %s » est un champ multiple : il se lit avec « Pour chaque … de … » ou « le nombre de … de … », "
                    "et change avec « gagnent » ou « perdent ».", champ));
                break;
            }
            if (code != I_LIRE_CHAMP) {
                char *probleme = verifier_type_champ(m, champ, o->classe->types[k], &pile.v[pile.n - 1],
                                                     (o->classe->uniques[k] & 2) != 0, o->classe->valeurs[k]);
                if (probleme) { ok = echouer(diag, b, debut, probleme); break; }
            }
            if (code == I_LIRE_CHAMP) {
                if (!o->definis[k] && (o->classe->uniques[k] & 2)) {   /* facultatif : absent */
                    valeur_liberer(vo);
                    *vo = vi_absent(champ);
                    break;
                }
                if (!o->definis[k] && m->ecran && o == m->ecran->objet) {
                    /* Une zone vide (§ 22.6) : absente pour ce qui décrit l'écran (le « dont » d'une liste, « actif si »,
                     * « visible si »), lu dans le cadre qui l'a ouvert ; dans un événement, une zone obligatoire
                     * vide arrête l'événement, avec un message pour l'utilisateur. */
                    if (nb_cadres == m->ecran->cadre || (ip < b->taille_code && b->code[ip] == I_EST_ABSENT)) {
                        /* … et pour un test de présence, dans un événement aussi : « Si le soin de l'écran est présent » */
                        valeur_liberer(vo);
                        *vo = vi_absent(champ);
                        break;
                    }
                    char *titre = capitale(champ);
                    ok = echouer(diag, b, debut, grym_formater("Remplis « %s » : cette zone est obligatoire.", titre));
                    free(titre);
                    break;
                }
                if (!o->definis[k]) {
                    ok = echouer(diag, b, debut, grym_formater("Le champ « %s » n'a pas de valeur.", champ));
                    break;
                }
                if (type_secret(o->classe->types[k]) && strncmp(o->classe->nom, "écran ", strlen("écran ")) != 0
                    && !m->envoi_ouvert && !(ip < b->taille_code && b->code[ip] == I_EST_ABSENT)) {
                    /* un secret ne se lit que pour être envoyé (§ 22.17) ; sa présence se teste comme un mot de passe */
                    ok = echouer(diag, b, debut, grym_formater(
                        "« %s » est un secret : il ne se lit que pour être envoyé.", champ));
                    break;
                }
                if (type_mot_de_passe(o->classe->types[k]) && strncmp(o->classe->nom, "écran ", strlen("écran ")) != 0
                    && !(ip < b->taille_code && (b->code[ip] == I_CORRESPOND || b->code[ip] == I_EST_ABSENT))) {
                    /* un mot de passe ne se lit pas, sauf pour être vérifié (§ 16.1), ou pour savoir s'il y en a un
                     * (« est présent », « est absent ») : ce test ne dit rien de lui ; la zone d'un écran, elle, se lit */
                    ok = echouer(diag, b, debut, grym_formater(
                        "« %s » est un mot de passe : il ne se lit pas, il se vérifie (« Si … correspond au %s … »).", champ, champ));
                    break;
                }
                if (o->id) base_noter_lecture(m->base, o->classe);   /* une règle qui s'évalue retient ce qu'elle lit */
                Valeur v = valeur_copier(&o->champs[k]);
                valeur_liberer(vo);
                *vo = v;
            } else {
                Valeur v = depiler(&pile);
                if (v.type == V_TEXTE && type_secret(o->classe->types[k]) && o->id && !module->base_chiffree) {
                    /* un secret se range tel quel, il doit pouvoir repartir : seulement dans une base chiffrée (§ 22.17) */
                    valeur_liberer(&v);
                    ok = echouer(diag, b, debut, grym_formater(
                        "« %s » est un secret : il se range dans une base chiffrée. Écris « La base est chiffrée. » dans le "
                        "programme.", champ));
                    break;
                }
                if (v.type == V_TEXTE && type_mot_de_passe(o->classe->types[k]) && strncmp(o->classe->nom, "écran ", strlen("écran ")) != 0) {
                    /* un mot de passe se range en empreinte, jamais en clair (§ 16.1) ; la zone d'un écran garde le sien */
                    char *e = empreinte_creer(v.texte);
                    valeur_liberer(&v);
                    if (!e) { ok = echouer(diag, b, debut, grym_dupliquer("La source aléatoire du système fait défaut : le mot de passe n'est pas rangé.")); break; }
                    v = vi_texte(e);
                    free(e);
                }
                if (v.type == V_NOMBRE && o->classe->types[k] && strcmp(o->classe->types[k], "année") == 0) {
                    long a = 1;   /* un nombre entier rangé dans un champ (année) devient une année (§ 14.5) */
                    dec_en_long_borne(&v.nombre, 1, 9999, &a);
                    valeur_liberer(&v);
                    v = valeur_annee(a);
                }
                ecrire_champ(m, o, (size_t)k, v);
                const long transcrit = transcrire_si_besoin(m, o, (size_t)k);   /* « transcrite du mémo » (§ 15.8) */
                if (code == I_ECRIRE_CHAMP) {
                    Valeur ob = depiler(&pile);
                    valeur_liberer(&ob);
                    /* objet conservé : la base suit aussitôt, dans la transaction (§ 16.3) */
                    char *erreur = NULL;
                    if (o->id && (!base_ecrire_champ(m->base, o, (size_t)k, &erreur)
                                  || (transcrit >= 0 && !base_ecrire_champ(m->base, o, (size_t)transcrit, &erreur)))) {
                        ok = echouer(diag, b, debut, erreur);
                        break;
                    }
                }
            }
            break;
        }
        case I_LIRE_LOCAL:
            if (!cadre->definis[op]) {
                ok = echouer(diag, b, debut, grym_formater("Case locale %u sans valeur.", op));
                break;
            }
            empiler(&pile, valeur_copier(&cadre->locaux[op]));
            break;
        case I_ECRIRE_LOCAL:
            if (cadre->definis[op]) valeur_liberer(&cadre->locaux[op]);
            cadre->locaux[op] = depiler(&pile);
            cadre->definis[op] = 1;
            break;
        case I_APPELER: {
            const char *nom = b->noms[op];
            unsigned nb_args = b->code[debut + 3];
            int rend = b->code[debut + 4];
            char *pourquoi = NULL;
            Formule *f = choisir_version(m, nom, nb_args ? &pile.v[pile.n - nb_args] : NULL, &pourquoi);
            if (!f) {
                ok = echouer(diag, b, debut, pourquoi);
                break;
            }
            if ((f->bloc->sorte == B_CALCUL) != rend || f->bloc->nb_parametres != (int)nb_args) {
                ok = echouer(diag, b, debut, grym_formater(
                    "Appel de « %s » incompatible : %s à %d paramètre%s attendu.", nom,
                    f->bloc->sorte == B_CALCUL ? "un calcul" : "une action",
                    f->bloc->nb_parametres, f->bloc->nb_parametres > 1 ? "s" : ""));
                break;
            }
            if (grym_interruption) {
                grym_interruption = 0;
                ok = echouer(diag, b, debut, grym_dupliquer("Interrompu (Ctrl+C)."));
                fatal = 1;
                break;
            }
            if (nb_cadres >= APPELS_MAX) {
                ok = echouer(diag, b, debut, grym_formater(
                    "Trop d'appels imbriqués : plus de %d. Une formule s'appelle-t-elle sans fin ?", APPELS_MAX));
                break;
            }
            if (nb_cadres == cap_cadres) {
                cap_cadres *= 2;
                Cadre *nc = grym_allouer(cap_cadres * sizeof *nc);
                memcpy(nc, cadres, nb_cadres * sizeof *nc);
                free(cadres);
                cadres = nc;
            }
            Cadre *n = &cadres[nb_cadres++];
            n->b = f->bloc;
            n->liaison = f->liaison;
            n->ip = 0;
            int nl = f->bloc->nb_locaux;
            n->locaux = grym_allouer((nl ? (size_t)nl : 1) * sizeof *n->locaux);
            n->definis = grym_allouer(nl ? (size_t)nl : 1);
            memset(n->definis, 0, nl ? (size_t)nl : 1);
            for (unsigned k = nb_args; k > 0; k--) {       /* le dernier argument est au sommet */
                n->locaux[k - 1] = depiler(&pile);
                n->definis[k - 1] = 1;
            }
            break;
        }
        default:
            ok = echouer(diag, b, debut, grym_dupliquer("Instruction inconnue."));
            break;
        }
        if (!ok && !fatal && m->nb_essais) {
            /* L'essai le plus récent rattrape l'erreur : tout ce qu'il a fait est annulé, puis son bloc
             * « En cas d'échec » s'exécute avec le motif sur la pile (grammaire, § 18). */
            Essai e = m->essais[--m->nb_essais];
            while (nb_cadres - 1 > e.cadre) liberer_cadre(&cadres[--nb_cadres]);
            while (pile.n > e.pile) valeur_liberer(&pile.v[--pile.n]);
            annuler_jusqu_a(m, e.journal);
            retirer_ecritures(m, e.a_ecrire);
            char *probleme = NULL;
            if (m->base && !base_revenir(m->base, m->nb_essais + 1, &probleme)) {
                oublier_photo(&e);
                free(diag->message);
                diag->message = probleme;
                break;
            }
            restaurer_photo(&e, &cadres[e.cadre]);
            oublier_photo(&e);
            Valeur motif = valeur_nombre(dec_zero());
            motif.type = V_TEXTE;
            motif.texte = diag->message;
            diag->message = NULL;
            diag->ligne = diag->colonne = 0;
            free(diag->fichier);
            diag->fichier = NULL;
            empiler(&pile, motif);
            cadres[e.cadre].ip = e.cible;
            m->epoque++;   /* ce que le bloc « En cas d'échec » écrit repasse au journal */
            ok = 1;
            continue;
        }
        if (!ok) break;
    }
    while (m->nb_essais > (imbrique ? nb_essais_dehors : 0)) oublier_photo(&m->essais[--m->nb_essais]);   /* la transaction les emporte */

    vider(&pile);
    for (size_t k = nb_cadres; k > 0; k--) liberer_cadre(&cadres[k - 1]);
    free(cadres);
    free(liaison_principale);
    if (imbrique) {   /* rien d'autre : la transaction, les écrans, les fichiers et le journal sont à l'exécution qui appelle */
        m->seuil = seuil_dehors;
        free(remplaces);
        return ok;
    }
    while (m->ecran) {   /* une erreur a interrompu les écrans : ils se ferment, du dessus au dessous */
        EcranVM *d = m->ecran->dessous;
        if (m->iface.ecran_fermer) m->iface.ecran_fermer(m->iface.contexte, sortie);
        ecran_detruire(m->ecran);
        m->ecran = d;
    }
    size_t ecrits = 0;
    m->fichiers_prevus = m->nb_a_ecrire > 0;
    if (ok && m->nb_a_ecrire) {
        /* écritures sur le disque, seulement si l'exécution a réussi (§ 15.2) */
        char *erreur = ecrire_sur_le_disque(m);
        if (erreur) {
            ok = 0;
            diag->message = erreur;
            diag->ligne = diag->colonne = 0;
            free(diag->fichier);
            diag->fichier = NULL;
        } else {
            ecrits = m->nb_a_ecrire;
        }
    }
    if (m->base) {
        /* la base valide en dernier ; si elle échoue, les fichiers écrits sont retirés */
        char *erreur = NULL;
        if (ok && !base_valider(m->base, &erreur)) {
            ok = 0;
            diag->message = erreur;
            diag->ligne = diag->colonne = 0;
            free(diag->fichier);
            diag->fichier = NULL;
            for (size_t k = 0; k < ecrits; k++) grym_supprimer(m->a_ecrire[k].chemin);
        } else if (!ok) {
            base_annuler(m->base);
        }
    }
    vider_ecritures(m);
    if (ok) valider(m);
    else annuler(m);
    ramasser(m, NULL, NULL, 0);
    if (ok) {
        for (size_t k = 0; k < nb_remplaces; k++) {
            bloc_detruire(remplaces[k].bloc);
            free(remplaces[k].liaison);
        }
    } else {
        /* La table des formules revient à son état d'avant. */
        for (size_t k = nb_remplaces; k > 0; k--) {
            Formule *f = &m->formules[remplaces[k - 1].index];
            bloc_detruire(f->bloc);
            free(f->liaison);
            f->bloc = remplaces[k - 1].bloc;
            f->liaison = remplaces[k - 1].liaison;
        }
        while (m->nb_formules > nb_avant) {
            Formule *f = &m->formules[--m->nb_formules];
            free(f->nom);
            bloc_detruire(f->bloc);
            free(f->liaison);
        }
    }
    free(remplaces);
    return ok;
}
