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
