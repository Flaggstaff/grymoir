/* GrymoiR : machine virtuelle à pile, v0.2
 * Spécification : docs/vm.md (révision 1.14).
 */
#include "vm.h"
#include "vm_interne.h"
#include "base.h"
#include "date.h"
#include "decimal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* Valeurs (docs/vm.md, § 2)                                        */
/* ---------------------------------------------------------------- */


static const char *nom_type(TypeValeur t) {
    return t == V_NOMBRE ? "un nombre" : t == V_TEXTE ? "un texte" : t == V_BOOLEEN ? "un booléen"
         : t == V_DATE ? "une date" : t == V_FICHIER ? "un fichier" : t == V_LISTE ? "une liste"
         : t == V_ABSENT ? "absente" : "un objet";
}

static Valeur valeur_copier(const Valeur *v) {
    Valeur r;
    r.type = v->type;
    r.nombre = v->type == V_NOMBRE ? dec_copier(&v->nombre) : dec_zero();
    r.texte = (v->type == V_TEXTE || v->type == V_ABSENT) && v->texte ? grym_dupliquer(v->texte) : NULL;
    r.vrai = v->vrai;
    r.objet = v->objet;
    r.jours = v->jours;
    r.fichier = v->fichier;
    if (r.fichier) r.fichier->references++;
    r.liste = v->liste;
    if (r.liste) r.liste->references++;
    return r;
}

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
        for (size_t k = 0; k < v->liste->n; k++) free(v->liste->classes[k]);
        free(v->liste->classes);
        free(v->liste->ids);
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

/* Écriture sur le disque, différée à la fin de l'exécution (§ 15.2). */
typedef struct {
    char *chemin;      /* chemin effectif */
    char *ecrit;       /* chemin tel qu'écrit dans le programme */
    Fichier *fichier;
} Ecriture_disque;

struct Machine {
    char *dossier;          /* dossier du programme : base des chemins relatifs */
    char *chemin_base;      /* fichier de la base des entités, NULL : en mémoire (§ 16.5) */
    Base *base;             /* ouverte au premier besoin */
    int base_engagee;       /* dernière exécution : une base était en jeu (message d'annulation, § 3.3) */
    int fichiers_prevus;    /* dernière exécution : des fichiers devaient être écrits */
    long *carte_cles;       /* carte d'identité : identifiant en base → objet en mémoire (§ 16.4) */
    Objet **carte_objets;   /* clé 0 : case vide ; clé −1 : case libérée */
    size_t carte_cap, carte_n;
    Ecriture_disque *a_ecrire;
    size_t nb_a_ecrire;
    Case *cases;
    size_t nb_cases, cap_cases;
    Ecriture *journal;
    size_t nb_journal, cap_journal;
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

Machine *machine_creer(void) {
    Machine *m = grym_allouer(sizeof *m);
    memset(m, 0, sizeof *m);
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

void machine_base(Machine *m, const char *chemin) {
    free(m->chemin_base);
    m->chemin_base = chemin ? grym_dupliquer(chemin) : NULL;
}

void machine_dossier(Machine *m, const char *dossier) {
    free(m->dossier);
    m->dossier = dossier && *dossier ? grym_dupliquer(dossier) : NULL;
}

void machine_detruire(Machine *m) {
    if (!m) return;
    base_fermer(m->base);
    free(m->carte_cles);
    free(m->carte_objets);
    free(m->chemin_base);
    free(m->dossier);
    for (size_t i = 0; i < m->nb_cases; i++) {
        free(m->cases[i].nom);
        if (m->cases[i].definie) valeur_liberer(&m->cases[i].valeur);
    }
    free(m->cases);
    free(m->journal);
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
        }
        free(m->classes[i]->departs);
        free(m->classes[i]->types);
        free(m->classes[i]->uniques);
        free(m->classes[i]->pluriel);
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

/* Rejoue le journal du plus récent au plus ancien. */
static void annuler(Machine *m) {
    while (m->nb_journal) {
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

static char *chemin_effectif(const Machine *m, const char *ecrit) {
    if (ecrit[0] == '/' || !m->dossier) return grym_dupliquer(ecrit);
    return grym_formater("%s/%s", m->dossier, ecrit);
}

/* Lit un fichier du disque ; NULL et *erreur sinon. */
static Fichier *lire_fichier(const Machine *m, const char *ecrit, char **erreur) {
    if (!*ecrit) { *erreur = grym_dupliquer("Chemin de fichier vide."); return NULL; }
    char *chemin = chemin_effectif(m, ecrit);
    FILE *f = fopen(chemin, "rb");
    free(chemin);
    if (!f) { *erreur = grym_formater("Fichier « %s » introuvable ou illisible.", ecrit); return NULL; }
    Fichier *x = grym_allouer(sizeof *x);
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
        FILE *existe = fopen(e->chemin, "rb");
        if (existe) {
            fclose(existe);
            erreur = grym_formater("« %s » existe déjà : il n'est jamais écrasé.", e->ecrit);
            break;
        }
        FILE *f = fopen(e->chemin, "wb");
        int ok = f && fwrite(e->fichier->octets, 1, e->fichier->taille, f) == e->fichier->taille;
        if (f && fclose(f) != 0) ok = 0;
        if (!ok) {
            if (f) remove(e->chemin);
            erreur = grym_formater("Écriture de « %s » impossible.", e->ecrit);
            break;
        }
    }
    if (erreur) while (k > 0) remove(m->a_ecrire[--k].chemin);
    return erreur;
}

static char *decrire_valeur_pour_type(const Valeur *v) {
    if (v->type == V_NOMBRE) {
        char *n = dec_formater(&v->nombre);
        char *r = grym_formater("%s", n);
        free(n);
        return r;
    }
    if (v->type == V_OBJET) {
        return grym_formater("%s %s", v->objet->classe->feminin ? "une" : "un", v->objet->classe->nom);
    }
    return grym_dupliquer(nom_type(v->type));
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

Valeur vi_fichier(const void *octets, size_t taille, const char *nom) {
    Fichier *f = grym_allouer(sizeof *f);
    f->octets = grym_allouer(taille ? taille : 1);
    if (taille) memcpy(f->octets, octets, taille);
    f->taille = taille;
    f->nom = grym_dupliquer(nom);
    f->format = format_image(f->octets, taille);
    f->references = 1;
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
    return v->texte ? grym_formater("Le champ « %s » est absent : vérifiez-le d'abord avec « est présent ».", v->texte)
                    : grym_dupliquer("La valeur est absente : vérifiez-la d'abord avec « est présent ».");
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
static int charger(Machine *m, Objet *o, char **erreur) {
    if (!o->a_charger) return 1;
    if (!base_charger(m->base, m, o, erreur)) return 0;
    o->a_charger = 0;
    return 1;
}

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
static char *verifier_type_champ(const Machine *m, const char *champ, const char *type, const Valeur *v, int facultatif) {
    if (v->type == V_ABSENT)
        return facultatif ? NULL : grym_formater("Le champ « %s » n'est pas facultatif : il ne devient pas absent.", champ);
    if (!type) return NULL;
    int ok = 0;
    if (strcmp(type, "texte") == 0) ok = v->type == V_TEXTE;
    else if (strcmp(type, "nombre") == 0) ok = v->type == V_NOMBRE;
    else if (strcmp(type, "nombre entier") == 0) ok = v->type == V_NOMBRE && dec_est_entier(&v->nombre);
    else if (strcmp(type, "vrai ou faux") == 0) ok = v->type == V_BOOLEEN;
    else if (strcmp(type, "date") == 0) ok = v->type == V_DATE;
    else if (strcmp(type, "fichier") == 0) ok = v->type == V_FICHIER;
    else if (strcmp(type, "image") == 0) {
        if (v->type == V_FICHIER && !v->fichier->format)
            return grym_formater("« %s » n'est pas une image (PNG, JPEG, GIF ou WebP).", v->fichier->nom);
        ok = v->type == V_FICHIER;
    } else if (v->type == V_OBJET) {
        for (const ClasseVM *c = v->objet->classe; c && !ok; c = c->parent) ok = strcmp(c->nom, type) == 0;
    }
    if (ok) return NULL;
    const ClasseVM *e = classe_vm(m, type);
    char *attendu = strcmp(type, "vrai ou faux") == 0 ? grym_dupliquer("vrai ou faux")
                  : strcmp(type, "date") == 0 || strcmp(type, "image") == 0 ? grym_formater("une %s", type)
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
static Formule *choisir_version(Machine *m, const char *nom, const Valeur *premier, char **pourquoi) {
    Formule *f = formule_de(m, nom, NULL);
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
    for (const ClasseVM *c = premier->objet->classe; c; c = c->parent) {
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
    char *qui = article_classe(premier->objet->classe);
    *pourquoi = grym_formater("Aucune version de « %s » pour %s.", nom, qui);
    free(qui);
    return NULL;
}

int machine_executer(Machine *m, Module *module, Chaine *sortie, Diagnostic *diag) {
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;

    char *erreur = NULL;
    if (!module_verifier(module, &erreur)) {
        diag->message = grym_formater("Bytecode invalide : %s", erreur);
        free(erreur);
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
        unsigned char *uniques = grym_allouer(total ? total : 1);
        size_t n = 0;
        for (size_t q = 0; parent && q < parent->nb_champs; q++) {
            types[n] = parent->types[q];
            departs[n] = parent->departs[q];
            uniques[n] = parent->uniques[q];
            champs[n++] = parent->champs[q];
        }
        for (size_t q = 0; q < cm->nb_aptitudes && !probleme; q++)
            for (size_t r = 0; r < aptitudes[q]->nb_champs; r++) {
                types[n] = aptitudes[q]->types[r];
                departs[n] = aptitudes[q]->departs[r];
                uniques[n] = aptitudes[q]->uniques[r] & 2;   /* une aptitude n'a pas de champ unique */
                champs[n++] = aptitudes[q]->champs[r];
            }
        for (size_t q = 0; q < cm->nb_champs && !probleme; q++) {
            types[n] = cm->types ? cm->types[q] : NULL;
            departs[n] = cm->departs ? cm->departs[q] : NULL;
            uniques[n] = cm->uniques ? cm->uniques[q] : 0;
            champs[n++] = cm->champs[q];
        }
        /* Un type désigne un type de base ou une entité connue (ou la classe elle-même). */
        static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image" };
        for (size_t q = 0; q < n && !probleme; q++) {
            if (!types[q]) continue;
            int ok_type = strcmp(types[q], cm->nom) == 0 && cm->conserve;
            for (size_t r = 0; r < sizeof BASE / sizeof *BASE; r++) ok_type |= strcmp(types[q], BASE[r]) == 0;
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
        c->pluriel = cm->pluriel ? grym_dupliquer(cm->pluriel) : NULL;
        c->champs = grym_allouer((n ? n : 1) * sizeof *c->champs);
        c->types = grym_allouer((n ? n : 1) * sizeof *c->types);
        c->departs = grym_allouer((n ? n : 1) * sizeof *c->departs);
        c->uniques = uniques;
        for (size_t q = 0; q < n; q++) {
            c->champs[q] = grym_dupliquer(champs[q]);
            c->types[q] = types[q] ? grym_dupliquer(types[q]) : NULL;
            c->departs[q] = departs[q] ? grym_dupliquer(departs[q]) : NULL;
        }
        free(champs);
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
    m->base_engagee = entites;
    m->fichiers_prevus = 0;
    if (entites) {
        char *erreur = NULL;
        if (!m->base) m->base = base_ouvrir(m->chemin_base, &erreur);
        int pret = m->base && base_commencer(m->base, &erreur);
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

    m->epoque++;
    long aujourdhui = date_aujourdhui();
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
        if (code == I_SAUTER || code == I_SAUTER_SI_FAUX)
            cible = (size_t)b->code[ip + 1] | ((size_t)b->code[ip + 2] << 8)
                  | ((size_t)b->code[ip + 3] << 16) | ((size_t)b->code[ip + 4] << 24);
        ip += instruction_taille(code);
        cadre->ip = ip;

        if (code == I_RETOUR || code == I_RENDRE) {
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
        case I_AFFICHER: {
            /* Les éléments sont dans la pile, du premier au dernier. */
            size_t base = pile.n - op;
            for (size_t k = 0; k < op; k++) {
                Valeur *v = &pile.v[base + k];
                if (k) chaine_ajouter(sortie, " ");
                if (v->type == V_TEXTE) {
                    chaine_ajouter(sortie, v->texte);
                } else if (v->type == V_BOOLEEN) {
                    chaine_ajouter(sortie, v->vrai ? "vrai" : "faux");
                } else if (v->type == V_ABSENT) {
                    chaine_ajouter(sortie, "absent");
                } else if (v->type == V_FICHIER) {
                    char *t = decrire_fichier(v->fichier);
                    chaine_ajouter(sortie, t);
                    free(t);
                } else if (v->type == V_DATE) {
                    char *t = date_suisse(v->jours);
                    chaine_ajouter(sortie, t);
                    free(t);
                } else if (v->type == V_OBJET) {
                    char *t = article_classe(v->objet->classe);
                    chaine_ajouter(sortie, t);
                    free(t);
                } else {
                    char *s = dec_formater(&v->nombre);
                    chaine_ajouter(sortie, s);
                    free(s);
                }
            }
            chaine_ajouter(sortie, "\n");
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
            if (va.type != vb.type || (!egalite && va.type != V_NOMBRE && va.type != V_DATE)) {
                ok = echouer(diag, b, debut, va.type != vb.type
                    ? grym_formater("Comparaison impossible entre %s et %s.",
                                    nom_type(va.type), nom_type(vb.type))
                    : grym_formater("Seuls deux nombres ou deux dates se comparent par ordre : la valeur est %s.",
                                    nom_type(va.type)));
            } else if (va.type == V_NOMBRE || va.type == V_DATE) {
                int c = va.type == V_DATE ? (va.jours > vb.jours) - (va.jours < vb.jours)
                                          : dec_comparer(&va.nombre, &vb.nombre);
                resultat = code == I_EGAL ? c == 0 : code == I_DIFFERENT ? c != 0
                         : code == I_INFERIEUR ? c < 0 : code == I_SUPERIEUR ? c > 0
                         : code == I_INFERIEUR_OU_EGAL ? c <= 0 : c >= 0;
            } else {
                int egaux = va.type == V_BOOLEEN ? va.vrai == vb.vrai
                          : va.type == V_OBJET  ? va.objet == vb.objet       /* identité */
                          : va.type == V_FICHIER ? va.fichier->taille == vb.fichier->taille
                                                   && memcmp(va.fichier->octets, vb.fichier->octets, va.fichier->taille) == 0
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
                char *v = x->type == V_NOMBRE ? dec_formater(&x->nombre) : grym_dupliquer(nom_type(x->type));
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
            char *chemin = probleme ? NULL : chemin_effectif(m, c.texte);
            if (chemin) {
                FILE *existe = fopen(chemin, "rb");
                if (existe) {
                    fclose(existe);
                    probleme = grym_formater("« %s » existe déjà : il n'est jamais écrasé.", c.texte);
                }
                for (size_t k = 0; k < m->nb_a_ecrire && !probleme; k++)
                    if (strcmp(m->a_ecrire[k].chemin, chemin) == 0)
                        probleme = grym_formater("« %s » est déjà enregistré par cette exécution.", c.texte);
            }
            if (probleme) {
                free(chemin);
                valeur_liberer(&c);
                valeur_liberer(&v);
                ok = echouer(diag, b, debut, probleme);
                break;
            }
            Ecriture_disque *t = grym_allouer((m->nb_a_ecrire + 1) * sizeof *t);
            if (m->nb_a_ecrire) memcpy(t, m->a_ecrire, m->nb_a_ecrire * sizeof *t);
            free(m->a_ecrire);
            m->a_ecrire = t;
            t[m->nb_a_ecrire].chemin = chemin;
            t[m->nb_a_ecrire].ecrit = grym_dupliquer(c.texte);
            t[m->nb_a_ecrire].fichier = v.fichier;   /* la référence passe à l'écriture */
            m->nb_a_ecrire++;
            v.fichier = NULL;
            valeur_liberer(&c);
            valeur_liberer(&v);
            break;
        }
        case I_CHERCHER: {
            /* recherche dans la base (§ 16.4) : les valeurs comparées sont au sommet de la pile */
            long np = requete_parametres(b->constantes[op].texte);
            Valeur r;
            char *erreur = NULL;
            int trouve = base_chercher(m->base, m, b->constantes[op].texte, &pile.v[pile.n - (size_t)np],
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
            snprintf(t, sizeof t, "%lu", (unsigned long)(l.type == V_LISTE ? l.liste->n : 0));
            valeur_liberer(&l);
            empiler(&pile, valeur_nombre(dec_depuis_canonique(t)));
            break;
        }
        case I_ELEMENT: {
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
        case I_SUPPRIMER: {
            Valeur v = depiler(&pile);
            const char *verbe = code == I_CONSERVER ? "conserve" : "supprime";
            char *probleme = NULL;
            if (v.type != V_OBJET)
                probleme = grym_formater("Seul un objet se %s : la valeur est %s.", verbe, nom_type(v.type));
            else if (!v.objet->classe->conserve)
                probleme = grym_formater("« %s » n'est pas une entité : ses objets ne se %s pas. "
                                         "Déclarez « %s %s, %s, a : ».", v.objet->classe->nom,
                                         code == I_CONSERVER ? "conservent" : "suppriment",
                                         v.objet->classe->feminin ? "Une" : "Un", v.objet->classe->nom,
                                         v.objet->classe->feminin ? "conservée" : "conservé");
            else if (code == I_CONSERVER && v.objet->id) {
                char *qui = article_classe(v.objet->classe);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s déjà conservé%s ne se conserve pas deux fois.", qui,
                                         v.objet->classe->feminin ? "e" : "");
                free(qui);
            } else if (code == I_SUPPRIMER && !v.objet->id) {
                char *qui = article_classe(v.objet->classe);
                qui[0] = (char)(qui[0] - 32);
                probleme = grym_formater("%s qui n'est pas conservé%s ne se supprime pas.", qui,
                                         v.objet->classe->feminin ? "e" : "");
                free(qui);
            } else if (code == I_CONSERVER) {
                long id = 0;
                if (base_conserver(m->base, v.objet, &id, &probleme)) ecrire_id(m, v.objet, id);
            } else if (charger(m, v.objet, &probleme)   /* l'objet reste en mémoire, avec ses valeurs */
                       && base_supprimer(m->base, v.objet, m->classes, m->nb_classes, &probleme)) {
                ecrire_id(m, v.objet, 0);   /* l'objet reste en mémoire, mais n'est plus conservé */
            }
            valeur_liberer(&v);
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
                    r.texte = grym_dupliquer(strcmp(champ, "format") == 0 ? (f->format ? f->format : "inconnu") : f->nom);
                } else {
                    ok = echouer(diag, b, debut, grym_formater(
                        "Un fichier n'a pas de champ « %s » : taille, format ou nom de fichier.", champ));
                    break;
                }
                valeur_liberer(vo);
                *vo = r;
                break;
            }
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
            if (code != I_LIRE_CHAMP) {
                char *probleme = verifier_type_champ(m, champ, o->classe->types[k], &pile.v[pile.n - 1],
                                                     (o->classe->uniques[k] & 2) != 0);
                if (probleme) { ok = echouer(diag, b, debut, probleme); break; }
            }
            if (code == I_LIRE_CHAMP) {
                if (!o->definis[k] && (o->classe->uniques[k] & 2)) {   /* facultatif : absent */
                    valeur_liberer(vo);
                    *vo = vi_absent(champ);
                    break;
                }
                if (!o->definis[k]) {
                    ok = echouer(diag, b, debut, grym_formater("Le champ « %s » n'a pas de valeur.", champ));
                    break;
                }
                Valeur v = valeur_copier(&o->champs[k]);
                valeur_liberer(vo);
                *vo = v;
            } else {
                Valeur v = depiler(&pile);
                ecrire_champ(m, o, (size_t)k, v);
                if (code == I_ECRIRE_CHAMP) {
                    Valeur ob = depiler(&pile);
                    valeur_liberer(&ob);
                    /* objet conservé : la base suit aussitôt, dans la transaction (§ 16.3) */
                    char *erreur = NULL;
                    if (o->id && !base_ecrire_champ(m->base, o, (size_t)k, &erreur)) {
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
        if (!ok) break;
    }

    vider(&pile);
    for (size_t k = nb_cadres; k > 0; k--) liberer_cadre(&cadres[k - 1]);
    free(cadres);
    free(liaison_principale);
    size_t ecrits = 0;
    m->fichiers_prevus = m->nb_a_ecrire > 0;
    if (ok && m->nb_a_ecrire) {
        /* écritures sur le disque, seulement si l'exécution a réussi (§ 15.2) */
        char *erreur = ecrire_sur_le_disque(m);
        if (erreur) {
            ok = 0;
            diag->message = erreur;
            diag->ligne = diag->colonne = 0;
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
            for (size_t k = 0; k < ecrits; k++) remove(m->a_ecrire[k].chemin);
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
