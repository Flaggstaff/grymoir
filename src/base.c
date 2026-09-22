/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.25), § 16 ; docs/vm.md (révision 1.18), § 8.
 */
#include "base.h"
#include "date.h"
#include "texte.h"

#include "sqlite3.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Base {
    sqlite3 *db;
    char *chemin;     /* tel qu'affiché dans les messages */
    int transaction;
};

/* ---------------------------------------------------------------- */
/* Outils                                                           */
/* ---------------------------------------------------------------- */

/* Nom SQL entre guillemets doubles : « e client », « c date d'inscription ». */
static void ajouter_nom(Chaine *c, const char *prefixe, const char *nom) {
    chaine_ajouter(c, "\"");
    chaine_ajouter(c, prefixe);
    for (const char *p = nom; *p; p++) {
        char t[3] = { *p, 0, 0 };
        if (*p == '"') t[1] = '"';
        chaine_ajouter(c, t);
    }
    chaine_ajouter(c, "\"");
}

static int executer(Base *b, const char *sql, char **erreur) {
    char *msg = NULL;
    if (sqlite3_exec(b->db, sql, NULL, NULL, &msg) == SQLITE_OK) return 1;
    *erreur = grym_formater("Base « %s » : %s.", b->chemin, msg ? msg : sqlite3_errmsg(b->db));
    sqlite3_free(msg);
    return 0;
}

static int est_fichier(const char *type) {
    return strcmp(type, "fichier") == 0 || strcmp(type, "image") == 0;
}

static int est_lien(const char *type) {
    static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image",
                                        "année" };
    for (size_t k = 0; k < sizeof BASE / sizeof *BASE; k++) if (strcmp(type, BASE[k]) == 0) return 0;
    return 1;
}

static int commence_par_voyelle(const char *s) {
    return strchr("aeiouyAEIOUY", s[0]) != NULL || strncmp(s, "é", strlen("é")) == 0 || strncmp(s, "h", 1) == 0;
}

static char *pluriel(const ClasseVM *c) {
    return c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
}

/* « un client », « une facture » */
static char *un(const ClasseVM *c) {
    return grym_formater("%s %s", c->feminin ? "une" : "un", c->nom);
}

/* Lignée d'une entité, de la racine à la classe elle-même. */
static size_t lignee(const ClasseVM *c, const ClasseVM **l, size_t max) {
    size_t n = 0;
    for (const ClasseVM *x = c; x && n < max; x = x->parent) n++;
    size_t k = n;
    for (const ClasseVM *x = c; x && k > 0; x = x->parent) l[--k] = x;
    return n;
}

#define LIGNEE_MAX 256

/* Champ multiple (grammaire, § 16.13) : pas de colonne, une table de liaison « m <entité>.<champ> ». */
static int multiple(const ClasseVM *c, size_t k) {
    return (c->uniques[k] & 8) != 0;
}

static void ajouter_liaison(Chaine *sql, const char *entite, const char *champ) {
    char *nom = grym_formater("%s.%s", entite, champ);
    ajouter_nom(sql, "m ", nom);
    free(nom);
}

/* Table de liaison : « a » désigne l'objet qui porte le champ, « b » l'objet gagné. Effacer le premier
 * efface ses liaisons ; effacer le second est refusé tant qu'une liaison le désigne (base_supprimer). */
static int creer_liaison(Base *b, const char *entite, const char *champ, const char *type, char **erreur) {
    Chaine sql = {0};
    chaine_ajouter(&sql, "CREATE TABLE ");
    ajouter_liaison(&sql, entite, champ);
    chaine_ajouter(&sql, " (a INTEGER NOT NULL REFERENCES ");
    ajouter_nom(&sql, "e ", entite);
    chaine_ajouter(&sql, "(id) ON DELETE CASCADE, b INTEGER NOT NULL REFERENCES ");
    ajouter_nom(&sql, "e ", type);
    chaine_ajouter(&sql, "(id), PRIMARY KEY (a, b)); CREATE INDEX ");
    char *index = grym_formater("%s.%s", entite, champ);
    ajouter_nom(&sql, "i ", index);
    free(index);
    chaine_ajouter(&sql, " ON ");
    ajouter_liaison(&sql, entite, champ);
    chaine_ajouter(&sql, " (b);");
    char *t = chaine_rendre(&sql);
    int ok = executer(b, t, erreur);
    free(t);
    return ok;
}

static long compter_liaisons(Base *b, const char *entite, const char *champ) {
    Chaine sql = {0};
    chaine_ajouter(&sql, "SELECT count(*) FROM ");
    ajouter_liaison(&sql, entite, champ);
    char *t = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    long n = 0;
    if (sqlite3_prepare_v2(b->db, t, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    free(t);
    return n;
}

int base_collations(sqlite3 *db);

/* ---------------------------------------------------------------- */
/* Ouverture et transaction                                         */
/* ---------------------------------------------------------------- */

Base *base_ouvrir(const char *chemin, char **erreur) {
    Base *b = grym_allouer(sizeof *b);
    b->db = NULL;
    b->transaction = 0;
    b->chemin = grym_dupliquer(chemin ? chemin : "en mémoire");
    if (sqlite3_open_v2(chemin ? chemin : ":memory:", &b->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)
        != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : ouverture impossible (%s).", b->chemin,
                                b->db ? sqlite3_errmsg(b->db) : "mémoire épuisée");
        base_fermer(b);
        return NULL;
    }
    sqlite3_busy_timeout(b->db, 2000);
    if (!base_collations(b->db)) {
        *erreur = grym_formater("Base « %s » : collations impossibles.", b->chemin);
        base_fermer(b);
        return NULL;
    }
    if (!executer(b, "PRAGMA foreign_keys = ON;"
                     "CREATE TABLE IF NOT EXISTS grym_objet (id INTEGER PRIMARY KEY AUTOINCREMENT, classe TEXT NOT NULL, "
                     "supprime TEXT, supprime_avec INTEGER);"
                     "CREATE TABLE IF NOT EXISTS grym_schema (entite TEXT PRIMARY KEY, definition TEXT NOT NULL);",
                  erreur)) {
        base_fermer(b);
        return NULL;
    }
    /* Base d'avant la corbeille (§ 16.12) : ses objets reçoivent les deux colonnes, vides. */
    sqlite3_stmt *st = NULL;
    int present = 0;
    sqlite3_prepare_v2(b->db, "SELECT count(*) FROM pragma_table_info('grym_objet') WHERE name = 'supprime'", -1, &st, NULL);
    if (sqlite3_step(st) == SQLITE_ROW) present = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (!present && !executer(b, "ALTER TABLE grym_objet ADD COLUMN supprime TEXT;"
                                 "ALTER TABLE grym_objet ADD COLUMN supprime_avec INTEGER;", erreur)) {
        base_fermer(b);
        return NULL;
    }
    return b;
}

void base_fermer(Base *b) {
    if (!b) return;
    if (b->transaction) base_annuler(b);
    sqlite3_close(b->db);
    free(b->chemin);
    free(b);
}

int base_commencer(Base *b, char **erreur) {
    if (b->transaction) return 1;
    /* IMMEDIATE : la base est réservée en écriture dès le début de l'exécution (§ 16.6). */
    int rc = sqlite3_exec(b->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
    if (rc == SQLITE_BUSY) {
        *erreur = grym_formater("La base « %s » est utilisée par un autre programme.", b->chemin);
        return 0;
    }
    if (rc != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    b->transaction = 1;
    return 1;
}

int base_valider(Base *b, char **erreur) {
    if (!b->transaction) return 1;
    b->transaction = 0;
    if (sqlite3_exec(b->db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK) return 1;
    *erreur = grym_formater("Base « %s » : validation impossible (%s).", b->chemin, sqlite3_errmsg(b->db));
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
    return 0;
}

void base_annuler(Base *b) {
    if (!b->transaction) return;
    b->transaction = 0;
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
}

/* ---------------------------------------------------------------- */
/* Schéma                                                           */
/* ---------------------------------------------------------------- */

/* Définition d'une entité : parent, puis chaque champ de sa table, « nom:type:unique ». */
static char *definition(const ClasseVM *c) {
    Chaine d = {0};
    chaine_ajouter(&d, "parent=");
    chaine_ajouter(&d, c->parent ? c->parent->nom : "");
    for (size_t k = 0; k < c->nb_champs; k++) {
        if (c->proprietaires[k] != c) continue;
        chaine_ajouter(&d, ";");
        chaine_ajouter(&d, c->champs[k]);
        chaine_ajouter(&d, ":");
        chaine_ajouter(&d, c->types[k] ? c->types[k] : "");
        chaine_ajouter(&d, c->uniques[k] & 1 ? ":unique" : "");
        chaine_ajouter(&d, c->uniques[k] & 2 ? ":facultatif" : "");
        chaine_ajouter(&d, c->uniques[k] & 4 ? ":cascade" : "");
        chaine_ajouter(&d, c->uniques[k] & 8 ? ":multiple" : "");
    }
    return chaine_rendre(&d);
}

/* ---------------------------------------------------------------- */
/* Migrations (grammaire, § 16.7)                                   */
/* ---------------------------------------------------------------- */

typedef struct {
    char *parent;
    char **noms, **types;
    int *uniques;
    size_t n;
} Definition;

/* « parent=P;nom:type[:unique];… » */
static void lire_definition(const char *d, Definition *x) {
    memset(x, 0, sizeof *x);
    char *copie = grym_dupliquer(d);
    size_t cap = 8;
    x->noms = grym_allouer(cap * sizeof *x->noms);
    x->types = grym_allouer(cap * sizeof *x->types);
    x->uniques = grym_allouer(cap * sizeof *x->uniques);
    char *p = copie;
    int premier = 1;
    while (p) {
        char *fin = strchr(p, ';');
        if (fin) *fin = '\0';
        if (premier) {
            x->parent = grym_dupliquer(strncmp(p, "parent=", 7) == 0 ? p + 7 : "");
            premier = 0;
        } else {
            if (x->n == cap) {
                cap *= 2;
                char **n2 = grym_allouer(cap * sizeof *n2), **t2 = grym_allouer(cap * sizeof *t2);
                int *u2 = grym_allouer(cap * sizeof *u2);
                memcpy(n2, x->noms, x->n * sizeof *n2);
                memcpy(t2, x->types, x->n * sizeof *t2);
                memcpy(u2, x->uniques, x->n * sizeof *u2);
                free(x->noms); free(x->types); free(x->uniques);
                x->noms = n2; x->types = t2; x->uniques = u2;
            }
            /* « nom:type[:unique][:facultatif] » ; bits : 1 unique, 2 facultatif */
            int drapeaux = (strstr(p, ":unique") ? 1 : 0) | (strstr(p, ":facultatif") ? 2 : 0)
                         | (strstr(p, ":cascade") ? 4 : 0) | (strstr(p, ":multiple") ? 8 : 0);
            char *d1 = strchr(p, ':');
            char *d2 = d1 ? strchr(d1 + 1, ':') : NULL;
            if (d1) *d1 = '\0';
            if (d2) *d2 = '\0';
            x->noms[x->n] = grym_dupliquer(p);
            x->types[x->n] = grym_dupliquer(d1 ? d1 + 1 : "");
            x->uniques[x->n] = drapeaux;
            x->n++;
        }
        p = fin ? fin + 1 : NULL;
    }
    free(copie);
}

static void liberer_definition(Definition *x) {
    for (size_t k = 0; k < x->n; k++) { free(x->noms[k]); free(x->types[k]); }
    free(x->noms); free(x->types); free(x->uniques); free(x->parent);
}

static long chercher_champ(const Definition *x, const char *nom) {
    for (size_t k = 0; k < x->n; k++) if (strcmp(x->noms[k], nom) == 0) return (long)k;
    return -1;
}

static long compter_lignes(Base *b, const ClasseVM *c) {
    Chaine sql = {0};
    chaine_ajouter(&sql, "SELECT count(*) FROM ");
    ajouter_nom(&sql, "e ", c->nom);
    char *t = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, t, -1, &st, NULL);
    free(t);
    long n = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    return n;
}

/* Littéral SQL d'une valeur de départ (forme canonique), ou d'une valeur neutre pour une table vide. */
static void ajouter_litteral(Chaine *sql, const char *type, const char *depart) {
    if (strcmp(type, "nombre entier") == 0 || strcmp(type, "année") == 0) {
        char *t = grym_dupliquer(depart ? depart : strcmp(type, "année") == 0 ? "1" : "0");
        char *point = strchr(t, '.');
        if (point) *point = '\0';                 /* « 3.0 » : un entier */
        chaine_ajouter(sql, t);
        free(t);
    } else if (strcmp(type, "vrai ou faux") == 0) {
        chaine_ajouter(sql, depart && strcmp(depart, "vrai") == 0 ? "1" : "0");
    } else {
        const char *v = depart ? depart : strcmp(type, "nombre") == 0 ? "0" : strcmp(type, "date") == 0 ? "0001-01-01" : "";
        chaine_ajouter(sql, "'");
        for (const char *p = v; *p; p++) chaine_ajouter(sql, *p == '\'' ? "''" : (char[2]){ *p, 0 });
        chaine_ajouter(sql, "'");
    }
}

static int executer_chaine(Base *b, Chaine *sql, char **erreur) {
    char *t = chaine_rendre(sql);
    int ok = executer(b, t, erreur);
    free(t);
    return ok;
}

/* Index d'unicité ajouté par une migration (une contrainte UNIQUE ne s'ajoute pas à une table existante). */
static int creer_index_unique(Base *b, const ClasseVM *c, const char *champ, char **erreur) {
    Chaine sql = {0};
    chaine_ajouter(&sql, "CREATE UNIQUE INDEX ");
    char *nom = grym_formater("%s.%s", c->nom, champ);
    ajouter_nom(&sql, "u ", nom);
    free(nom);
    chaine_ajouter(&sql, " ON ");
    ajouter_nom(&sql, "e ", c->nom);
    chaine_ajouter(&sql, " (");
    ajouter_nom(&sql, "c ", champ);
    chaine_ajouter(&sql, ")");
    char *t = chaine_rendre(&sql);
    int ok = sqlite3_exec(b->db, t, NULL, NULL, NULL) == SQLITE_OK;
    free(t);
    if (!ok) *erreur = grym_formater("« %s » devient unique, mais des %s conservés partagent déjà une même valeur.",
                                     champ, c->pluriel ? c->pluriel : c->nom);
    return ok;
}

/* « 1 client est déjà conservé », « 3 clients sont déjà conservés » */
static char *deja_conserves(const ClasseVM *c, long n) {
    if (n == 1) return grym_formater("1 %s est déjà conservé%s", c->nom, c->feminin ? "e" : "");
    char *pl = c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
    char *r = grym_formater("%ld %s sont déjà conservé%ss", n, pl, c->feminin ? "e" : "");
    free(pl);
    return r;
}

static int migrer(Base *b, const ClasseVM *c, const char *ancienne, const char *nouvelle, char **erreur) {
    Definition a, n;
    lire_definition(ancienne, &a);
    lire_definition(nouvelle, &n);
    long lignes = compter_lignes(b, c);
    char *pl = c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
    int ok = 1;
    char *deja = deja_conserves(c, lignes);
    if (strcmp(a.parent, n.parent) != 0) {
        *erreur = grym_formater("« %s » ne peut pas changer de classe parente : %s.", c->nom, deja);
        ok = 0;
    }
    /* un champ retiré, ou renommé : ses valeurs seraient perdues */
    for (size_t k = 0; ok && k < a.n; k++)
        if (chercher_champ(&n, a.noms[k]) < 0 && (a.uniques[k] & 8)) {
            /* champ multiple retiré : sa table de liaison part si elle est vide (§ 16.13) */
            long liaisons = compter_liaisons(b, c->nom, a.noms[k]);
            if (liaisons > 0) {
                *erreur = grym_formater("« %s » a disparu de « %s » : %ld liaison%s serai%s perdue%s. Remettez ce champ.",
                                        a.noms[k], c->nom, liaisons, liaisons > 1 ? "s" : "", liaisons > 1 ? "ent" : "t",
                                        liaisons > 1 ? "s" : "");
                ok = 0;
            } else {
                Chaine sql = {0};
                chaine_ajouter(&sql, "DROP TABLE ");
                ajouter_liaison(&sql, c->nom, a.noms[k]);
                ok = executer_chaine(b, &sql, erreur);
            }
        } else if (chercher_champ(&n, a.noms[k]) < 0 && lignes > 0) {
            *erreur = grym_formater("« %s » a disparu de « %s » : %ld valeur%s conservée%s serai%s perdue%s. Remettez ce "
                                    "champ ; un renommage se déclare comme un retrait suivi d'un ajout, et n'est pas "
                                    "encore pris en charge.", a.noms[k], c->nom, lignes, lignes > 1 ? "s" : "",
                                    lignes > 1 ? "s" : "", lignes > 1 ? "ent" : "t", lignes > 1 ? "s" : "");
            ok = 0;
        } else if (chercher_champ(&n, a.noms[k]) < 0) {
            Chaine sql = {0};   /* table vide : la colonne part sans rien emporter */
            chaine_ajouter(&sql, "ALTER TABLE ");
            ajouter_nom(&sql, "e ", c->nom);
            chaine_ajouter(&sql, " DROP COLUMN ");
            ajouter_nom(&sql, "c ", a.noms[k]);
            if (est_fichier(a.types[k])) {
                chaine_ajouter(&sql, "; ALTER TABLE ");
                ajouter_nom(&sql, "e ", c->nom);
                chaine_ajouter(&sql, " DROP COLUMN ");
                ajouter_nom(&sql, "n ", a.noms[k]);
            }
            if (!executer_chaine(b, &sql, erreur)) {
                free(*erreur);
                *erreur = grym_formater("« %s » ne peut pas être retiré de « %s » : c'est un champ unique ou un lien, "
                                        "ce que SQLite ne retire pas d'une table existante.", a.noms[k], c->nom);
                ok = 0;
            }
        }
    /* un champ gardé : même type, ou nombre entier devenu nombre */
    for (size_t k = 0; ok && k < n.n; k++) {
        long i = chercher_champ(&a, n.noms[k]);
        if (i < 0) continue;
        if ((a.uniques[i] & 8) != (n.uniques[k] & 8)) {
            *erreur = grym_formater("« %s » ne peut pas devenir %s : un lien simple et un champ multiple ne se convertissent "
                                    "pas encore l'un dans l'autre.", n.noms[k], n.uniques[k] & 8 ? "multiple" : "un lien simple");
            ok = 0;
            break;
        }
        if ((n.uniques[k] & 8) && strcmp(a.types[i], n.types[k]) != 0) {
            *erreur = grym_formater("« %s » ne peut pas passer de « %s » à « %s » : ses liaisons seraient perdues.",
                                    n.noms[k], a.types[i], n.types[k]);
            ok = 0;
            break;
        }
        if (n.uniques[k] & 8) continue;
        int vers_annee = strcmp(a.types[i], "nombre entier") == 0 && strcmp(n.types[k], "année") == 0;
        int depuis_annee = strcmp(a.types[i], "année") == 0 && strcmp(n.types[k], "nombre entier") == 0;
        if (vers_annee || depuis_annee) {
            /* même colonne INTEGER : seules les valeurs hors du calendrier empêchent le passage (§ 14.5) */
            long hors = 0;
            if (vers_annee) {
                Chaine sql = {0};
                chaine_ajouter(&sql, "SELECT count(*) FROM ");
                ajouter_nom(&sql, "e ", c->nom);
                chaine_ajouter(&sql, " WHERE ");
                ajouter_nom(&sql, "c ", n.noms[k]);
                chaine_ajouter(&sql, " NOT BETWEEN 1 AND 9999");
                char *t = chaine_rendre(&sql);
                sqlite3_stmt *st = NULL;
                if (sqlite3_prepare_v2(b->db, t, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
                    hors = (long)sqlite3_column_int64(st, 0);
                sqlite3_finalize(st);
                free(t);
            }
            if (hors) {
                *erreur = grym_formater("« %s » ne peut pas devenir une année : %ld valeur%s conservée%s %s hors de 1 à 9999.",
                                        n.noms[k], hors, hors > 1 ? "s" : "", hors > 1 ? "s" : "", hors > 1 ? "sont" : "est");
                ok = 0;
                break;
            }
        } else if (strcmp(a.types[i], n.types[k]) != 0) {
            if (strcmp(a.types[i], "nombre entier") != 0 || strcmp(n.types[k], "nombre") != 0 || (a.uniques[i] & 1)) {
                *erreur = grym_formater("« %s » ne peut pas passer de « %s » à « %s » : seul un nombre entier non unique "
                                        "devient un nombre sans perte.", n.noms[k], a.types[i], n.types[k]);
                ok = 0;
                break;
            }
            Chaine sql = {0};
            chaine_ajouter(&sql, "ALTER TABLE "); ajouter_nom(&sql, "e ", c->nom);
            chaine_ajouter(&sql, " ADD COLUMN "); ajouter_nom(&sql, "x ", n.noms[k]);
            chaine_ajouter(&sql, " TEXT NOT NULL DEFAULT '0'; UPDATE "); ajouter_nom(&sql, "e ", c->nom);
            chaine_ajouter(&sql, " SET "); ajouter_nom(&sql, "x ", n.noms[k]);
            chaine_ajouter(&sql, " = CAST("); ajouter_nom(&sql, "c ", n.noms[k]); chaine_ajouter(&sql, " AS TEXT); ALTER TABLE ");
            ajouter_nom(&sql, "e ", c->nom); chaine_ajouter(&sql, " DROP COLUMN "); ajouter_nom(&sql, "c ", n.noms[k]);
            chaine_ajouter(&sql, "; ALTER TABLE "); ajouter_nom(&sql, "e ", c->nom); chaine_ajouter(&sql, " RENAME COLUMN ");
            ajouter_nom(&sql, "x ", n.noms[k]); chaine_ajouter(&sql, " TO "); ajouter_nom(&sql, "c ", n.noms[k]);
            chaine_ajouter(&sql, ";");
            ok = executer_chaine(b, &sql, erreur);
        }
        if (ok && (a.uniques[i] & 2) != (n.uniques[k] & 2)) {
            *erreur = grym_formater("« %s » ne peut pas %s facultatif : ce n'est pas encore pris en charge sur une table "
                                    "existante.", n.noms[k], n.uniques[k] & 2 ? "devenir" : "cesser d'être");
            ok = 0;
        }
        if (ok && !(a.uniques[i] & 1) && (n.uniques[k] & 1)) ok = creer_index_unique(b, c, n.noms[k], erreur);
        if (ok && (a.uniques[i] & 1) && !(n.uniques[k] & 1)) {
            /* un index ajouté par migration se retire ; une contrainte d'origine, pas encore */
            Chaine sql = {0};
            chaine_ajouter(&sql, "DROP INDEX ");
            char *nom = grym_formater("%s.%s", c->nom, n.noms[k]);
            ajouter_nom(&sql, "u ", nom);
            free(nom);
            char *t = chaine_rendre(&sql);
            ok = sqlite3_exec(b->db, t, NULL, NULL, NULL) == SQLITE_OK;
            free(t);
            if (!ok) *erreur = grym_formater("« %s » ne peut pas cesser d'être unique : ce n'est pas encore pris en charge.",
                                             n.noms[k]);
        }
    }
    /* un champ nouveau */
    for (size_t k = 0; ok && k < n.n; k++) {
        if (chercher_champ(&a, n.noms[k]) >= 0) continue;
        if (n.uniques[k] & 8) {   /* champ multiple nouveau : ensembles vides, quel que soit le nombre d'objets */
            ok = creer_liaison(b, c->nom, n.noms[k], n.types[k], erreur);
            continue;
        }
        long q = -1;
        for (size_t j = 0; j < c->nb_champs && q < 0; j++)
            if (c->proprietaires[j] == c && strcmp(c->champs[j], n.noms[k]) == 0) q = (long)j;
        const char *depart = q >= 0 ? c->departs[q] : NULL;
        const char *t = n.types[k];
        int lien = !(strcmp(t, "texte") == 0 || strcmp(t, "nombre") == 0 || strcmp(t, "nombre entier") == 0
                     || strcmp(t, "année") == 0
                     || strcmp(t, "vrai ou faux") == 0 || strcmp(t, "date") == 0 || est_fichier(t));
        int facultatif = (n.uniques[k] & 2) != 0;
        if (lignes > 0 && !depart && !facultatif) {
            *erreur = lien || est_fichier(t)
                ? grym_formater("« %s » est nouveau, et %s : un %s n'a pas de valeur de départ, il ne s'ajoute qu'à une "
                                "entité sans objet conservé.", n.noms[k], deja, lien ? "lien" : "fichier")
                : grym_formater("« %s » est nouveau, et %s : donnez-lui une valeur de départ, après son type : "
                                "« (%s), … au départ ».", n.noms[k], deja, t);
            ok = 0;
            break;
        }
        if ((n.uniques[k] & 1) && lignes > 1 && depart) {
            *erreur = grym_formater("« %s » est nouveau et unique : une même valeur de départ pour %ld %s n'est pas possible.",
                                    n.noms[k], lignes, pl);
            ok = 0;
            break;
        }
        Chaine sql = {0};
        chaine_ajouter(&sql, "ALTER TABLE ");
        ajouter_nom(&sql, "e ", c->nom);
        chaine_ajouter(&sql, " ADD COLUMN ");
        ajouter_nom(&sql, "c ", n.noms[k]);
        if (lien) {
            /* SQLite n'ajoute une colonne de lien que sans NOT NULL : la machine vérifie qu'elle est remplie */
            chaine_ajouter(&sql, " INTEGER REFERENCES ");
            ajouter_nom(&sql, "e ", t);
            chaine_ajouter(&sql, "(id)");
        } else if (facultatif && !depart) {
            /* facultatif : les objets déjà conservés le reçoivent absent (NULL) */
            chaine_ajouter(&sql, est_fichier(t) ? " BLOB" : strcmp(t, "nombre entier") == 0 || strcmp(t, "vrai ou faux") == 0
                                                             || strcmp(t, "année") == 0 ? " INTEGER" : " TEXT");
            if (est_fichier(t)) {
                chaine_ajouter(&sql, "; ALTER TABLE ");
                ajouter_nom(&sql, "e ", c->nom);
                chaine_ajouter(&sql, " ADD COLUMN ");
                ajouter_nom(&sql, "n ", n.noms[k]);
                chaine_ajouter(&sql, " TEXT");
            }
        } else if (est_fichier(t)) {
            chaine_ajouter(&sql, " BLOB NOT NULL DEFAULT x''; ALTER TABLE ");
            ajouter_nom(&sql, "e ", c->nom);
            chaine_ajouter(&sql, " ADD COLUMN ");
            ajouter_nom(&sql, "n ", n.noms[k]);
            chaine_ajouter(&sql, " TEXT NOT NULL DEFAULT ''");
        } else {
            chaine_ajouter(&sql, strcmp(t, "nombre entier") == 0 || strcmp(t, "vrai ou faux") == 0 || strcmp(t, "année") == 0
                                 ? " INTEGER" : " TEXT");
            chaine_ajouter(&sql, " NOT NULL DEFAULT ");
            ajouter_litteral(&sql, t, depart);
        }
        chaine_ajouter(&sql, ";");
        ok = executer_chaine(b, &sql, erreur) && (!(n.uniques[k] & 1) || creer_index_unique(b, c, n.noms[k], erreur));
    }
    if (ok) {
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(b->db, "UPDATE grym_schema SET definition = ? WHERE entite = ?", -1, &st, NULL);
        sqlite3_bind_text(st, 1, nouvelle, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, c->nom, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    }
    free(pl);
    free(deja);
    liberer_definition(&a);
    liberer_definition(&n);
    return ok;
}

int base_preparer(Base *b, const ClasseVM *c, char **erreur) {
    if (!c->conserve) return 1;
    char *def = definition(c);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT definition FROM grym_schema WHERE entite = ?", -1, &st, NULL);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        char *ancienne = grym_dupliquer((const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        int ok = strcmp(ancienne, def) == 0 || migrer(b, c, ancienne, def, erreur);
        free(ancienne);
        free(def);
        return ok;
    }
    sqlite3_finalize(st);

    Chaine sql = {0};
    chaine_ajouter(&sql, "CREATE TABLE ");
    ajouter_nom(&sql, "e ", c->nom);
    chaine_ajouter(&sql, " (id INTEGER PRIMARY KEY REFERENCES ");
    if (c->parent) ajouter_nom(&sql, "e ", c->parent->nom);
    else chaine_ajouter(&sql, "grym_objet");
    chaine_ajouter(&sql, "(id) ON DELETE CASCADE");
    for (size_t k = 0; k < c->nb_champs; k++) {
        if (c->proprietaires[k] != c || multiple(c, k)) continue;
        const char *t = c->types[k];
        const char *nn = c->uniques[k] & 2 ? "" : " NOT NULL";   /* facultatif : NULL permis (§ 16.9) */
        chaine_ajouter(&sql, ", ");
        ajouter_nom(&sql, "c ", c->champs[k]);
        if (strcmp(t, "nombre entier") == 0 || strcmp(t, "année") == 0) { chaine_ajouter(&sql, " INTEGER"); chaine_ajouter(&sql, nn); }
        else if (strcmp(t, "vrai ou faux") == 0) {
            chaine_ajouter(&sql, " INTEGER");
            chaine_ajouter(&sql, nn);
            chaine_ajouter(&sql, " CHECK (");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&sql, " IN (0, 1))");
        } else if (est_fichier(t)) { chaine_ajouter(&sql, " BLOB"); chaine_ajouter(&sql, nn); }
        else if (est_lien(t)) {
            chaine_ajouter(&sql, " INTEGER");
            chaine_ajouter(&sql, nn);
            chaine_ajouter(&sql, " REFERENCES ");
            ajouter_nom(&sql, "e ", t);
            chaine_ajouter(&sql, "(id)");
        } else { chaine_ajouter(&sql, " TEXT"); chaine_ajouter(&sql, nn); }
        if (c->uniques[k] & 1) chaine_ajouter(&sql, " UNIQUE");
        if (est_fichier(t)) {
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "n ", c->champs[k]);
            chaine_ajouter(&sql, " TEXT");
            chaine_ajouter(&sql, nn);
        }
    }
    chaine_ajouter(&sql, ");");
    char *texte = chaine_rendre(&sql);
    int ok = executer(b, texte, erreur);
    free(texte);
    for (size_t k = 0; ok && k < c->nb_champs; k++)
        if (c->proprietaires[k] == c && multiple(c, k)) ok = creer_liaison(b, c->nom, c->champs[k], c->types[k], erreur);
    if (ok) {
        sqlite3_prepare_v2(b->db, "INSERT INTO grym_schema (entite, definition) VALUES (?, ?)", -1, &st, NULL);
        sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, def, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        sqlite3_finalize(st);
    }
    free(def);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Valeurs                                                          */
/* ---------------------------------------------------------------- */

/* Lie la valeur d'un champ à partir du paramètre i ; renvoie le paramètre suivant, 0 en cas d'erreur. */
/* soi, id_soi : l'objet en train d'être conservé, qui peut se désigner lui-même (« un parrain (client) »). */
int base_est_supprime(Base *b, long id) {
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT supprime IS NOT NULL FROM grym_objet WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    int r = sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return r;
}

static int lier(Base *b, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v,
                const Objet *soi, long id_soi, char **erreur) {
    if (!v || v->type == V_ABSENT) {   /* champ facultatif sans valeur : NULL (§ 16.9) */
        sqlite3_bind_null(st, i);
        if (est_fichier(type)) { sqlite3_bind_null(st, i + 1); return i + 2; }
        return i + 1;
    }
    if (strcmp(type, "texte") == 0) {
        sqlite3_bind_text(st, i, v->texte, -1, SQLITE_TRANSIENT);
    } else if (strcmp(type, "nombre") == 0) {
        char *t = dec_canonique(&v->nombre);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
        free(t);
    } else if (strcmp(type, "nombre entier") == 0) {
        char *t = dec_canonique(&v->nombre), *fin = NULL;
        errno = 0;
        long long n = strtoll(t, &fin, 10);
        int ok = errno == 0 && fin != t && (*fin == '\0' || (*fin == '.' && strspn(fin + 1, "0") == strlen(fin + 1)));
        free(t);
        if (!ok) {
            *erreur = grym_formater("Le champ « %s » est trop grand pour la base : un nombre entier y tient entre "
                                    "−9'223'372'036'854'775'808 et 9'223'372'036'854'775'807.", champ);
            return 0;
        }
        sqlite3_bind_int64(st, i, (sqlite3_int64)n);
    } else if (strcmp(type, "vrai ou faux") == 0) {
        sqlite3_bind_int(st, i, v->vrai ? 1 : 0);
    } else if (strcmp(type, "année") == 0) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)v->jours);
    } else if (strcmp(type, "date") == 0) {
        char *t = date_iso(v->jours);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
        free(t);
    } else if (est_fichier(type)) {
        sqlite3_bind_blob64(st, i, v->fichier->octets, (sqlite3_uint64)v->fichier->taille, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, i + 1, v->fichier->nom, -1, SQLITE_TRANSIENT);
        return i + 2;
    } else if (v->objet == soi) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)id_soi);
    } else {
        if (!v->objet->id) {
            char *qui = un(v->objet->classe);
            *erreur = grym_formater("Le champ « %s » désigne %s qui n'est pas conservé%s : conservez-%s d'abord.", champ,
                                    qui, v->objet->classe->feminin ? "e" : "", v->objet->classe->feminin ? "la" : "le");
            free(qui);
            return 0;
        }
        if (base_est_supprime(b, v->objet->id)) {   /* règle 3 : pas de nouveau lien vers la corbeille (§ 16.12) */
            const ClasseVM *c = v->objet->classe;
            *erreur = grym_formater("Le champ « %s » désignerait %s %s supprimé%s : rétablissez-%s d'abord.", champ,
                                    c->feminin ? "une" : "un", c->nom, c->feminin ? "e" : "", c->feminin ? "la" : "le");
            return 0;
        }
        sqlite3_bind_int64(st, i, (sqlite3_int64)v->objet->id);
    }
    return i + 1;
}

/* Message d'une contrainte refusée par SQLite. */
static char *message_contrainte(Base *b, const Objet *o) {
    int code = sqlite3_extended_errcode(b->db);
    const char *m = sqlite3_errmsg(b->db);
    if (code == SQLITE_CONSTRAINT_UNIQUE) {
        const char *p = strstr(m, ".c ");   /* « UNIQUE constraint failed: e client.c licence » */
        if (!p) return grym_formater("Base « %s » : %s.", b->chemin, m);
        const ClasseVM *c = o->classe;
        char *valeur = NULL;
        for (size_t k = 0; k < c->nb_champs && !valeur; k++) {
            if (strcmp(c->champs[k], p + 3) != 0 || !o->definis[k]) continue;
            const Valeur *v = &o->champs[k];
            valeur = v->type == V_TEXTE ? grym_formater("« %s »", v->texte)
                   : v->type == V_NOMBRE ? dec_formater(&v->nombre)
                   : v->type == V_DATE ? date_suisse(v->jours) : grym_dupliquer("cette valeur");
        }
        /* règle 4 : la valeur est peut-être gardée par un objet de la corbeille (§ 16.12) */
        int dans_corbeille = 0;
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (strcmp(c->champs[k], p + 3) != 0 || !o->definis[k]) continue;
            Chaine sql = {0};
            chaine_ajouter(&sql, "SELECT count(*) FROM ");
            ajouter_nom(&sql, "e ", c->proprietaires[k]->nom);
            chaine_ajouter(&sql, " AS t JOIN grym_objet AS g ON g.id = t.id WHERE g.supprime IS NOT NULL AND t.");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&sql, " = ?");
            char *texte = chaine_rendre(&sql);
            sqlite3_stmt *st = NULL;
            char *ignore = NULL;
            if (sqlite3_prepare_v2(b->db, texte, -1, &st, NULL) == SQLITE_OK
                && lier(b, st, 1, c->types[k], c->champs[k], &o->champs[k], o, o->id, &ignore)
                && sqlite3_step(st) == SQLITE_ROW)
                dans_corbeille = sqlite3_column_int(st, 0) > 0;
            free(ignore);
            sqlite3_finalize(st);
            free(texte);
        }
        char *r = dans_corbeille
            ? grym_formater("« %s » est unique : %s appartient à %s %s supprimé%s. Rétablissez-%s, ou supprimez-%s "
                            "définitivement.", p + 3, valeur ? valeur : "cette valeur", c->feminin ? "une" : "un", c->nom,
                            c->feminin ? "e" : "", c->feminin ? "la" : "le", c->feminin ? "la" : "le")
            : grym_formater("« %s » est unique : %s %s conservé%s a déjà %s.", p + 3,
                            c->feminin ? "une autre" : "un autre", c->nom, c->feminin ? "e" : "",
                            valeur ? valeur : "cette valeur");
        free(valeur);
        return r;
    }
    if (code == SQLITE_CONSTRAINT_FOREIGNKEY)
        return grym_dupliquer("Un lien désigne un objet qui n'est plus conservé.");
    return grym_formater("Base « %s » : %s.", b->chemin, m);
}

/* ---------------------------------------------------------------- */
/* Conserver, écrire, supprimer                                     */
/* ---------------------------------------------------------------- */

int base_conserver(Base *b, const Objet *o, long *id, char **erreur) {
    const ClasseVM *c = o->classe;
    for (size_t k = 0; k < c->nb_champs; k++)
        if (!o->definis[k] && !(c->uniques[k] & 2) && !multiple(c, k)) {
            char *qui = un(c);
            *erreur = grym_formater("Le champ « %s » n'a pas de valeur : %s %s ne se conserve pas.",
                                    c->champs[k], qui, c->feminin ? "incomplète" : "incomplet");
            free(qui);
            return 0;
        }
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "INSERT INTO grym_objet (classe) VALUES (?)", -1, &st, NULL);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) { *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db)); return 0; }
    long nouvel = (long)sqlite3_last_insert_rowid(b->db);

    const ClasseVM *l[LIGNEE_MAX];
    size_t n = lignee(c, l, LIGNEE_MAX);
    for (size_t e = 0; e < n; e++) {
        Chaine sql = {0}, valeurs = {0};
        chaine_ajouter(&sql, "INSERT INTO ");
        ajouter_nom(&sql, "e ", l[e]->nom);
        chaine_ajouter(&sql, " (id");
        chaine_ajouter(&valeurs, "?");
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e] || multiple(c, k)) continue;
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&valeurs, ", ?");
            if (est_fichier(c->types[k])) {
                chaine_ajouter(&sql, ", ");
                ajouter_nom(&sql, "n ", c->champs[k]);
                chaine_ajouter(&valeurs, ", ?");
            }
        }
        chaine_ajouter(&sql, ") VALUES (");
        char *v = chaine_rendre(&valeurs);
        chaine_ajouter(&sql, v);
        free(v);
        chaine_ajouter(&sql, ")");
        char *texte = chaine_rendre(&sql);
        sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)nouvel);
        int i = 2;
        for (size_t k = 0; k < c->nb_champs && i; k++)
            if (c->proprietaires[k] == l[e] && !multiple(c, k))
                i = lier(b, st, i, c->types[k], c->champs[k], o->definis[k] ? &o->champs[k] : NULL, o, nouvel, erreur);
        ok = i && sqlite3_step(st) == SQLITE_DONE;
        if (i && !ok) *erreur = message_contrainte(b, o);
        sqlite3_finalize(st);
        if (!ok) return 0;   /* la transaction de l'exécution annulera les lignes déjà écrites */
    }
    *id = nouvel;
    return 1;
}

int base_ecrire_champ(Base *b, const Objet *o, size_t k, char **erreur) {
    const ClasseVM *c = o->classe;
    const ClasseVM *table = c->proprietaires[k];
    if (!table || !c->types[k] || multiple(c, k)) return 1;
    Chaine sql = {0};
    chaine_ajouter(&sql, "UPDATE ");
    ajouter_nom(&sql, "e ", table->nom);
    chaine_ajouter(&sql, " SET ");
    ajouter_nom(&sql, "c ", c->champs[k]);
    chaine_ajouter(&sql, " = ?");
    if (est_fichier(c->types[k])) {
        chaine_ajouter(&sql, ", ");
        ajouter_nom(&sql, "n ", c->champs[k]);
        chaine_ajouter(&sql, " = ?");
    }
    chaine_ajouter(&sql, " WHERE id = ?");
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
    free(texte);
    int i = lier(b, st, 1, c->types[k], c->champs[k], &o->champs[k], o, o->id, erreur);
    int ok = 0;
    if (i) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)o->id);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = message_contrainte(b, o);
    }
    sqlite3_finalize(st);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Gagner, perdre (grammaire, § 16.13)                              */
/* ---------------------------------------------------------------- */

int base_gagner(Base *b, const Objet *o, size_t k, const Valeur *v, int perdre, char **erreur) {
    const ClasseVM *c = o->classe;
    const Objet *x = v->objet;
    if (!x->id) {
        if (perdre) return 1;   /* un objet jamais conservé n'est dans aucun ensemble */
        char *qui = un(x->classe);
        *erreur = grym_formater("Le champ « %s » gagnerait %s qui n'est pas conservé%s : conservez-%s d'abord.", c->champs[k],
                                qui, x->classe->feminin ? "e" : "", x->classe->feminin ? "la" : "le");
        free(qui);
        return 0;
    }
    if (!perdre && base_est_supprime(b, x->id)) {   /* pas de nouveau lien vers la corbeille (§ 16.12) */
        const ClasseVM *xc = x->classe;
        *erreur = grym_formater("Le champ « %s » gagnerait %s %s supprimé%s : rétablissez-%s d'abord.", c->champs[k],
                                xc->feminin ? "une" : "un", xc->nom, xc->feminin ? "e" : "", xc->feminin ? "la" : "le");
        return 0;
    }
    Chaine sql = {0};
    chaine_ajouter(&sql, perdre ? "DELETE FROM " : "INSERT OR IGNORE INTO ");
    ajouter_liaison(&sql, c->proprietaires[k]->nom, c->champs[k]);
    chaine_ajouter(&sql, perdre ? " WHERE a = ?1 AND b = ?2" : " (a, b) VALUES (?1, ?2)");
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    int ok = sqlite3_prepare_v2(b->db, texte, -1, &st, NULL) == SQLITE_OK;
    free(texte);
    if (ok) {
        sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
        sqlite3_bind_int64(st, 2, (sqlite3_int64)x->id);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    sqlite3_finalize(st);
    return ok;
}

/* ---------------------------------------------------------------- */
/* Supprimer, rétablir (grammaire, § 16.12)                         */
/* ---------------------------------------------------------------- */

typedef struct {
    long *ids;
    char **classes;
    size_t n, cap;
} Ensemble;

static int ens_contient(const Ensemble *e, long id) {
    for (size_t k = 0; k < e->n; k++) if (e->ids[k] == id) return 1;
    return 0;
}

static void ens_ajouter(Ensemble *e, long id, const char *classe) {
    if (ens_contient(e, id)) return;
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        long *i2 = grym_allouer(e->cap * sizeof *i2);
        char **c2 = grym_allouer(e->cap * sizeof *c2);
        if (e->n) { memcpy(i2, e->ids, e->n * sizeof *i2); memcpy(c2, e->classes, e->n * sizeof *c2); }
        free(e->ids);
        free(e->classes);
        e->ids = i2;
        e->classes = c2;
    }
    e->ids[e->n] = id;
    e->classes[e->n++] = grym_dupliquer(classe);
}

static void ens_liberer(Ensemble *e) {
    for (size_t k = 0; k < e->n; k++) free(e->classes[k]);
    free(e->ids);
    free(e->classes);
}

static const ClasseVM *classe_nommee(ClasseVM *const *classes, size_t n, const char *nom) {
    for (size_t i = 0; i < n; i++) if (strcmp(classes[i]->nom, nom) == 0) return classes[i];
    return NULL;
}

/* Le champ k de x (lien) peut-il désigner un objet de la classe c ? */
static int vise(const ClasseVM *x, size_t k, const ClasseVM *c) {
    if (x->proprietaires[k] != x || !x->types[k] || !est_lien(x->types[k]) || multiple(x, k)) return 0;
    for (const ClasseVM *p = c; p; p = p->parent) if (strcmp(p->nom, x->types[k]) == 0) return 1;
    return 0;
}

/* Le champ multiple k de x peut-il contenir un objet de la classe c ? (§ 16.13) */
static int contient(const ClasseVM *x, size_t k, const ClasseVM *c) {
    if (x->proprietaires[k] != x || !multiple(x, k)) return 0;
    for (const ClasseVM *p = c; p; p = p->parent) if (strcmp(p->nom, x->types[k]) == 0) return 1;
    return 0;
}

/* Objets dont le champ k de x désigne id (sauf id lui-même) ; pour un champ multiple, ceux qui l'ont gagné. */
static void designants(Base *b, const ClasseVM *x, size_t k, long id, Ensemble *sortie) {
    Chaine sql = {0};
    if (multiple(x, k)) {
        chaine_ajouter(&sql, "SELECT t.a, g.classe FROM ");
        ajouter_liaison(&sql, x->nom, x->champs[k]);
        chaine_ajouter(&sql, " AS t JOIN grym_objet AS g ON g.id = t.a WHERE t.b = ?1 AND t.a <> ?1");
    } else {
        chaine_ajouter(&sql, "SELECT t.id, g.classe FROM ");
        ajouter_nom(&sql, "e ", x->nom);
        chaine_ajouter(&sql, " AS t JOIN grym_objet AS g ON g.id = t.id WHERE t.");
        ajouter_nom(&sql, "c ", x->champs[k]);
        chaine_ajouter(&sql, " = ?1 AND t.id <> ?1");
    }
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
    free(texte);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    while (sqlite3_step(st) == SQLITE_ROW)
        ens_ajouter(sortie, (long)sqlite3_column_int64(st, 0), (const char *)sqlite3_column_text(st, 1));
    sqlite3_finalize(st);
}

/* L'objet et, de proche en proche, ceux qui « disparaissent avec » lui (bit 4). */
static void fermeture(Base *b, ClasseVM *const *classes, size_t nb, long id, const char *classe, int garder_supprimes,
                      Ensemble *e) {
    ens_ajouter(e, id, classe);
    for (size_t q = 0; q < e->n; q++) {
        const ClasseVM *c = classe_nommee(classes, nb, e->classes[q]);
        if (!c) continue;
        for (size_t i = 0; i < nb; i++) {
            const ClasseVM *x = classes[i];
            if (!x->conserve) continue;
            for (size_t k = 0; k < x->nb_champs; k++) {
                if (!(x->uniques[k] & 4) || !vise(x, k, c)) continue;
                Ensemble d = {0};
                designants(b, x, k, e->ids[q], &d);
                for (size_t r = 0; r < d.n; r++)
                    if (garder_supprimes || !base_est_supprime(b, d.ids[r])) ens_ajouter(e, d.ids[r], d.classes[r]);
                ens_liberer(&d);
            }
        }
    }
}

static const char *ce(const ClasseVM *c) {
    return c->feminin ? "Cette" : commence_par_voyelle(c->nom) ? "Cet" : "Ce";
}

int base_supprimer(Base *b, struct Machine *m, const Objet *o, ClasseVM *const *classes, size_t nb_classes,
                   int definitif, char **erreur) {
    const ClasseVM *oc = o->classe;
    if (!definitif && base_est_supprime(b, o->id)) {
        *erreur = grym_formater("%s %s est déjà supprimé%s : « Supprimer … définitivement » l'efface de la base.",
                                ce(oc), oc->nom, oc->feminin ? "e" : "");
        return 0;
    }
    Ensemble e = {0};
    fermeture(b, classes, nb_classes, o->id, oc->nom, definitif, &e);
    if (!definitif) {
        /* mise de côté : l'objet, et ceux qui disparaissent avec lui, gardent tout ; la date les cache */
        long jours = date_aujourdhui();
        char *date = date_iso(jours);
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(b->db, "UPDATE grym_objet SET supprime = ?, supprime_avec = ? WHERE id = ?", -1, &st, NULL);
        for (size_t k = 0; k < e.n; k++) {
            sqlite3_bind_text(st, 1, date, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, (sqlite3_int64)o->id);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)e.ids[k]);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        sqlite3_finalize(st);
        free(date);
        ens_liberer(&e);
        return 1;
    }
    /* définitive : un lien venu d'ailleurs (hors de l'ensemble) l'empêche */
    for (size_t q = 0; q < e.n; q++) {
        const ClasseVM *c = classe_nommee(classes, nb_classes, e.classes[q]);
        if (!c) continue;
        for (size_t i = 0; i < nb_classes; i++) {
            const ClasseVM *x = classes[i];
            if (!x->conserve) continue;
            for (size_t k = 0; k < x->nb_champs; k++) {
                if (!vise(x, k, c) && !contient(x, k, c)) continue;
                Ensemble d = {0};
                designants(b, x, k, e.ids[q], &d);
                long dehors = 0, en_corbeille = 0;
                for (size_t r = 0; r < d.n; r++)
                    if (!ens_contient(&e, d.ids[r])) { dehors++; en_corbeille += base_est_supprime(b, d.ids[r]); }
                ens_liberer(&d);
                if (!dehors) continue;
                char *qui = dehors > 1 ? pluriel(x) : un(x);
                /* s'ils sont tous dans la corbeille, le dire : on ne les voit plus */
                const char *suite = en_corbeille == dehors
                    ? (dehors > 1 ? (x->feminin ? " supprimées : supprimez-les définitivement d'abord"
                                                : " supprimés : supprimez-les définitivement d'abord")
                                  : (x->feminin ? " supprimée : supprimez-la définitivement d'abord"
                                                : " supprimé : supprimez-le définitivement d'abord"))
                    : "";
                *erreur = dehors > 1
                    ? grym_formater("%s %s est encore désigné%s par le champ « %s » de %ld %s%s.", ce(c), c->nom,
                                    c->feminin ? "e" : "", x->champs[k], dehors, qui, suite)
                    : grym_formater("%s %s est encore désigné%s par le champ « %s » d'%s%s.", ce(c), c->nom,
                                    c->feminin ? "e" : "", x->champs[k], qui, suite);
                free(qui);
                ens_liberer(&e);
                return 0;
            }
        }
    }
    /* les liens internes à l'ensemble se vérifient à la validation : l'ordre des effacements n'importe plus */
    int ok = executer(b, "PRAGMA defer_foreign_keys = ON;", erreur);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "DELETE FROM grym_objet WHERE id = ?", -1, &st, NULL);
    for (size_t k = e.n; ok && k > 0; k--) {
        machine_objet_efface(m, e.ids[k - 1]);   /* avant l'effacement : ses valeurs sont lues s'il le faut */
        sqlite3_bind_int64(st, 1, (sqlite3_int64)e.ids[k - 1]);
        if (sqlite3_step(st) != SQLITE_DONE) {
            *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
            ok = 0;
        }
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    ens_liberer(&e);
    return ok;
}

int base_retablir(Base *b, const Objet *o, ClasseVM *const *classes, size_t nb_classes, char **erreur) {
    const ClasseVM *oc = o->classe;
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT supprime IS NOT NULL, supprime_avec FROM grym_objet WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
    int supprime = 0;
    long avec = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        supprime = sqlite3_column_int(st, 0);
        avec = (long)sqlite3_column_int64(st, 1);
    }
    sqlite3_finalize(st);
    if (!supprime) {
        *erreur = grym_formater("%s %s n'est pas supprimé%s.", ce(oc), oc->nom, oc->feminin ? "e" : "");
        return 0;
    }
    if (avec != o->id) {
        *erreur = grym_formater("%s %s a disparu avec un autre objet : rétablissez celui-là, et %s reviendra avec lui.",
                                ce(oc), oc->nom, oc->feminin ? "elle" : "il");
        return 0;
    }
    /* il ne revient pas s'il « disparaît avec » un objet qui, lui, reste supprimé */
    for (size_t k = 0; k < oc->nb_champs; k++) {
        if (!(oc->uniques[k] & 4) || !o->definis[k] || o->champs[k].type != V_OBJET) continue;
        const Objet *cible = o->champs[k].objet;
        if (cible->id && base_est_supprime(b, cible->id)) {
            const ClasseVM *c = cible->classe;
            *erreur = grym_formater("%s %s disparaît avec %s %s qui est supprimé%s : rétablissez-%s d'abord.", ce(oc),
                                    oc->nom, c->feminin ? "une" : "un", c->nom, c->feminin ? "e" : "",
                                    c->feminin ? "la" : "le");
            return 0;
        }
    }
    (void)classes;
    (void)nb_classes;
    sqlite3_prepare_v2(b->db, "UPDATE grym_objet SET supprime = NULL, supprime_avec = NULL WHERE supprime_avec = ?",
                       -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    return ok;
}

/* ---------------------------------------------------------------- */
/* Collations : comparer comme GrymoiR, pas comme des octets         */
/* ---------------------------------------------------------------- */

/* GRYM_NOMBRE : deux textes canoniques comparés en décimal exact (« 10 » > « 9 », « 3.0 » = « 3 »). */
static int collation_nombre(void *ctx, int na, const void *a, int nb, const void *b) {
    (void)ctx;
    char *x = grym_allouer((size_t)na + 1), *y = grym_allouer((size_t)nb + 1);
    memcpy(x, a, (size_t)na);
    x[na] = '\0';
    memcpy(y, b, (size_t)nb);
    y[nb] = '\0';
    Decimal dx = dec_depuis_canonique(x), dy = dec_depuis_canonique(y);
    int c = dec_comparer(&dx, &dy);
    dec_liberer(&dx);
    dec_liberer(&dy);
    free(x);
    free(y);
    return c;
}

/* Lettre de base d'une lettre latine accentuée (UTF-8), en minuscule ; 0 si ce n'en est pas une. */
static int base_lettre(const unsigned char *p, int *longueur) {
    static const struct { const char *u; char b; } T[] = {
        { "à", 'a' }, { "â", 'a' }, { "ä", 'a' }, { "á", 'a' }, { "À", 'a' }, { "Â", 'a' }, { "Ä", 'a' },
        { "ç", 'c' }, { "Ç", 'c' }, { "é", 'e' }, { "è", 'e' }, { "ê", 'e' }, { "ë", 'e' }, { "É", 'e' },
        { "È", 'e' }, { "Ê", 'e' }, { "Ë", 'e' }, { "î", 'i' }, { "ï", 'i' }, { "í", 'i' }, { "Î", 'i' },
        { "Ï", 'i' }, { "ô", 'o' }, { "ö", 'o' }, { "ó", 'o' }, { "Ô", 'o' }, { "Ö", 'o' }, { "ù", 'u' },
        { "û", 'u' }, { "ü", 'u' }, { "ú", 'u' }, { "Ù", 'u' }, { "Û", 'u' }, { "Ü", 'u' }, { "ÿ", 'y' },
        { "ñ", 'n' }, { "Ñ", 'n' }
    };
    for (size_t k = 0; k < sizeof T / sizeof *T; k++) {
        size_t l = strlen(T[k].u);
        if (memcmp(p, T[k].u, l) == 0) { *longueur = (int)l; return T[k].b; }
    }
    *longueur = 1;
    if (*p >= 'A' && *p <= 'Z') return *p - 'A' + 'a';
    return *p;
}

/* GRYM_TEXTE : ordre du dictionnaire, sans tenir compte des accents ni de la casse, puis octets. */
static int collation_texte(void *ctx, int na, const void *a, int nb, const void *b) {
    (void)ctx;
    const unsigned char *x = a, *y = b;
    int i = 0, j = 0;
    while (i < na && j < nb) {
        int lx, ly;
        int cx = base_lettre(x + i, &lx), cy = base_lettre(y + j, &ly);
        if (cx != cy) return cx < cy ? -1 : 1;
        i += lx;
        j += ly;
    }
    if (i < na || j < nb) return i < na ? 1 : -1;
    int c = memcmp(a, b, (size_t)(na < nb ? na : nb));
    return c ? c : (na > nb) - (na < nb);
}

int base_collations(sqlite3 *db) {
    return sqlite3_create_collation(db, "GRYM_NOMBRE", SQLITE_UTF8, NULL, collation_nombre) == SQLITE_OK
        && sqlite3_create_collation(db, "GRYM_TEXTE", SQLITE_UTF8, NULL, collation_texte) == SQLITE_OK;
}

/* ---------------------------------------------------------------- */
/* Charger un objet retrouvé                                        */
/* ---------------------------------------------------------------- */

static char *classe_en_base(Base *b, long id) {
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(b->db, "SELECT classe FROM grym_objet WHERE id = ?", -1, &st, NULL);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    char *r = sqlite3_step(st) == SQLITE_ROW ? grym_dupliquer((const char *)sqlite3_column_text(st, 0)) : NULL;
    sqlite3_finalize(st);
    return r;
}

int base_charger(Base *b, struct Machine *m, Objet *o, char **erreur) {
    const ClasseVM *c = o->classe;
    const ClasseVM *l[LIGNEE_MAX];
    size_t n = lignee(c, l, LIGNEE_MAX);
    for (size_t e = 0; e < n; e++) {
        Chaine sql = {0};
        int colonnes = 0;
        chaine_ajouter(&sql, "SELECT id");
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e] || multiple(c, k)) continue;
            chaine_ajouter(&sql, ", ");
            ajouter_nom(&sql, "c ", c->champs[k]);
            if (est_fichier(c->types[k])) {
                chaine_ajouter(&sql, ", ");
                ajouter_nom(&sql, "n ", c->champs[k]);
            }
            colonnes++;
        }
        chaine_ajouter(&sql, " FROM ");
        ajouter_nom(&sql, "e ", l[e]->nom);
        chaine_ajouter(&sql, " WHERE id = ?");
        char *texte = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        sqlite3_prepare_v2(b->db, texte, -1, &st, NULL);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
        if (sqlite3_step(st) != SQLITE_ROW) {
            sqlite3_finalize(st);
            char *qui = un(c);
            *erreur = grym_formater("%s a disparu de la base « %s ».", qui, b->chemin);
            free(qui);
            return 0;
        }
        int col = 1;
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (c->proprietaires[k] != l[e] || multiple(c, k)) continue;
            const char *t = c->types[k];
            Valeur v;
            if (sqlite3_column_type(st, col) == SQLITE_NULL) {
                v = vi_absent(c->champs[k]);
                if (est_fichier(t)) col++;
            } else if (strcmp(t, "texte") == 0) v = vi_texte((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre") == 0) v = vi_nombre_canonique((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre entier") == 0) {
                char tampon[32];
                snprintf(tampon, sizeof tampon, "%lld", (long long)sqlite3_column_int64(st, col));
                v = vi_nombre_canonique(tampon);
            } else if (strcmp(t, "vrai ou faux") == 0) v = vi_booleen(sqlite3_column_int(st, col));
            else if (strcmp(t, "année") == 0) v = vi_annee((long)sqlite3_column_int64(st, col));
            else if (strcmp(t, "date") == 0) {
                long jours = 0;
                date_lire_iso((const char *)sqlite3_column_text(st, col), &jours);
                v = vi_date(jours);
            } else if (est_fichier(t)) {
                v = vi_fichier(sqlite3_column_blob(st, col), (size_t)sqlite3_column_bytes(st, col),
                               (const char *)sqlite3_column_text(st, col + 1));
                col++;
            } else {
                long id = (long)sqlite3_column_int64(st, col);
                char *classe = classe_en_base(b, id);
                Objet *lie = classe ? machine_objet_en_base(m, id, classe, erreur) : NULL;
                if (!classe && !*erreur) *erreur = grym_formater("Base « %s » : lien vers un objet absent.", b->chemin);
                free(classe);
                if (!lie) { sqlite3_finalize(st); return 0; }
                v = vi_objet(lie);
            }
            o->champs[k] = v;
            o->definis[k] = 1;
            col++;
        }
        (void)colonnes;
        sqlite3_finalize(st);
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Chercher                                                         */
/* ---------------------------------------------------------------- */

typedef struct {
    Base *b;
    struct Machine *m;
    const ClasseVM *e;
    const ClasseVM *l[LIGNEE_MAX];
    size_t n;                 /* taille de la lignée ; l[n − 1] est l'entité cherchée */
    const char *p;            /* position dans la condition */
    Chaine sql;
    const Valeur *params;
    size_t nb_params;
    /* paramètres liés dans l'ordre de la condition : index, type du champ, nom du champ */
    size_t liens[64];
    const char *types[64];
    const char *champs[64];
    size_t nb_liens;
    char *erreur;
} Recherche;

/* Alias SQL de la table qui porte le champ k : « t0 » pour la racine de la lignée. */
static int alias_du_champ(const Recherche *r, size_t k) {
    for (size_t i = 0; i < r->n; i++) if (r->e->proprietaires[k] == r->l[i]) return (int)i;
    return 0;
}

static void colonne(Recherche *r, size_t k) {
    char t[16];
    snprintf(t, sizeof t, "t%d.", alias_du_champ(r, k));
    chaine_ajouter(&r->sql, t);
    ajouter_nom(&r->sql, "c ", r->e->champs[k]);
}

static int lire_condition(Recherche *r) {
    if (*r->p != '(') { r->erreur = grym_dupliquer("condition illisible"); return 0; }
    char op = r->p[1];
    r->p += 2;
    if (op == 'e' || op == 'o') {
        chaine_ajouter(&r->sql, "(");
        if (!lire_condition(r)) return 0;
        chaine_ajouter(&r->sql, op == 'e' ? " AND " : " OR ");
        if (!lire_condition(r)) return 0;
        chaine_ajouter(&r->sql, ")");
    } else if (op == 'n') {
        chaine_ajouter(&r->sql, "NOT ");
        if (!lire_condition(r)) return 0;
    } else if (op == 'I' || op == 'M') {
        /* relation inverse (grammaire, § 16.10) : le lien de l'entité qui peut désigner l'objet ?k, ou le champ
         * multiple qui le relie à l'entité, dans un sens ou dans l'autre (§ 16.13) ;
         * « M » : le champ multiple nommé de l'objet ?k, « (M?k[interprètes]) » */
        char *fin;
        size_t i = (size_t)strtoul(r->p + 1, &fin, 10);
        r->p = fin;
        char *nomme = NULL;
        if (op == 'M') {
            const char *ferme = *r->p == '[' ? strchr(r->p, ']') : NULL;
            if (!ferme) { r->erreur = grym_dupliquer("champ attendu"); return 0; }
            nomme = grym_formater("%.*s", (int)(ferme - r->p - 1), r->p + 1);
            r->p = ferme + 1;
        }
        if (i < 1 || i > r->nb_params || r->nb_liens == 64) {
            free(nomme);
            r->erreur = grym_dupliquer("paramètre invalide");
            return 0;
        }
        const Valeur *v = &r->params[i - 1];
        if (v->type == V_ABSENT) {
            free(nomme);
            r->erreur = v->texte ? grym_formater("Le champ « %s » est absent : vérifiez-le d'abord avec « est présent ».", v->texte)
                                 : grym_dupliquer("La valeur est absente.");
            return 0;
        }
        if (v->type != V_OBJET) {
            free(nomme);
            r->erreur = grym_dupliquer("« de … » désigne un objet : un nombre, un texte ou une date n'a pas de liens.");
            return 0;
        }
        const ClasseVM *oc = v->objet->classe;
        /* candidats : 0 lien de l'entité, 1 champ multiple de l'entité, 2 champ multiple de l'objet */
        int sortes[64];
        size_t ks[64], combien = 0;
        int multiples = 0;
        for (size_t k = 0; op == 'I' && k < r->e->nb_champs && combien < 64; k++) {
            const char *t = r->e->types[k];
            if (!t || !est_lien(t)) continue;
            int vise = 0;
            for (const ClasseVM *p = oc; p && !vise; p = p->parent) vise = strcmp(p->nom, t) == 0;
            if (!vise) continue;
            sortes[combien] = multiple(r->e, k) ? 1 : 0;
            multiples |= sortes[combien];
            ks[combien++] = k;
        }
        for (size_t k = 0; k < oc->nb_champs && combien < 64; k++) {
            if (!multiple(oc, k) || (nomme && strcmp(oc->champs[k], nomme) != 0)) continue;
            const ClasseVM *t = machine_classe(r->m, oc->types[k]);
            int apparente = 0;
            for (const ClasseVM *p = r->e; p && !apparente; p = p->parent) apparente = p == t;
            for (const ClasseVM *p = t; p && !apparente; p = p->parent) apparente = p == r->e;
            if (!apparente) continue;
            sortes[combien] = 2;
            multiples = 1;
            ks[combien++] = k;
        }
        if (nomme && combien == 0) {
            /* « Pour chaque œuvre de baroque », quand une autre entité a « des œuvres » : l'objet n'a pas ce champ,
             * la relation inverse prend le relais (§ 16.13) */
            free(nomme);
            nomme = NULL;
            for (size_t k = 0; k < r->e->nb_champs && combien < 64; k++) {
                const char *t = r->e->types[k];
                if (!t || !est_lien(t)) continue;
                int vise = 0;
                for (const ClasseVM *p = oc; p && !vise; p = p->parent) vise = strcmp(p->nom, t) == 0;
                if (!vise) continue;
                sortes[combien] = multiple(r->e, k) ? 1 : 0;
                multiples |= sortes[combien];
                ks[combien++] = k;
            }
        }
        char *qui = un(oc);
        if (combien != 1) {
            char *pl = pluriel(r->e);
            if (nomme)
                r->erreur = grym_formater("%s n'a pas de champ multiple « %s » qui contienne des %s.", qui, nomme, pl);
            else if (combien == 0)
                r->erreur = grym_formater("Aucun champ %s %s ne peut désigner %s : « les %s de … » ne désigne rien.",
                                          r->e->feminin ? "d'une" : "d'un", r->e->nom, qui, pl);
            else if (!multiples) {
                Chaine noms = {0};
                for (size_t q = 0; q < combien; q++) {
                    chaine_ajouter(&noms, q ? " et « " : "« ");
                    chaine_ajouter(&noms, r->e->champs[ks[q]]);
                    chaine_ajouter(&noms, " »");
                }
                char *n = chaine_rendre(&noms);
                r->erreur = grym_formater("Plusieurs champs %s %s peuvent désigner %s : %s. Précisez avec "
                                          "« dont … est le … », par exemple « dont … est le %s ».",
                                          r->e->feminin ? "d'une" : "d'un", r->e->nom, qui, n, r->e->champs[ks[0]]);
                free(n);
            } else {
                Chaine noms = {0}, precis = {0};
                for (size_t q = 0; q < combien; q++) {
                    const char *ch = sortes[q] == 2 ? oc->champs[ks[q]] : r->e->champs[ks[q]];
                    const char *sep = q == 0 ? "" : q + 1 == combien ? " et " : ", ";
                    const char *ou = q == 0 ? "" : q + 1 == combien ? " ou " : ", ";
                    chaine_ajouter(&noms, sep);
                    chaine_ajouter(&noms, "« ");
                    chaine_ajouter(&noms, ch);
                    chaine_ajouter(&noms, " »");
                    chaine_ajouter(&precis, ou);
                    chaine_ajouter(&precis, sortes[q] == 0 ? "« dont … est le " : sortes[q] == 1 ? "« dont … est parmi les "
                                                                                               : "« les ");
                    chaine_ajouter(&precis, ch);
                    chaine_ajouter(&precis, sortes[q] == 2 ? " de … »" : " »");
                }
                char *n = chaine_rendre(&noms), *pr = chaine_rendre(&precis);
                char *e1 = un(r->e);
                r->erreur = grym_formater("Plusieurs champs relient %s à %s : %s. Précisez avec %s.", e1, qui, n, pr);
                free(e1);
                free(n);
                free(pr);
            }
            free(pl);
            free(qui);
            free(nomme);
            return 0;
        }
        free(qui);
        free(nomme);
        size_t k = ks[0];
        char t[40];
        if (sortes[0] == 0) {
            chaine_ajouter(&r->sql, "(");
            colonne(r, k);
            snprintf(t, sizeof t, " = ?%lu)", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, t);
            r->types[r->nb_liens] = r->e->types[k];
            r->champs[r->nb_liens] = r->e->champs[k];
        } else {
            const ClasseVM *porteur = sortes[0] == 1 ? r->e : oc;
            chaine_ajouter(&r->sql, sortes[0] == 1 ? "(g.id IN (SELECT a FROM " : "(g.id IN (SELECT b FROM ");
            ajouter_liaison(&r->sql, porteur->proprietaires[k]->nom, porteur->champs[k]);
            snprintf(t, sizeof t, sortes[0] == 1 ? " WHERE b = ?%lu))" : " WHERE a = ?%lu))", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, t);
            r->types[r->nb_liens] = porteur->types[k];
            r->champs[r->nb_liens] = porteur->champs[k];
        }
        r->liens[r->nb_liens] = i - 1;
        r->nb_liens++;
    } else {
        if (*r->p != '[') { r->erreur = grym_dupliquer("champ attendu"); return 0; }
        const char *fin = strchr(r->p, ']');
        if (!fin) { r->erreur = grym_dupliquer("champ mal fermé"); return 0; }
        char *champ = grym_formater("%.*s", (int)(fin - r->p - 1), r->p + 1);
        r->p = fin + 1;
        long k = -1;
        for (size_t q = 0; q < r->e->nb_champs && k < 0; q++) if (strcmp(r->e->champs[q], champ) == 0) k = (long)q;
        free(champ);
        if (k < 0 || !r->e->types[k]) { r->erreur = grym_dupliquer("champ inconnu"); return 0; }
        const char *t = r->e->types[k];
        if (op == 'p' || multiple(r->e, (size_t)k)) {
            /* « dont baroque est parmi les genres » (§ 16.13) : seule tournure d'un champ multiple */
            if (op != 'p' || !multiple(r->e, (size_t)k) || *r->p != '?') {
                r->erreur = grym_formater("« %s » : « parmi » s'emploie avec un champ multiple, et lui seul.", r->e->champs[k]);
                return 0;
            }
            size_t i = (size_t)strtoul(r->p + 1, (char **)&fin, 10);
            r->p = fin;
            if (i < 1 || i > r->nb_params || r->nb_liens == 64) { r->erreur = grym_dupliquer("paramètre invalide"); return 0; }
            char tampon[40];
            chaine_ajouter(&r->sql, "(g.id IN (SELECT a FROM ");
            ajouter_liaison(&r->sql, r->e->proprietaires[k]->nom, r->e->champs[k]);
            snprintf(tampon, sizeof tampon, " WHERE b = ?%lu))", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, tampon);
            r->liens[r->nb_liens] = i - 1;
            r->types[r->nb_liens] = t;
            r->champs[r->nb_liens] = r->e->champs[k];
            r->nb_liens++;
            if (*r->p != ')') { r->erreur = grym_dupliquer("parenthèse attendue"); return 0; }
            r->p++;
            return 1;
        }
        int nombre = strcmp(t, "nombre") == 0, entier = strcmp(t, "nombre entier") == 0;
        const char *sqlop = op == '=' ? " = " : op == '!' ? " <> " : op == '<' ? " < " : op == '>' ? " > "
                          : op == 'l' ? " <= " : op == 'g' ? " >= " : NULL;
        chaine_ajouter(&r->sql, "(");
        if (op == 'P' || op == 'N' || op == '0') {
            const char *o2 = op == 'P' ? " > " : op == 'N' ? " < " : " = ";
            if (entier) { colonne(r, (size_t)k); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "0"); }
            else { colonne(r, (size_t)k); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "'0' COLLATE GRYM_NOMBRE"); }
        } else if (op == 'A' || op == 'R') {
            colonne(r, (size_t)k);
            chaine_ajouter(&r->sql, op == 'A' ? " IS NULL" : " IS NOT NULL");
        } else if (op == 'V' || op == 'F') {
            colonne(r, (size_t)k);
            chaine_ajouter(&r->sql, op == 'V' ? " = 1" : " = 0");
        } else {
            if (*r->p != '?' || !sqlop) { r->erreur = grym_dupliquer("paramètre attendu"); return 0; }
            size_t i = (size_t)strtoul(r->p + 1, (char **)&fin, 10);
            r->p = fin;
            if (i < 1 || i > r->nb_params || r->nb_liens == 64) { r->erreur = grym_dupliquer("paramètre invalide"); return 0; }
            if (entier) { chaine_ajouter(&r->sql, "CAST("); colonne(r, (size_t)k); chaine_ajouter(&r->sql, " AS TEXT)"); }
            else colonne(r, (size_t)k);
            chaine_ajouter(&r->sql, sqlop);
            char tampon[24];
            snprintf(tampon, sizeof tampon, "?%lu", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, tampon);
            if (nombre || entier) chaine_ajouter(&r->sql, " COLLATE GRYM_NOMBRE");
            else if (strcmp(t, "texte") == 0 && op != '=' && op != '!') chaine_ajouter(&r->sql, " COLLATE GRYM_TEXTE");
            r->liens[r->nb_liens] = i - 1;
            r->types[r->nb_liens] = t;
            r->champs[r->nb_liens] = r->e->champs[k];
            r->nb_liens++;
        }
        chaine_ajouter(&r->sql, ")");
    }
    if (*r->p != ')') { r->erreur = grym_dupliquer("parenthèse attendue"); return 0; }
    r->p++;
    return 1;
}

/* Lie la valeur d'un paramètre, comparée au champ d'un type donné. */
static int lier_parametre(Recherche *r, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v) {
    const char *attendu = NULL;
    if (strcmp(type, "texte") == 0) {
        if (v->type == V_TEXTE) { sqlite3_bind_text(st, i, v->texte, -1, SQLITE_TRANSIENT); return 1; }
        attendu = "un texte";
    } else if (strcmp(type, "année") == 0) {
        if (v->type == V_ANNEE) { sqlite3_bind_int64(st, i, (sqlite3_int64)v->jours); return 1; }
        if (v->type == V_NOMBRE) {   /* colonne INTEGER : le texte canonique y est comparé comme un nombre */
            char *t = dec_canonique(&v->nombre);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            free(t);
            return 1;
        }
        attendu = "une année ou un nombre";
    } else if (strcmp(type, "nombre") == 0 || strcmp(type, "nombre entier") == 0) {
        if (v->type == V_ANNEE) {
            char t[24];
            snprintf(t, sizeof t, "%ld", v->jours);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            return 1;
        }
        if (v->type == V_NOMBRE) {
            char *t = dec_canonique(&v->nombre);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            free(t);
            return 1;
        }
        attendu = "un nombre";
    } else if (strcmp(type, "vrai ou faux") == 0) {
        if (v->type == V_BOOLEEN) { sqlite3_bind_int(st, i, v->vrai); return 1; }
        attendu = "vrai ou faux";
    } else if (strcmp(type, "date") == 0) {
        if (v->type == V_DATE) {
            char *t = date_iso(v->jours);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            free(t);
            return 1;
        }
        attendu = "une date";
    } else {
        if (v->type == V_OBJET) { sqlite3_bind_int64(st, i, (sqlite3_int64)v->objet->id); return 1; }
        attendu = "un objet";
    }
    r->erreur = grym_formater("Le champ « %s » se compare à %s.", champ, attendu);
    return 0;
}

int base_chercher(Base *b, struct Machine *m, const char *d, const Valeur *params, size_t nb_params,
                  Valeur *resultat, char **erreur) {
    /* entité ␟ mode ␟ tri ␟ décroissant ␟ condition */
    const char *s1 = strchr(d, '\x1f');
    const char *s2 = s1 ? strchr(s1 + 1, '\x1f') : NULL;
    const char *s3 = s2 ? strchr(s2 + 1, '\x1f') : NULL;
    const char *s4 = s3 ? strchr(s3 + 1, '\x1f') : NULL;
    if (!s4) { *erreur = grym_dupliquer("Recherche mal décrite."); return 0; }
    char *entite = grym_formater("%.*s", (int)(s1 - d), d);
    int mode = s1[1] - '0';
    int corbeille = mode >= 3;   /* « supprimés » (§ 16.12) : modes 3, 4, 5 */
    if (corbeille) mode -= 3;
    char *tri = grym_formater("%.*s", (int)(s3 - s2 - 1), s2 + 1);
    int decroissant = s3[1] == '1';
    Recherche r;
    memset(&r, 0, sizeof r);
    r.b = b;
    r.m = m;
    r.e = machine_classe(m, entite);
    r.params = params;
    r.nb_params = nb_params;
    if (!r.e || !r.e->conserve) {
        *erreur = grym_formater("« %s » n'est pas une entité connue.", entite);
        free(entite);
        free(tri);
        return 0;
    }
    free(entite);
    r.n = lignee(r.e, r.l, LIGNEE_MAX);
    const ClasseVM *e = r.e;

    /* SELECT … FROM la table de l'entité, jointe à celles de sa lignée */
    chaine_ajouter(&r.sql, mode == 0 ? "SELECT g.id, g.classe" : "SELECT count(*)");
    chaine_ajouter(&r.sql, " FROM ");
    for (size_t i = r.n; i > 0; i--) {
        char t[64];
        if (i < r.n) chaine_ajouter(&r.sql, " JOIN ");
        ajouter_nom(&r.sql, "e ", r.l[i - 1]->nom);
        snprintf(t, sizeof t, " AS t%lu", (unsigned long)(i - 1));
        chaine_ajouter(&r.sql, t);
        if (i < r.n) {
            snprintf(t, sizeof t, " ON t%lu.id = t%lu.id", (unsigned long)(i - 1), (unsigned long)(r.n - 1));
            chaine_ajouter(&r.sql, t);
        }
    }
    char t[64];
    snprintf(t, sizeof t, " JOIN grym_objet AS g ON g.id = t%lu.id", (unsigned long)(r.n - 1));
    chaine_ajouter(&r.sql, t);
    int ok = 1;
    chaine_ajouter(&r.sql, corbeille ? " WHERE g.supprime IS NOT NULL" : " WHERE g.supprime IS NULL");
    if (s4[1]) {
        chaine_ajouter(&r.sql, " AND ");
        r.p = s4 + 1;
        ok = lire_condition(&r);
    }
    if (ok && mode == 0) {
        chaine_ajouter(&r.sql, " ORDER BY ");
        if (*tri) {
            long k = -1;
            for (size_t q = 0; q < e->nb_champs && k < 0; q++) if (strcmp(e->champs[q], tri) == 0) k = (long)q;
            if (k < 0 || !e->types[k] || multiple(e, (size_t)k)) { r.erreur = grym_dupliquer("champ du tri inconnu"); ok = 0; }
            else {
                colonne(&r, (size_t)k);
                if (strcmp(e->types[k], "nombre") == 0) chaine_ajouter(&r.sql, " COLLATE GRYM_NOMBRE");
                else if (strcmp(e->types[k], "texte") == 0) chaine_ajouter(&r.sql, " COLLATE GRYM_TEXTE");
                if (decroissant) chaine_ajouter(&r.sql, " DESC");
                chaine_ajouter(&r.sql, " NULLS LAST");   /* les absents en dernier, dans les deux sens */
                chaine_ajouter(&r.sql, ", ");
            }
        }
        chaine_ajouter(&r.sql, "g.id");
    }
    free(tri);
    char *texte = chaine_rendre(&r.sql);
    sqlite3_stmt *st = NULL;
    if (ok && sqlite3_prepare_v2(b->db, texte, -1, &st, NULL) != SQLITE_OK) {
        r.erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        ok = 0;
    }
    free(texte);
    for (size_t i = 0; ok && i < r.nb_liens; i++)
        ok = lier_parametre(&r, st, (int)i + 1, r.types[i], r.champs[i], &params[r.liens[i]]);
    if (!ok) {
        sqlite3_finalize(st);
        *erreur = r.erreur ? r.erreur : grym_dupliquer("Recherche impossible.");
        return 0;
    }
    if (mode == 0) {
        size_t cap = 16, n = 0;
        long *ids = grym_allouer(cap * sizeof *ids);
        char **classes = grym_allouer(cap * sizeof *classes);
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            if (n == cap) {
                cap *= 2;
                long *i2 = grym_allouer(cap * sizeof *i2);
                char **c2 = grym_allouer(cap * sizeof *c2);
                memcpy(i2, ids, n * sizeof *ids);
                memcpy(c2, classes, n * sizeof *classes);
                free(ids);
                free(classes);
                ids = i2;
                classes = c2;
            }
            ids[n] = (long)sqlite3_column_int64(st, 0);
            classes[n] = grym_dupliquer((const char *)sqlite3_column_text(st, 1));
            n++;
        }
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) {
            for (size_t k = 0; k < n; k++) free(classes[k]);
            free(classes);
            free(ids);
            *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
            return 0;
        }
        *resultat = vi_liste(ids, classes, n);
        return 1;
    }
    long compte = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    sqlite3_finalize(st);
    if (mode == 2) {
        char tampon[32];
        snprintf(tampon, sizeof tampon, "%ld", compte);
        *resultat = vi_nombre_canonique(tampon);
        return 1;
    }
    /* mode 1 : exactement un objet */
    if (compte != 1) {
        char *qui = pluriel(e);
        *erreur = compte == 0
            ? grym_formater("Aucun%s %s %s%s ne répond à cette condition.", e->feminin ? "e" : "", e->nom,
                            corbeille ? "supprimé" : "conservé", e->feminin ? "e" : "")
            : grym_formater("%ld %s %s%ss répondent à cette condition : « %s %s %s%s dont … » en "
                            "attend un seul.", compte, qui, corbeille ? "supprimé" : "conservé", e->feminin ? "e" : "",
                            e->feminin ? "la" : "le", e->nom, corbeille ? "supprimé" : "conservé", e->feminin ? "e" : "");
        free(qui);
        return 0;
    }
    /* même recherche, qui rend l'objet */
    char *d0 = grym_formater("%.*s%c%s", (int)(s1 - d + 1), d, corbeille ? '3' : '0', s1 + 2);
    Valeur liste;
    ok = base_chercher(b, m, d0, params, nb_params, &liste, erreur);
    free(d0);
    if (!ok) return 0;
    Objet *o = machine_objet_en_base(m, liste.liste->ids[0], liste.liste->classes[0], erreur);
    vi_liberer(&liste);
    if (!o) return 0;
    *resultat = vi_objet(o);
    return 1;
}
