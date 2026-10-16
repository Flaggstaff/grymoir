/* Les types de valeur qui sont des textes (grammaire, § 16.1) : « texte », et trois textes à la forme contrôlée,
 * « courriel », « téléphone », « adresse web ». Rangés et comparés comme des textes ; seul leur contrôle diffère. */
#ifndef GRYM_TYPES_H
#define GRYM_TYPES_H

#include <string.h>

static inline int type_textuel(const char *t) {
    return t && (strcmp(t, "texte") == 0 || strcmp(t, "courriel") == 0 || strcmp(t, "téléphone") == 0
                 || strcmp(t, "adresse web") == 0 || strcmp(t, "mot de passe") == 0);
}

/* Un mot de passe (§ 16.1) : rangé en empreinte, jamais en clair ; il ne se lit pas, il se vérifie (« correspond à »). */
static inline int type_secret(const char *t) {
    return t && strcmp(t, "mot de passe") == 0;
}

/* La forme d'un texte d'un de ces types : NULL si elle convient, sinon ce qu'on attendait. Volontairement simple :
 * attraper les fautes de frappe, pas valider tous les cas que les normes admettent. */
static inline const char *forme_refusee(const char *type, const char *v) {
    if (!type || !v) return NULL;
    if (strcmp(type, "courriel") == 0) {   /* un seul @, quelque chose avant, un point après, pas d'espace */
        const char *a = strchr(v, '@');
        if (!a || a == v || strchr(a + 1, '@') || strchr(v, ' ')) return "un courriel (nom@domaine.ch)";
        const char *p = strchr(a + 1, '.');
        if (!p || p == a + 1 || !p[1]) return "un courriel (nom@domaine.ch)";
        return NULL;
    }
    if (strcmp(type, "téléphone") == 0) {   /* des chiffres, espaces, + ( ) - . ; au moins 6 chiffres */
        int chiffres = 0;
        for (const char *p = v; *p; p++) {
            if (*p >= '0' && *p <= '9') chiffres++;
            else if (!strchr(" +()-./", *p)) return "un numéro de téléphone (+41 26 912 34 56)";
        }
        return chiffres >= 6 ? NULL : "un numéro de téléphone (+41 26 912 34 56)";
    }
    if (strcmp(type, "adresse web") == 0) {   /* http:// ou https://, puis quelque chose, sans espace */
        const char *r = strncmp(v, "https://", 8) == 0 ? v + 8 : strncmp(v, "http://", 7) == 0 ? v + 7 : NULL;
        if (!r || !*r || strchr(v, ' ')) return "une adresse web (https://exemple.ch)";
        return NULL;
    }
    return NULL;
}

#endif
