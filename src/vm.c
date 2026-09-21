/* GrymoiR : machine virtuelle à pile, v0.2
 * Spécification : docs/vm.md (révision 1.4).
 */
#include "vm.h"
#include "decimal.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* Valeurs (docs/vm.md, § 2)                                        */
/* ---------------------------------------------------------------- */

typedef enum { V_NOMBRE, V_TEXTE, V_BOOLEEN, V_OBJET } TypeValeur;

struct Objet;

typedef struct {
    TypeValeur type;
    Decimal nombre;
    char *texte;
    int vrai;
    struct Objet *objet;   /* référence : l'objet appartient au tas, pas à la valeur */
} Valeur;

/* Classe connue de la machine (grammaire, § 13). */
typedef struct {
    char *nom;
    int feminin;
    char **champs;
    size_t nb_champs;
} ClasseVM;

/* Objet du tas : sa classe, ses champs, et de quoi le ramasser (docs/vm.md, § 7). */
typedef struct Objet {
    const ClasseVM *classe;
    Valeur *champs;
    unsigned char *definis;
    unsigned long *epoques;   /* exécution où le champ est entré au journal */
    int marque;
    struct Objet *suivant;
} Objet;

static const char *nom_type(TypeValeur t) {
    return t == V_NOMBRE ? "un nombre" : t == V_TEXTE ? "un texte" : t == V_BOOLEEN ? "un booléen" : "un objet";
}

static Valeur valeur_copier(const Valeur *v) {
    Valeur r;
    r.type = v->type;
    r.nombre = v->type == V_NOMBRE ? dec_copier(&v->nombre) : dec_zero();
    r.texte = v->type == V_TEXTE ? grym_dupliquer(v->texte) : NULL;
    r.vrai = v->vrai;
    r.objet = v->objet;
    return r;
}

static void valeur_liberer(Valeur *v) {
    dec_liberer(&v->nombre);
    free(v->texte);
    v->texte = NULL;
}

static Valeur valeur_nombre(Decimal d) {
    Valeur v;
    v.type = V_NOMBRE;
    v.nombre = d;
    v.texte = NULL;
    v.vrai = 0;
    v.objet = NULL;
    return v;
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
    size_t index;
    int etait_definie;
    Valeur ancienne;
} Ecriture;

typedef struct {
    char *nom;
    Bloc *bloc;
    size_t *liaison;   /* noms du bloc → cases globales */
} Formule;

struct Machine {
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

void machine_detruire(Machine *m) {
    if (!m) return;
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
        free(m->classes[i]->champs);
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

/* Rejoue le journal du plus récent au plus ancien. */
static void annuler(Machine *m) {
    while (m->nb_journal) {
        Ecriture *e = &m->journal[--m->nb_journal];
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

static Formule *formule_de(Machine *m, const char *nom) {
    for (size_t i = 0; i < m->nb_formules; i++)
        if (strcmp(m->formules[i].nom, nom) == 0) return &m->formules[i];
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

size_t machine_objets_vivants(const Machine *m) {
    return m->nb_objets;
}

static const ClasseVM *classe_vm(const Machine *m, const char *nom) {
    for (size_t i = m->nb_classes; i > 0; i--)
        if (strcmp(m->classes[i - 1]->nom, nom) == 0) return m->classes[i - 1];
    return NULL;
}

static long index_champ(const ClasseVM *c, const char *champ) {
    for (size_t k = 0; k < c->nb_champs; k++)
        if (strcmp(c->champs[k], champ) == 0) return (long)k;
    return -1;
}

static char *article_classe(const ClasseVM *c) {
    return grym_formater("%s %s", c->feminin ? "une" : "un", c->nom);
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

    /* Enregistrement des formules (docs/vm.md, § 5) : une formule du même nom est remplacée. */
    size_t nb_avant = m->nb_formules;
    Remplacement *remplaces = grym_allouer(module->nb * sizeof *remplaces);
    size_t nb_remplaces = 0;
    for (size_t k = 1; k < module->nb; k++) {
        Bloc *f = module->blocs[k];
        module->blocs[k] = NULL;
        Formule *ex = formule_de(m, f->nom);
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

    /* Classes du module : ajoutées à la machine ; une redéclaration l'emporte pour la suite. */
    for (size_t k = 0; k < module->nb_classes; k++) {
        const ClasseModule *cm = &module->classes[k];
        ClasseVM *c = grym_allouer(sizeof *c);
        c->nom = grym_dupliquer(cm->nom);
        c->feminin = cm->feminin;
        c->nb_champs = cm->nb_champs;
        c->champs = grym_allouer((cm->nb_champs ? cm->nb_champs : 1) * sizeof *c->champs);
        for (size_t q = 0; q < cm->nb_champs; q++) c->champs[q] = grym_dupliquer(cm->champs[q]);
        ClasseVM **t = grym_allouer((m->nb_classes + 1) * sizeof *t);
        if (m->nb_classes) memcpy(t, m->classes, m->nb_classes * sizeof *t);
        free(m->classes);
        m->classes = t;
        m->classes[m->nb_classes++] = c;
    }
    if (m->seuil == 0) m->seuil = SEUIL_RAMASSAGE;

    m->epoque++;
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
            int egalite = code == I_EGAL || code == I_DIFFERENT;
            int resultat = 0;
            if (va.type != vb.type || (!egalite && va.type != V_NOMBRE)) {
                ok = echouer(diag, b, debut, va.type != vb.type
                    ? grym_formater("Comparaison impossible entre %s et %s.",
                                    nom_type(va.type), nom_type(vb.type))
                    : grym_formater("Seuls deux nombres se comparent par ordre : la valeur est %s.",
                                    nom_type(va.type)));
            } else if (va.type == V_NOMBRE) {
                int c = dec_comparer(&va.nombre, &vb.nombre);
                resultat = code == I_EGAL ? c == 0 : code == I_DIFFERENT ? c != 0
                         : code == I_INFERIEUR ? c < 0 : code == I_SUPERIEUR ? c > 0
                         : code == I_INFERIEUR_OU_EGAL ? c <= 0 : c >= 0;
            } else {
                int egaux = va.type == V_BOOLEEN ? va.vrai == vb.vrai
                          : va.type == V_OBJET  ? va.objet == vb.objet       /* identité */
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
                char *v = x->type == V_NOMBRE ? dec_formater(&x->nombre)
                                              : grym_dupliquer(x->type == V_TEXTE ? "un texte" : "un booléen");
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
        case I_NOUVEAU: {
            const ClasseVM *cl = classe_vm(m, b->noms[op]);
            if (!cl) {
                ok = echouer(diag, b, debut, grym_formater("Classe « %s » inconnue.", b->noms[op]));
                break;
            }
            if (m->depuis_ramassage >= m->seuil) ramasser(m, &pile, cadres, nb_cadres);
            Objet *o = grym_allouer(sizeof *o);
            size_t nc = cl->nb_champs ? cl->nb_champs : 1;
            o->classe = cl;
            o->champs = grym_allouer(nc * sizeof *o->champs);
            o->definis = grym_allouer(nc);
            memset(o->definis, 0, nc);
            o->epoques = grym_allouer(nc * sizeof *o->epoques);
            for (size_t q = 0; q < nc; q++) o->epoques[q] = m->epoque;   /* objet neuf : rien à journaliser */
            o->marque = 0;
            o->suivant = m->tas;
            m->tas = o;
            m->nb_objets++;
            m->depuis_ramassage++;
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
            if (vo->type != V_OBJET) {
                ok = echouer(diag, b, debut, grym_formater(
                    "« %s » : la valeur n'est pas un objet, c'est %s.", champ, nom_type(vo->type)));
                break;
            }
            Objet *o = vo->objet;
            long k = index_champ(o->classe, champ);
            if (k < 0) {
                char *qui = article_classe(o->classe);
                qui[0] = (char)(qui[0] - 32);
                ok = echouer(diag, b, debut, grym_formater("%s n'a pas de champ « %s ».", qui, champ));
                free(qui);
                break;
            }
            if (code == I_LIRE_CHAMP) {
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
            Formule *f = formule_de(m, nom);
            if (!f) {
                ok = echouer(diag, b, debut, grym_formater("Formule « %s » inconnue.", nom));
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
