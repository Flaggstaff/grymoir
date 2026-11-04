/* GrymoiR : lecture et écriture de JSON (RFC 8259), sans dépendance.
 * Spécification : docs/lsp.md.
 */
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFONDEUR_MAX 256

typedef struct {
    const char *p, *fin;
    int profondeur;
    char *erreur;
} Lecture;

static Json *nouveau(TypeJson t) {
    Json *j = grym_allouer(sizeof *j);
    memset(j, 0, sizeof *j);
    j->type = t;
    return j;
}

static void *echec(Lecture *l, const char *m) {
    if (!l->erreur) l->erreur = grym_formater("JSON mal formé : %s.", m);
    return NULL;
}

static void blancs(Lecture *l) {
    while (l->p < l->fin && (*l->p == ' ' || *l->p == '\t' || *l->p == '\n' || *l->p == '\r')) l->p++;
}

static int hex4(Lecture *l, unsigned *v) {
    if (l->fin - l->p < 4) return 0;
    *v = 0;
    for (int k = 0; k < 4; k++) {
        char c = l->p[k];
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    l->p += 4;
    return 1;
}

static void utf8(Chaine *c, unsigned cp) {
    char t[5] = { 0 };
    if (cp < 0x80) t[0] = (char)cp;
    else if (cp < 0x800) { t[0] = (char)(0xC0 | (cp >> 6)); t[1] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        t[0] = (char)(0xE0 | (cp >> 12)); t[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[2] = (char)(0x80 | (cp & 0x3F));
    } else {
        t[0] = (char)(0xF0 | (cp >> 18)); t[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        t[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); t[3] = (char)(0x80 | (cp & 0x3F));
    }
    chaine_ajouter(c, t);
}

static char *lire_texte(Lecture *l) {
    l->p++;   /* « " » */
    Chaine c = {0};
    chaine_ajouter(&c, "");
    while (l->p < l->fin && *l->p != '"') {
        unsigned char x = (unsigned char)*l->p;
        if (x < 0x20) { free(chaine_rendre(&c)); return echec(l, "caractère de contrôle dans un texte"); }
        if (x != '\\') {
            const char *d = l->p;
            while (l->p < l->fin && *l->p != '"' && *l->p != '\\' && (unsigned char)*l->p >= 0x20) l->p++;
            char *morceau = grym_formater("%.*s", (int)(l->p - d), d);
            chaine_ajouter(&c, morceau);
            free(morceau);
            continue;
        }
        if (++l->p >= l->fin) break;
        char e = *l->p++;
        switch (e) {
        case '"': chaine_ajouter(&c, "\""); break;
        case '\\': chaine_ajouter(&c, "\\"); break;
        case '/': chaine_ajouter(&c, "/"); break;
        case 'b': chaine_ajouter(&c, "\b"); break;
        case 'f': chaine_ajouter(&c, "\f"); break;
        case 'n': chaine_ajouter(&c, "\n"); break;
        case 'r': chaine_ajouter(&c, "\r"); break;
        case 't': chaine_ajouter(&c, "\t"); break;
        case 'u': {
            unsigned cp;
            if (!hex4(l, &cp)) { free(chaine_rendre(&c)); return echec(l, "échappement \\u invalide"); }
            if (cp >= 0xD800 && cp <= 0xDBFF) {   /* paire de substitution UTF-16 */
                unsigned bas;
                if (l->fin - l->p >= 6 && l->p[0] == '\\' && l->p[1] == 'u' && (l->p += 2, hex4(l, &bas))
                    && bas >= 0xDC00 && bas <= 0xDFFF)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (bas - 0xDC00);
                else cp = 0xFFFD;
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
            if (cp == 0) cp = 0xFFFD;   /* pas d'octet nul dans nos textes C */
            utf8(&c, cp);
            break;
        }
        default: free(chaine_rendre(&c)); return echec(l, "échappement inconnu");
        }
    }
    if (l->p >= l->fin) { free(chaine_rendre(&c)); return echec(l, "texte non fermé"); }
    l->p++;
    return chaine_rendre(&c);
}

static Json *valeur(Lecture *l);

static Json *conteneur(Lecture *l, int objet) {
    if (++l->profondeur > PROFONDEUR_MAX) return echec(l, "imbrication trop profonde");
    Json *j = nouveau(objet ? JSON_OBJET : JSON_TABLEAU);
    size_t cap = 0;
    l->p++;
    blancs(l);
    if (l->p < l->fin && *l->p == (objet ? '}' : ']')) { l->p++; l->profondeur--; return j; }
    for (;;) {
        char *cle = NULL;
        if (objet) {
            blancs(l);
            if (l->p >= l->fin || *l->p != '"') { json_liberer(j); return echec(l, "clé attendue"); }
            cle = lire_texte(l);
            if (!cle) { json_liberer(j); return NULL; }
            blancs(l);
            if (l->p >= l->fin || *l->p != ':') { free(cle); json_liberer(j); return echec(l, "« : » attendu"); }
            l->p++;
        }
        Json *v = valeur(l);
        if (!v) { free(cle); json_liberer(j); return NULL; }
        if (j->nb == cap) {
            cap = cap ? cap * 2 : 8;
            Json **e = grym_allouer(cap * sizeof *e);
            char **k = grym_allouer(cap * sizeof *k);
            if (j->nb) { memcpy(e, j->elements, j->nb * sizeof *e); if (j->cles) memcpy(k, j->cles, j->nb * sizeof *k); }
            free(j->elements);
            free(j->cles);
            j->elements = e;
            j->cles = k;
        }
        j->cles[j->nb] = cle;
        j->elements[j->nb++] = v;
        blancs(l);
        if (l->p < l->fin && *l->p == ',') { l->p++; continue; }
        if (l->p < l->fin && *l->p == (objet ? '}' : ']')) { l->p++; break; }
        json_liberer(j);
        return echec(l, objet ? "« , » ou « } » attendu" : "« , » ou « ] » attendu");
    }
    l->profondeur--;
    return j;
}

static Json *valeur(Lecture *l) {
    blancs(l);
    if (l->p >= l->fin) return echec(l, "valeur attendue");
    char c = *l->p;
    if (c == '{') return conteneur(l, 1);
    if (c == '[') return conteneur(l, 0);
    if (c == '"') {
        char *t = lire_texte(l);
        if (!t) return NULL;
        Json *j = nouveau(JSON_TEXTE);
        j->texte = t;
        return j;
    }
    static const struct { const char *mot; TypeJson type; int vrai; } MOTS[] = {
        { "true", JSON_BOOLEEN, 1 }, { "false", JSON_BOOLEEN, 0 }, { "null", JSON_NUL, 0 }
    };
    for (size_t k = 0; k < 3; k++) {
        size_t n = strlen(MOTS[k].mot);
        if ((size_t)(l->fin - l->p) >= n && memcmp(l->p, MOTS[k].mot, n) == 0) {
            l->p += n;
            Json *j = nouveau(MOTS[k].type);
            j->vrai = MOTS[k].vrai;
            return j;
        }
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *d = l->p;
        if (*l->p == '-') l->p++;
        int chiffres = 0;
        while (l->p < l->fin && ((*l->p >= '0' && *l->p <= '9') || *l->p == '.' || *l->p == 'e' || *l->p == 'E'
                                 || ((*l->p == '+' || *l->p == '-') && (l->p[-1] == 'e' || l->p[-1] == 'E')))) {
            chiffres += *l->p >= '0' && *l->p <= '9';
            l->p++;
        }
        if (!chiffres) return echec(l, "nombre invalide");
        Json *j = nouveau(JSON_NOMBRE);
        j->texte = grym_formater("%.*s", (int)(l->p - d), d);
        j->nombre = strtod(j->texte, NULL);
        return j;
    }
    return echec(l, "valeur inattendue");
}

Json *json_lire(const char *texte, size_t taille, char **erreur) {
    Lecture l = { texte, texte + taille, 0, NULL };
    Json *j = valeur(&l);
    if (j) {
        blancs(&l);
        if (l.p != l.fin) { json_liberer(j); j = echec(&l, "contenu après la valeur"); }
    }
    if (!j) *erreur = l.erreur ? l.erreur : grym_dupliquer("JSON mal formé.");
    return j;
}

void json_liberer(Json *j) {
    if (!j) return;
    for (size_t k = 0; k < j->nb; k++) {
        json_liberer(j->elements[k]);
        if (j->cles) free(j->cles[k]);
    }
    free(j->elements);
    free(j->cles);
    free(j->texte);
    free(j);
}

const Json *json_champ(const Json *o, const char *cle) {
    if (!o || o->type != JSON_OBJET) return NULL;
    for (size_t k = 0; k < o->nb; k++) if (strcmp(o->cles[k], cle) == 0) return o->elements[k];
    return NULL;
}

const Json *json_chemin(const Json *j, const char *chemin) {
    char *c = grym_dupliquer(chemin);
    const Json *r = j;
    for (char *p = strtok(c, "."); p && r; p = strtok(NULL, ".")) r = json_champ(r, p);
    free(c);
    return r;
}

const char *json_texte(const Json *j) {
    return j && j->type == JSON_TEXTE ? j->texte : NULL;
}

long json_entier(const Json *j, long defaut) {
    return j && j->type == JSON_NOMBRE ? (long)j->nombre : defaut;
}

void json_ecrire_texte(Chaine *c, const char *t) {
    chaine_ajouter(c, "\"");
    for (const unsigned char *p = (const unsigned char *)t; *p; p++) {
        char e[8];
        if (*p == '"') chaine_ajouter(c, "\\\"");
        else if (*p == '\\') chaine_ajouter(c, "\\\\");
        else if (*p == '\n') chaine_ajouter(c, "\\n");
        else if (*p == '\r') chaine_ajouter(c, "\\r");
        else if (*p == '\t') chaine_ajouter(c, "\\t");
        else if (*p < 0x20) { snprintf(e, sizeof e, "\\u%04x", *p); chaine_ajouter(c, e); }
        else { e[0] = (char)*p; e[1] = '\0'; chaine_ajouter(c, e); }
    }
    chaine_ajouter(c, "\"");
}

void json_ecrire(Chaine *c, const Json *j) {
    if (!j) { chaine_ajouter(c, "null"); return; }
    switch (j->type) {
    case JSON_NUL: chaine_ajouter(c, "null"); return;
    case JSON_BOOLEEN: chaine_ajouter(c, j->vrai ? "true" : "false"); return;
    case JSON_NOMBRE: chaine_ajouter(c, j->texte); return;
    case JSON_TEXTE: json_ecrire_texte(c, j->texte); return;
    case JSON_TABLEAU:
    case JSON_OBJET:
        chaine_ajouter(c, j->type == JSON_OBJET ? "{" : "[");
        for (size_t k = 0; k < j->nb; k++) {
            if (k) chaine_ajouter(c, ",");
            if (j->type == JSON_OBJET) { json_ecrire_texte(c, j->cles[k]); chaine_ajouter(c, ":"); }
            json_ecrire(c, j->elements[k]);
        }
        chaine_ajouter(c, j->type == JSON_OBJET ? "}" : "]");
        return;
    }
}

/* ---------------------------------------------------------------- */
/* XML lu comme JSON (grammaire 2.84, § 22.18) : pour « le champ »  */
/* ---------------------------------------------------------------- */
/* Un élément qui a des éléments enfants devient un objet, ses enfants rangés par leur nom local (sans préfixe d'espace de
 * noms) ; un enfant répété devient un tableau, dans l'ordre. Un élément sans enfant devient un texte, débarrassé des blancs
 * à ses bords. Les attributs d'un objet sont ses clés « @nom » ; ceux d'un texte, des clés « élément@nom » de son parent.
 * Les « xmlns » sont ignorés. Le texte d'un objet garde sa source (son XML tel qu'écrit), pour qu'un élément parcouru ou lu
 * se relise lui-même : la racine du document devient le document, son nom ne s'écrit donc pas dans les chemins. */

typedef struct {
    const char *p, *fin, *debut;
    int profondeur;
    char *erreur;
} LectureXml;

static void *xml_echec(LectureXml *l, const char *m) {
    if (!l->erreur) {
        int ligne = 1;
        for (const char *q = l->debut; q < l->p && q < l->fin; q++) ligne += *q == '\n';
        l->erreur = grym_formater("XML mal formé, ligne %d : %s.", ligne, m);
    }
    return NULL;
}

static int xml_blanc(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static int xml_nom_car(char c) {
    return (unsigned char)c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
           || c == '_' || c == '-' || c == '.' || c == ':';
}

static int xml_commence(LectureXml *l, const char *m) {
    size_t n = strlen(m);
    return (size_t)(l->fin - l->p) >= n && memcmp(l->p, m, n) == 0;
}

/* Saute jusqu'après « m » ; 0 s'il manque. */
static int xml_sauter(LectureXml *l, const char *m) {
    size_t n = strlen(m);
    while ((size_t)(l->fin - l->p) >= n) {
        if (memcmp(l->p, m, n) == 0) { l->p += n; return 1; }
        l->p++;
    }
    l->p = l->fin;
    return 0;
}

/* Commentaires, instructions de traitement et déclarations ; 0 si rien de tel, -1 si mal fermé. */
static int xml_annexe(LectureXml *l) {
    if (xml_commence(l, "<!--")) return xml_sauter(l, "-->") ? 1 : -1;
    if (xml_commence(l, "<?")) return xml_sauter(l, "?>") ? 1 : -1;
    if (xml_commence(l, "<!DOCTYPE") || xml_commence(l, "<!doctype")) {
        int crochets = 0;
        while (l->p < l->fin) {
            char c = *l->p++;
            if (c == '[') crochets++;
            else if (c == ']') crochets--;
            else if (c == '>' && crochets <= 0) return 1;
        }
        return -1;
    }
    return 0;
}

static char *xml_nom(LectureXml *l) {
    const char *d = l->p;
    while (l->p < l->fin && xml_nom_car(*l->p)) l->p++;
    if (l->p == d) return NULL;
    const char *local = d;
    for (const char *q = d; q < l->p; q++) if (*q == ':') local = q + 1;
    return grym_formater("%.*s", (int)(l->p - local), local);
}

/* Le texte de [d, f), entités et références de caractère résolues. */
static void xml_decoder(Chaine *c, const char *d, const char *f) {
    while (d < f) {
        if (*d != '&') {
            const char *e = d;
            while (e < f && *e != '&') e++;
            char *m = grym_formater("%.*s", (int)(e - d), d);
            chaine_ajouter(c, m);
            free(m);
            d = e;
            continue;
        }
        const char *pv = d;
        while (pv < f && *pv != ';' && pv - d < 12) pv++;
        if (pv >= f || *pv != ';') { chaine_ajouter(c, "&"); d++; continue; }
        size_t n = (size_t)(pv - d - 1);
        const char *e = d + 1;
        static const struct { const char *nom, *car; } ENT[] = {
            { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }
        };
        int fait = 0;
        for (size_t k = 0; k < 5 && !fait; k++)
            if (strlen(ENT[k].nom) == n && memcmp(e, ENT[k].nom, n) == 0) { chaine_ajouter(c, ENT[k].car); fait = 1; }
        if (!fait && n >= 2 && *e == '#') {
            unsigned long cp = (e[1] == 'x' || e[1] == 'X') ? strtoul(e + 2, NULL, 16) : strtoul(e + 1, NULL, 10);
            if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
            utf8(c, (unsigned)cp);
            fait = 1;
        }
        if (!fait) { char *m = grym_formater("%.*s", (int)(pv - d + 1), d); chaine_ajouter(c, m); free(m); }
        d = pv + 1;
    }
}

static void xml_ranger(Json *o, char *cle, Json *v, size_t *cap) {
    for (size_t k = 0; k < o->nb; k++) {
        if (strcmp(o->cles[k], cle) != 0) continue;
        Json *x = o->elements[k];
        if (x->type != JSON_TABLEAU) {   /* un second enfant du même nom : le premier et lui forment un tableau */
            Json *t = nouveau(JSON_TABLEAU);
            t->elements = grym_allouer(2 * sizeof *t->elements);
            t->elements[0] = x;
            t->nb = 1;
            o->elements[k] = x = t;
        } else {
            Json **e = grym_allouer((x->nb + 1) * sizeof *e);
            memcpy(e, x->elements, x->nb * sizeof *e);
            free(x->elements);
            x->elements = e;
        }
        x->elements[x->nb++] = v;
        free(cle);
        return;
    }
    if (o->nb == *cap) {
        *cap = *cap ? *cap * 2 : 8;
        Json **e = grym_allouer(*cap * sizeof *e);
        char **k = grym_allouer(*cap * sizeof *k);
        if (o->nb) { memcpy(e, o->elements, o->nb * sizeof *e); memcpy(k, o->cles, o->nb * sizeof *k); }
        free(o->elements);
        free(o->cles);
        o->elements = e;
        o->cles = k;
    }
    o->cles[o->nb] = cle;
    o->elements[o->nb++] = v;
}

typedef struct { char *nom; char *valeur; } AttributXml;

/* Un élément, « < » compris ; *nom reçoit son nom local, *attributs ses attributs (hors xmlns). */
static Json *xml_element(LectureXml *l, char **nom, AttributXml **attributs, size_t *nb_attributs) {
    if (++l->profondeur > PROFONDEUR_MAX) return xml_echec(l, "imbrication trop profonde");
    const char *source = l->p;
    l->p++;
    *nom = xml_nom(l);
    if (!*nom) return xml_echec(l, "nom d'élément attendu après « < »");
    *attributs = NULL;
    *nb_attributs = 0;
    for (;;) {
        while (l->p < l->fin && xml_blanc(*l->p)) l->p++;
        if (l->p >= l->fin) return xml_echec(l, "élément non fermé");
        if (*l->p == '/' || *l->p == '>') break;
        const char *debut_nom = l->p;
        char *an = xml_nom(l);
        if (!an) return xml_echec(l, "nom d'attribut attendu");
        int xmlns = (l->p - debut_nom == 5 && memcmp(debut_nom, "xmlns", 5) == 0)
                    || (l->p - debut_nom > 6 && memcmp(debut_nom, "xmlns:", 6) == 0);
        while (l->p < l->fin && xml_blanc(*l->p)) l->p++;
        if (l->p >= l->fin || *l->p != '=') { free(an); return xml_echec(l, "« = » attendu après un attribut"); }
        l->p++;
        while (l->p < l->fin && xml_blanc(*l->p)) l->p++;
        if (l->p >= l->fin || (*l->p != '"' && *l->p != '\'')) { free(an); return xml_echec(l, "valeur d'attribut entre guillemets attendue"); }
        char q = *l->p++;
        const char *d = l->p;
        while (l->p < l->fin && *l->p != q) l->p++;
        if (l->p >= l->fin) { free(an); return xml_echec(l, "valeur d'attribut non fermée"); }
        Chaine v = {0};
        chaine_ajouter(&v, "");
        xml_decoder(&v, d, l->p);
        l->p++;
        if (xmlns) { free(an); free(chaine_rendre(&v)); continue; }
        AttributXml *t = grym_allouer((*nb_attributs + 1) * sizeof *t);
        if (*nb_attributs) memcpy(t, *attributs, *nb_attributs * sizeof *t);
        free(*attributs);
        *attributs = t;
        t[*nb_attributs].nom = an;
        t[(*nb_attributs)++].valeur = chaine_rendre(&v);
    }
    Json *objet = NULL;
    size_t cap = 0;
    Chaine texte = {0};
    chaine_ajouter(&texte, "");
    if (*l->p == '/') {
        l->p++;
        if (l->p >= l->fin || *l->p != '>') { free(chaine_rendre(&texte)); return xml_echec(l, "« > » attendu après « / »"); }
        l->p++;
    } else {
        l->p++;
        for (;;) {
            const char *d = l->p;
            while (l->p < l->fin && *l->p != '<') l->p++;
            if (!objet) xml_decoder(&texte, d, l->p);
            if (l->p >= l->fin) { free(chaine_rendre(&texte)); json_liberer(objet); return xml_echec(l, "élément non fermé"); }
            if (xml_commence(l, "<![CDATA[")) {
                l->p += 9;
                const char *c0 = l->p;
                if (!xml_sauter(l, "]]>")) { free(chaine_rendre(&texte)); json_liberer(objet); return xml_echec(l, "CDATA non fermé"); }
                if (!objet) { char *m = grym_formater("%.*s", (int)(l->p - 3 - c0), c0); chaine_ajouter(&texte, m); free(m); }
                continue;
            }
            int a = xml_annexe(l);
            if (a < 0) { free(chaine_rendre(&texte)); json_liberer(objet); return xml_echec(l, "commentaire non fermé"); }
            if (a > 0) continue;
            if (xml_commence(l, "</")) {
                l->p += 2;
                char *fin_nom = xml_nom(l);
                int pareil = fin_nom && strcmp(fin_nom, *nom) == 0;
                free(fin_nom);
                while (l->p < l->fin && xml_blanc(*l->p)) l->p++;
                if (!pareil || l->p >= l->fin || *l->p != '>') {
                    free(chaine_rendre(&texte)); json_liberer(objet);
                    return xml_echec(l, "la balise fermante ne répond pas à l'élément ouvert");
                }
                l->p++;
                break;
            }
            char *enfant_nom = NULL;
            AttributXml *ea = NULL;
            size_t nea = 0;
            Json *enfant = xml_element(l, &enfant_nom, &ea, &nea);
            if (!enfant) {
                free(enfant_nom);
                for (size_t k = 0; k < nea; k++) { free(ea[k].nom); free(ea[k].valeur); }
                free(ea);
                free(chaine_rendre(&texte)); json_liberer(objet);
                return NULL;
            }
            if (!objet) objet = nouveau(JSON_OBJET);
            if (enfant->type == JSON_TEXTE) {   /* les attributs d'un texte : « élément@nom » chez le parent */
                for (size_t k = 0; k < nea; k++) {
                    char *cle = grym_formater("%s@%s", enfant_nom, ea[k].nom);
                    if (json_champ(objet, cle)) { free(cle); free(ea[k].valeur); }
                    else { Json *v = nouveau(JSON_TEXTE); v->texte = ea[k].valeur; xml_ranger(objet, cle, v, &cap); }
                    free(ea[k].nom);
                }
            } else {
                for (size_t k = 0; k < nea; k++) { free(ea[k].nom); free(ea[k].valeur); }
            }
            free(ea);
            xml_ranger(objet, enfant_nom, enfant, &cap);
        }
    }
    l->profondeur--;
    if (objet) {
        free(chaine_rendre(&texte));
        for (size_t k = 0; k < *nb_attributs; k++) {   /* les attributs d'un objet : ses clés « @nom » */
            Json *v = nouveau(JSON_TEXTE);
            v->texte = (*attributs)[k].valeur;
            (*attributs)[k].valeur = NULL;
            xml_ranger(objet, grym_formater("@%s", (*attributs)[k].nom), v, &cap);
        }
        objet->texte = grym_formater("%.*s", (int)(l->p - source), source);
        return objet;
    }
    char *t = chaine_rendre(&texte);
    char *d = t, *f = t + strlen(t);
    while (*d && xml_blanc(*d)) d++;
    while (f > d && xml_blanc(f[-1])) f--;
    Json *j = nouveau(JSON_TEXTE);
    j->texte = grym_formater("%.*s", (int)(f - d), d);
    free(t);
    /* Un élément vide qui n'a que des attributs (« <Pt Cd="X"/> ») : un objet de ses attributs. */
    if (*nb_attributs && !*j->texte) {
        json_liberer(j);
        Json *o = nouveau(JSON_OBJET);
        for (size_t k = 0; k < *nb_attributs; k++) {
            Json *v = nouveau(JSON_TEXTE);
            v->texte = (*attributs)[k].valeur;
            (*attributs)[k].valeur = NULL;
            xml_ranger(o, grym_formater("@%s", (*attributs)[k].nom), v, &cap);
        }
        o->texte = grym_formater("%.*s", (int)(l->p - source), source);
        return o;
    }
    return j;
}

int json_est_xml(const char *t, size_t n) {
    size_t k = 0;
    if (n >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) k = 3;
    while (k < n && xml_blanc(t[k])) k++;
    return k < n && t[k] == '<';
}

Json *json_depuis_xml(const char *texte, size_t taille, char **erreur) {
    LectureXml l = { texte, texte + taille, texte, 0, NULL };
    if (taille >= 3 && (unsigned char)texte[0] == 0xEF && (unsigned char)texte[1] == 0xBB && (unsigned char)texte[2] == 0xBF) l.p += 3;
    Json *j = NULL;
    for (;;) {
        while (l.p < l.fin && xml_blanc(*l.p)) l.p++;
        if (l.p >= l.fin) { xml_echec(&l, "aucun élément"); break; }
        if (*l.p != '<') { xml_echec(&l, "« < » attendu"); break; }
        int a = xml_annexe(&l);
        if (a < 0) { xml_echec(&l, "déclaration non fermée"); break; }
        if (a > 0) continue;
        char *nom = NULL;
        AttributXml *at = NULL;
        size_t nat = 0;
        j = xml_element(&l, &nom, &at, &nat);
        free(nom);
        for (size_t k = 0; k < nat; k++) { free(at[k].nom); free(at[k].valeur); }
        free(at);
        break;
    }
    if (j) {
        for (;;) {
            while (l.p < l.fin && xml_blanc(*l.p)) l.p++;
            if (l.p >= l.fin) break;
            int a = xml_annexe(&l);
            if (a <= 0) { json_liberer(j); j = xml_echec(&l, "contenu après l'élément racine"); break; }
        }
    }
    if (!j) *erreur = l.erreur ? l.erreur : grym_dupliquer("XML mal formé.");
    return j;
}
