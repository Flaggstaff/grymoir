/* GrymoiR : blocs de bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.0).
 */
#include "bytecode.h"
#include "decimal.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_MAX 0xFFFF

/* ---------------------------------------------------------------- */
/* Construction                                                     */
/* ---------------------------------------------------------------- */

Bloc *bloc_creer(void) {
    Bloc *b = grym_allouer(sizeof *b);
    memset(b, 0, sizeof *b);
    return b;
}

void bloc_detruire(Bloc *b) {
    if (!b) return;
    for (size_t i = 0; i < b->nb_constantes; i++) free(b->constantes[i].texte);
    free(b->constantes);
    for (size_t i = 0; i < b->nb_noms; i++) free(b->noms[i]);
    free(b->noms);
    free(b->code);
    free(b->positions);
    free(b);
}

static void *agrandir(void *p, size_t n, size_t taille_element) {
    void *q = realloc(p, n * taille_element);
    if (!q) {
        fputs("GrymoiR : mémoire épuisée\n", stderr);
        exit(EXIT_FAILURE);
    }
    return q;
}

long bloc_constante(Bloc *b, TypeConstante type, const char *texte) {
    for (size_t i = 0; i < b->nb_constantes; i++)
        if (b->constantes[i].type == type && strcmp(b->constantes[i].texte, texte) == 0)
            return (long)i;
    if (b->nb_constantes > INDEX_MAX) return -1;
    b->constantes = agrandir(b->constantes, b->nb_constantes + 1, sizeof *b->constantes);
    b->constantes[b->nb_constantes].type = type;
    b->constantes[b->nb_constantes].texte = grym_dupliquer(texte);
    return (long)b->nb_constantes++;
}

long bloc_nom(Bloc *b, const char *nom) {
    for (size_t i = 0; i < b->nb_noms; i++)
        if (strcmp(b->noms[i], nom) == 0) return (long)i;
    if (b->nb_noms > INDEX_MAX) return -1;
    b->noms = agrandir(b->noms, b->nb_noms + 1, sizeof *b->noms);
    b->noms[b->nb_noms] = grym_dupliquer(nom);
    return (long)b->nb_noms++;
}

static void octet(Bloc *b, uint8_t o) {
    if (b->taille_code == b->cap_code) {
        b->cap_code = b->cap_code ? b->cap_code * 2 : 64;
        b->code = agrandir(b->code, b->cap_code, 1);
    }
    b->code[b->taille_code++] = o;
}

void bloc_emettre(Bloc *b, CodeInstruction code, uint16_t operande, int ligne, int colonne) {
    if (b->nb_positions == b->cap_positions) {
        b->cap_positions = b->cap_positions ? b->cap_positions * 2 : 32;
        b->positions = agrandir(b->positions, b->cap_positions, sizeof *b->positions);
    }
    Position *p = &b->positions[b->nb_positions++];
    p->decalage = (uint32_t)b->taille_code;
    p->ligne = (uint32_t)(ligne > 0 ? ligne : 0);
    p->colonne = (uint32_t)(colonne > 0 ? colonne : 0);
    octet(b, (uint8_t)code);
    if (instruction_a_operande(code)) {
        octet(b, (uint8_t)(operande & 0xFF));
        octet(b, (uint8_t)(operande >> 8));
    }
}

const char *instruction_nom(CodeInstruction code) {
    switch (code) {
    case I_CONSTANTE:      return "CONSTANTE";
    case I_LIRE:           return "LIRE";
    case I_ECRIRE:         return "ÉCRIRE";
    case I_NEGATION:       return "NÉGATION";
    case I_ADDITION:       return "ADDITION";
    case I_SOUSTRACTION:   return "SOUSTRACTION";
    case I_MULTIPLICATION: return "MULTIPLICATION";
    case I_DIVISION:       return "DIVISION";
    case I_PUISSANCE:      return "PUISSANCE";
    case I_AFFICHER:       return "AFFICHER";
    case I_RETOUR:         return "RETOUR";
    }
    return "INCONNUE";
}

int instruction_a_operande(CodeInstruction code) {
    return code == I_CONSTANTE || code == I_LIRE || code == I_ECRIRE || code == I_AFFICHER;
}

void bloc_position(const Bloc *b, size_t decalage, int *ligne, int *colonne) {
    *ligne = *colonne = 0;
    for (size_t i = 0; i < b->nb_positions; i++)
        if (b->positions[i].decalage == decalage) {
            *ligne = (int)b->positions[i].ligne;
            *colonne = (int)b->positions[i].colonne;
            return;
        }
}

/* ---------------------------------------------------------------- */
/* Vérification (docs/vm.md, § 4)                                   */
/* ---------------------------------------------------------------- */

static int refuser(char **erreur, char *message) {
    *erreur = message;
    return 0;
}

int bloc_verifier(const Bloc *b, char **erreur) {
    size_t ip = 0, pile = 0;
    int termine = 0;
    while (ip < b->taille_code) {
        if (termine)
            return refuser(erreur, grym_formater("instructions après RETOUR (octet %lu).", (unsigned long)ip));
        size_t debut = ip;
        uint8_t c = b->code[ip++];
        if (c < I_CONSTANTE || c > I_DERNIER)
            return refuser(erreur, grym_formater("code d'instruction %u inconnu (octet %lu).",
                                                 (unsigned)c, (unsigned long)debut));
        unsigned op = 0;
        if (instruction_a_operande((CodeInstruction)c)) {
            if (ip + 2 > b->taille_code)
                return refuser(erreur, grym_formater("opérande tronqué (octet %lu).", (unsigned long)debut));
            op = (unsigned)b->code[ip] | ((unsigned)b->code[ip + 1] << 8);
            ip += 2;
        }
        size_t besoin = 0;
        long effet = 0;
        switch ((CodeInstruction)c) {
        case I_CONSTANTE:
            if (op >= b->nb_constantes)
                return refuser(erreur, grym_formater("constante %u inexistante (octet %lu).", op, (unsigned long)debut));
            effet = 1;
            break;
        case I_LIRE:
        case I_ECRIRE:
            if (op >= b->nb_noms)
                return refuser(erreur, grym_formater("nom %u inexistant (octet %lu).", op, (unsigned long)debut));
            besoin = c == I_ECRIRE ? 1 : 0;
            effet = c == I_ECRIRE ? -1 : 1;
            break;
        case I_NEGATION:
            besoin = 1;
            break;
        case I_ADDITION: case I_SOUSTRACTION: case I_MULTIPLICATION:
        case I_DIVISION: case I_PUISSANCE:
            besoin = 2;
            effet = -1;
            break;
        case I_AFFICHER:
            if (op == 0)
                return refuser(erreur, grym_formater("AFFICHER sans élément (octet %lu).", (unsigned long)debut));
            besoin = op;
            effet = -(long)op;
            break;
        case I_RETOUR:
            if (pile != 0)
                return refuser(erreur, grym_formater("RETOUR avec une pile non vide (octet %lu).", (unsigned long)debut));
            termine = 1;
            break;
        }
        if (pile < besoin)
            return refuser(erreur, grym_formater("pile insuffisante pour %s (octet %lu).",
                                                 instruction_nom((CodeInstruction)c), (unsigned long)debut));
        pile = (size_t)((long)pile + effet);
    }
    if (!termine) return refuser(erreur, grym_dupliquer("le bloc ne se termine pas par RETOUR."));
    for (size_t i = 0; i < b->nb_constantes; i++)
        if (b->constantes[i].type == C_NOMBRE && !dec_canonique_valide(b->constantes[i].texte))
            return refuser(erreur, grym_formater("constante %lu : nombre mal formé.", (unsigned long)i));
    return 1;
}

/* ---------------------------------------------------------------- */
/* Fichier .grymb (docs/vm.md, § 8)                                 */
/* ---------------------------------------------------------------- */

#define VERSION_FORMAT 1

typedef struct { unsigned char *d; size_t n, cap; } Octets;

static void ecrire_octets(Octets *o, const void *src, size_t n) {
    if (o->n + n > o->cap) {
        size_t cap = o->cap ? o->cap : 256;
        while (o->n + n > cap) cap *= 2;
        o->d = agrandir(o->d, cap, 1);
        o->cap = cap;
    }
    memcpy(o->d + o->n, src, n);
    o->n += n;
}

static void ecrire_u8(Octets *o, unsigned v) {
    unsigned char c = (unsigned char)v;
    ecrire_octets(o, &c, 1);
}

static void ecrire_u16(Octets *o, unsigned v) {
    ecrire_u8(o, v & 0xFF);
    ecrire_u8(o, (v >> 8) & 0xFF);
}

static void ecrire_u32(Octets *o, uint32_t v) {
    for (int k = 0; k < 4; k++) ecrire_u8(o, (v >> (8 * k)) & 0xFF);
}

static void ecrire_chaine(Octets *o, const char *s) {
    size_t l = strlen(s);
    ecrire_u32(o, (uint32_t)l);
    ecrire_octets(o, s, l);
}

unsigned char *bloc_serialiser(const Bloc *b, size_t *taille) {
    Octets o = { NULL, 0, 0 };
    ecrire_octets(&o, "GRYM", 4);
    ecrire_u16(&o, VERSION_FORMAT);
    ecrire_u32(&o, (uint32_t)b->nb_constantes);
    for (size_t i = 0; i < b->nb_constantes; i++) {
        ecrire_u8(&o, b->constantes[i].type);
        ecrire_chaine(&o, b->constantes[i].texte);
    }
    ecrire_u32(&o, (uint32_t)b->nb_noms);
    for (size_t i = 0; i < b->nb_noms; i++) ecrire_chaine(&o, b->noms[i]);
    ecrire_u32(&o, (uint32_t)b->taille_code);
    ecrire_octets(&o, b->code, b->taille_code);
    ecrire_u32(&o, (uint32_t)b->nb_positions);
    for (size_t i = 0; i < b->nb_positions; i++) {
        ecrire_u32(&o, b->positions[i].decalage);
        ecrire_u32(&o, b->positions[i].ligne);
        ecrire_u32(&o, b->positions[i].colonne);
    }
    *taille = o.n;
    return o.d;
}

int est_fichier_bytecode(const unsigned char *d, size_t n) {
    return n >= 4 && memcmp(d, "GRYM", 4) == 0;
}

typedef struct {
    const unsigned char *d;
    size_t n, pos;
    int echec;
} Lecture;

static int reste(Lecture *l, size_t k) {
    if (l->echec || l->n - l->pos < k) { l->echec = 1; return 0; }
    return 1;
}

static uint32_t lire_u(Lecture *l, int octets) {
    if (!reste(l, (size_t)octets)) return 0;
    uint32_t v = 0;
    for (int k = 0; k < octets; k++) v |= (uint32_t)l->d[l->pos + (size_t)k] << (8 * k);
    l->pos += (size_t)octets;
    return v;
}

/* UTF-8 bien formé, sans octet nul. */
static int utf8_valide(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t len;
        uint32_t v, min;
        if (c == 0) return 0;
        if (c < 0x80)                { i++; continue; }
        else if ((c & 0xE0) == 0xC0) { len = 2; v = c & 0x1F; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; v = c & 0x0F; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; v = c & 0x07; min = 0x10000; }
        else return 0;
        if (i + len > n) return 0;
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return 0;
            v = (v << 6) | (s[i + k] & 0x3F);
        }
        if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
        i += len;
    }
    return 1;
}

static char *lire_chaine(Lecture *l) {
    uint32_t len = lire_u(l, 4);
    if (!reste(l, len) || !utf8_valide(l->d + l->pos, len)) { l->echec = 1; return NULL; }
    char *s = grym_allouer((size_t)len + 1);
    memcpy(s, l->d + l->pos, len);
    s[len] = '\0';
    l->pos += len;
    return s;
}

static Bloc *echec_lecture(Bloc *b, char **erreur, char *detail) {
    bloc_detruire(b);
    *erreur = grym_formater("Fichier .grymb invalide : %s", detail);
    free(detail);
    return NULL;
}

Bloc *bloc_lire(const unsigned char *donnees, size_t taille, char **erreur) {
    Lecture l = { donnees, taille, 0, 0 };
    Bloc *b = bloc_creer();
    if (!est_fichier_bytecode(donnees, taille))
        return echec_lecture(b, erreur, grym_dupliquer("en-tête « GRYM » absent."));
    l.pos = 4;
    uint32_t version = lire_u(&l, 2);
    if (!l.echec && version != VERSION_FORMAT)
        return echec_lecture(b, erreur, grym_formater(
            "format version %u, cette version de grym lit la version %d.", (unsigned)version, VERSION_FORMAT));

    uint32_t nc = lire_u(&l, 4);
    if (l.echec || nc > (taille - l.pos) / 5)
        return echec_lecture(b, erreur, grym_dupliquer("table des constantes tronquée."));
    b->constantes = grym_allouer((nc ? nc : 1) * sizeof *b->constantes);
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t type = lire_u(&l, 1);
        char *t = lire_chaine(&l);
        if (!t || (type != C_NOMBRE && type != C_TEXTE)) {
            free(t);
            return echec_lecture(b, erreur, grym_formater("constante %u illisible.", (unsigned)i));
        }
        b->constantes[i].type = (TypeConstante)type;
        b->constantes[i].texte = t;
        b->nb_constantes++;
    }

    uint32_t nn = lire_u(&l, 4);
    if (l.echec || nn > (taille - l.pos) / 4)
        return echec_lecture(b, erreur, grym_dupliquer("table des noms tronquée."));
    b->noms = grym_allouer((nn ? nn : 1) * sizeof *b->noms);
    for (uint32_t i = 0; i < nn; i++) {
        char *t = lire_chaine(&l);
        if (!t || !*t) {
            free(t);
            return echec_lecture(b, erreur, grym_formater("nom %u illisible.", (unsigned)i));
        }
        b->noms[i] = t;
        b->nb_noms++;
    }

    uint32_t tc = lire_u(&l, 4);
    if (!reste(&l, tc)) return echec_lecture(b, erreur, grym_dupliquer("code tronqué."));
    b->code = grym_allouer(tc ? tc : 1);
    memcpy(b->code, l.d + l.pos, tc);
    b->taille_code = b->cap_code = tc;
    l.pos += tc;

    uint32_t np = lire_u(&l, 4);
    if (l.echec || np > (taille - l.pos) / 12)
        return echec_lecture(b, erreur, grym_dupliquer("table des positions tronquée."));
    b->positions = grym_allouer((np ? np : 1) * sizeof *b->positions);
    for (uint32_t i = 0; i < np; i++) {
        b->positions[i].decalage = lire_u(&l, 4);
        b->positions[i].ligne = lire_u(&l, 4);
        b->positions[i].colonne = lire_u(&l, 4);
    }
    b->nb_positions = b->cap_positions = np;
    if (l.echec) return echec_lecture(b, erreur, grym_dupliquer("table des positions tronquée."));
    if (l.pos != taille) return echec_lecture(b, erreur, grym_dupliquer("octets en trop à la fin du fichier."));

    char *detail = NULL;
    if (!bloc_verifier(b, &detail)) return echec_lecture(b, erreur, detail);
    return b;
}

/* ---------------------------------------------------------------- */
/* Désassemblage (docs/vm.md, § 9)                                  */
/* ---------------------------------------------------------------- */

static void completer(Chaine *c, const char *s, size_t largeur) {
    chaine_ajouter(c, s);
    for (size_t k = longueur_utf8(s); k < largeur; k++) chaine_ajouter(c, " ");
}

char *bloc_desassembler(const Bloc *b) {
    Chaine c = {0};
    int ligne_prec = -1;
    size_t ip = 0;
    while (ip < b->taille_code) {
        size_t debut = ip;
        CodeInstruction code = (CodeInstruction)b->code[ip++];
        unsigned op = 0;
        int avec = instruction_a_operande(code);
        if (avec && ip + 2 <= b->taille_code) {
            op = (unsigned)b->code[ip] | ((unsigned)b->code[ip + 1] << 8);
            ip += 2;
        }
        int ligne, colonne;
        bloc_position(b, debut, &ligne, &colonne);
        char marge[16];
        if (ligne != ligne_prec && ligne > 0) {
            snprintf(marge, sizeof marge, "%4d  ", ligne);
            ligne_prec = ligne;
        } else {
            snprintf(marge, sizeof marge, "      ");
        }
        chaine_ajouter(&c, marge);
        if (!avec) {
            chaine_ajouter(&c, instruction_nom(code));
            chaine_ajouter(&c, "\n");
            continue;
        }
        completer(&c, instruction_nom(code), 17);
        char nombre[16];
        snprintf(nombre, sizeof nombre, "%u", op);
        completer(&c, nombre, 6);
        char *commentaire = NULL;
        if (code == I_CONSTANTE && op < b->nb_constantes) {
            const Constante *k = &b->constantes[op];
            if (k->type == C_NOMBRE) {
                Decimal d = dec_depuis_canonique(k->texte);
                commentaire = dec_formater(&d);
                dec_liberer(&d);
            } else {
                commentaire = grym_formater("« %s »", k->texte);
            }
        } else if ((code == I_LIRE || code == I_ECRIRE) && op < b->nb_noms) {
            commentaire = grym_dupliquer(b->noms[op]);
        }
        if (commentaire) {
            chaine_ajouter(&c, "; ");
            chaine_ajouter(&c, commentaire);
            free(commentaire);
        }
        /* retirer les espaces de fin */
        while (c.n && c.d[c.n - 1] == ' ') c.d[--c.n] = '\0';
        chaine_ajouter(&c, "\n");
    }
    return chaine_rendre(&c);
}
