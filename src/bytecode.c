/* GrymoiR : blocs de bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.23).
 */
#include "bytecode.h"
#include "date.h"
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
    free(b->nom);
    free(b->classe);
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

long requete_parametres(const char *d) {
    int separateurs = 0;
    long n = 0;
    for (const char *p = d; *p; p++) {
        if (*p == '\x1f') separateurs++;
        else if (*p == '?' && separateurs == 4) {
            long i = strtol(p + 1, NULL, 10);
            if (i > n) n = i;
        }
    }
    return separateurs == 4 && n <= 64 ? n : -1;
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

void bloc_emettre_appel(Bloc *b, uint16_t nom, uint8_t nb_arguments, int rend, int ligne, int colonne) {
    bloc_emettre(b, I_APPELER, nom, ligne, colonne);
    octet(b, nb_arguments);
    octet(b, (uint8_t)(rend ? 1 : 0));
}

size_t bloc_emettre_saut(Bloc *b, CodeInstruction code, int ligne, int colonne) {
    bloc_emettre(b, code, 0, ligne, colonne);
    size_t pos = b->taille_code;
    for (int k = 0; k < 4; k++) octet(b, 0);
    return pos;
}

void bloc_corriger_saut(Bloc *b, size_t operande, size_t cible) {
    for (int k = 0; k < 4; k++) b->code[operande + (size_t)k] = (uint8_t)((cible >> (8 * k)) & 0xFF);
}

static uint32_t lire_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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
    case I_EGAL:           return "ÉGAL";
    case I_DIFFERENT:      return "DIFFÉRENT";
    case I_INFERIEUR:      return "INFÉRIEUR";
    case I_SUPERIEUR:      return "SUPÉRIEUR";
    case I_INFERIEUR_OU_EGAL: return "INFÉRIEUR_OU_ÉGAL";
    case I_SUPERIEUR_OU_EGAL: return "SUPÉRIEUR_OU_ÉGAL";
    case I_NON:            return "NON";
    case I_SAUTER:         return "SAUTER";
    case I_SAUTER_SI_FAUX: return "SAUTER_SI_FAUX";
    case I_APPELER:        return "APPELER";
    case I_RENDRE:         return "RENDRE";
    case I_LIRE_LOCAL:     return "LIRE_LOCAL";
    case I_ECRIRE_LOCAL:   return "ÉCRIRE_LOCAL";
    case I_ECHOUER:        return "ÉCHOUER";
    case I_EXIGER_ENTIER_NATUREL: return "EXIGER_ENTIER_NATUREL";
    case I_NOUVEAU:        return "NOUVEAU";
    case I_INITIALISER_CHAMP: return "INITIALISER_CHAMP";
    case I_LIRE_CHAMP:     return "LIRE_CHAMP";
    case I_ECRIRE_CHAMP:   return "ÉCRIRE_CHAMP";
    case I_AUJOURDHUI:     return "AUJOURD'HUI";
    case I_LIRE_FICHIER:   return "LIRE_FICHIER";
    case I_ENREGISTRER:    return "ENREGISTRER";
    case I_CONSERVER:      return "CONSERVER";
    case I_SUPPRIMER:      return "SUPPRIMER";
    case I_CHERCHER:       return "CHERCHER";
    case I_TAILLE_LISTE:   return "TAILLE_LISTE";
    case I_ELEMENT:        return "ÉLÉMENT";
    case I_ABSENT:         return "ABSENT";
    case I_EST_ABSENT:     return "EST_ABSENT";
    case I_SUPPRIMER_DEFINITIVEMENT: return "SUPPRIMER_DÉFINITIVEMENT";
    case I_RETABLIR:       return "RÉTABLIR";
    case I_GAGNER:         return "GAGNER";
    case I_PERDRE:         return "PERDRE";
    case I_DEMANDER:       return "DEMANDER";
    case I_CADRER:         return "CADRER";
    case I_AFFICHER_SANS_LIGNE: return "AFFICHER_SANS_LIGNE";
    case I_STYLE:          return "STYLE";
    case I_EFFACER:        return "EFFACER";
    case I_ESSAYER:        return "ESSAYER";
    case I_FIN_ESSAI:      return "FIN_ESSAI";
    }
    return "INCONNUE";
}

int instruction_a_operande(CodeInstruction code) {
    return code == I_CONSTANTE || code == I_LIRE || code == I_ECRIRE || code == I_AFFICHER
        || code == I_APPELER || code == I_LIRE_LOCAL || code == I_ECRIRE_LOCAL || code == I_ECHOUER
        || code == I_NOUVEAU || code == I_INITIALISER_CHAMP || code == I_LIRE_CHAMP || code == I_ECRIRE_CHAMP
        || code == I_GAGNER || code == I_PERDRE || code == I_DEMANDER
        || code == I_CADRER || code == I_AFFICHER_SANS_LIGNE || code == I_STYLE
        || code == I_CHERCHER;
}

static int est_saut(CodeInstruction code) {
    return code == I_SAUTER || code == I_SAUTER_SI_FAUX || code == I_ESSAYER;
}

size_t instruction_taille(CodeInstruction code) {
    return 1 + (instruction_a_operande(code) ? 2 : 0) + (est_saut(code) ? 4 : 0)
             + (code == I_APPELER ? 2 : 0);
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

/* Deux passes : décodage linéaire (chaque instruction est complète et ses index
 * existent), puis parcours de tous les chemins (profondeur de pile identique à
 * chaque point de rencontre, jamais négative, nulle à RETOUR). */
int bloc_verifier(const Bloc *b, char **erreur) {
    size_t n = b->taille_code;
    if (n == 0) return refuser(erreur, grym_dupliquer(b->sorte == B_CALCUL
        ? "le calcul ne se termine pas par RENDRE." : "le bloc ne se termine pas par RETOUR."));
    uint8_t *debut_instr = grym_allouer(n);
    memset(debut_instr, 0, n);
    size_t ip = 0, dernier = 0;
    int ok = 1;
    while (ip < n && ok) {
        size_t d = ip;
        uint8_t c = b->code[ip];
        if (c < I_CONSTANTE || c > I_DERNIER) {
            ok = refuser(erreur, grym_formater("code d'instruction %u inconnu (octet %lu).",
                                               (unsigned)c, (unsigned long)d));
            break;
        }
        size_t t = instruction_taille((CodeInstruction)c);
        if (d + t > n) {
            ok = refuser(erreur, grym_formater("opérande tronqué (octet %lu).", (unsigned long)d));
            break;
        }
        unsigned op = instruction_a_operande((CodeInstruction)c)
                    ? (unsigned)b->code[d + 1] | ((unsigned)b->code[d + 2] << 8) : 0;
        if ((c == I_CONSTANTE || c == I_CHERCHER) && op >= b->nb_constantes)
            ok = refuser(erreur, grym_formater("constante %u inexistante (octet %lu).", op, (unsigned long)d));
        else if (c == I_CONSTANTE && b->constantes[op].type == C_RECHERCHE)
            ok = refuser(erreur, grym_formater("constante %u : une recherche ne s'empile pas (octet %lu).", op,
                                               (unsigned long)d));
        else if ((c == I_LIRE || c == I_ECRIRE || c == I_NOUVEAU || c == I_INITIALISER_CHAMP
                  || c == I_LIRE_CHAMP || c == I_ECRIRE_CHAMP || c == I_GAGNER || c == I_PERDRE || c == I_DEMANDER) && op >= b->nb_noms)
            ok = refuser(erreur, grym_formater("nom %u inexistant (octet %lu).", op, (unsigned long)d));
        else if (c == I_AFFICHER && op == 0)
            ok = refuser(erreur, grym_formater("AFFICHER sans élément (octet %lu).", (unsigned long)d));
        else if ((c == I_LIRE_LOCAL || c == I_ECRIRE_LOCAL) && (int)op >= b->nb_locaux)
            ok = refuser(erreur, grym_formater("case locale %u inexistante (octet %lu).", op, (unsigned long)d));
        else if (c == I_APPELER && (op >= b->nb_noms || b->code[d + 4] > 1))
            ok = refuser(erreur, grym_formater("appel mal formé (octet %lu).", (unsigned long)d));
        else if (c == I_ECHOUER && (op >= b->nb_constantes || b->constantes[op].type != C_TEXTE))
            ok = refuser(erreur, grym_formater("ÉCHOUER sans message (octet %lu).", (unsigned long)d));
        else if (c == I_RENDRE && b->sorte != B_CALCUL)
            ok = refuser(erreur, grym_formater("RENDRE hors d'un calcul (octet %lu).", (unsigned long)d));
        else if (c == I_RETOUR && b->sorte == B_CALCUL)
            ok = refuser(erreur, grym_formater("RETOUR dans un calcul (octet %lu).", (unsigned long)d));
        debut_instr[d] = 1;
        dernier = d;
        ip += t;
    }
    if (ok && b->code[dernier] != (b->sorte == B_CALCUL ? I_RENDRE : I_RETOUR))
        ok = refuser(erreur, grym_dupliquer(b->sorte == B_CALCUL ? "le calcul ne se termine pas par RENDRE."
                                                                 : "le bloc ne se termine pas par RETOUR."));
    if (ok && (b->nb_parametres < 0 || b->nb_parametres > b->nb_locaux))
        ok = refuser(erreur, grym_dupliquer("plus de paramètres que de cases locales."));
    /* cibles des sauts */
    for (ip = 0; ok && ip < n; ip += instruction_taille((CodeInstruction)b->code[ip])) {
        if (!est_saut((CodeInstruction)b->code[ip])) continue;
        uint32_t cible = lire_u32(&b->code[ip + 1]);
        if (cible >= n || !debut_instr[cible])
            ok = refuser(erreur, grym_formater("saut vers l'octet %lu, qui ne commence pas une instruction "
                                               "(octet %lu).", (unsigned long)cible, (unsigned long)ip));
    }
    /* profondeur de pile sur tous les chemins */
    long *prof = NULL;
    size_t *travail = NULL;
    if (ok) {
        prof = grym_allouer(n * sizeof *prof);
        for (size_t k = 0; k < n; k++) prof[k] = -1;
        travail = grym_allouer(n * sizeof *travail);
        size_t nt = 0;
        prof[0] = 0;
        travail[nt++] = 0;
        while (nt && ok) {
            size_t d = travail[--nt];
            CodeInstruction c = (CodeInstruction)b->code[d];
            long p = prof[d];
            unsigned op = instruction_a_operande(c) ? (unsigned)b->code[d + 1] | ((unsigned)b->code[d + 2] << 8) : 0;
            long besoin = 0, effet = 0;
            switch (c) {
            case I_CONSTANTE: case I_LIRE: case I_LIRE_LOCAL: effet = 1; break;
            case I_ECRIRE: case I_ECRIRE_LOCAL: besoin = 1; effet = -1; break;
            case I_APPELER:
                besoin = b->code[d + 3];
                effet = -(long)b->code[d + 3] + b->code[d + 4];
                break;
            case I_RENDRE: besoin = 1; break;
            case I_EXIGER_ENTIER_NATUREL: besoin = 1; break;
            case I_NOUVEAU: case I_AUJOURDHUI: effet = 1; break;
            case I_LIRE_FICHIER: besoin = 1; break;
            case I_ENREGISTRER: besoin = 2; effet = -2; break;
            case I_CONSERVER: case I_SUPPRIMER: case I_SUPPRIMER_DEFINITIVEMENT: case I_RETABLIR:
                besoin = 1; effet = -1; break;
            case I_CHERCHER: {
                long np = op < b->nb_constantes && b->constantes[op].type == C_RECHERCHE
                        ? requete_parametres(b->constantes[op].texte) : -1;
                if (np < 0) {
                    ok = refuser(erreur, grym_formater("CHERCHER sans recherche valide (octet %lu).", (unsigned long)d));
                    np = 0;
                }
                besoin = np;
                effet = 1 - np;
                break;
            }
            case I_TAILLE_LISTE: case I_EST_ABSENT: besoin = 1; break;
            case I_ABSENT: effet = 1; break;
            case I_ELEMENT: besoin = 2; effet = -1; break;
            case I_INITIALISER_CHAMP: besoin = 2; effet = -1; break;
            case I_LIRE_CHAMP: besoin = 1; break;
            case I_ECRIRE_CHAMP: case I_GAGNER: case I_PERDRE: besoin = 2; effet = -2; break;
            case I_DEMANDER: besoin = 1; effet = 0; break;   /* dépile la question, empile la réponse */
            case I_ECHOUER: break;
            case I_NEGATION: case I_NON: besoin = 1; break;
            case I_ADDITION: case I_SOUSTRACTION: case I_MULTIPLICATION: case I_DIVISION:
            case I_PUISSANCE: case I_EGAL: case I_DIFFERENT: case I_INFERIEUR: case I_SUPERIEUR:
            case I_INFERIEUR_OU_EGAL: case I_SUPERIEUR_OU_EGAL:
                besoin = 2; effet = -1; break;
            case I_AFFICHER: case I_AFFICHER_SANS_LIGNE: besoin = (long)op; effet = -(long)op; break;
            case I_CADRER: besoin = 2; effet = -1; break;
            case I_STYLE: case I_EFFACER: case I_FIN_ESSAI: case I_ESSAYER: besoin = 0; effet = 0; break;
            case I_SAUTER_SI_FAUX: besoin = 1; effet = -1; break;
            case I_SAUTER: case I_RETOUR: break;
            }
            if (p < besoin) {
                ok = refuser(erreur, grym_formater("pile insuffisante pour %s (octet %lu).",
                                                   instruction_nom(c), (unsigned long)d));
                break;
            }
            if (c == I_RETOUR) {
                if (p != 0)
                    ok = refuser(erreur, grym_formater("RETOUR avec une pile non vide (octet %lu).",
                                                       (unsigned long)d));
                continue;
            }
            if (c == I_ECHOUER) continue;   /* l'exécution s'arrête : pas de suite */
            if (c == I_RENDRE) {
                if (p != 1)
                    ok = refuser(erreur, grym_formater("RENDRE sans exactement une valeur (octet %lu).",
                                                       (unsigned long)d));
                continue;
            }
            long q = p + effet;
            size_t suites[2];
            int ns = 0;
            if (c == I_SAUTER) {
                suites[ns++] = lire_u32(&b->code[d + 1]);
            } else {
                suites[ns++] = d + instruction_taille(c);
                if (c == I_SAUTER_SI_FAUX || c == I_ESSAYER) suites[ns++] = lire_u32(&b->code[d + 1]);
            }
            for (int k = 0; k < ns && ok; k++) {
                size_t s = suites[k];
                /* ESSAYER : le bloc « En cas d'échec » commence avec le motif sur la pile */
                long qk = c == I_ESSAYER && k == 1 ? q + 1 : q;
                if (s >= n) {
                    ok = refuser(erreur, grym_formater("le code se termine sans RETOUR (octet %lu).",
                                                       (unsigned long)d));
                } else if (prof[s] < 0) {
                    prof[s] = qk;
                    travail[nt++] = s;
                } else if (prof[s] != qk) {
                    ok = refuser(erreur, grym_formater("profondeur de pile incohérente à l'octet %lu.",
                                                       (unsigned long)s));
                }
            }
        }
    }
    free(prof);
    free(travail);
    free(debut_instr);
    if (!ok) return 0;
    for (size_t i = 0; i < b->nb_constantes; i++) {
        const Constante *k = &b->constantes[i];
        if (k->type == C_NOMBRE && !dec_canonique_valide(k->texte))
            return refuser(erreur, grym_formater("constante %lu : nombre mal formé.", (unsigned long)i));
        long jours;
        if (k->type == C_DATE && !date_lire_iso(k->texte, &jours))
            return refuser(erreur, grym_formater("constante %lu : date invalide.", (unsigned long)i));
        if (k->type == C_BOOLEEN && strcmp(k->texte, "vrai") != 0 && strcmp(k->texte, "faux") != 0)
            return refuser(erreur, grym_formater("constante %lu : booléen mal formé.", (unsigned long)i));
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Fichier .grymb (docs/vm.md, § 11)                                */
/* ---------------------------------------------------------------- */

#define VERSION_FORMAT 20  /* versions 1 à 19 restent lisibles : un seul bloc (1, 2), sans classes (3),
                              sans héritage (4), sans méthodes (5), sans aptitudes (6), sans dates (7),
                              sans fichiers (8), sans entités (9), sans base (10), sans recherche (11),
                              sans valeur de départ (12), sans champ facultatif (13), sans corbeille (14),
                              sans champ multiple (15), sans question (16),
                              sans mise en forme (17), sans effacement de l'écran (18),
                              sans essai (19) */

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

static void ecrire_corps(Octets *o, const Bloc *b) {
    ecrire_u32(o, (uint32_t)b->nb_constantes);
    for (size_t i = 0; i < b->nb_constantes; i++) {
        ecrire_u8(o, b->constantes[i].type);
        ecrire_chaine(o, b->constantes[i].texte);
    }
    ecrire_u32(o, (uint32_t)b->nb_noms);
    for (size_t i = 0; i < b->nb_noms; i++) ecrire_chaine(o, b->noms[i]);
    ecrire_u32(o, (uint32_t)b->taille_code);
    ecrire_octets(o, b->code, b->taille_code);
    ecrire_u32(o, (uint32_t)b->nb_positions);
    for (size_t i = 0; i < b->nb_positions; i++) {
        ecrire_u32(o, b->positions[i].decalage);
        ecrire_u32(o, b->positions[i].ligne);
        ecrire_u32(o, b->positions[i].colonne);
    }
}

unsigned char *module_serialiser(const Module *m, size_t *taille) {
    Octets o = { NULL, 0, 0 };
    ecrire_octets(&o, "GRYM", 4);
    ecrire_u16(&o, VERSION_FORMAT);
    ecrire_u32(&o, (uint32_t)m->nb);
    for (size_t k = 0; k < m->nb; k++) {
        const Bloc *b = m->blocs[k];
        ecrire_chaine(&o, b->nom ? b->nom : "");
        ecrire_chaine(&o, b->classe ? b->classe : "");
        ecrire_u8(&o, (unsigned)b->sorte);
        ecrire_u16(&o, (unsigned)b->nb_parametres);
        ecrire_u16(&o, (unsigned)b->nb_locaux);
        ecrire_corps(&o, b);
    }
    ecrire_u32(&o, (uint32_t)m->nb_classes);
    for (size_t k = 0; k < m->nb_classes; k++) {
        const ClasseModule *c = &m->classes[k];
        ecrire_chaine(&o, c->nom);
        ecrire_u8(&o, (c->feminin ? 1u : 0u) | (c->aptitude ? 2u : 0u) | (c->conserve ? 4u : 0u));
        ecrire_chaine(&o, c->parent ? c->parent : "");
        ecrire_chaine(&o, c->pluriel ? c->pluriel : "");
        ecrire_u32(&o, (uint32_t)c->nb_aptitudes);
        for (size_t q = 0; q < c->nb_aptitudes; q++) ecrire_chaine(&o, c->aptitudes[q]);
        ecrire_u32(&o, (uint32_t)c->nb_champs);
        for (size_t q = 0; q < c->nb_champs; q++) {
            ecrire_chaine(&o, c->champs[q]);
            ecrire_chaine(&o, c->types && c->types[q] ? c->types[q] : "");
            ecrire_u8(&o, c->uniques ? c->uniques[q] : 0u);
            ecrire_chaine(&o, c->departs && c->departs[q] ? c->departs[q] : "");
        }
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

/* Constantes, noms, code et positions d'un bloc. Renvoie NULL ou un message d'erreur. */
static char *lire_corps(Lecture *l, Bloc *b, uint32_t version) {
    size_t taille = l->n;
    uint32_t nc = lire_u(l, 4);
    if (l->echec || nc > (taille - l->pos) / 5) return grym_dupliquer("table des constantes tronquée.");
    b->constantes = grym_allouer((nc ? nc : 1) * sizeof *b->constantes);
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t type = lire_u(l, 1);
        char *t = lire_chaine(l);
        if (!t || (type != C_NOMBRE && type != C_TEXTE && type != C_BOOLEEN && (type != C_DATE || version < 8)
                   && (type != C_RECHERCHE || version < 12))) {
            free(t);
            return grym_formater("constante %u illisible.", (unsigned)i);
        }
        b->constantes[i].type = (TypeConstante)type;
        b->constantes[i].texte = t;
        b->nb_constantes++;
    }
    uint32_t nn = lire_u(l, 4);
    if (l->echec || nn > (taille - l->pos) / 4) return grym_dupliquer("table des noms tronquée.");
    b->noms = grym_allouer((nn ? nn : 1) * sizeof *b->noms);
    for (uint32_t i = 0; i < nn; i++) {
        char *t = lire_chaine(l);
        if (!t || !*t) {
            free(t);
            return grym_formater("nom %u illisible.", (unsigned)i);
        }
        b->noms[i] = t;
        b->nb_noms++;
    }
    uint32_t tc = lire_u(l, 4);
    if (!reste(l, tc)) return grym_dupliquer("code tronqué.");
    b->code = grym_allouer(tc ? tc : 1);
    memcpy(b->code, l->d + l->pos, tc);
    b->taille_code = b->cap_code = tc;
    l->pos += tc;
    uint32_t np = lire_u(l, 4);
    if (l->echec || np > (taille - l->pos) / 12) return grym_dupliquer("table des positions tronquée.");
    b->positions = grym_allouer((np ? np : 1) * sizeof *b->positions);
    for (uint32_t i = 0; i < np; i++) {
        b->positions[i].decalage = lire_u(l, 4);
        b->positions[i].ligne = lire_u(l, 4);
        b->positions[i].colonne = lire_u(l, 4);
    }
    b->nb_positions = b->cap_positions = np;
    if (l->echec) return grym_dupliquer("table des positions tronquée.");
    return NULL;
}

static Module *echec_module(Module *m, char **erreur, char *detail) {
    module_detruire(m);
    *erreur = grym_formater("Fichier .grymb invalide : %s", detail);
    free(detail);
    return NULL;
}

Module *module_lire(const unsigned char *donnees, size_t taille, char **erreur) {
    Lecture l = { donnees, taille, 0, 0 };
    Module *m = module_creer();
    if (!est_fichier_bytecode(donnees, taille))
        return echec_module(m, erreur, grym_dupliquer("en-tête « GRYM » absent."));
    l.pos = 4;
    uint32_t version = lire_u(&l, 2);
    if (!l.echec && (version < 1 || version > VERSION_FORMAT))
        return echec_module(m, erreur, grym_formater(
            "format version %u, cette version de grym lit les versions 1 à %d.", (unsigned)version, VERSION_FORMAT));
    if (l.echec) return echec_module(m, erreur, grym_dupliquer("en-tête tronqué."));

    uint32_t nb = 1;
    if (version >= 3) {
        nb = lire_u(&l, 4);
        if (l.echec || nb == 0 || nb > (taille - l.pos) / 25)
            return echec_module(m, erreur, grym_dupliquer("table des blocs tronquée."));
    }
    for (uint32_t k = 0; k < nb; k++) {
        Bloc *b = bloc_creer();
        module_ajouter(m, b);
        if (version >= 3) {
            char *nom = lire_chaine(&l);
            char *classe = version >= 6 ? lire_chaine(&l) : grym_dupliquer("");
            if (classe && *classe) b->classe = classe;
            else free(classe);
            uint32_t sorte = lire_u(&l, 1);
            b->nb_parametres = (int)lire_u(&l, 2);
            b->nb_locaux = (int)lire_u(&l, 2);
            if (!nom || l.echec || sorte > B_ACTION) {
                free(nom);
                return echec_module(m, erreur, grym_formater("en-tête du bloc %u illisible.", (unsigned)k));
            }
            b->sorte = (SorteBloc)sorte;
            if (*nom) b->nom = nom;
            else free(nom);
        }
        char *detail = lire_corps(&l, b, version);
        if (detail) return echec_module(m, erreur, detail);
    }
    if (version >= 4) {
        uint32_t nc = lire_u(&l, 4);
        if (l.echec || nc > (taille - l.pos) / 9)
            return echec_module(m, erreur, grym_dupliquer("table des classes tronquée."));
        for (uint32_t k = 0; k < nc; k++) {
            char *nom = lire_chaine(&l);
            uint32_t fem = lire_u(&l, 1);
            char *parent = version >= 5 ? lire_chaine(&l) : grym_dupliquer("");
            char *pluriel = version >= 10 ? lire_chaine(&l) : grym_dupliquer("");
            uint32_t nap = version >= 7 ? lire_u(&l, 4) : 0;
            if (!nom || !*nom || !parent || !pluriel || l.echec || fem > 7 || nap > (taille - l.pos) / 4) {
                free(nom);
                free(parent);
                free(pluriel);
                return echec_module(m, erreur, grym_formater("classe %u illisible.", (unsigned)k));
            }
            ClasseModule *c = module_ajouter_classe(m, nom, (int)(fem & 1));
            c->aptitude = (fem & 2) != 0;
            c->conserve = (fem & 4) != 0;
            if (*pluriel) c->pluriel = pluriel;
            else free(pluriel);
            if (*parent) c->parent = parent;
            else free(parent);
            free(nom);
            for (uint32_t q = 0; q < nap; q++) {
                char *ap = lire_chaine(&l);
                if (!ap || !*ap) {
                    free(ap);
                    return echec_module(m, erreur, grym_formater("aptitude %u de la classe %u illisible.",
                                                                 (unsigned)q, (unsigned)k));
                }
                classe_ajouter_aptitude(c, ap);
                free(ap);
            }
            uint32_t nch = lire_u(&l, 4);
            if (l.echec || nch > (taille - l.pos) / 4)
                return echec_module(m, erreur, grym_formater("classe %u illisible.", (unsigned)k));
            for (uint32_t q = 0; q < nch; q++) {
                char *ch = lire_chaine(&l);
                if (!ch || !*ch) {
                    free(ch);
                    return echec_module(m, erreur, grym_formater("champ %u de la classe %u illisible.",
                                                                 (unsigned)q, (unsigned)k));
                }
                classe_ajouter_champ(c, ch);
                free(ch);
                if (version >= 10) {
                    char *type = lire_chaine(&l);
                    uint32_t unique = lire_u(&l, 1);
                    if (!type || l.echec || unique > (version >= 16 ? 15u : version >= 15 ? 7u : version >= 14 ? 3u : 1u)) {
                        free(type);
                        return echec_module(m, erreur, grym_formater("type du champ %u de la classe %u illisible.",
                                                                     (unsigned)q, (unsigned)k));
                    }
                    classe_typer_dernier_champ(c, *type ? type : NULL, (int)(unique & 1));
                    if (unique & 2) classe_facultatif_dernier_champ(c);
                    if (unique & 4) classe_cascade_dernier_champ(c);
                    if (unique & 8) classe_multiple_dernier_champ(c);
                    free(type);
                    if (version >= 13) {
                        char *depart = lire_chaine(&l);
                        if (!depart) return echec_module(m, erreur, grym_formater("valeur de départ illisible (classe %u).",
                                                                                   (unsigned)k));
                        if (*depart) classe_depart_dernier_champ(c, depart);
                        free(depart);
                    }
                }
            }
        }
    }
    if (l.pos != taille) return echec_module(m, erreur, grym_dupliquer("octets en trop à la fin du fichier."));
    char *detail = NULL;
    if (!module_verifier(m, &detail)) return echec_module(m, erreur, detail);
    return m;
}

/* ---------------------------------------------------------------- */
/* Modules                                                          */
/* ---------------------------------------------------------------- */

Module *module_creer(void) {
    Module *m = grym_allouer(sizeof *m);
    m->blocs = NULL;
    m->nb = 0;
    m->classes = NULL;
    m->nb_classes = 0;
    return m;
}

void module_ajouter(Module *m, Bloc *b) {
    m->blocs = agrandir(m->blocs, m->nb + 1, sizeof *m->blocs);
    m->blocs[m->nb++] = b;
}

ClasseModule *module_ajouter_classe(Module *m, const char *nom, int feminin) {
    m->classes = agrandir(m->classes, m->nb_classes + 1, sizeof *m->classes);
    ClasseModule *c = &m->classes[m->nb_classes++];
    c->nom = grym_dupliquer(nom);
    c->feminin = feminin;
    c->parent = NULL;
    c->conserve = 0;
    c->pluriel = NULL;
    c->types = NULL;
    c->uniques = NULL;
    c->departs = NULL;
    c->aptitude = 0;
    c->aptitudes = NULL;
    c->nb_aptitudes = 0;
    c->champs = NULL;
    c->nb_champs = 0;
    return c;
}

void classe_ajouter_aptitude(ClasseModule *c, const char *aptitude) {
    c->aptitudes = agrandir(c->aptitudes, c->nb_aptitudes + 1, sizeof *c->aptitudes);
    c->aptitudes[c->nb_aptitudes++] = grym_dupliquer(aptitude);
}

void classe_ajouter_champ(ClasseModule *c, const char *champ) {
    c->champs = agrandir(c->champs, c->nb_champs + 1, sizeof *c->champs);
    c->types = agrandir(c->types, c->nb_champs + 1, sizeof *c->types);
    c->uniques = agrandir(c->uniques, c->nb_champs + 1, sizeof *c->uniques);
    c->departs = agrandir(c->departs, c->nb_champs + 1, sizeof *c->departs);
    c->departs[c->nb_champs] = NULL;
    c->types[c->nb_champs] = NULL;
    c->uniques[c->nb_champs] = 0;
    c->champs[c->nb_champs++] = grym_dupliquer(champ);
}

void classe_depart_dernier_champ(ClasseModule *c, const char *depart) {
    if (!c->nb_champs) return;
    free(c->departs[c->nb_champs - 1]);
    c->departs[c->nb_champs - 1] = depart ? grym_dupliquer(depart) : NULL;
}

void classe_cascade_dernier_champ(ClasseModule *c) {
    if (c->nb_champs) c->uniques[c->nb_champs - 1] |= 4;
}

void classe_multiple_dernier_champ(ClasseModule *c) {
    if (c->nb_champs) c->uniques[c->nb_champs - 1] |= 8;
}

void classe_facultatif_dernier_champ(ClasseModule *c) {
    if (c->nb_champs) c->uniques[c->nb_champs - 1] |= 2;
}

void classe_typer_dernier_champ(ClasseModule *c, const char *type, int unique) {
    if (!c->nb_champs) return;
    free(c->types[c->nb_champs - 1]);
    c->types[c->nb_champs - 1] = type ? grym_dupliquer(type) : NULL;
    c->uniques[c->nb_champs - 1] = (unsigned char)((c->uniques[c->nb_champs - 1] & 14) | (unique != 0));
}

void module_detruire(Module *m) {
    if (!m) return;
    for (size_t k = 0; k < m->nb; k++) bloc_detruire(m->blocs[k]);
    free(m->blocs);
    for (size_t k = 0; k < m->nb_classes; k++) {
        free(m->classes[k].nom);
        free(m->classes[k].parent);
        free(m->classes[k].pluriel);
        for (size_t q = 0; q < m->classes[k].nb_champs; q++) {
            free(m->classes[k].types[q]);
            free(m->classes[k].departs[q]);
        }
        free(m->classes[k].departs);
        free(m->classes[k].types);
        free(m->classes[k].uniques);
        for (size_t q = 0; q < m->classes[k].nb_aptitudes; q++) free(m->classes[k].aptitudes[q]);
        free(m->classes[k].aptitudes);
        for (size_t q = 0; q < m->classes[k].nb_champs; q++) free(m->classes[k].champs[q]);
        free(m->classes[k].champs);
    }
    free(m->classes);
    free(m);
}

/* Bloc 0 : le programme ; les suivants : des formules nommées, sans doublon. */
int module_verifier(const Module *m, char **erreur) {
    if (m->nb == 0 || !m->blocs[0] || m->blocs[0]->sorte != B_PROGRAMME || m->blocs[0]->nom)
        return refuser(erreur, grym_dupliquer("le premier bloc doit être le programme."));
    for (size_t k = 0; k < m->nb; k++) {
        const Bloc *b = m->blocs[k];
        if (k > 0) {
            if (b->sorte == B_PROGRAMME || !b->nom)
                return refuser(erreur, grym_formater("bloc %lu : une formule doit avoir un nom.", (unsigned long)k));
            for (size_t q = 1; q < k; q++) {
                const Bloc *o = m->blocs[q];
                if (strcmp(o->nom, b->nom) != 0) continue;
                /* plusieurs versions : chacune a une classe différente, même sorte, même nombre de paramètres */
                if (!o->classe || !b->classe || strcmp(o->classe, b->classe) == 0)
                    return refuser(erreur, grym_formater("formule « %s » définie deux fois.", b->nom));
                if (o->sorte != b->sorte || o->nb_parametres != b->nb_parametres)
                    return refuser(erreur, grym_formater("versions incompatibles de « %s ».", b->nom));
            }
            if (b->classe && b->nb_parametres < 1)
                return refuser(erreur, grym_formater("méthode « %s » sans paramètre.", b->nom));
        }
        char *detail = NULL;
        if (!bloc_verifier(b, &detail)) {
            *erreur = b->nom ? grym_formater("formule « %s » : %s", b->nom, detail) : grym_dupliquer(detail);
            free(detail);
            return 0;
        }
    }
    for (size_t k = 0; k < m->nb_classes; k++) {
        const ClasseModule *c = &m->classes[k];
        for (size_t q = 0; q < k; q++)
            if (strcmp(m->classes[q].nom, c->nom) == 0)
                return refuser(erreur, grym_formater("classe « %s » définie deux fois.", c->nom));
        for (size_t a = 0; a < c->nb_champs; a++)
            for (size_t q = 0; q < a; q++)
                if (strcmp(c->champs[q], c->champs[a]) == 0)
                    return refuser(erreur, grym_formater("champ « %s » déclaré deux fois dans « %s ».",
                                                         c->champs[a], c->nom));
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Désassemblage (docs/vm.md, § 12)                                  */
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
        CodeInstruction code = (CodeInstruction)b->code[ip];
        size_t t = instruction_taille(code);
        if (ip + t > b->taille_code) t = b->taille_code - ip;
        ip += t;
        int ligne, colonne;
        bloc_position(b, debut, &ligne, &colonne);
        char marge[32];
        if (ligne != ligne_prec && ligne > 0) {
            snprintf(marge, sizeof marge, "%4d  %04lu  ", ligne, (unsigned long)debut);
            ligne_prec = ligne;
        } else {
            snprintf(marge, sizeof marge, "      %04lu  ", (unsigned long)debut);
        }
        chaine_ajouter(&c, marge);
        if (t == 1) {
            chaine_ajouter(&c, instruction_nom(code));
            chaine_ajouter(&c, "\n");
            continue;
        }
        completer(&c, instruction_nom(code), 18);
        int saut = est_saut(code);
        unsigned long op = saut ? lire_u32(&b->code[debut + 1])
                                : (unsigned long)(b->code[debut + 1] | (b->code[debut + 2] << 8));
        char nombre[16];
        snprintf(nombre, sizeof nombre, saut ? "%04lu" : "%lu", op);
        completer(&c, nombre, 6);
        char *commentaire = NULL;
        if ((code == I_CONSTANTE || code == I_ECHOUER || code == I_CHERCHER) && op < b->nb_constantes) {
            const Constante *k = &b->constantes[op];
            if (k->type == C_NOMBRE) {
                Decimal d = dec_depuis_canonique(k->texte);
                commentaire = dec_formater(&d);
                dec_liberer(&d);
            } else if (k->type == C_BOOLEEN) {
                commentaire = grym_dupliquer(k->texte);
            } else if (k->type == C_DATE) {
                long j = 0;
                commentaire = date_lire_iso(k->texte, &j) ? date_suisse(j) : grym_dupliquer(k->texte);
            } else if (k->type == C_RECHERCHE) {
                Chaine c = {0};
                for (const char *p = k->texte; *p; p++) {
                    char t[2] = { *p, 0 };
                    chaine_ajouter(&c, *p == '\x1f' ? " | " : t);
                }
                commentaire = chaine_rendre(&c);
            } else {
                commentaire = grym_formater("« %s »", k->texte);
            }
        } else if ((code == I_LIRE || code == I_ECRIRE || code == I_NOUVEAU || code == I_INITIALISER_CHAMP
                    || code == I_LIRE_CHAMP || code == I_ECRIRE_CHAMP || code == I_GAGNER || code == I_PERDRE || code == I_DEMANDER) && op < b->nb_noms) {
            commentaire = grym_dupliquer(b->noms[op]);
        } else if (code == I_APPELER && op < b->nb_noms) {
            unsigned na = b->code[debut + 3];
            commentaire = grym_formater("%s (%u argument%s%s)", b->noms[op], na, na > 1 ? "s" : "",
                                        b->code[debut + 4] ? ", rend une valeur" : "");
        } else if ((code == I_LIRE_LOCAL || code == I_ECRIRE_LOCAL) && (int)op < b->nb_parametres) {
            commentaire = grym_formater("paramètre %lu", op + 1);
        }
        if (commentaire) {
            chaine_ajouter(&c, "; ");
            chaine_ajouter(&c, commentaire);
            free(commentaire);
        }
        while (c.n && c.d[c.n - 1] == ' ') c.d[--c.n] = '\0';
        chaine_ajouter(&c, "\n");
    }
    return chaine_rendre(&c);
}

char *module_desassembler(const Module *m) {
    Chaine c = {0};
    for (size_t k = 0; k < m->nb; k++) {
        const Bloc *b = m->blocs[k];
        if (m->nb > 1) {
            char *titre;
            if (b->sorte == B_PROGRAMME) {
                titre = grym_dupliquer("Programme\n");
            } else {
                char *pour = b->classe ? grym_formater(" pour « %s »", b->classe) : grym_dupliquer("");
                titre = grym_formater("%s« %s »%s : %d paramètre%s, %d case%s locale%s\n",
                                      k > 0 ? "\n" : "",
                                      b->nom, pour, b->nb_parametres, b->nb_parametres > 1 ? "s" : "",
                                      b->nb_locaux, b->nb_locaux > 1 ? "s" : "", b->nb_locaux > 1 ? "s" : "");
                free(pour);
                char *t2 = grym_formater("%s%s", b->sorte == B_CALCUL ? "Calcul " : "Action ", titre + (k > 0));
                free(titre);
                titre = grym_formater("%s%s", k > 0 ? "\n" : "", t2);
                free(t2);
            }
            chaine_ajouter(&c, titre);
            free(titre);
        }
        char *texte = bloc_desassembler(b);
        chaine_ajouter(&c, texte);
        free(texte);
    }
    return chaine_rendre(&c);
}
