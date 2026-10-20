/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.37), § 16 ; docs/vm.md (révision 1.32), § 8.
 */
#include "base.h"
#include "chemins.h"
#include "decimal.h"
#include "date.h"
#include "texte.h"

#include "sqlite3.h"
#include "types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REQUETES_GARDEES 512

/* Noms d'entités : celles qu'une transaction a écrites, celles qu'une règle a lues (docs/vm.md, § 3.3). */
typedef struct {
    char **noms;
    size_t n, cap;
    int tout;   /* tout compte : suppression, migration, base changée par un autre programme */
} Noms;

/* Ce que la dernière vérification complète d'une règle a lu ; « attente » : lu dans la transaction en cours,
 * qui ne vaut qu'une fois validée. */
typedef struct {
    char *cle;
    Noms lues, attente;
    int confirmee, en_attente;
} LecturesRegle;

struct Base {
    sqlite3 *db;
    char *chemin;     /* tel qu'affiché dans les messages */
    int transaction;
    int changements;  /* sqlite3_total_changes au début de la transaction : « la base a-t-elle changé ? » (§ 16.14) */
    Chaine *rapport;  /* ce que les migrations font, une ligne par changement (A2-c), ou NULL */
    /* Requêtes préparées gardées pour resservir (docs/vm.md, § 8.2) : une requête se compile une fois par base,
     * pas à chaque objet lu. Une entrée en cours d'emploi n'est pas resservie : la même requête, demandée
     * pendant ce temps, se prépare à part et se jette. */
    struct { char *sql; sqlite3_stmt *st; int en_usage; } cache[REQUETES_GARDEES];
    int a_convertir;  /* base d'avant le format 2 dont des fichiers sont encore dans les tables des entités */
    long convertis;   /* fichiers déménagés par la transaction en cours : le rapport des migrations les compte */
    int fichiers_prets;   /* grym_fichier existe, validée par une transaction */
    Noms ecrites;         /* entités écrites par la transaction en cours, lignées comprises */
    int lecture;          /* une règle s'évalue : ses lectures se notent dans « lues » */
    Noms lues;
    LecturesRegle *regles;
    size_t nb_regles;
    sqlite3_int64 version_connue;   /* PRAGMA data_version : un autre programme a-t-il écrit ? */
    int changee_ailleurs;           /* oui, depuis notre transaction précédente */
};

static void noms_vider(Noms *n) {
    for (size_t k = 0; k < n->n; k++) free(n->noms[k]);
    n->n = 0;
    n->tout = 0;
}

static void noms_liberer(Noms *n) {
    noms_vider(n);
    free(n->noms);
    n->noms = NULL;
    n->cap = 0;
}

static void noms_ajouter(Noms *n, const char *nom) {
    for (size_t k = 0; k < n->n; k++) if (strcmp(n->noms[k], nom) == 0) return;
    if (n->n == n->cap) {
        n->cap = n->cap ? n->cap * 2 : 8;
        char **t = grym_allouer(n->cap * sizeof *t);
        if (n->n) memcpy(t, n->noms, n->n * sizeof *t);
        free(n->noms);
        n->noms = t;
    }
    n->noms[n->n++] = grym_dupliquer(nom);
}

/* Une entité et ses parents : chercher un parent trouve aussi ses descendants. */
static void noms_lignee(Noms *n, const ClasseVM *c) {
    for (; c; c = c->parent) noms_ajouter(n, c->nom);
}

static int noms_croisent(const Noms *a, const Noms *b) {
    if (a->tout || b->tout) return 1;
    for (size_t i = 0; i < a->n; i++)
        for (size_t j = 0; j < b->n; j++)
            if (strcmp(a->noms[i], b->noms[j]) == 0) return 1;
    return 0;
}

static void ecrit(Base *b, const ClasseVM *c) {
    if (b->transaction) noms_lignee(&b->ecrites, c);
}

static void ecrit_tout(Base *b) { b->ecrites.tout = 1; }

void base_noter_lecture(Base *b, const ClasseVM *c) {
    if (b && b->lecture) noms_lignee(&b->lues, c);
}

/* Les entités qu'une requête lit : ses tables « "e X" » et « "m X.champ" ». */
static void noter_requete(Base *b, const char *sql) {
    if (!b->lecture) return;
    for (const char *p = sql; (p = strchr(p, '"')) != NULL;) {
        const char *fin = strchr(p + 1, '"');
        if (!fin) break;
        if ((p[1] == 'e' || p[1] == 'm') && p[2] == ' ') {
            const char *f = fin;
            if (p[1] == 'm') {   /* « m entité.champ » : l'entité qui porte le champ */
                const char *point = NULL;
                for (const char *q = p + 3; q < fin; q++) if (*q == '.') { point = q; break; }
                if (point) f = point;
            }
            char *nom = grym_formater("%.*s", (int)(f - p - 3), p + 3);
            noms_ajouter(&b->lues, nom);
            free(nom);
        }
        p = fin + 1;
    }
}

static LecturesRegle *regle_trouver(Base *b, const char *cle, int creer) {
    for (size_t k = 0; k < b->nb_regles; k++) if (strcmp(b->regles[k].cle, cle) == 0) return &b->regles[k];
    if (!creer) return NULL;
    LecturesRegle *t = grym_allouer((b->nb_regles + 1) * sizeof *t);
    if (b->nb_regles) memcpy(t, b->regles, b->nb_regles * sizeof *t);
    free(b->regles);
    b->regles = t;
    LecturesRegle *r = &b->regles[b->nb_regles++];
    memset(r, 0, sizeof *r);
    r->cle = grym_dupliquer(cle);
    return r;
}

int base_regle_utile(Base *b, const char *cle) {
    if (!b) return 1;
    LecturesRegle *r = regle_trouver(b, cle, 0);
    int utile = !r || !r->confirmee || noms_croisent(&r->lues, &b->ecrites);
    b->lecture = utile;
    noms_vider(&b->lues);
    return utile;
}

void base_regle_faite(Base *b, const char *cle) {
    if (!b) return;
    b->lecture = 0;
    LecturesRegle *r = regle_trouver(b, cle, 1);
    noms_liberer(&r->attente);
    r->attente = b->lues;   /* les lectures passent à la règle, sans copie */
    memset(&b->lues, 0, sizeof b->lues);
    r->en_attente = 1;
}

/* Validée : ce que les règles ont lu vaut désormais ; annulée (ou ramenée à un point de reprise), rien. */
static void regles_valider(Base *b, int valide) {
    b->lecture = 0;
    for (size_t k = 0; k < b->nb_regles; k++) {
        LecturesRegle *r = &b->regles[k];
        if (!r->en_attente) continue;
        if (valide) {
            noms_liberer(&r->lues);
            r->lues = r->attente;
            memset(&r->attente, 0, sizeof r->attente);
            r->confirmee = 1;
        } else {
            noms_liberer(&r->attente);
        }
        r->en_attente = 0;
    }
}

void base_rapport(Base *b, Chaine *rapport) {
    b->rapport = rapport;
    if (rapport && b->convertis) {   /* le format 2, fait au début de la transaction (docs/vm.md, § 8) */
        char *l = grym_formater("Les fichiers conservés (%ld) quittent les tables de leurs entités pour grym_fichier : "
                                "la base passe au format 2.\n", b->convertis);
        chaine_ajouter(rapport, l);
        free(l);
    }
}

static unsigned long empreinte(const char *t) {
    unsigned long h = 5381;
    for (; *t; t++) h = h * 33 + (unsigned char)*t;
    return h;
}

/* Prépare sql, ou ressert la requête déjà compilée ; à rendre par rendre(). Code de retour de SQLite. */
static void noter_requete(Base *b, const char *sql);

static int preparer(Base *b, const char *sql, sqlite3_stmt **st) {
    noter_requete(b, sql);   /* une règle qui s'évalue : chaque requête dit ce qu'elle lit (docs/vm.md, § 3.3) */
    unsigned long h = empreinte(sql);
    for (size_t q = 0; q < REQUETES_GARDEES; q++) {
        size_t i = (h + q) % REQUETES_GARDEES;
        if (!b->cache[i].sql) {
            int rc = sqlite3_prepare_v2(b->db, sql, -1, st, NULL);
            if (rc != SQLITE_OK || !*st) return rc;
            b->cache[i].sql = grym_dupliquer(sql);
            b->cache[i].st = *st;
            b->cache[i].en_usage = 1;
            return rc;
        }
        if (strcmp(b->cache[i].sql, sql) == 0) {
            if (b->cache[i].en_usage) break;
            b->cache[i].en_usage = 1;
            *st = b->cache[i].st;
            return SQLITE_OK;
        }
    }
    return sqlite3_prepare_v2(b->db, sql, -1, st, NULL);   /* cache plein, ou requête déjà en cours */
}

static void rendre(Base *b, sqlite3_stmt *st) {
    if (!st) return;
    for (size_t i = 0; i < REQUETES_GARDEES; i++)
        if (b->cache[i].st == st) {
            sqlite3_reset(st);
            sqlite3_clear_bindings(st);
            b->cache[i].en_usage = 0;
            return;
        }
    sqlite3_finalize(st);
}

static void oublier_requetes(Base *b) {
    for (size_t i = 0; i < REQUETES_GARDEES; i++) {
        if (!b->cache[i].sql) continue;
        sqlite3_finalize(b->cache[i].st);
        free(b->cache[i].sql);
        b->cache[i].sql = NULL;
        b->cache[i].st = NULL;
    }
}

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
    return type_fichier(type);
}

static int est_lien(const char *type) {
    static const char *const BASE[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image", "son",
                                        "année", "heure" };
    for (size_t k = 0; k < sizeof BASE / sizeof *BASE; k++) if (strcmp(type, BASE[k]) == 0) return 0;
    return !type_textuel(type);   /* courriel, téléphone, adresse web : des textes, pas des liens (types.h) */
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
    if (preparer(b, t, &st) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        n = (long)sqlite3_column_int64(st, 0);
    rendre(b, st);
    free(t);
    return n;
}

int base_collations(sqlite3 *db);

/* ---------------------------------------------------------------- */
/* Ouverture et transaction                                         */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/* Fichiers en base (format 2)                                      */
/* ---------------------------------------------------------------- */

/* Les octets d'un fichier vivent dans grym_fichier, une ligne par valeur rangée : la table de l'entité n'en garde
 * que le numéro, et le nom d'origine dans « n <champ> ». Lire un objet ne lit donc que la taille et les premiers
 * octets (le format) ; les octets attendent le premier besoin. Une ligne appartient à un seul champ d'un seul
 * objet : la remplacer ou effacer l'objet l'efface. */

#define DEBUT_FICHIER 16   /* assez pour reconnaître PNG, JPEG, GIF et WebP */

static sqlite3_int64 empreinte_octets(const unsigned char *o, size_t n) {   /* FNV-1a, 64 bits */
    unsigned long long h = 14695981039346656037ULL;
    for (size_t i = 0; i < n; i++) { h ^= o[i]; h *= 1099511628211ULL; }
    return (sqlite3_int64)h;
}

/* Range les octets d'un fichier dans une ligne neuve de grym_fichier ; 0 et *erreur sinon. */
static long ranger_fichier(Base *b, Fichier *f, char **erreur) {
    sqlite3_stmt *st = NULL;
    int ok;
    if (f->base == b && f->cle && !f->octets) {   /* resté en base : copié sans passer par la mémoire */
        ok = preparer(b, "INSERT INTO grym_fichier (taille, empreinte, debut, octets) "
                         "SELECT taille, empreinte, debut, octets FROM grym_fichier WHERE id = ?", &st) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)f->cle);
            ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(b->db) == 1;
        }
    } else {
        if (!f->octets && !base_fichier_lire(f, erreur)) return 0;
        ok = preparer(b, "INSERT INTO grym_fichier (taille, empreinte, debut, octets) VALUES (?, ?, ?, ?)", &st)
             == SQLITE_OK;
        if (ok) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)f->taille);
            sqlite3_bind_int64(st, 2, empreinte_octets(f->octets, f->taille));
            sqlite3_bind_blob(st, 3, f->octets, (int)(f->taille < DEBUT_FICHIER ? f->taille : DEBUT_FICHIER),
                              SQLITE_TRANSIENT);
            sqlite3_bind_blob64(st, 4, f->octets, (sqlite3_uint64)f->taille, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
        }
    }
    rendre(b, st);
    if (!ok) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    return (long)sqlite3_last_insert_rowid(b->db);
}

int base_fichier_lire(Fichier *f, char **erreur) {
    if (f->octets) return 1;
    Base *b = f->base;
    if (!b) { *erreur = grym_formater("Le fichier « %s » est illisible.", f->nom); return 0; }
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT octets FROM grym_fichier WHERE id = ?", &st);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)f->cle);
    if (sqlite3_step(st) != SQLITE_ROW) {
        rendre(b, st);
        *erreur = grym_formater("Le fichier « %s » a disparu de la base « %s ».", f->nom, b->chemin);
        return 0;
    }
    size_t n = (size_t)sqlite3_column_bytes(st, 0);
    f->octets = grym_allouer(n ? n : 1);
    if (n) memcpy(f->octets, sqlite3_column_blob(st, 0), n);
    f->taille = n;
    rendre(b, st);
    return 1;
}

/* Efface la ligne de grym_fichier désignée par une colonne de fichier, si elle en désigne une. */
static int effacer_fichier(Base *b, sqlite3_int64 cle) {
    sqlite3_stmt *st = NULL;
    preparer(b, "DELETE FROM grym_fichier WHERE id = ?", &st);
    sqlite3_bind_int64(st, 1, cle);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    rendre(b, st);
    return ok;
}

/* Colonnes de fichier d'une base : « c X » accompagnée de « n X ». Rend leur nombre ; *tables et *champs (à
 * libérer) les nomment. */
static size_t colonnes_de_fichier(Base *b, char ***tables, char ***champs) {
    size_t n = 0, cap = 8;
    *tables = grym_allouer(cap * sizeof **tables);
    *champs = grym_allouer(cap * sizeof **champs);
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT m.name, p.name FROM sqlite_master AS m, pragma_table_info(m.name) AS p "
                "WHERE m.type = 'table' AND m.name LIKE 'e %' AND p.name LIKE 'n %' "
                "AND EXISTS (SELECT 1 FROM pragma_table_info(m.name) AS q WHERE q.name = 'c ' || substr(p.name, 3))",
             &st);
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        if (n == cap) {
            cap *= 2;
            char **t2 = grym_allouer(cap * sizeof *t2), **c2 = grym_allouer(cap * sizeof *c2);
            memcpy(t2, *tables, n * sizeof *t2);
            memcpy(c2, *champs, n * sizeof *c2);
            free(*tables);
            free(*champs);
            *tables = t2;
            *champs = c2;
        }
        (*tables)[n] = grym_dupliquer((const char *)sqlite3_column_text(st, 0) + 2);
        (*champs)[n] = grym_dupliquer((const char *)sqlite3_column_text(st, 1) + 2);
        n++;
    }
    rendre(b, st);
    return n;
}

static void liberer_noms(char **t, size_t n) {
    for (size_t k = 0; k < n; k++) free(t[k]);
    free(t);
}

/* Une base d'avant le format 2 a-t-elle des octets de fichier dans les tables de ses entités ? */
static int fichiers_a_convertir(Base *b) {
    char **tables, **champs;
    size_t n = colonnes_de_fichier(b, &tables, &champs);
    int r = 0;
    for (size_t k = 0; k < n && !r; k++) {
        Chaine sql = {0};
        chaine_ajouter(&sql, "SELECT EXISTS (SELECT 1 FROM ");
        ajouter_nom(&sql, "e ", tables[k]);
        chaine_ajouter(&sql, " WHERE typeof(");
        ajouter_nom(&sql, "c ", champs[k]);
        chaine_ajouter(&sql, ") = 'blob')");
        char *t = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        if (preparer(b, t, &st) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) r = sqlite3_column_int(st, 0);
        rendre(b, st);
        free(t);
    }
    liberer_noms(tables, n);
    liberer_noms(champs, n);
    return r;
}

/* Format 1 → 2, dans la transaction : chaque fichier rangé dans la table de son entité déménage dans
 * grym_fichier, une ligne par valeur ; la colonne garde le numéro. Puis le format 2 est écrit. */
static int convertir_fichiers(Base *b, char **erreur) {
    char **tables, **champs;
    size_t n = colonnes_de_fichier(b, &tables, &champs);
    int ok = 1;
    for (size_t k = 0; ok && k < n; k++) {
        /* d'abord les identifiants : on ne réécrit pas une table pendant qu'on la parcourt */
        Chaine sql = {0};
        chaine_ajouter(&sql, "SELECT id FROM ");
        ajouter_nom(&sql, "e ", tables[k]);
        chaine_ajouter(&sql, " WHERE typeof(");
        ajouter_nom(&sql, "c ", champs[k]);
        chaine_ajouter(&sql, ") = 'blob'");
        char *t = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        size_t nb = 0, cap = 16;
        sqlite3_int64 *ids = grym_allouer(cap * sizeof *ids);
        ok = preparer(b, t, &st) == SQLITE_OK;
        while (ok && sqlite3_step(st) == SQLITE_ROW) {
            if (nb == cap) {
                cap *= 2;
                sqlite3_int64 *i2 = grym_allouer(cap * sizeof *i2);
                memcpy(i2, ids, nb * sizeof *ids);
                free(ids);
                ids = i2;
            }
            ids[nb++] = sqlite3_column_int64(st, 0);
        }
        rendre(b, st);
        free(t);
        Chaine lire = {0}, ecrire = {0};
        chaine_ajouter(&lire, "SELECT ");
        ajouter_nom(&lire, "c ", champs[k]);
        chaine_ajouter(&lire, " FROM ");
        ajouter_nom(&lire, "e ", tables[k]);
        chaine_ajouter(&lire, " WHERE id = ?");
        chaine_ajouter(&ecrire, "UPDATE ");
        ajouter_nom(&ecrire, "e ", tables[k]);
        chaine_ajouter(&ecrire, " SET ");
        ajouter_nom(&ecrire, "c ", champs[k]);
        chaine_ajouter(&ecrire, " = ? WHERE id = ?");
        char *tl = chaine_rendre(&lire), *te = chaine_rendre(&ecrire);
        for (size_t q = 0; ok && q < nb; q++) {   /* un fichier à la fois : jamais toute la table en mémoire */
            Fichier f = { 1, NULL, 0, NULL, NULL, NULL, 0 };
            st = NULL;
            ok = preparer(b, tl, &st) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_int64(st, 1, ids[q]);
                ok = sqlite3_step(st) == SQLITE_ROW;
                if (ok) {
                    f.taille = (size_t)sqlite3_column_bytes(st, 0);
                    f.octets = grym_allouer(f.taille ? f.taille : 1);
                    if (f.taille) memcpy(f.octets, sqlite3_column_blob(st, 0), f.taille);
                }
            }
            rendre(b, st);
            long cle = ok ? ranger_fichier(b, &f, erreur) : 0;
            if (cle) b->convertis++;
            free(f.octets);
            ok = ok && cle;
            if (!ok) break;
            st = NULL;
            ok = preparer(b, te, &st) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_int64(st, 1, (sqlite3_int64)cle);
                sqlite3_bind_int64(st, 2, ids[q]);
                ok = sqlite3_step(st) == SQLITE_DONE;
            }
            rendre(b, st);
        }
        free(tl);
        free(te);
        free(ids);
    }
    liberer_noms(tables, n);
    liberer_noms(champs, n);
    if (ok) {
        char *sql = grym_formater("PRAGMA user_version = %d;", BASE_FORMAT);
        ok = executer(b, sql, erreur);
        free(sql);
    } else if (!*erreur) {
        *erreur = grym_formater("Base « %s » : les fichiers n'ont pas pu passer au format %d (%s).", b->chemin,
                                BASE_FORMAT, sqlite3_errmsg(b->db));
    }
    return ok;
}

/* Une restauration en attente (grammaire 2.30) : « base.grymd.restaurer », écrit par « Restaurer la base … » à la fin
 * d'une exécution réussie, prend la place de la base avant qu'elle s'ouvre. La base d'avant n'est jamais perdue :
 * elle devient « base.grymd avant restauration AAAA-MM-JJ HHhMM.grymd » (puis « (2) »…), avec son journal WAL s'il en
 * reste un, et le fichier partagé (-shm), qui ne décrit que l'ancienne, est retiré. */
static int restaurer_en_attente(const char *chemin, char **erreur) {
    char *attente = grym_formater("%s.restaurer", chemin);
    FILE *f = grym_fopen(attente, "rb");
    if (!f) { free(attente); return 1; }
    fclose(f);
    char date[32];
    const time_t maintenant = time(NULL);
    strftime(date, sizeof date, "%Y-%m-%d %Hh%M", localtime(&maintenant));
    char *avant = NULL;
    for (int rang = 1; rang < 1000 && !avant; rang++) {
        char *essai = rang == 1 ? grym_formater("%s avant restauration %s.grymd", chemin, date)
                                : grym_formater("%s avant restauration %s (%d).grymd", chemin, date, rang);
        FILE *e = grym_fopen(essai, "rb");
        if (e) { fclose(e); free(essai); } else avant = essai;
    }
    int ok = avant != NULL;
    FILE *existe = grym_fopen(chemin, "rb");
    if (existe) {
        fclose(existe);
        ok = ok && grym_renommer(chemin, avant) == 0;
        if (ok) {
            char *wal = grym_formater("%s-wal", chemin), *wal_avant = grym_formater("%s-wal", avant), *shm = grym_formater("%s-shm", chemin);
            FILE *w = grym_fopen(wal, "rb");
            if (w) { fclose(w); grym_renommer(wal, wal_avant); }
            grym_supprimer(shm);
            free(wal);
            free(wal_avant);
            free(shm);
        }
    }
    if (ok) ok = grym_renommer(attente, chemin) == 0;
    if (!ok) *erreur = grym_formater("Base « %s » : la restauration en attente n'a pas pu prendre sa place.", chemin);
    free(avant);
    free(attente);
    return ok;
}

int base_verifier_copie(const unsigned char *o, size_t n, char **erreur) {
    if (n < 100 || memcmp(o, "SQLite format 3", 16) != 0) {
        *erreur = grym_dupliquer("Ce fichier n'est pas une base GrymoiR.");
        return 0;
    }
    sqlite3 *db = NULL;
    if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        *erreur = grym_dupliquer("Mémoire insuffisante pour lire la sauvegarde.");
        return 0;
    }
    unsigned char *copie = sqlite3_malloc64(n);
    int ok = copie != NULL;
    if (ok) {
        memcpy(copie, o, n);
        ok = sqlite3_deserialize(db, "main", copie, (sqlite3_int64)n, (sqlite3_int64)n,
                                 SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_READONLY) == SQLITE_OK;
    }
    sqlite3_stmt *st = NULL;
    if (ok) ok = sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master WHERE name = 'grym_schema';", -1, &st, NULL) == SQLITE_OK
                 && sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == 1;
    sqlite3_finalize(st);
    if (ok) {   /* l'intégrité, page par page : une copie tronquée ou abîmée est refusée avant de remplacer quoi que ce soit */
        ok = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW
             && strcmp((const char *)sqlite3_column_text(st, 0), "ok") == 0;
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    if (!ok) *erreur = grym_dupliquer("Ce fichier n'est pas une base GrymoiR intacte : rien n'est restauré.");
    return ok;
}

Base *base_ouvrir(const char *chemin, char **erreur) {
    if (chemin && !restaurer_en_attente(chemin, erreur)) return NULL;
    Base *b = grym_allouer(sizeof *b);
    memset(b, 0, sizeof *b);
    b->db = NULL;
    b->transaction = 0;
    b->rapport = NULL;
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
    /* Format de la base (charte, art. 13) : lu avant toute écriture ; plus récent que nous, refusé. */
    long format = 0;
    {
        sqlite3_stmt *sv = NULL;
        if (preparer(b, "PRAGMA user_version;", &sv) != SQLITE_OK) {
            *erreur = grym_formater("Base « %s » : fichier illisible (%s).", b->chemin, sqlite3_errmsg(b->db));
            rendre(b, sv);
            base_fermer(b);
            return NULL;
        }
        if (sqlite3_step(sv) == SQLITE_ROW) format = (long)sqlite3_column_int64(sv, 0);
        rendre(b, sv);
    }
    if (format > BASE_FORMAT) {
        *erreur = grym_formater("La base « %s » a le format %ld, plus récent que celui de ce grym (%d) : "
                                "ouvre-la avec une version plus récente de grym. Elle n'a pas été modifiée.",
                                b->chemin, format, BASE_FORMAT);
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
    preparer(b, "SELECT count(*) FROM pragma_table_info('grym_objet') WHERE name = 'supprime'", &st);
    if (sqlite3_step(st) == SQLITE_ROW) present = sqlite3_column_int(st, 0);
    rendre(b, st);
    if (!present && !executer(b, "ALTER TABLE grym_objet ADD COLUMN supprime TEXT;"
                                 "ALTER TABLE grym_objet ADD COLUMN supprime_avec INTEGER;", erreur)) {
        base_fermer(b);
        return NULL;
    }
    if (format < BASE_FORMAT) {   /* base neuve ou d'avant le numéro : elle reçoit le format courant */
        /* des fichiers encore rangés dans les tables des entités : ils déménagent dans la première transaction,
         * qui écrit le format avec eux ; « grym migration », qui annule la sienne, ne change donc rien */
        b->a_convertir = format < 2 && fichiers_a_convertir(b);
        if (!b->a_convertir) {
            char *sql = grym_formater("PRAGMA user_version = %d;", BASE_FORMAT);
            int ok = executer(b, sql, erreur);
            free(sql);
            if (!ok) { base_fermer(b); return NULL; }
        }
    }
    return b;
}

void base_fermer(Base *b) {
    if (!b) return;
    if (b->transaction) base_annuler(b);
    oublier_requetes(b);
    noms_liberer(&b->ecrites);
    noms_liberer(&b->lues);
    for (size_t k = 0; k < b->nb_regles; k++) {
        free(b->regles[k].cle);
        noms_liberer(&b->regles[k].lues);
        noms_liberer(&b->regles[k].attente);
    }
    free(b->regles);
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
    /* créée dans une transaction : un essai de migration, qui annule la sienne, ne laisse rien derrière lui */
    if (!b->fichiers_prets
        && !executer(b, "CREATE TABLE IF NOT EXISTS grym_fichier (id INTEGER PRIMARY KEY, taille INTEGER NOT NULL, "
                        "empreinte INTEGER NOT NULL, debut BLOB NOT NULL, octets BLOB NOT NULL);", erreur)) {
        base_annuler(b);
        return 0;
    }
    b->convertis = 0;
    noms_vider(&b->ecrites);
    {   /* un autre programme a écrit depuis notre dernière transaction : ce que les règles ont lu ne vaut plus */
        sqlite3_stmt *st = NULL;
        sqlite3_int64 v = -1;
        if (preparer(b, "PRAGMA data_version;", &st) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
            v = sqlite3_column_int64(st, 0);
        rendre(b, st);
        b->changee_ailleurs = v != b->version_connue;
        if (b->changee_ailleurs)
            for (size_t k = 0; k < b->nb_regles; k++) b->regles[k].confirmee = 0;
        b->version_connue = v;
    }
    if (b->a_convertir) {   /* format 1 → 2, avec la première transaction */
        ecrit_tout(b);
        if (!convertir_fichiers(b, erreur)) {
            base_annuler(b);
            return 0;
        }
    }
    b->changements = sqlite3_total_changes(b->db);
    return 1;
}

int base_changee_ailleurs(Base *b) { return b && b->changee_ailleurs; }

int base_modifiee(Base *b) {
    return b && b->transaction && sqlite3_total_changes(b->db) != b->changements;
}

int base_valider(Base *b, char **erreur) {
    if (!b->transaction) return 1;
    b->transaction = 0;
    if (sqlite3_exec(b->db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK) {
        b->a_convertir = 0;
        b->fichiers_prets = 1;
        regles_valider(b, 1);
        return 1;
    }
    regles_valider(b, 0);
    *erreur = grym_formater("Base « %s » : validation impossible (%s).", b->chemin, sqlite3_errmsg(b->db));
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
    return 0;
}

void base_annuler(Base *b) {
    regles_valider(b, 0);
    if (!b->transaction) return;
    b->transaction = 0;
    sqlite3_exec(b->db, "ROLLBACK;", NULL, NULL, NULL);
}

int base_membres(Base *b, const ClasseVM *c, size_t k, long id, long **ids, char ***classes, size_t *n,
                 char **erreur) {
    *ids = NULL;
    *classes = NULL;
    *n = 0;
    Chaine sql = {0};
    chaine_ajouter(&sql, "SELECT o.id, o.classe FROM ");
    ajouter_liaison(&sql, c->proprietaires[k]->nom, c->champs[k]);
    chaine_ajouter(&sql, " l JOIN grym_objet o ON o.id = l.b WHERE l.a = ?1 AND o.supprime IS NULL ORDER BY l.rowid");
    sqlite3_stmt *st = NULL;
    int rc = preparer(b, sql.d, &st);
    free(sql.d);
    if (rc != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        rendre(b, st);
        return 0;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    size_t cap = 0;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (*n == cap) {
            cap = cap ? cap * 2 : 8;
            long *ti = grym_allouer(cap * sizeof *ti);
            char **tc = grym_allouer(cap * sizeof *tc);
            if (*n) { memcpy(ti, *ids, *n * sizeof *ti); memcpy(tc, *classes, *n * sizeof *tc); }
            free(*ids);
            free(*classes);
            *ids = ti;
            *classes = tc;
        }
        (*ids)[*n] = (long)sqlite3_column_int64(st, 0);
        const unsigned char *t = sqlite3_column_text(st, 1);
        (*classes)[(*n)++] = grym_dupliquer(t ? (const char *)t : "");
    }
    rendre(b, st);
    if (rc != SQLITE_DONE) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    return 1;
}

int base_valeurs(Base *b, const char *entite, const char *table, const char *champ, size_t max,
                 char ***valeurs, size_t *n, char **erreur) {
    *valeurs = NULL;
    *n = 0;
    Chaine sql = {0};
    chaine_ajouter(&sql, "SELECT p.");
    ajouter_nom(&sql, "c ", champ);
    chaine_ajouter(&sql, " FROM ");
    ajouter_nom(&sql, "e ", entite);
    chaine_ajouter(&sql, " l JOIN ");
    ajouter_nom(&sql, "e ", table);
    chaine_ajouter(&sql, " p ON p.id = l.id JOIN grym_objet o ON o.id = l.id WHERE o.supprime IS NULL ORDER BY 1 "
                         "COLLATE GRYM_TEXTE LIMIT ?");
    sqlite3_stmt *st = NULL;
    int rc = preparer(b, sql.d, &st);
    free(sql.d);
    if (rc != SQLITE_OK) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        rendre(b, st);
        return 0;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)max);
    *valeurs = grym_allouer((max ? max : 1) * sizeof **valeurs);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW && *n < max) {
        const unsigned char *t = sqlite3_column_text(st, 0);
        (*valeurs)[(*n)++] = grym_dupliquer(t ? (const char *)t : "");
    }
    rendre(b, st);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        for (size_t k = 0; k < *n; k++) free((*valeurs)[k]);
        free(*valeurs);
        *valeurs = NULL;
        *n = 0;
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        return 0;
    }
    return 1;
}

static int point_sql(Base *b, char *sql, char **erreur) {
    if (!b->transaction) { free(sql); return 1; }
    int rc = sqlite3_exec(b->db, sql, NULL, NULL, NULL);
    free(sql);
    if (rc == SQLITE_OK) return 1;
    *erreur = grym_formater("Base « %s » : point de reprise impossible (%s).", b->chemin, sqlite3_errmsg(b->db));
    return 0;
}

int base_point(Base *b, size_t n, char **erreur) {
    return point_sql(b, grym_formater("SAVEPOINT grym_essai_%lu;", (unsigned long)n), erreur);
}

int base_lacher(Base *b, size_t n, char **erreur) {
    return point_sql(b, grym_formater("RELEASE grym_essai_%lu;", (unsigned long)n), erreur);
}

int base_revenir(Base *b, size_t n, char **erreur) {
    regles_valider(b, 0);   /* ce qu'une règle a lu dans ce qui s'annule ne vaut plus */
    return point_sql(b, grym_formater("ROLLBACK TO grym_essai_%lu; RELEASE grym_essai_%lu;", (unsigned long)n,
                                      (unsigned long)n), erreur);
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
    preparer(b, t, &st);
    free(t);
    long n = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    rendre(b, st);
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
        const char *v = depart ? depart : strcmp(type, "nombre") == 0 ? "0" : strcmp(type, "date") == 0 ? "0001-01-01"
                      : strcmp(type, "heure") == 0 ? "00:00" : "";
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

/* ---------------------------------------------------------------- */
/* Renommages (grammaire, § 16.7 : « anciennement « client » »)    */
/* ---------------------------------------------------------------- */

/* Les tables et index internes dont le nom commence par « prefixe » (« m client. », « u séance.client »), sans les
 * guillemets : SQLite garde le nom tel qu'écrit entre guillemets. */
static char **objets_sqlite(Base *b, const char *sorte, const char *prefixe, size_t *n) {
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT name FROM sqlite_master WHERE type = ? AND substr(name, 1, length(?2)) = ?2 ORDER BY name", &st);
    sqlite3_bind_text(st, 1, sorte, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, prefixe, -1, SQLITE_TRANSIENT);
    char **r = NULL;
    *n = 0;
    while (sqlite3_step(st) == SQLITE_ROW) {
        char **t = grym_allouer((*n + 1) * sizeof *t);
        if (*n) memcpy(t, r, *n * sizeof *t);
        free(r);
        r = t;
        r[(*n)++] = grym_dupliquer((const char *)sqlite3_column_text(st, 0));
    }
    rendre(b, st);
    return r;
}

static char **agrandir_noms(char **t, size_t n) {
    char **r = grym_allouer(n * sizeof *r);
    if (n > 1) memcpy(r, t, (n - 1) * sizeof *r);
    free(t);
    return r;
}

/* « ALTER TABLE "x" RENAME TO "y" », les deux noms complets, sans guillemets. */
static int renommer_table(Base *b, const char *ancien, const char *nouveau, char **erreur) {
    Chaine sql = {0};
    chaine_ajouter(&sql, "ALTER TABLE ");
    ajouter_nom(&sql, "", ancien);
    chaine_ajouter(&sql, " RENAME TO ");
    ajouter_nom(&sql, "", nouveau);
    return executer_chaine(b, &sql, erreur);
}

/* Les index nommés d'après « entité.champ » : l'index d'un lien (« l ») part, indexer_liens le refait sous son nouveau
 * nom ; l'index d'unicité ajouté par une migration (« u ») est refait tout de suite, sur la colonne déjà renommée. */
static int renommer_index(Base *b, const char *entite_avant, const char *champ_avant, const char *entite,
                          const char *champ, char **erreur) {
    int ok = 1;
    static const char SORTES[] = "lui";   /* lien, unicité, liaison (« i », sur la colonne b de « m entité.champ ») */
    for (int u = 0; ok && u < 3; u++) {
        char *nom = champ_avant ? grym_formater("%c %s.%s", SORTES[u], entite_avant, champ_avant)
                                : grym_formater("%c %s.", SORTES[u], entite_avant);
        size_t n = 0;
        char **index = objets_sqlite(b, "index", nom, &n);
        free(nom);
        for (size_t k = 0; ok && k < n; k++) {
            const char *suite = strchr(index[k], '.') + 1;   /* le champ */
            Chaine sql = {0};
            chaine_ajouter(&sql, "DROP INDEX ");
            ajouter_nom(&sql, "", index[k]);
            ok = executer_chaine(b, &sql, erreur);
            if (ok && u == 1) {
                const char *colonne = champ ? champ : suite;
                char *nouveau = grym_formater("%s.%s", entite, colonne);
                Chaine c = {0};
                chaine_ajouter(&c, "CREATE UNIQUE INDEX ");
                ajouter_nom(&c, "u ", nouveau);
                chaine_ajouter(&c, " ON ");
                ajouter_nom(&c, "e ", entite);
                chaine_ajouter(&c, " (");
                ajouter_nom(&c, "c ", colonne);
                chaine_ajouter(&c, ")");
                free(nouveau);
                ok = executer_chaine(b, &c, erreur);
            } else if (ok && u == 2) {   /* la table de liaison est déjà renommée */
                const char *colonne = champ ? champ : suite;
                char *nouveau = grym_formater("%s.%s", entite, colonne);
                Chaine c = {0};
                chaine_ajouter(&c, "CREATE INDEX ");
                ajouter_nom(&c, "i ", nouveau);
                chaine_ajouter(&c, " ON ");
                ajouter_liaison(&c, entite, colonne);
                chaine_ajouter(&c, " (b)");
                free(nouveau);
                ok = executer_chaine(b, &c, erreur);
            }
        }
        liberer_noms(index, n);
    }
    return ok;
}

/* Une définition où l'entité « avant » s'appelle désormais « apres » : parent, types des liens. */
static char *definition_renommee(const char *d, const char *avant, const char *apres) {
    Chaine r = {0};
    const char *p = d;
    for (int premier = 1; p; premier = 0) {
        const char *fin = strchr(p, ';');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        char *seg = grym_formater("%.*s", (int)l, p);
        if (!premier) chaine_ajouter(&r, ";");
        if (premier) {   /* « parent=X » */
            if (strncmp(seg, "parent=", 7) == 0 && strcmp(seg + 7, avant) == 0) {
                chaine_ajouter(&r, "parent=");
                chaine_ajouter(&r, apres);
            } else chaine_ajouter(&r, seg);
        } else {         /* « nom:type[:drapeaux] » */
            char *d1 = strchr(seg, ':');
            char *d2 = d1 ? strchr(d1 + 1, ':') : NULL;
            size_t lt = d1 ? (d2 ? (size_t)(d2 - d1 - 1) : strlen(d1 + 1)) : 0;
            if (d1 && strlen(avant) == lt && strncmp(d1 + 1, avant, lt) == 0) {
                char *x = grym_formater("%.*s:%s%s", (int)(d1 - seg), seg, apres, d2 ? d2 : "");
                chaine_ajouter(&r, x);
                free(x);
            } else chaine_ajouter(&r, seg);
        }
        free(seg);
        p = fin ? fin + 1 : NULL;
    }
    return chaine_rendre(&r);
}

static long compter(Base *b, const char *sql, const char *a) {
    sqlite3_stmt *st = NULL;
    preparer(b, sql, &st);
    sqlite3_bind_text(st, 1, a, -1, SQLITE_TRANSIENT);
    long n = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    rendre(b, st);
    return n;
}

int base_renommer_entites(Base *b, ClasseVM *const *classes, size_t nb, char **erreur) {
    for (size_t i = 0; i < nb; i++) {
        const ClasseVM *c = classes[i];
        if (!c->conserve || !c->ancien) continue;
        if (compter(b, "SELECT count(*) FROM grym_schema WHERE entite = ?", c->nom)) continue;   /* déjà fait */
        if (!compter(b, "SELECT count(*) FROM grym_schema WHERE entite = ?", c->ancien)) continue;   /* jamais conservée */
        const char *avant = c->ancien, *apres = c->nom;
        long objets = compter(b, "SELECT count(*) FROM grym_objet WHERE classe = ?", avant);
        char *ta = grym_formater("e %s", avant), *tn = grym_formater("e %s", apres);
        int ok = renommer_table(b, ta, tn, erreur);
        free(ta);
        free(tn);
        /* ses tables de liaison, « m client.genres » */
        char *pre = grym_formater("m %s.", avant);
        size_t n = 0;
        char **liaisons = objets_sqlite(b, "table", pre, &n);
        for (size_t k = 0; ok && k < n; k++) {
            char *nouveau = grym_formater("m %s.%s", apres, liaisons[k] + strlen(pre));
            ok = renommer_table(b, liaisons[k], nouveau, erreur);
            free(nouveau);
        }
        liberer_noms(liaisons, n);
        free(pre);
        if (ok) ok = renommer_index(b, avant, NULL, apres, NULL, erreur);
        if (ok) {
            sqlite3_stmt *st = NULL;
            preparer(b, "UPDATE grym_schema SET entite = ?1 WHERE entite = ?2", &st);
            sqlite3_bind_text(st, 1, apres, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, avant, -1, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE;
            rendre(b, st);
            preparer(b, "UPDATE grym_objet SET classe = ?1 WHERE classe = ?2", &st);
            sqlite3_bind_text(st, 1, apres, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(st, 2, avant, -1, SQLITE_TRANSIENT);
            ok = ok && sqlite3_step(st) == SQLITE_DONE;
            rendre(b, st);
        }
        if (ok) {   /* les définitions qui la désignent : parent, liens */
            sqlite3_stmt *st = NULL;
            preparer(b, "SELECT entite, definition FROM grym_schema", &st);
            size_t nd = 0;
            char **entites = NULL, **defs = NULL;
            while (sqlite3_step(st) == SQLITE_ROW) {
                entites = agrandir_noms(entites, nd + 1);
                defs = agrandir_noms(defs, nd + 1);
                entites[nd] = grym_dupliquer((const char *)sqlite3_column_text(st, 0));
                defs[nd++] = grym_dupliquer((const char *)sqlite3_column_text(st, 1));
            }
            rendre(b, st);
            for (size_t k = 0; ok && k < nd; k++) {
                char *nouvelle = definition_renommee(defs[k], avant, apres);
                if (strcmp(nouvelle, defs[k]) != 0) {
                    preparer(b, "UPDATE grym_schema SET definition = ?1 WHERE entite = ?2", &st);
                    sqlite3_bind_text(st, 1, nouvelle, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(st, 2, entites[k], -1, SQLITE_TRANSIENT);
                    ok = sqlite3_step(st) == SQLITE_DONE;
                    rendre(b, st);
                }
                free(nouvelle);
            }
            liberer_noms(entites, nd);
            liberer_noms(defs, nd);
        }
        if (!ok) {
            if (!*erreur) *erreur = grym_formater("« %s » ne se renomme pas en « %s ».", avant, apres);
            return 0;
        }
        if (b->rapport) {
            char *l = grym_formater("« %s » devient « %s » : %ld objet%s gardé%s.\n", avant, apres, objets,
                                    objets > 1 ? "s" : "", objets > 1 ? "s" : "");
            chaine_ajouter(b->rapport, l);
            free(l);
        }
    }
    return 1;
}

/* Un champ renommé (« , anciennement « client » ») : sa colonne, ou sa table de liaison, change de nom ; la définition
 * lue suit, pour que la suite de la migration le reconnaisse. */
static int renommer_champs(Base *b, const ClasseVM *c, Definition *a, const Definition *n, long lignes, char **erreur) {
    for (size_t q = 0; c->anciens && q < c->nb_champs; q++) {
        const char *avant = c->anciens[q], *apres = c->champs[q];
        if (!avant || c->proprietaires[q] != c) continue;
        long i = chercher_champ(a, avant);
        if (i < 0 || chercher_champ(a, apres) >= 0 || chercher_champ(n, avant) >= 0) continue;
        int ok;
        if (a->uniques[i] & 8) {   /* champ multiple : sa table de liaison (§ 16.13), et son index */
            char *ta = grym_formater("m %s.%s", c->nom, avant), *tn = grym_formater("m %s.%s", c->nom, apres);
            ok = renommer_table(b, ta, tn, erreur) && renommer_index(b, c->nom, avant, c->nom, apres, erreur);
            free(ta);
            free(tn);
        } else {
            Chaine sql = {0};
            chaine_ajouter(&sql, "ALTER TABLE ");
            ajouter_nom(&sql, "e ", c->nom);
            chaine_ajouter(&sql, " RENAME COLUMN ");
            ajouter_nom(&sql, "c ", avant);
            chaine_ajouter(&sql, " TO ");
            ajouter_nom(&sql, "c ", apres);
            if (est_fichier(a->types[i])) {   /* le nom du fichier, dans sa colonne « n » */
                chaine_ajouter(&sql, "; ALTER TABLE ");
                ajouter_nom(&sql, "e ", c->nom);
                chaine_ajouter(&sql, " RENAME COLUMN ");
                ajouter_nom(&sql, "n ", avant);
                chaine_ajouter(&sql, " TO ");
                ajouter_nom(&sql, "n ", apres);
            }
            ok = executer_chaine(b, &sql, erreur) && renommer_index(b, c->nom, avant, c->nom, apres, erreur);
        }
        if (!ok) return 0;
        free(a->noms[i]);
        a->noms[i] = grym_dupliquer(apres);
        if (b->rapport) {
            const int liaisons = (a->uniques[i] & 8) != 0;
            const long nb = liaisons ? compter_liaisons(b, c->nom, apres) : lignes;
            char *l = grym_formater("« %s » : « %s » devient « %s », %ld %s%s gardée%s.\n", c->nom, avant, apres, nb,
                                    liaisons ? "liaison" : "valeur", nb > 1 ? "s" : "", nb > 1 ? "s" : "");
            chaine_ajouter(b->rapport, l);
            free(l);
        }
    }
    return 1;
}

static int migrer(Base *b, const ClasseVM *c, const char *ancienne, const char *nouvelle, char **erreur) {
    Definition a, n;
    lire_definition(ancienne, &a);
    lire_definition(nouvelle, &n);
    long lignes = compter_lignes(b, c);
    char *pl = c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
    int ok = 1;
    char *deja = deja_conserves(c, lignes);
    if (!renommer_champs(b, c, &a, &n, lignes, erreur)) ok = 0;   /* « , anciennement « client » » (§ 16.7) */
    if (ok && strcmp(a.parent, n.parent) != 0) {
        *erreur = grym_formater("« %s » ne peut pas changer de classe parente : %s.", c->nom, deja);
        ok = 0;
    }
    /* un champ retiré, ou renommé : ses valeurs seraient perdues */
    for (size_t k = 0; ok && k < a.n; k++)
        if (chercher_champ(&n, a.noms[k]) < 0 && (a.uniques[k] & 8)) {
            /* champ multiple retiré : sa table de liaison part si elle est vide (§ 16.13) */
            long liaisons = compter_liaisons(b, c->nom, a.noms[k]);
            if (liaisons > 0) {
                *erreur = grym_formater("« %s » a disparu de « %s » : %ld liaison%s serai%s perdue%s. Remets ce champ.",
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
            *erreur = grym_formater("« %s » a disparu de « %s » : %ld valeur%s conservée%s serai%s perdue%s. Remets ce "
                                    "champ ; s'il a changé de nom, écris-le sur le nouveau : « , anciennement « %s » ».", a.noms[k], c->nom,
                                    lignes, lignes > 1 ? "s" : "", lignes > 1 ? "s" : "", lignes > 1 ? "ent" : "t",
                                    lignes > 1 ? "s" : "", a.noms[k]);
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
                if (preparer(b, t, &st) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
                    hors = (long)sqlite3_column_int64(st, 0);
                rendre(b, st);
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
        int lien = !(type_textuel(t) || strcmp(t, "nombre") == 0 || strcmp(t, "nombre entier") == 0
                     || strcmp(t, "année") == 0 || strcmp(t, "heure") == 0
                     || strcmp(t, "vrai ou faux") == 0 || strcmp(t, "date") == 0 || est_fichier(t));
        int facultatif = (n.uniques[k] & 2) != 0;
        if (lignes > 0 && !depart && !facultatif) {
            *erreur = lien || est_fichier(t)
                ? grym_formater("« %s » est nouveau, et %s : un %s n'a pas de valeur de départ, il ne s'ajoute qu'à une "
                                "entité sans objet conservé.", n.noms[k], deja, lien ? "lien" : "fichier")
                : grym_formater("« %s » est nouveau, et %s : donne-lui une valeur de départ, après son type : "
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
    if (ok && b->rapport) {   /* ce que la migration a fait, dit en clair (docs/atelier.md, A2-c) */
        for (size_t k = 0; k < n.n; k++) {
            int ia = chercher_champ(&a, n.noms[k]);
            if (ia >= 0) {
                if (strcmp(a.types[ia], n.types[k]) != 0) {
                    char *l = grym_formater("« %s » : le champ « %s » passe de « %s » à « %s ».\n", c->nom, n.noms[k],
                                            a.types[ia], n.types[k]);
                    chaine_ajouter(b->rapport, l);
                    free(l);
                }
                if ((n.uniques[k] & 1) && !(a.uniques[ia] & 1)) {
                    char *l = grym_formater("« %s » : le champ « %s » devient unique ; ses %ld valeurs sont déjà toutes "
                                            "différentes.\n", c->nom, n.noms[k], lignes);
                    chaine_ajouter(b->rapport, l);
                    free(l);
                }
                continue;
            }
            const char *depart = NULL;
            for (size_t q = 0; q < c->nb_champs; q++)
                if (strcmp(c->champs[q], n.noms[k]) == 0) depart = c->departs[q];
            char *l;
            if (lignes == 0) l = grym_formater("« %s » : champ « %s » ajouté.\n", c->nom, n.noms[k]);
            else if (n.uniques[k] & 8) l = grym_formater("« %s » : champ « %s » ajouté, vide pour les %ld %s déjà conservés.\n",
                                                           c->nom, n.noms[k], lignes, pl);
            else if (!depart) l = grym_formater("« %s » : champ « %s » ajouté ; %ld %s le %s absent.\n",
                                                c->nom, n.noms[k], lignes, lignes > 1 ? pl : c->nom,
                                                lignes > 1 ? "reçoivent" : "reçoit");
            else l = grym_formater("« %s » : champ « %s » ajouté ; %ld %s %s « %s ».\n", c->nom, n.noms[k], lignes,
                                   lignes > 1 ? pl : c->nom, lignes > 1 ? "reçoivent" : "reçoit", depart);
            chaine_ajouter(b->rapport, l);
            free(l);
        }
        for (size_t k = 0; k < a.n; k++)
            if (chercher_champ(&n, a.noms[k]) < 0) {
                char *l = grym_formater("« %s » : champ « %s » retiré ; aucune valeur n'est perdue.\n", c->nom, a.noms[k]);
                chaine_ajouter(b->rapport, l);
                free(l);
            }
    }
    if (ok) {
        sqlite3_stmt *st = NULL;
        preparer(b, "UPDATE grym_schema SET definition = ? WHERE entite = ?", &st);
        sqlite3_bind_text(st, 1, nouvelle, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, c->nom, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        rendre(b, st);
        if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    }
    free(pl);
    free(deja);
    liberer_definition(&a);
    liberer_definition(&n);
    return ok;
}

/* Un index par colonne de lien : « les lignes de l'écriture », une somme, une relation inverse ou une règle
 * (grammaire, § 16.10, § 16.14) y trouvent leurs objets sans parcourir la table. Sans lui, une règle sur une
 * base de 5'000 écritures coûtait 2,4 s par transaction (mesure du 28 septembre 2026). Aucune donnée ne change ;
 * une base plus ancienne reçoit ses index au premier lancement. */
static int indexer_liens(Base *b, const ClasseVM *c, char **erreur) {
    int ok = 1;
    for (size_t k = 0; ok && k < c->nb_champs; k++) {
        if (c->proprietaires[k] != c || multiple(c, k) || !c->types[k] || !est_lien(c->types[k])) continue;
        Chaine sql = {0};
        char *nom = grym_formater("%s.%s", c->nom, c->champs[k]);
        chaine_ajouter(&sql, "CREATE INDEX IF NOT EXISTS ");
        ajouter_nom(&sql, "l ", nom);
        chaine_ajouter(&sql, " ON ");
        ajouter_nom(&sql, "e ", c->nom);
        chaine_ajouter(&sql, " (");
        ajouter_nom(&sql, "c ", c->champs[k]);
        chaine_ajouter(&sql, ");");
        free(nom);
        char *texte = chaine_rendre(&sql);
        ok = executer(b, texte, erreur);
        free(texte);
    }
    return ok;
}

static int base_preparer_table(Base *b, const ClasseVM *c, char **erreur);

/* Valeurs permises (grammaire, § 16.15) : une liste qui perd une valeur encore portée par des objets conservés
 * (ou dans la corbeille) est refusée ; rien n'est perdu ni changé en silence. */
static int verifier_valeurs(Base *b, const ClasseVM *c, char **erreur) {
    for (size_t k = 0; k < c->nb_champs; k++) {
        if (!c->valeurs || !c->valeurs[k] || c->proprietaires[k] != c) continue;
        Chaine sql = {0};
        chaine_ajouter(&sql, "SELECT ");
        ajouter_nom(&sql, "c ", c->champs[k]);
        chaine_ajouter(&sql, ", count(*) FROM ");
        ajouter_nom(&sql, "e ", c->nom);
        chaine_ajouter(&sql, " WHERE ");
        ajouter_nom(&sql, "c ", c->champs[k]);
        chaine_ajouter(&sql, " IS NOT NULL GROUP BY 1 ORDER BY 1");
        char *t = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        int ok = preparer(b, t, &st) == SQLITE_OK;
        free(t);
        while (ok && sqlite3_step(st) == SQLITE_ROW) {
            const char *v = (const char *)sqlite3_column_text(st, 0);
            long n = (long)sqlite3_column_int64(st, 1);
            int permise = 0;
            size_t l = strlen(v ? v : "");
            for (const char *p = c->valeurs[k]; p && !permise; ) {
                const char *q = strchr(p, '\x1e');
                size_t m = q ? (size_t)(q - p) : strlen(p);
                permise = m == l && strncmp(p, v ? v : "", l) == 0;
                p = q ? q + 1 : NULL;
            }
            if (!permise) {
                char *pl = c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
                *erreur = grym_formater("« %s » ne permet plus « %s », que %ld %s conservé%s%s encore : remets « %s » "
                                        "dans la liste, ou change d'abord ces objets.", c->champs[k], v ? v : "", n,
                                        n > 1 ? pl : c->nom, c->feminin ? "e" : "", n > 1 ? "s portent" : " porte",
                                        v ? v : "");
                free(pl);
                ok = 0;
                rendre(b, st);
                return 0;
            }
        }
        rendre(b, st);
    }
    return 1;
}

int base_preparer(Base *b, const ClasseVM *c, char **erreur) {
    ecrit_tout(b);   /* une migration peut tout changer : les règles se revérifient toutes */
    if (!c->conserve) return 1;
    if (!base_preparer_table(b, c, erreur)) return 0;
    return verifier_valeurs(b, c, erreur);
}

static int base_preparer_table(Base *b, const ClasseVM *c, char **erreur) {
    char *def = definition(c);
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT definition FROM grym_schema WHERE entite = ?", &st);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        char *ancienne = grym_dupliquer((const char *)sqlite3_column_text(st, 0));
        rendre(b, st);
        int ok = strcmp(ancienne, def) == 0 || migrer(b, c, ancienne, def, erreur);
        free(ancienne);
        free(def);
        return ok && indexer_liens(b, c, erreur);
    }
    rendre(b, st);

    if (b->rapport) {
        char *l = grym_formater("« %s » : table créée.\n", c->nom);
        chaine_ajouter(b->rapport, l);
        free(l);
    }
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
    if (ok) ok = indexer_liens(b, c, erreur);
    if (ok) {
        preparer(b, "INSERT INTO grym_schema (entite, definition) VALUES (?, ?)", &st);
        sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, def, -1, SQLITE_TRANSIENT);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        rendre(b, st);
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
    preparer(b, "SELECT supprime IS NOT NULL FROM grym_objet WHERE id = ?", &st);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    int r = sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0);
    rendre(b, st);
    return r;
}

static int lier(Base *b, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v,
                const Objet *soi, long id_soi, char **erreur) {
    if (!v || v->type == V_ABSENT) {   /* champ facultatif sans valeur : NULL (§ 16.9) */
        sqlite3_bind_null(st, i);
        if (est_fichier(type)) { sqlite3_bind_null(st, i + 1); return i + 2; }
        return i + 1;
    }
    if (type_textuel(type)) {
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
    } else if (strcmp(type, "heure") == 0) {
        char t[32];
        snprintf(t, sizeof t, "%02ld:%02ld", v->jours / 60, v->jours % 60);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
    } else if (strcmp(type, "date") == 0) {
        char *t = date_iso(v->jours);
        sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
        free(t);
    } else if (est_fichier(type)) {   /* les octets dans grym_fichier, le numéro dans la colonne (format 2) */
        long cle = ranger_fichier(b, v->fichier, erreur);
        if (!cle) return 0;
        sqlite3_bind_int64(st, i, (sqlite3_int64)cle);
        sqlite3_bind_text(st, i + 1, v->fichier->nom, -1, SQLITE_TRANSIENT);
        return i + 2;
    } else if (v->objet == soi) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)id_soi);
    } else {
        if (!v->objet->id) {
            char *qui = un(v->objet->classe);
            *erreur = grym_formater("Le champ « %s » désigne %s qui n'est pas conservé%s : conserve-%s d'abord.", champ,
                                    qui, v->objet->classe->feminin ? "e" : "", v->objet->classe->feminin ? "la" : "le");
            free(qui);
            return 0;
        }
        if (base_est_supprime(b, v->objet->id)) {   /* règle 3 : pas de nouveau lien vers la corbeille (§ 16.12) */
            const ClasseVM *c = v->objet->classe;
            *erreur = grym_formater("Le champ « %s » désignerait %s %s supprimé%s : rétablis-%s d'abord.", champ,
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
                   : v->type == V_NOMBRE ? dec_afficher(&v->nombre)
                   : v->type == V_DATE ? date_suisse(v->jours) : grym_dupliquer("cette valeur");
        }
        /* règle 4 : la valeur est peut-être gardée par un objet de la corbeille (§ 16.12) */
        int dans_corbeille = 0;
        for (size_t k = 0; k < c->nb_champs; k++) {
            if (strcmp(c->champs[k], p + 3) != 0 || !o->definis[k] || est_fichier(c->types[k])) continue;
            Chaine sql = {0};
            chaine_ajouter(&sql, "SELECT count(*) FROM ");
            ajouter_nom(&sql, "e ", c->proprietaires[k]->nom);
            chaine_ajouter(&sql, " AS t JOIN grym_objet AS g ON g.id = t.id WHERE g.supprime IS NOT NULL AND t.");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&sql, " = ?");
            char *texte = chaine_rendre(&sql);
            sqlite3_stmt *st = NULL;
            char *ignore = NULL;
            if (preparer(b, texte, &st) == SQLITE_OK
                && lier(b, st, 1, c->types[k], c->champs[k], &o->champs[k], o, o->id, &ignore)
                && sqlite3_step(st) == SQLITE_ROW)
                dans_corbeille = sqlite3_column_int(st, 0) > 0;
            free(ignore);
            rendre(b, st);
            free(texte);
        }
        char *r = dans_corbeille
            ? grym_formater("« %s » est unique : %s appartient à %s %s supprimé%s. Rétablis-%s, ou supprime-%s "
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

static int fichier_unique(Base *b, const Objet *o, size_t k, long id, char **erreur);

int base_conserver(Base *b, const Objet *o, long *id, char **erreur) {
    ecrit(b, o->classe);
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
    preparer(b, "INSERT INTO grym_objet (classe) VALUES (?)", &st);
    sqlite3_bind_text(st, 1, c->nom, -1, SQLITE_TRANSIENT);
    int ok = sqlite3_step(st) == SQLITE_DONE;
    rendre(b, st);
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
        preparer(b, texte, &st);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)nouvel);
        int i = 2;
        for (size_t k = 0; k < c->nb_champs && i; k++)
            if (c->proprietaires[k] == l[e] && !multiple(c, k))
                i = lier(b, st, i, c->types[k], c->champs[k], o->definis[k] ? &o->champs[k] : NULL, o, nouvel, erreur);
        ok = i && sqlite3_step(st) == SQLITE_DONE;
        if (i && !ok) *erreur = message_contrainte(b, o);
        rendre(b, st);
        for (size_t k = 0; ok && k < c->nb_champs; k++)
            if (c->proprietaires[k] == l[e] && !multiple(c, k) && est_fichier(c->types[k]))
                ok = fichier_unique(b, o, k, nouvel, erreur);
        if (!ok) return 0;   /* la transaction de l'exécution annulera les lignes déjà écrites */
    }
    *id = nouvel;
    return 1;
}

/* Un fichier unique : sa colonne ne garde que des numéros, tous différents ; c'est le contenu qui doit l'être.
 * Après l'écriture de l'objet id, un autre objet de la même table au fichier identique est refusé. */
static int fichier_unique(Base *b, const Objet *o, size_t k, long id, char **erreur) {
    const ClasseVM *c = o->classe;
    if (!(c->uniques[k] & 1) || !o->definis[k] || o->champs[k].type != V_FICHIER) return 1;
    Chaine sql = {0};
    chaine_ajouter(&sql, "SELECT g.supprime IS NOT NULL FROM ");
    ajouter_nom(&sql, "e ", c->proprietaires[k]->nom);
    chaine_ajouter(&sql, " AS t JOIN grym_objet AS g ON g.id = t.id JOIN grym_fichier AS x ON x.id = t.");
    ajouter_nom(&sql, "c ", c->champs[k]);
    chaine_ajouter(&sql, " JOIN grym_fichier AS s ON s.id = (SELECT ");
    ajouter_nom(&sql, "c ", c->champs[k]);
    chaine_ajouter(&sql, " FROM ");
    ajouter_nom(&sql, "e ", c->proprietaires[k]->nom);
    chaine_ajouter(&sql, " WHERE id = ?1) WHERE t.id <> ?1 AND x.taille = s.taille AND x.empreinte = s.empreinte "
                         "AND x.octets = s.octets LIMIT 1");
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    int ok = preparer(b, texte, &st) == SQLITE_OK;
    free(texte);
    int pris = 0, corbeille = 0;
    if (ok) {
        sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
        int rc = sqlite3_step(st);
        pris = rc == SQLITE_ROW;
        if (pris) corbeille = sqlite3_column_int(st, 0);
        ok = rc == SQLITE_ROW || rc == SQLITE_DONE;
    }
    if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    rendre(b, st);
    if (!ok) return 0;
    if (!pris) return 1;
    *erreur = corbeille
        ? grym_formater("« %s » est unique : ce fichier appartient à %s %s supprimé%s. Rétablis-%s, ou supprime-%s "
                        "définitivement.", c->champs[k], c->feminin ? "une" : "un", c->nom, c->feminin ? "e" : "",
                        c->feminin ? "la" : "le", c->feminin ? "la" : "le")
        : grym_formater("« %s » est unique : %s %s conservé%s a déjà ce fichier.", c->champs[k],
                        c->feminin ? "une autre" : "un autre", c->nom, c->feminin ? "e" : "");
    return 0;
}

int base_ecrire_champ(Base *b, const Objet *o, size_t k, char **erreur) {
    ecrit(b, o->classe);
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
    /* un fichier remplacé : sa ligne de grym_fichier part avec lui (la machine a lu ses octets s'il sert encore) */
    sqlite3_int64 ancien = 0;
    if (est_fichier(c->types[k])) {
        Chaine q = {0};
        chaine_ajouter(&q, "SELECT ");
        ajouter_nom(&q, "c ", c->champs[k]);
        chaine_ajouter(&q, " FROM ");
        ajouter_nom(&q, "e ", table->nom);
        chaine_ajouter(&q, " WHERE id = ?");
        char *tq = chaine_rendre(&q);
        if (preparer(b, tq, &st) == SQLITE_OK) {
            sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
            if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_INTEGER)
                ancien = sqlite3_column_int64(st, 0);
        }
        rendre(b, st);
        free(tq);
        st = NULL;
    }
    preparer(b, texte, &st);
    free(texte);
    int i = lier(b, st, 1, c->types[k], c->champs[k], &o->champs[k], o, o->id, erreur);
    int ok = 0;
    if (i) {
        sqlite3_bind_int64(st, i, (sqlite3_int64)o->id);
        ok = sqlite3_step(st) == SQLITE_DONE;
        if (!ok) *erreur = message_contrainte(b, o);
    }
    rendre(b, st);
    if (ok && est_fichier(c->types[k])) ok = fichier_unique(b, o, k, o->id, erreur);
    if (ok && ancien && !effacer_fichier(b, ancien)) {
        *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        ok = 0;
    }
    return ok;
}

/* ---------------------------------------------------------------- */
/* Gagner, perdre (grammaire, § 16.13)                              */
/* ---------------------------------------------------------------- */

int base_gagner(Base *b, const Objet *o, size_t k, const Valeur *v, int perdre, char **erreur) {
    ecrit(b, o->classe);
    const ClasseVM *c = o->classe;
    const Objet *x = v->objet;
    if (!x->id) {
        if (perdre) return 1;   /* un objet jamais conservé n'est dans aucun ensemble */
        char *qui = un(x->classe);
        *erreur = grym_formater("Le champ « %s » gagnerait %s qui n'est pas conservé%s : conserve-%s d'abord.", c->champs[k],
                                qui, x->classe->feminin ? "e" : "", x->classe->feminin ? "la" : "le");
        free(qui);
        return 0;
    }
    if (!perdre && base_est_supprime(b, x->id)) {   /* pas de nouveau lien vers la corbeille (§ 16.12) */
        const ClasseVM *xc = x->classe;
        *erreur = grym_formater("Le champ « %s » gagnerait %s %s supprimé%s : rétablis-%s d'abord.", c->champs[k],
                                xc->feminin ? "une" : "un", xc->nom, xc->feminin ? "e" : "", xc->feminin ? "la" : "le");
        return 0;
    }
    Chaine sql = {0};
    chaine_ajouter(&sql, perdre ? "DELETE FROM " : "INSERT OR IGNORE INTO ");
    ajouter_liaison(&sql, c->proprietaires[k]->nom, c->champs[k]);
    chaine_ajouter(&sql, perdre ? " WHERE a = ?1 AND b = ?2" : " (a, b) VALUES (?1, ?2)");
    char *texte = chaine_rendre(&sql);
    sqlite3_stmt *st = NULL;
    int ok = preparer(b, texte, &st) == SQLITE_OK;
    free(texte);
    if (ok) {
        sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
        sqlite3_bind_int64(st, 2, (sqlite3_int64)x->id);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    if (!ok) *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
    rendre(b, st);
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
    preparer(b, texte, &st);
    free(texte);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    while (sqlite3_step(st) == SQLITE_ROW)
        ens_ajouter(sortie, (long)sqlite3_column_int64(st, 0), (const char *)sqlite3_column_text(st, 1));
    rendre(b, st);
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
    ecrit_tout(b);   /* ce qui disparaît avec l'objet peut être de toute entité */
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
        preparer(b, "UPDATE grym_objet SET supprime = ?, supprime_avec = ? WHERE id = ?", &st);
        for (size_t k = 0; k < e.n; k++) {
            sqlite3_bind_text(st, 1, date, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(st, 2, (sqlite3_int64)o->id);
            sqlite3_bind_int64(st, 3, (sqlite3_int64)e.ids[k]);
            sqlite3_step(st);
            sqlite3_reset(st);
        }
        rendre(b, st);
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
                    ? (dehors > 1 ? (x->feminin ? " supprimées : supprime-les définitivement d'abord"
                                                : " supprimés : supprime-les définitivement d'abord")
                                  : (x->feminin ? " supprimée : supprime-la définitivement d'abord"
                                                : " supprimé : supprime-le définitivement d'abord"))
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
    /* leurs fichiers : les lignes de grym_fichier partent avec eux */
    size_t nf = 0, capf = 8;
    sqlite3_int64 *fichiers = grym_allouer(capf * sizeof *fichiers);
    for (size_t q = 0; q < e.n; q++) {
        const ClasseVM *c = classe_nommee(classes, nb_classes, e.classes[q]);
        for (size_t k = 0; c && k < c->nb_champs; k++) {
            if (multiple(c, k) || !c->types[k] || !est_fichier(c->types[k])) continue;
            Chaine sql = {0};
            chaine_ajouter(&sql, "SELECT ");
            ajouter_nom(&sql, "c ", c->champs[k]);
            chaine_ajouter(&sql, " FROM ");
            ajouter_nom(&sql, "e ", c->proprietaires[k]->nom);
            chaine_ajouter(&sql, " WHERE id = ?");
            char *t = chaine_rendre(&sql);
            sqlite3_stmt *sf = NULL;
            if (preparer(b, t, &sf) == SQLITE_OK) {
                sqlite3_bind_int64(sf, 1, (sqlite3_int64)e.ids[q]);
                if (sqlite3_step(sf) == SQLITE_ROW && sqlite3_column_type(sf, 0) == SQLITE_INTEGER) {
                    if (nf == capf) {
                        capf *= 2;
                        sqlite3_int64 *f2 = grym_allouer(capf * sizeof *f2);
                        memcpy(f2, fichiers, nf * sizeof *fichiers);
                        free(fichiers);
                        fichiers = f2;
                    }
                    fichiers[nf++] = sqlite3_column_int64(sf, 0);
                }
            }
            rendre(b, sf);
            free(t);
        }
    }
    /* les liens internes à l'ensemble se vérifient à la validation : l'ordre des effacements n'importe plus */
    int ok = executer(b, "PRAGMA defer_foreign_keys = ON;", erreur);
    sqlite3_stmt *st = NULL;
    preparer(b, "DELETE FROM grym_objet WHERE id = ?", &st);
    for (size_t k = e.n; ok && k > 0; k--) {
        machine_objet_efface(m, e.ids[k - 1]);   /* avant l'effacement : ses valeurs sont lues s'il le faut */
        sqlite3_bind_int64(st, 1, (sqlite3_int64)e.ids[k - 1]);
        if (sqlite3_step(st) != SQLITE_DONE) {
            *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
            ok = 0;
        }
        sqlite3_reset(st);
    }
    rendre(b, st);
    for (size_t q = 0; ok && q < nf; q++)
        if (!effacer_fichier(b, fichiers[q])) {
            *erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
            ok = 0;
        }
    free(fichiers);
    ens_liberer(&e);
    return ok;
}

int base_retablir(Base *b, const Objet *o, ClasseVM *const *classes, size_t nb_classes, char **erreur) {
    ecrit_tout(b);   /* ce qui revient avec l'objet peut être de toute entité */
    const ClasseVM *oc = o->classe;
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT supprime IS NOT NULL, supprime_avec FROM grym_objet WHERE id = ?", &st);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
    int supprime = 0;
    long avec = 0;
    if (sqlite3_step(st) == SQLITE_ROW) {
        supprime = sqlite3_column_int(st, 0);
        avec = (long)sqlite3_column_int64(st, 1);
    }
    rendre(b, st);
    if (!supprime) {
        *erreur = grym_formater("%s %s n'est pas supprimé%s.", ce(oc), oc->nom, oc->feminin ? "e" : "");
        return 0;
    }
    if (avec != o->id) {
        *erreur = grym_formater("%s %s a disparu avec un autre objet : rétablis celui-là, et %s reviendra avec lui.",
                                ce(oc), oc->nom, oc->feminin ? "elle" : "il");
        return 0;
    }
    /* il ne revient pas s'il « disparaît avec » un objet qui, lui, reste supprimé */
    for (size_t k = 0; k < oc->nb_champs; k++) {
        if (!(oc->uniques[k] & 4) || !o->definis[k] || o->champs[k].type != V_OBJET) continue;
        const Objet *cible = o->champs[k].objet;
        if (cible->id && base_est_supprime(b, cible->id)) {
            const ClasseVM *c = cible->classe;
            *erreur = grym_formater("%s %s disparaît avec %s %s qui est supprimé%s : rétablis-%s d'abord.", ce(oc),
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
    rendre(b, st);
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

/* grym_contient(champ, partie, sens) : « dont le nom contient « mül » », « commence par », « finit par »
 * (grammaire, § 4.5) ; NULL si le champ est absent, que la condition écarte. */
static void fonction_contient(sqlite3_context *ctx, int n, sqlite3_value **v) {
    (void)n;
    if (sqlite3_value_type(v[0]) == SQLITE_NULL || sqlite3_value_type(v[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }
    const char *t = (const char *)sqlite3_value_text(v[0]), *p = (const char *)sqlite3_value_text(v[1]);
    const char *sens = (const char *)sqlite3_value_text(v[2]);
    sqlite3_result_int(ctx, texte_contient(t ? t : "", p ? p : "", sens ? sens[0] : 'c'));
}

int base_collations(sqlite3 *db) {
    return sqlite3_create_collation(db, "GRYM_NOMBRE", SQLITE_UTF8, NULL, collation_nombre) == SQLITE_OK
        && sqlite3_create_collation(db, "GRYM_TEXTE", SQLITE_UTF8, NULL, collation_texte) == SQLITE_OK
        && sqlite3_create_function(db, "grym_contient", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL,
                                   fonction_contient, NULL, NULL) == SQLITE_OK;
}

/* ---------------------------------------------------------------- */
/* Charger un objet retrouvé                                        */
/* ---------------------------------------------------------------- */

static char *classe_en_base(Base *b, long id) {
    sqlite3_stmt *st = NULL;
    preparer(b, "SELECT classe FROM grym_objet WHERE id = ?", &st);
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    char *r = sqlite3_step(st) == SQLITE_ROW ? grym_dupliquer((const char *)sqlite3_column_text(st, 0)) : NULL;
    rendre(b, st);
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
            if (est_fichier(c->types[k])) {   /* le nom, puis taille et premiers octets : jamais le contenu */
                chaine_ajouter(&sql, ", ");
                ajouter_nom(&sql, "n ", c->champs[k]);
                chaine_ajouter(&sql, ", (SELECT taille FROM grym_fichier WHERE id = ");
                ajouter_nom(&sql, "c ", c->champs[k]);
                chaine_ajouter(&sql, "), (SELECT debut FROM grym_fichier WHERE id = ");
                ajouter_nom(&sql, "c ", c->champs[k]);
                chaine_ajouter(&sql, ")");
            }
            colonnes++;
        }
        chaine_ajouter(&sql, " FROM ");
        ajouter_nom(&sql, "e ", l[e]->nom);
        chaine_ajouter(&sql, " WHERE id = ?");
        char *texte = chaine_rendre(&sql);
        sqlite3_stmt *st = NULL;
        preparer(b, texte, &st);
        free(texte);
        sqlite3_bind_int64(st, 1, (sqlite3_int64)o->id);
        if (sqlite3_step(st) != SQLITE_ROW) {
            rendre(b, st);
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
                if (est_fichier(t)) col += 3;
            } else if (type_textuel(t)) v = vi_texte((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre") == 0) v = vi_nombre_canonique((const char *)sqlite3_column_text(st, col));
            else if (strcmp(t, "nombre entier") == 0) {
                char tampon[32];
                snprintf(tampon, sizeof tampon, "%lld", (long long)sqlite3_column_int64(st, col));
                v = vi_nombre_canonique(tampon);
            } else if (strcmp(t, "vrai ou faux") == 0) v = vi_booleen(sqlite3_column_int(st, col));
            else if (strcmp(t, "année") == 0) v = vi_annee((long)sqlite3_column_int64(st, col));
            else if (strcmp(t, "heure") == 0) {   /* « 14:30 » : texte, triable (§ 14.7) */
                long mi = 0;
                heure_lire((const char *)sqlite3_column_text(st, col), &mi);
                v = vi_heure(mi);
            } else if (strcmp(t, "date") == 0) {
                long jours = 0;
                date_lire_iso((const char *)sqlite3_column_text(st, col), &jours);
                v = vi_date(jours);
            } else if (est_fichier(t)) {
                if (sqlite3_column_type(st, col) == SQLITE_INTEGER)   /* format 2 : les octets attendent */
                    v = vi_fichier_en_base(b, (long)sqlite3_column_int64(st, col),
                                           (size_t)sqlite3_column_int64(st, col + 2),
                                           sqlite3_column_blob(st, col + 3), (size_t)sqlite3_column_bytes(st, col + 3),
                                           (const char *)sqlite3_column_text(st, col + 1));
                else   /* base pas encore convertie */
                    v = vi_fichier(sqlite3_column_blob(st, col), (size_t)sqlite3_column_bytes(st, col),
                                   (const char *)sqlite3_column_text(st, col + 1));
                col += 3;
            } else {
                long id = (long)sqlite3_column_int64(st, col);
                char *classe = classe_en_base(b, id);
                Objet *lie = classe ? machine_objet_en_base(m, id, classe, erreur) : NULL;
                if (!classe && !*erreur) *erreur = grym_formater("Base « %s » : lien vers un objet absent.", b->chemin);
                free(classe);
                if (!lie) { rendre(b, st); return 0; }
                v = vi_objet(lie);
            }
            o->champs[k] = v;
            o->definis[k] = 1;
            col++;
        }
        (void)colonnes;
        rendre(b, st);
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

/* Le champ d'une condition, en SQL : sa colonne ; ou, à travers des liens (« écriture␜comptabilisation »,
 * grammaire, § 16.4), une sous-requête par lien, qui suit l'identifiant de proche en proche. Rend l'expression
 * (à libérer), le type et le nom du champ au bout, et *k son rang dans l'entité examinée (−1 pour un chemin) ;
 * NULL si le chemin ne mène pas à un champ. */
static char *expression_champ(Recherche *r, const char *chemin, const char **type, const char **nom, long *k) {
    Chaine c = { 0 };
    *k = -1;
    if (!strchr(chemin, '\x1c')) {
        for (size_t q = 0; q < r->e->nb_champs && *k < 0; q++) if (strcmp(r->e->champs[q], chemin) == 0) *k = (long)q;
        if (*k < 0 || !r->e->types[*k]) return NULL;
        char t[16];
        snprintf(t, sizeof t, "t%d.", alias_du_champ(r, (size_t)*k));
        chaine_ajouter(&c, t);
        ajouter_nom(&c, "c ", r->e->champs[*k]);
        *type = r->e->types[*k];
        *nom = r->e->champs[*k];
        return chaine_rendre(&c);
    }
    char *copie = grym_dupliquer(chemin);
    const ClasseVM *cl = r->e;
    char *expr = NULL;
    int premier = 1;
    for (char *s1 = copie, *s2; s1; s1 = s2, premier = 0) {
        s2 = strchr(s1, '\x1c');
        if (s2) *s2++ = '\0';
        long q = -1;
        for (size_t i = 0; cl && i < cl->nb_champs && q < 0; i++) if (strcmp(cl->champs[i], s1) == 0) q = (long)i;
        if (q < 0 || !cl->types[q] || multiple(cl, (size_t)q)) { free(expr); free(copie); return NULL; }
        Chaine e = { 0 };
        if (premier) {
            char t[16];
            snprintf(t, sizeof t, "t%d.", alias_du_champ(r, (size_t)q));
            chaine_ajouter(&e, t);
            ajouter_nom(&e, "c ", cl->champs[q]);
        } else {   /* le champ de l'objet que désigne l'étape précédente : sa table, sa ligne */
            chaine_ajouter(&e, "(SELECT ");
            ajouter_nom(&e, "c ", cl->champs[q]);
            chaine_ajouter(&e, " FROM ");
            ajouter_nom(&e, "e ", cl->proprietaires[q]->nom);
            chaine_ajouter(&e, " WHERE id = ");
            chaine_ajouter(&e, expr);
            chaine_ajouter(&e, ")");
        }
        free(expr);
        expr = chaine_rendre(&e);
        if (s2) {   /* une étape intermédiaire : un lien vers une entité */
            cl = machine_classe(r->m, cl->types[q]);
            if (!cl || !cl->conserve) { free(expr); free(copie); return NULL; }
        } else {
            *type = cl->types[q];
            *nom = cl->champs[q];
        }
    }
    free(copie);
    return expr;
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
            r->erreur = v->texte ? grym_formater("Le champ « %s » est absent : vérifie-le d'abord avec « est présent ».", v->texte)
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
                r->erreur = grym_formater("Plusieurs champs %s %s peuvent désigner %s : %s. Précise avec "
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
                r->erreur = grym_formater("Plusieurs champs relient %s à %s : %s. Précise avec %s.", e1, qui, n, pr);
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
        long k;
        const char *t = NULL, *nom_champ = NULL;
        char *expr = expression_champ(r, champ, &t, &nom_champ, &k);
        free(champ);
        if (!expr) { r->erreur = grym_dupliquer("champ inconnu"); return 0; }
        if (op == 'p' || (k >= 0 && multiple(r->e, (size_t)k))) {
            free(expr);
            if (k < 0) { r->erreur = grym_dupliquer("« parmi » ne s'emploie pas à travers un lien."); return 0; }
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
        if (op == 'c' || op == 'd' || op == 'f') {   /* contient, commence par, finit par (§ 4.5) */
            if (*r->p != '?') { free(expr); r->erreur = grym_dupliquer("paramètre attendu"); return 0; }
            size_t i = (size_t)strtoul(r->p + 1, (char **)&fin, 10);
            r->p = fin;
            if (i < 1 || i > r->nb_params || r->nb_liens == 64) { free(expr); r->erreur = grym_dupliquer("paramètre invalide"); return 0; }
            char tampon[48];
            chaine_ajouter(&r->sql, "grym_contient(");
            chaine_ajouter(&r->sql, expr);
            snprintf(tampon, sizeof tampon, ", ?%lu, '%c') = 1", (unsigned long)(r->nb_liens + 1), op);
            chaine_ajouter(&r->sql, tampon);
            r->liens[r->nb_liens] = i - 1;
            r->types[r->nb_liens] = t;
            r->champs[r->nb_liens] = nom_champ;
            r->nb_liens++;
        } else if (op == 'P' || op == 'N' || op == '0') {
            const char *o2 = op == 'P' ? " > " : op == 'N' ? " < " : " = ";
            if (entier) { chaine_ajouter(&r->sql, expr); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "0"); }
            else { chaine_ajouter(&r->sql, expr); chaine_ajouter(&r->sql, o2); chaine_ajouter(&r->sql, "'0' COLLATE GRYM_NOMBRE"); }
        } else if (op == 'A' || op == 'R') {
            chaine_ajouter(&r->sql, expr);
            chaine_ajouter(&r->sql, op == 'A' ? " IS NULL" : " IS NOT NULL");
        } else if (op == 'V' || op == 'F') {
            chaine_ajouter(&r->sql, expr);
            chaine_ajouter(&r->sql, op == 'V' ? " = 1" : " = 0");
        } else {
            if (*r->p != '?' || !sqlop) { free(expr); r->erreur = grym_dupliquer("paramètre attendu"); return 0; }
            size_t i = (size_t)strtoul(r->p + 1, (char **)&fin, 10);
            r->p = fin;
            if (i < 1 || i > r->nb_params || r->nb_liens == 64) { free(expr); r->erreur = grym_dupliquer("paramètre invalide"); return 0; }
            if (entier) { chaine_ajouter(&r->sql, "CAST("); chaine_ajouter(&r->sql, expr); chaine_ajouter(&r->sql, " AS TEXT)"); }
            else chaine_ajouter(&r->sql, expr);
            chaine_ajouter(&r->sql, sqlop);
            char tampon[24];
            snprintf(tampon, sizeof tampon, "?%lu", (unsigned long)(r->nb_liens + 1));
            chaine_ajouter(&r->sql, tampon);
            if (nombre || entier) chaine_ajouter(&r->sql, " COLLATE GRYM_NOMBRE");
            else if (type_textuel(t) && op != '=' && op != '!') chaine_ajouter(&r->sql, " COLLATE GRYM_TEXTE");
            r->liens[r->nb_liens] = i - 1;
            r->types[r->nb_liens] = t;
            r->champs[r->nb_liens] = nom_champ;
            r->nb_liens++;
        }
        chaine_ajouter(&r->sql, ")");
        free(expr);
    }
    if (*r->p != ')') { r->erreur = grym_dupliquer("parenthèse attendue"); return 0; }
    r->p++;
    return 1;
}

/* Lie la valeur d'un paramètre, comparée au champ d'un type donné. */
static int lier_parametre(Recherche *r, sqlite3_stmt *st, int i, const char *type, const char *champ, const Valeur *v) {
    const char *attendu = NULL;
    if (type_textuel(type)) {
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
    } else if (strcmp(type, "heure") == 0) {
        if (v->type == V_HEURE) {
            char t[32];
            snprintf(t, sizeof t, "%02ld:%02ld", v->jours / 60, v->jours % 60);
            sqlite3_bind_text(st, i, t, -1, SQLITE_TRANSIENT);
            return 1;
        }
        attendu = "une heure";
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
    /* modes 0 à 9, puis a à f (compilateur.c, chercher) ; e, f : l'objet au plus grand champ, une liste triée réduite à
     * son premier élément, parmi les objets où ce champ est présent */
    char cm = s1[1];
    if (cm == 'e' || cm == 'f') {
        const char *s2e = strchr(s1 + 1, '\x1f');
        const char *s3e = s2e ? strchr(s2e + 1, '\x1f') : NULL;
        const char *s4e = s3e ? strchr(s3e + 1, '\x1f') : NULL;
        if (!s4e) { *erreur = grym_dupliquer("Recherche mal décrite."); return 0; }
        char *champ = grym_formater("%.*s", (int)(s3e - s2e - 1), s2e + 1);
        char *d0 = s4e[1] ? grym_formater("%.*s%c%.*s(e(R[%s])%s)", (int)(s1 - d + 1), d, cm == 'f' ? '3' : '0',
                                           (int)(s4e - s1 - 1), s1 + 2, champ, s4e + 1)
                          : grym_formater("%.*s%c%.*s(R[%s])", (int)(s1 - d + 1), d, cm == 'f' ? '3' : '0',
                                           (int)(s4e - s1 - 1), s1 + 2, champ);
        Valeur liste;
        int bon = base_chercher(b, m, d0, params, nb_params, &liste, erreur);
        free(d0);
        if (!bon) { free(champ); return 0; }
        if (!liste.liste->n) {
            char *ent = grym_formater("%.*s", (int)(s1 - d), d);
            const ClasseVM *ce = machine_classe(m, ent);
            int fem = ce && ce->feminin;
            *erreur = grym_formater("Aucun%s %s %s%s%s n'a de « %s ».", fem ? "e" : "", ent, cm == 'f' ? "supprimé" : "conservé",
                                    fem ? "e" : "", s4e[1] ? " répondant à la condition" : "", champ);
            free(ent);
            free(champ);
            vi_liberer(&liste);
            return 0;
        }
        free(champ);
        Objet *o = machine_objet_en_base(m, liste.liste->ids[0], liste.liste->classes[0], erreur);
        vi_liberer(&liste);
        if (!o) return 0;
        *resultat = vi_objet(o);
        return 1;
    }
    char *entite = grym_formater("%.*s", (int)(s1 - d), d);
    int mode = cm >= 'a' ? 10 + cm - 'a' : cm - '0';
    int corbeille = (mode >= 3 && mode <= 5) || (mode >= 7 && mode % 2 == 1);   /* « supprimés » (§ 16.12) */
    if (mode >= 3 && mode <= 5) mode -= 3;
    /* 6 : un agrégat du champ écrit à la place du tri (§ 16.4) : la somme, la moyenne (8), le plus grand (10),
     * le plus petit (12) */
    char agregat = mode >= 6 ? "ssmmggpp"[mode - 6] : 0;
    if (agregat) mode = 6;
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
    long k_somme = -1;   /* mode 6 : le champ additionné */
    if (mode == 6) {
        for (size_t q = 0; q < r.e->nb_champs && k_somme < 0; q++) if (strcmp(r.e->champs[q], tri) == 0) k_somme = (long)q;
        if (k_somme < 0 || !r.e->types[k_somme]
            || (strcmp(r.e->types[k_somme], "nombre") != 0 && strcmp(r.e->types[k_somme], "nombre entier") != 0)) {
            *erreur = grym_formater("« %s » n'est pas un champ nombre de « %s ».", tri, r.e->nom);
            free(tri);
            return 0;
        }
    }
    if (mode == 6) {
        chaine_ajouter(&r.sql, "SELECT ");
        colonne(&r, (size_t)k_somme);
    } else {
        chaine_ajouter(&r.sql, mode == 0 ? "SELECT g.id, g.classe" : "SELECT count(*)");
    }
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
                else if (type_textuel(e->types[k])) chaine_ajouter(&r.sql, " COLLATE GRYM_TEXTE");
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
    if (ok && preparer(b, texte, &st) != SQLITE_OK) {
        r.erreur = grym_formater("Base « %s » : %s.", b->chemin, sqlite3_errmsg(b->db));
        ok = 0;
    }
    free(texte);
    for (size_t i = 0; ok && i < r.nb_liens; i++)
        ok = lier_parametre(&r, st, (int)i + 1, r.types[i], r.champs[i], &params[r.liens[i]]);
    if (!ok) {
        rendre(b, st);
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
        rendre(b, st);
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
    if (mode == 6) {   /* l'agrégat, en décimal exact : jamais l'arithmétique flottante de SQLite ; les absents ne comptent pas */
        Decimal total = dec_zero(), extreme = dec_zero();
        long nb = 0;
        int rc;
        StatutDecimal statut = DEC_OK;
        while (statut == DEC_OK && (rc = sqlite3_step(st)) == SQLITE_ROW) {
            if (sqlite3_column_type(st, 0) == SQLITE_NULL) continue;
            Decimal x = dec_depuis_canonique((const char *)sqlite3_column_text(st, 0));
            if (agregat == 'g' || agregat == 'p') {
                int c = nb ? dec_comparer(&x, &extreme) : 0;
                if (!nb || (agregat == 'g' ? c > 0 : c < 0)) { dec_liberer(&extreme); extreme = x; }
                else dec_liberer(&x);
                nb++;
                continue;
            }
            Decimal s;   /* dec_addition l'écrit entièrement : rien à préparer, rien à perdre */
            statut = dec_addition(&total, &x, &s);
            dec_liberer(&x);
            dec_liberer(&total);
            total = s;
            nb++;
        }
        rendre(b, st);
        if (statut != DEC_OK) {
            dec_liberer(&total);
            dec_liberer(&extreme);
            *erreur = grym_dupliquer("Nombre trop grand : un résultat est limité à 1000 chiffres.");
            return 0;
        }
        if (agregat != 's' && !nb) {   /* une moyenne, un plus grand sur rien : pas de valeur, jamais 0 en silence */
            dec_liberer(&total);
            dec_liberer(&extreme);
            *erreur = grym_formater("Aucun%s %s %s%s n'a de « %s » : pas de %s.", e->feminin ? "e" : "", e->nom,
                                    corbeille ? "supprimé" : "conservé", e->feminin ? "e" : "", e->champs[k_somme],
                                    agregat == 'm' ? "moyenne" : agregat == 'g' ? "plus grand" : "plus petit");
            return 0;
        }
        if (agregat == 'g' || agregat == 'p') { dec_liberer(&total); total = extreme; }
        else {
            dec_liberer(&extreme);
            if (agregat == 'm') {
                Decimal n = dec_zero(), q;
                char tn[32];
                snprintf(tn, sizeof tn, "%ld", nb);
                dec_liberer(&n);
                n = dec_depuis_canonique(tn);
                dec_division(&total, &n, &q);
                dec_liberer(&n);
                dec_liberer(&total);
                total = q;
            }
        }
        char *c = dec_canonique(&total);
        dec_liberer(&total);
        *resultat = vi_nombre_canonique(c);
        free(c);
        return 1;
    }
    long compte = sqlite3_step(st) == SQLITE_ROW ? (long)sqlite3_column_int64(st, 0) : 0;
    rendre(b, st);
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

unsigned char *base_copie(Base *b, size_t *taille, char **erreur) {
    sqlite3_int64 n = 0;
    unsigned char *o = sqlite3_serialize(b->db, "main", &n, 0);
    if (!o || n <= 0) {
        *erreur = grym_formater("Sauvegarde de la base impossible : %s.", sqlite3_errmsg(b->db));
        sqlite3_free(o);
        return NULL;
    }
    unsigned char *r = grym_allouer((size_t)n);
    memcpy(r, o, (size_t)n);
    sqlite3_free(o);
    *taille = (size_t)n;
    return r;
}
