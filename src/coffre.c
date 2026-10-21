/* Le coffre : voir coffre.h. */
#include "coffre.h"
#include "empreinte.h"
#include "texte.h"
#include "../vendor/monocypher/monocypher.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char MAGIE[12] = { 'G', 'R', 'Y', 'M', 'C', 'O', 'F', 'F', 'R', 'E', '1', '\n' };
#define BLOCS 65536u   /* 64 Mio : le second jeu de paramètres conseillé par la RFC 9106, § 4 */
#define PASSES 3u

static void ecrire32(unsigned char *o, uint32_t v) {
    for (int k = 0; k < 4; k++) o[k] = (unsigned char)(v >> (8 * k));
}

static uint32_t lire32(const unsigned char *o) {
    return (uint32_t)o[0] | (uint32_t)o[1] << 8 | (uint32_t)o[2] << 16 | (uint32_t)o[3] << 24;
}

int coffre_est(const unsigned char *o, size_t n) {
    return n >= COFFRE_ENTETE && memcmp(o, MAGIE, sizeof MAGIE) == 0;
}

int coffre_cle(const char *mdp, const unsigned char sel[16], unsigned long blocs, unsigned long passes, unsigned char cle[32]) {
    void *travail = malloc((size_t)blocs * 1024);
    if (!travail) return 0;
    crypto_argon2_config c = { CRYPTO_ARGON2_ID, (uint32_t)blocs, (uint32_t)passes, 1 };
    crypto_argon2_inputs e = { (const uint8_t *)mdp, sel, (uint32_t)strlen(mdp), 16 };
    crypto_argon2(cle, 32, travail, c, e, crypto_argon2_no_extras);
    crypto_wipe(travail, (size_t)blocs * 1024);
    free(travail);
    return 1;
}

unsigned char *coffre_fermer(const unsigned char *clair, size_t n, const char *mdp, size_t *taille, char **erreur) {
    if (!mdp || !*mdp) { *erreur = grym_dupliquer("Un coffre se ferme avec un mot de passe : celui-ci est vide."); return NULL; }
    unsigned char *r = malloc(COFFRE_ENTETE + n);
    if (!r) { *erreur = grym_dupliquer("Mémoire insuffisante pour chiffrer la sauvegarde."); return NULL; }
    memcpy(r, MAGIE, sizeof MAGIE);
    ecrire32(r + 12, BLOCS);
    ecrire32(r + 16, PASSES);
    unsigned char *sel = r + 20, *nonce = r + 36, *mac = r + 60;
    if (!aleatoire_systeme(sel, 16) || !aleatoire_systeme(nonce, 24)) {
        free(r);
        *erreur = grym_dupliquer("Aucun générateur aléatoire du système : la sauvegarde ne peut pas être chiffrée.");
        return NULL;
    }
    unsigned char cle[32];
    if (!coffre_cle(mdp, sel, BLOCS, PASSES, cle)) {
        free(r);
        *erreur = grym_dupliquer("Mémoire insuffisante pour la clé (64 Mio).");
        return NULL;
    }
    crypto_aead_lock(r + COFFRE_ENTETE, mac, cle, nonce, r, 60, clair, n);
    crypto_wipe(cle, sizeof cle);
    *taille = COFFRE_ENTETE + n;
    return r;
}

unsigned char *coffre_ouvrir(const unsigned char *coffre, size_t n, const char *mdp, size_t *taille, char **erreur) {
    if (!coffre_est(coffre, n)) { *erreur = grym_dupliquer("Ce fichier n'est pas une sauvegarde chiffrée par GrymoiR."); return NULL; }
    const uint32_t blocs = lire32(coffre + 12), passes = lire32(coffre + 16);
    if (blocs < 8 || blocs > (1u << 21) || passes < 1 || passes > 16) {   /* au plus 2 Gio : un en-tête absurde est refusé */
        *erreur = grym_dupliquer("Cette sauvegarde chiffrée est abîmée.");
        return NULL;
    }
    unsigned char cle[32];
    if (!coffre_cle(mdp ? mdp : "", coffre + 20, blocs, passes, cle)) {
        *erreur = grym_dupliquer("Mémoire insuffisante pour la clé de la sauvegarde.");
        return NULL;
    }
    const size_t m = n - COFFRE_ENTETE;
    unsigned char *r = malloc(m ? m : 1);
    if (!r) { crypto_wipe(cle, sizeof cle); *erreur = grym_dupliquer("Mémoire insuffisante pour déchiffrer."); return NULL; }
    const int faux = crypto_aead_unlock(r, coffre + 60, cle, coffre + 36, coffre, 60, coffre + COFFRE_ENTETE, m);
    crypto_wipe(cle, sizeof cle);
    if (faux) {
        free(r);
        *erreur = grym_dupliquer("Mot de passe faux, ou sauvegarde abîmée : elle ne s'ouvre pas.");
        return NULL;
    }
    *taille = m;
    return r;
}

/* ---- Les clés d'une base chiffrée et le coffre de format 2 (21 octobre 2026) ---- */

static const char MAGIE_CLES[10] = { 'G', 'R', 'Y', 'M', 'C', 'L', 'E', 'S', '1', '\n' };
static const char MAGIE2[12] = { 'G', 'R', 'Y', 'M', 'C', 'O', 'F', 'F', 'R', 'E', '2', '\n' };

/* Une enveloppe de mot de passe, écrite en `e` (97 octets) : la clé de la base, enfermée sous `mdp`. */
static int envelopper(unsigned char *e, const char *mdp, const unsigned char cle[32], char **erreur) {
    e[0] = 1;
    ecrire32(e + 1, BLOCS);
    ecrire32(e + 5, PASSES);
    unsigned char *sel = e + 9, *nonce = e + 25, *mac = e + 49, *enveloppee = e + 65;
    if (!aleatoire_systeme(sel, 16) || !aleatoire_systeme(nonce, 24)) {
        *erreur = grym_dupliquer("Aucun générateur aléatoire du système : la base ne peut pas être chiffrée.");
        return 0;
    }
    unsigned char k[32];
    if (!coffre_cle(mdp, sel, BLOCS, PASSES, k)) { *erreur = grym_dupliquer("Mémoire insuffisante pour la clé (64 Mio)."); return 0; }
    unsigned char ad[10 + 49];
    memcpy(ad, MAGIE_CLES, 10);
    memcpy(ad + 10, e, 49);
    crypto_aead_lock(enveloppee, mac, k, nonce, ad, sizeof ad, cle, 32);
    crypto_wipe(k, sizeof k);
    return 1;
}

/* La longueur d'une enveloppe de sorte `s`, 0 si inconnue. */
static size_t longueur_enveloppe(unsigned s) { return s == 1 ? CLES_ENVELOPPE_MDP : 0; }

/* Vrai si le fichier des clés est bien formé : la magie, puis des enveloppes de longueur connue, jusqu'au bout. */
static int cles_bien_formees(const unsigned char *c, size_t n) {
    if (n < 11 || memcmp(c, MAGIE_CLES, 10) != 0) return 0;
    size_t p = 11;
    for (unsigned k = 0; k < c[10]; k++) {
        if (p >= n) return 0;
        const size_t l = longueur_enveloppe(c[p]);
        if (!l || p + l > n) return 0;
        p += l;
    }
    return p == n;
}

int cles_creer(const char *mdp, unsigned char cle[32], unsigned char **cles, size_t *n, char **erreur) {
    if (!mdp || !*mdp) { *erreur = grym_dupliquer("Le mot de passe de la base est vide."); return 0; }
    if (!aleatoire_systeme(cle, 32)) {
        *erreur = grym_dupliquer("Aucun générateur aléatoire du système : la base ne peut pas être chiffrée.");
        return 0;
    }
    unsigned char *c = malloc(11 + CLES_ENVELOPPE_MDP);
    if (!c) { *erreur = grym_dupliquer("Mémoire insuffisante pour les clés."); return 0; }
    memcpy(c, MAGIE_CLES, 10);
    c[10] = 1;
    if (!envelopper(c + 11, mdp, cle, erreur)) { free(c); crypto_wipe(cle, 32); return 0; }
    *cles = c;
    *n = 11 + CLES_ENVELOPPE_MDP;
    return 1;
}

int cles_ouvrir(const unsigned char *c, size_t n, const char *mdp, unsigned char cle[32], char **erreur) {
    if (!cles_bien_formees(c, n)) { *erreur = grym_dupliquer("Le fichier des clés de la base est abîmé."); return 0; }
    size_t p = 11;
    for (unsigned k = 0; k < c[10]; p += longueur_enveloppe(c[p]), k++) {
        if (c[p] != 1) continue;
        const unsigned char *e = c + p;
        const uint32_t blocs = lire32(e + 1), passes = lire32(e + 5);
        if (blocs < 8 || blocs > (1u << 21) || passes < 1 || passes > 16) continue;
        unsigned char kk[32];
        if (!coffre_cle(mdp ? mdp : "", e + 9, blocs, passes, kk)) {
            *erreur = grym_dupliquer("Mémoire insuffisante pour la clé de la base.");
            return 0;
        }
        unsigned char ad[10 + 49];
        memcpy(ad, MAGIE_CLES, 10);
        memcpy(ad + 10, e, 49);
        const int faux = crypto_aead_unlock(cle, e + 49, kk, e + 25, ad, sizeof ad, e + 65, 32);
        crypto_wipe(kk, sizeof kk);
        if (!faux) return 1;
    }
    *erreur = grym_dupliquer("Mot de passe faux.");
    return 0;
}

unsigned char *cles_changer(const unsigned char *c, size_t n, const char *ancien, const char *nouveau, size_t *taille,
                            char **erreur) {
    unsigned char cle[32];
    if (!cles_ouvrir(c, n, ancien, cle, erreur)) return NULL;
    if (!nouveau || !*nouveau) { crypto_wipe(cle, 32); *erreur = grym_dupliquer("Le nouveau mot de passe est vide."); return NULL; }
    unsigned char *r = malloc(n);
    if (!r) { crypto_wipe(cle, 32); *erreur = grym_dupliquer("Mémoire insuffisante pour les clés."); return NULL; }
    memcpy(r, c, n);
    /* chaque enveloppe de mot de passe est refaite sous le nouveau ; les autres (le secours) restent telles quelles */
    size_t p = 11;
    int ok = 1;
    for (unsigned k = 0; ok && k < r[10]; p += longueur_enveloppe(r[p]), k++)
        if (r[p] == 1) ok = envelopper(r + p, nouveau, cle, erreur);
    crypto_wipe(cle, 32);
    if (!ok) { free(r); return NULL; }
    *taille = n;
    return r;
}

int coffre2_est(const unsigned char *o, size_t n) { return n >= 16 && memcmp(o, MAGIE2, 12) == 0; }

unsigned char *coffre2_fermer(const unsigned char *clair, size_t n, const unsigned char *cles, size_t ncles,
                              const unsigned char cle[32], size_t *taille, char **erreur) {
    const size_t tete = 12 + 4 + ncles + 24 + 16;
    unsigned char *r = malloc(tete + n);
    if (!r) { *erreur = grym_dupliquer("Mémoire insuffisante pour chiffrer la sauvegarde."); return NULL; }
    memcpy(r, MAGIE2, 12);
    ecrire32(r + 12, (uint32_t)ncles);
    memcpy(r + 16, cles, ncles);
    unsigned char *nonce = r + 16 + ncles, *mac = nonce + 24;
    if (!aleatoire_systeme(nonce, 24)) {
        free(r);
        *erreur = grym_dupliquer("Aucun générateur aléatoire du système : la sauvegarde ne peut pas être chiffrée.");
        return NULL;
    }
    crypto_aead_lock(r + tete, mac, cle, nonce, r, (size_t)(mac - r), clair, n);
    *taille = tete + n;
    return r;
}

unsigned char *coffre2_ouvrir(const unsigned char *o, size_t n, const char *mdp, size_t *taille, unsigned char cle[32],
                              unsigned char **cles, size_t *ncles, char **erreur) {
    if (!coffre2_est(o, n)) { *erreur = grym_dupliquer("Ce fichier n'est pas une sauvegarde d'une base chiffrée."); return NULL; }
    const size_t nc = lire32(o + 12);
    if (nc > n || 16 + nc + 40 > n) { *erreur = grym_dupliquer("Cette sauvegarde chiffrée est abîmée."); return NULL; }
    unsigned char k[32];
    if (!cles_ouvrir(o + 16, nc, mdp, k, erreur)) {
        if (!strcmp(*erreur, "Mot de passe faux.")) {
            free(*erreur);
            *erreur = grym_dupliquer("Mot de passe faux, ou sauvegarde abîmée : elle ne s'ouvre pas.");
        }
        return NULL;
    }
    const unsigned char *nonce = o + 16 + nc, *mac = nonce + 24, *chiffre = mac + 16;
    const size_t m = n - (size_t)(chiffre - o);
    unsigned char *r = malloc(m ? m : 1);
    if (!r) { crypto_wipe(k, 32); *erreur = grym_dupliquer("Mémoire insuffisante pour déchiffrer."); return NULL; }
    if (crypto_aead_unlock(r, mac, k, nonce, o, (size_t)(mac - o), chiffre, m)) {
        crypto_wipe(k, 32);
        free(r);
        *erreur = grym_dupliquer("Cette sauvegarde chiffrée est abîmée : elle ne s'ouvre pas.");
        return NULL;
    }
    if (cle) memcpy(cle, k, 32);
    crypto_wipe(k, 32);
    if (cles) {
        *cles = malloc(nc ? nc : 1);
        if (*cles) memcpy(*cles, o + 16, nc);
        *ncles = nc;
    }
    *taille = m;
    return r;
}
