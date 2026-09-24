/* GrymoiR : l'interface par le navigateur, servie en local (v2.0-a).
 * Spécification : docs/v2.md (révision 0.7), § 4, § 5 et § 9 ; docs/vm.md, § 13.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "serveur.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
typedef SOCKET Prise;
#define PRISE_INVALIDE INVALID_SOCKET
#define fermer_prise closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int Prise;
#define PRISE_INVALIDE (-1)
#define fermer_prise close
#endif

#define ENTETES_MAX 8192          /* octets d'en-têtes (§ 9) */
#define CORPS_MAX (50L * 1024 * 1024)   /* octets d'un envoi, fichiers compris (docs/v2.md, § 10) */
#define LIGNES_MAX 1000           /* affichage gardé dans la page */
#define ATTENTE_CLIENT 5          /* secondes : un client muet est congédié */
#define ATTENTE_FIN 2             /* secondes : après la page finale, plus rien n'est demandé */

/* Une image affichée par le programme, à sa place dans le fil. */
struct Image {
    size_t position;      /* octet de l'affichage devant lequel elle se place */
    long id;              /* /image/ID */
    unsigned char *octets;
    size_t taille;
    const char *type;     /* type MIME, d'après la signature */
    char *description;
    char *html;           /* une fiche (grammaire, § 20), déjà échappée, à la place d'une image ; ou NULL */
    int cachee;           /* image d'une fiche : servie, mais montrée par sa fiche */
};

/* Une question en cours : les champs, ce que l'utilisateur a tapé, les refus. */
typedef struct {
    Champ *champs;
    size_t n;
    char **saisi;
    char **refus;
    char *acceptes;
    Validation valider;
    void *vcontexte;
} Question;

struct Serveur {
    int port;
    char jeton[33];
    char cookie[24];      /* « grym_PORT » : les cookies ne sont pas isolés par port */
    char hote[32];        /* « 127.0.0.1:PORT » */
    char origine[40];     /* « http://127.0.0.1:PORT » */
    char adresse[96];
    char *titre;
    Transport t;
    Prise ecoute;
    Prise client;
    int reseau;           /* ouvert par serveur_ouvrir : prises et WSAStartup à rendre ; refus notés sur stderr */
    Chaine affichage;     /* depuis le dernier effacement, plafonné à LIGNES_MAX lignes */
    long numero;          /* numéro de la question courante (décision 6) */
    struct Image *images; /* images du fil, par position croissante (docs/v2.md, § 10) */
    size_t nb_images, cap_images;
    long prochaine_image;
    int fin_proche;       /* page finale servie : on ne sert plus que sa feuille et ses images, puis on s'en va */
    char *avis;           /* message à montrer une fois (réponse à une question close) */
};

/* ---------------------------------------------------------------- */
/* Outils                                                           */
/* ---------------------------------------------------------------- */

static void echapper(Chaine *c, const char *s) {   /* HTML : rien de ce qui est saisi ne devient du HTML */
    char t[2] = { 0, 0 };
    for (; *s; s++) {
        switch (*s) {
        case '&': chaine_ajouter(c, "&amp;"); break;
        case '<': chaine_ajouter(c, "&lt;"); break;
        case '>': chaine_ajouter(c, "&gt;"); break;
        case '"': chaine_ajouter(c, "&quot;"); break;
        case '\'': chaine_ajouter(c, "&#39;"); break;
        default: t[0] = *s; chaine_ajouter(c, t);
        }
    }
}

static int egal_sans_casse(const char *a, const char *b, size_t n) {
    for (size_t k = 0; k < n; k++) {
        char x = a[k], y = b[k];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

/* Comparaison en temps constant : la durée ne dit rien du jeton. */
static int meme_jeton(const char *a, size_t na, const char *b) {
    size_t nb = strlen(b);
    unsigned d = (unsigned)(na ^ nb);
    for (size_t k = 0; k < nb; k++) d |= (unsigned char)(k < na ? a[k] : 0) ^ (unsigned char)b[k];
    return d == 0;
}

static int utf8_valide(const unsigned char *s) {
    while (*s) {
        int n = *s < 0x80 ? 0 : (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || (n == 1 && *s < 0xC2)) return 0;
        s++;
        for (int k = 0; k < n; k++, s++) if ((*s & 0xC0) != 0x80) return 0;
    }
    return 1;
}

/* Décodage « application/x-www-form-urlencoded » d'un morceau. NULL si mal formé. */
static char *decoder(const char *s, size_t n) {
    char *r = grym_allouer(n + 1), *w = r;
    for (size_t k = 0; k < n; k++) {
        if (s[k] == '+') { *w++ = ' '; continue; }
        if (s[k] != '%') { *w++ = s[k]; continue; }
        int h = 0;
        for (int q = 1; q <= 2; q++) {
            char c = k + (size_t)q < n ? s[k + (size_t)q] : 0;
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) { free(r); return NULL; }
            h = h * 16 + v;
        }
        if (h == 0) { free(r); return NULL; }   /* un octet nul couperait le texte */
        *w++ = (char)h;
        k += 2;
    }
    *w = '\0';
    if (!utf8_valide((unsigned char *)r)) { free(r); return NULL; }
    return r;
}



/* Un envoi de formulaire, en « application/x-www-form-urlencoded » ou en « multipart/form-data ». */
typedef struct {
    char *nom;
    char *valeur;          /* texte décodé, ou octets d'un fichier */
    size_t taille;
    int fichier;           /* partie de fichier : nom_fichier dit son nom d'origine */
    int choisi;            /* un fichier a été choisi (le navigateur envoie une partie vide sinon) */
    char *nom_fichier;
    int mal;               /* texte illisible : octet nul ou UTF-8 invalide */
} Partie;

typedef struct {
    Partie *p;
    size_t n, cap;
} Envoi;

static void envoi_ajouter(Envoi *e, Partie x) {
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        Partie *t = grym_allouer(e->cap * sizeof *t);
        if (e->n) memcpy(t, e->p, e->n * sizeof *t);
        free(e->p);
        e->p = t;
    }
    e->p[e->n++] = x;
}

static void envoi_liberer(Envoi *e) {
    for (size_t k = 0; k < e->n; k++) {
        free(e->p[k].nom);
        free(e->p[k].valeur);
        free(e->p[k].nom_fichier);
    }
    free(e->p);
}

static Partie *envoi_partie(Envoi *e, const char *nom) {
    for (size_t k = 0; k < e->n; k++) if (strcmp(e->p[k].nom, nom) == 0) return &e->p[k];
    return NULL;
}

/* Recherche binaire d'une suite d'octets. */
static const char *chercher_octets(const char *d, size_t n, const char *motif, size_t m) {
    if (m == 0 || n < m) return NULL;
    for (size_t k = 0; k + m <= n; k++) if (d[k] == motif[0] && memcmp(d + k, motif, m) == 0) return d + k;
    return NULL;
}

/* Nom d'origine d'un fichier : le dernier segment, sans caractère de contrôle, jamais un chemin. */
static char *nom_de_fichier(const char *brut, size_t n) {
    size_t debut = 0;
    for (size_t k = 0; k < n; k++) if (brut[k] == '/' || brut[k] == '\\') debut = k + 1;
    char *r = grym_allouer(n - debut + 1), *w = r;
    for (size_t k = debut; k < n && (size_t)(w - r) < 255; k++)
        if ((unsigned char)brut[k] >= 0x20 && brut[k] != 0x7F) *w++ = brut[k];
    *w = '\0';
    if (!*r || !utf8_valide((unsigned char *)r) || strcmp(r, ".") == 0 || strcmp(r, "..") == 0) {
        free(r);
        r = grym_dupliquer("fichier");
    }
    return r;
}

/* Valeur d'un attribut d'en-tête : « name="c0" » dans « form-data; name="c0"; filename="a.jpg" ». */
static char *attribut(const char *entete, size_t n, const char *nom, int *present) {
    size_t ln = strlen(nom);
    for (size_t k = 0; k + ln + 2 <= n; k++) {
        if ((k == 0 || entete[k - 1] == ' ' || entete[k - 1] == ';') && strncmp(entete + k, nom, ln) == 0
            && entete[k + ln] == '=' && entete[k + ln + 1] == '"') {
            const char *v = entete + k + ln + 2;
            const char *f = memchr(v, '"', n - (size_t)(v - entete));
            if (!f) return NULL;
            *present = 1;
            return grym_formater("%.*s", (int)(f - v), v);
        }
    }
    return NULL;
}


/* ---------------------------------------------------------------- */
/* Requêtes                                                         */
/* ---------------------------------------------------------------- */

typedef struct {
    char methode[8];
    char *cible;          /* « / », « /?jeton=… », « /reponse », « /style.css » */
    char *hote, *cookie, *origine, *type_contenu;
    const char *corps;
    size_t taille_corps;
} Requete;

static void requete_liberer(Requete *r) {
    free(r->cible);
    free(r->hote);
    free(r->cookie);
    free(r->origine);
    free(r->type_contenu);
}

/* Lit la ligne de requête et les en-têtes utiles. 0 si mal formée. */
static int lire_requete(const char *d, size_t n, Requete *r) {
    memset(r, 0, sizeof *r);
    const char *fin_entetes = NULL;
    for (size_t k = 0; k + 3 < n; k++)
        if (d[k] == '\r' && d[k + 1] == '\n' && d[k + 2] == '\r' && d[k + 3] == '\n') { fin_entetes = d + k; break; }
    if (!fin_entetes) return 0;
    const char *p = d, *eol = strstr(d, "\r\n");
    const char *sp1 = memchr(p, ' ', (size_t)(eol - p));
    if (!sp1 || sp1 - p >= (long)sizeof r->methode) return 0;
    const char *sp2 = memchr(sp1 + 1, ' ', (size_t)(eol - sp1 - 1));
    if (!sp2 || strncmp(sp2 + 1, "HTTP/1.", 7) != 0) return 0;
    memcpy(r->methode, p, (size_t)(sp1 - p));
    r->cible = grym_formater("%.*s", (int)(sp2 - sp1 - 1), sp1 + 1);
    for (p = eol + 2; p < fin_entetes; p = eol + 2) {
        eol = strstr(p, "\r\n");
        const char *dp = memchr(p, ':', (size_t)(eol - p));
        if (!dp) { requete_liberer(r); return 0; }
        size_t ln = (size_t)(dp - p);
        const char *v = dp + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *valeur = grym_formater("%.*s", (int)(eol - v), v);
        char **cible = ln == 4 && egal_sans_casse(p, "host", 4) ? &r->hote
                     : ln == 6 && egal_sans_casse(p, "cookie", 6) ? &r->cookie
                     : ln == 6 && egal_sans_casse(p, "origin", 6) ? &r->origine
                     : ln == 12 && egal_sans_casse(p, "content-type", 12) ? &r->type_contenu : NULL;
        if (cible && *cible) { free(valeur); requete_liberer(r); return 0; }   /* en-tête en double : refus */
        if (cible) *cible = valeur;
        else free(valeur);
    }
    r->corps = fin_entetes + 4;
    r->taille_corps = n - (size_t)(r->corps - d);
    return 1;
}

/* Lit le corps d'un envoi. 0 si mal formé. */
static int lire_envoi(const Requete *r, Envoi *e) {
    memset(e, 0, sizeof *e);
    const char *t = r->type_contenu ? r->type_contenu : "application/x-www-form-urlencoded";
    if (egal_sans_casse(t, "multipart/form-data", strlen("multipart/form-data")) && strlen(t) >= 19) {
        const char *b = strstr(t, "boundary=");
        if (!b) return 0;
        b += 9;
        size_t lb = strcspn(b, "; ");
        if (*b == '"') { b++; lb = strcspn(b, "\""); }
        if (lb == 0 || lb > 70) return 0;
        char *delim = grym_formater("--%.*s", (int)lb, b);
        size_t ld = strlen(delim);
        const char *d = r->corps, *fin = r->corps + r->taille_corps;
        const char *p = chercher_octets(d, (size_t)(fin - d), delim, ld);
        int ok = p != NULL;
        while (ok) {
            p += ld;
            if (fin - p >= 2 && p[0] == '-' && p[1] == '-') break;   /* dernier délimiteur */
            if (fin - p < 2 || p[0] != '\r' || p[1] != '\n') { ok = 0; break; }
            p += 2;
            const char *fe = chercher_octets(p, (size_t)(fin - p), "\r\n\r\n", 4);
            if (!fe) { ok = 0; break; }
            char *sep = grym_formater("\r\n%s", delim);
            const char *fd = chercher_octets(fe + 4, (size_t)(fin - fe - 4), sep, ld + 2);
            free(sep);
            if (!fd) { ok = 0; break; }
            int a_nom = 0, a_fichier = 0;
            char *nom = attribut(p, (size_t)(fe - p), "name", &a_nom);
            char *brut = attribut(p, (size_t)(fe - p), "filename", &a_fichier);
            if (!nom) { free(brut); ok = 0; break; }
            Partie x;
            memset(&x, 0, sizeof x);
            x.nom = nom;
            x.taille = (size_t)(fd - fe - 4);
            x.valeur = grym_allouer(x.taille + 1);
            if (x.taille) memcpy(x.valeur, fe + 4, x.taille);
            x.valeur[x.taille] = '\0';
            x.fichier = a_fichier;
            if (a_fichier) {
                x.nom_fichier = nom_de_fichier(brut, strlen(brut));
                x.choisi = brut[0] != '\0' || x.taille > 0;
            }
            else x.mal = strlen(x.valeur) != x.taille || !utf8_valide((unsigned char *)x.valeur);
            free(brut);
            envoi_ajouter(e, x);
            p = fd + 2;
        }
        free(delim);
        if (!ok) envoi_liberer(e);
        return ok;
    }
    char *corps = grym_formater("%.*s", (int)r->taille_corps, r->corps);
    for (const char *p = corps; *p; ) {
        const char *f = strchr(p, '&');
        size_t l = f ? (size_t)(f - p) : strlen(p);
        const char *eg = memchr(p, '=', l);
        if (eg) {
            Partie x;
            memset(&x, 0, sizeof x);
            x.nom = decoder(p, (size_t)(eg - p));
            x.valeur = decoder(eg + 1, l - (size_t)(eg - p) - 1);
            if (!x.nom) x.nom = grym_dupliquer("");
            if (!x.valeur) { x.mal = 1; x.valeur = grym_dupliquer(""); }
            x.taille = strlen(x.valeur);
            envoi_ajouter(e, x);
        }
        if (!f) break;
        p = f + 1;
    }
    free(corps);
    return 1;
}

static int authentifie(const Serveur *s, const Requete *r) {
    if (!r->cookie) return 0;
    size_t ln = strlen(s->cookie);
    for (const char *p = r->cookie; p && *p; ) {
        while (*p == ' ') p++;
        const char *fin = strchr(p, ';');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        if (l > ln && strncmp(p, s->cookie, ln) == 0 && p[ln] == '=') return meme_jeton(p + ln + 1, l - ln - 1, s->jeton);
        p = fin ? fin + 1 : NULL;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Réponses                                                         */
/* ---------------------------------------------------------------- */

static const char STYLE[] =
    "body{margin:0;background:#f7f7f5;color:#1d1d1b;font:16px/1.5 system-ui,sans-serif}"
    "main{max-width:52rem;margin:2rem auto;padding:0 1rem}"
    "pre.sortie{font:15px/1.4 ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;margin:0 0 1.5rem}"
    "form{background:#fff;border:1px solid #d6d6d0;border-radius:6px;padding:1rem 1.25rem}"
    "p.champ{margin:.5rem 0}label{display:inline-block;min-width:12rem}"
    "input[type=text]{font:inherit;padding:.25rem .4rem;width:18rem;border:1px solid #b9b9b2;border-radius:4px}"
    "input[readonly]{background:#eeeeea;color:#555}"
    ".refus{display:block;color:#a31d1d;margin-left:12rem}.avis{color:#8a5a00}.erreur{color:#a31d1d}"
    "button{font:inherit;padding:.3rem .9rem;margin-right:.5rem}"
    "select{font:inherit;padding:.2rem .3rem}.actuel,.recu{color:#555;margin-left:.5rem}"
    "pre.sortie img{display:block;max-width:100%;max-height:24rem;margin:.4rem 0;border-radius:4px}"
    "table.fiche{border-collapse:collapse;margin:.5rem 0 1rem;background:#fff;border:1px solid #d6d6d0}"
    "table.fiche caption{text-align:left;font-weight:600;padding:.3rem 0}"
    "table.fiche th{text-align:left;font-weight:500;color:#555;padding:.3rem 1.2rem .3rem .7rem;vertical-align:top}"
    "table.fiche td{padding:.3rem .7rem}table.fiche img{max-width:16rem;max-height:12rem;border-radius:4px}";

static void repondre_octets(Serveur *s, const char *statut, const char *type, const unsigned char *corps,
                            size_t taille, const char *en_plus) {
    Chaine r = {0};
    char *entete = grym_formater(
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; form-action 'self'; "
        "frame-ancestors 'none'; base-uri 'none'\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: same-origin\r\n"   /* no-referrer ferait envoyer « Origin: null » aux formulaires */
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        statut, type, (unsigned long)taille, en_plus ? en_plus : "");
    chaine_ajouter(&r, entete);
    free(entete);
    size_t n = r.n + taille;
    char *tout = grym_allouer(n + 1);
    memcpy(tout, r.d, r.n);
    if (taille) memcpy(tout + r.n, corps, taille);
    s->t.envoyer(s->t.contexte, tout, n);
    free(tout);
    free(r.d);
}

static void repondre(Serveur *s, const char *statut, const char *type, const char *corps, const char *en_plus) {
    repondre_octets(s, statut, type, (const unsigned char *)corps, strlen(corps), en_plus);
}

static void rediriger(Serveur *s, const char *en_plus) {
    char *h = grym_formater("Location: /\r\n%s", en_plus ? en_plus : "");
    repondre(s, "303 See Other", "text/plain; charset=utf-8", "", h);
    free(h);
}

static void debut_page(const Serveur *s, Chaine *c) {
    chaine_ajouter(c, "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>");
    echapper(c, s->titre);
    chaine_ajouter(c, "</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><main>\n");
    if (s->affichage.n || s->nb_images) {
        chaine_ajouter(c, "<pre class=\"sortie\">");
        size_t depuis = 0;
        for (size_t k = 0; k <= s->nb_images; k++) {
            size_t jusqua = k < s->nb_images ? s->images[k].position : s->affichage.n;
            char *morceau = grym_formater("%.*s", (int)(jusqua - depuis), s->affichage.d ? s->affichage.d + depuis : "");
            echapper(c, morceau);
            free(morceau);
            depuis = jusqua;
            if (k < s->nb_images && s->images[k].html) {
                chaine_ajouter(c, s->images[k].html);
            } else if (k < s->nb_images && !s->images[k].cachee) {
                char *img = grym_formater("<img src=\"/image/%ld\" alt=\"", s->images[k].id);
                chaine_ajouter(c, img);
                free(img);
                echapper(c, s->images[k].description);
                chaine_ajouter(c, "\">");
            }
        }
        chaine_ajouter(c, "</pre>\n");
    }
}

/* Un refus se note dans le terminal, avec sa raison : c'est là qu'on cherche quand la page dit « Accès refusé ». */
static void noter_refus(const Serveur *s, const char *raison, const char *valeur) {
    if (s->reseau) fprintf(stderr, "grym servir : requête refusée, %s%s%s%s.\n", raison,
                           valeur ? " (« " : "", valeur ? valeur : "", valeur ? " »)" : "");
}

static void page_simple(Serveur *s, const char *statut, const char *message) {
    Chaine c = {0};
    chaine_ajouter(&c, "<!DOCTYPE html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>GrymoiR</title>"
                       "</head><body><p>");
    echapper(&c, message);
    chaine_ajouter(&c, "</p></body></html>\n");
    repondre(s, statut, "text/html; charset=utf-8", c.d, NULL);
    free(c.d);
}

static int est_fichier(const Champ *c) {
    return c->type && (strcmp(c->type, "fichier") == 0 || strcmp(c->type, "image") == 0);
}

static int est_oui_non(const Champ *c) {
    return c->type && strcmp(c->type, "vrai ou faux") == 0;
}

/* « oui », « non » ou « » : la valeur affichée d'un champ vrai ou faux. */
static const char *oui_non(const char *v) {
    if (!v) return "";
    if (strcmp(v, "oui") == 0 || strcmp(v, "vrai") == 0) return "oui";
    if (strcmp(v, "non") == 0 || strcmp(v, "faux") == 0) return "non";
    return "";
}

static void option(Chaine *c, const char *valeur, const char *texte, int choisie) {
    chaine_ajouter(c, "<option value=\"");
    echapper(c, valeur);
    chaine_ajouter(c, choisie ? "\" selected>" : "\">");
    echapper(c, texte);
    chaine_ajouter(c, "</option>");
}

static void page_question(Serveur *s, const Question *q) {
    Chaine c = {0};
    debut_page(s, &c);
    if (s->avis) {
        chaine_ajouter(&c, "<p class=\"avis\">");
        echapper(&c, s->avis);
        chaine_ajouter(&c, "</p>\n");
        free(s->avis);
        s->avis = NULL;
    }
    int multipart = 0;
    for (size_t k = 0; k < q->n; k++) multipart |= est_fichier(&q->champs[k]) && !q->acceptes[k];
    char *num = grym_formater("%ld", s->numero);
    /* autocomplete="off" : chaque page nomme ses champs c0, c1…, et le navigateur proposerait sous « Choix ? »
     * tout ce qu'on a tapé un jour dans un premier champ, noms de compositeurs compris (docs/v2.md, § 10) */
    chaine_ajouter(&c, multipart ? "<form method=\"post\" action=\"/reponse\" autocomplete=\"off\" enctype=\"multipart/form-data\">"
                                 : "<form method=\"post\" action=\"/reponse\" autocomplete=\"off\">");
    chaine_ajouter(&c, "<input type=\"hidden\" name=\"q\" value=\"");
    chaine_ajouter(&c, num);
    chaine_ajouter(&c, "\">\n");
    free(num);
    int focus = 0;
    for (size_t k = 0; k < q->n; k++) {
        const Champ *ch = &q->champs[k];
        const char *valeur = q->saisi[k] ? q->saisi[k] : ch->valeur ? ch->valeur : "";
        char *id = grym_formater("c%lu", (unsigned long)k);
        chaine_ajouter(&c, "<p class=\"champ\"><label for=\"");
        chaine_ajouter(&c, id);
        chaine_ajouter(&c, "\">");
        echapper(&c, ch->libelle);
        chaine_ajouter(&c, "</label> ");
        const char *attente = !q->acceptes[k] && !focus ? " autofocus" : "";
        if (!q->acceptes[k]) focus = 1;
        if (est_fichier(ch)) {   /* un fichier : choisi sur la machine, jamais un chemin (§ 10) */
            if (q->acceptes[k]) {
                chaine_ajouter(&c, "<span class=\"recu\">reçu</span>");
            } else {
                chaine_ajouter(&c, "<input type=\"file\" id=\"");
                chaine_ajouter(&c, id);
                chaine_ajouter(&c, "\" name=\"");
                chaine_ajouter(&c, id);
                chaine_ajouter(&c, "\"");
                if (strcmp(ch->type, "image") == 0) chaine_ajouter(&c, " accept=\"image/png,image/jpeg,image/gif,image/webp\"");
                chaine_ajouter(&c, attente);
                chaine_ajouter(&c, ">");
                if (ch->valeur) {
                    chaine_ajouter(&c, " <span class=\"actuel\">actuel : ");
                    echapper(&c, ch->valeur);
                    chaine_ajouter(&c, "</span>");
                }
            }
        } else if (ch->suggestions_completes) {   /* un lien, tous les objets connus : un menu (docs/v2.md, § 10) */
            chaine_ajouter(&c, "<select id=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\" name=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\"");
            if (q->acceptes[k]) chaine_ajouter(&c, " disabled");
            chaine_ajouter(&c, attente);
            chaine_ajouter(&c, ">");
            const char *v = valeur ? valeur : "";
            int trouvee = !*v;
            for (size_t j = 0; j < ch->nb_suggestions && !trouvee; j++) trouvee = strcmp(ch->suggestions[j], v) == 0;
            /* le choix vide : absent pour un facultatif, valeur actuelle gardée en modification, sinon refusé */
            option(&c, "", "", !*v);
            /* la valeur actuelle hors de la liste (objet dans la corbeille) reste choisie, jamais perdue en silence */
            if (!trouvee) option(&c, v, v, 1);
            for (size_t j = 0; j < ch->nb_suggestions; j++)
                option(&c, ch->suggestions[j], ch->suggestions[j], strcmp(ch->suggestions[j], v) == 0);
            chaine_ajouter(&c, "</select>");
        } else if (est_oui_non(ch)) {   /* vrai ou faux : oui, non, ou rien pour un champ facultatif */
            chaine_ajouter(&c, "<select id=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\" name=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\"");
            if (q->acceptes[k]) chaine_ajouter(&c, " disabled");
            chaine_ajouter(&c, attente);
            chaine_ajouter(&c, ">");
            const char *v = oui_non(valeur);
            if (ch->facultatif || !*v) option(&c, "", "", !*v);
            option(&c, "oui", "oui", strcmp(v, "oui") == 0);
            option(&c, "non", "non", strcmp(v, "non") == 0);
            chaine_ajouter(&c, "</select>");
        } else {
            chaine_ajouter(&c, "<input type=\"text\" id=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\" name=\"");
            chaine_ajouter(&c, id);
            chaine_ajouter(&c, "\" value=\"");
            echapper(&c, valeur);
            chaine_ajouter(&c, "\"");
            if (ch->type && (strcmp(ch->type, "nombre") == 0)) chaine_ajouter(&c, " inputmode=\"decimal\"");
            else if (ch->type && (strcmp(ch->type, "nombre entier") == 0 || strcmp(ch->type, "année") == 0))
                chaine_ajouter(&c, " inputmode=\"numeric\"");
            if (ch->nb_suggestions && !q->acceptes[k]) {
                chaine_ajouter(&c, " list=\"l");
                chaine_ajouter(&c, id + 1);
                chaine_ajouter(&c, "\" autocomplete=\"off\"");
            }
            if (q->acceptes[k]) chaine_ajouter(&c, " readonly");
            chaine_ajouter(&c, attente);
            chaine_ajouter(&c, ">");
            if (ch->nb_suggestions && !q->acceptes[k]) {   /* les clés existantes : suggérées, sans script (§ 10) */
                chaine_ajouter(&c, "<datalist id=\"l");
                chaine_ajouter(&c, id + 1);
                chaine_ajouter(&c, "\">");
                for (size_t j = 0; j < ch->nb_suggestions; j++) {
                    chaine_ajouter(&c, "<option value=\"");
                    echapper(&c, ch->suggestions[j]);
                    chaine_ajouter(&c, "\">");
                }
                chaine_ajouter(&c, "</datalist>");
            }
        }
        if (ch->videable && !q->acceptes[k]) {
            chaine_ajouter(&c, " <label><input type=\"checkbox\" name=\"v");
            chaine_ajouter(&c, id + 1);
            chaine_ajouter(&c, "\" value=\"1\"> vider</label>");
        }
        if (q->refus[k]) {
            chaine_ajouter(&c, "<span class=\"refus\">");
            echapper(&c, q->refus[k]);
            chaine_ajouter(&c, "</span>");
        }
        chaine_ajouter(&c, "</p>\n");
        free(id);
    }
    chaine_ajouter(&c, "<p><button name=\"action\" value=\"envoyer\">Envoyer</button>"
                       "<button name=\"action\" value=\"annuler\">Annuler</button></p></form>\n</main></body></html>\n");
    repondre(s, "200 OK", "text/html; charset=utf-8", c.d, NULL);
    free(c.d);
}

static void page_finale(Serveur *s, const char *erreur, const char *annulation) {
    Chaine c = {0};
    debut_page(s, &c);
    if (erreur) {
        chaine_ajouter(&c, "<p class=\"erreur\">");
        echapper(&c, erreur);
        chaine_ajouter(&c, "</p>\n");
        if (annulation) {
            chaine_ajouter(&c, "<p>");
            echapper(&c, annulation);
            chaine_ajouter(&c, "</p>\n");
        }
    }
    chaine_ajouter(&c, "<p><strong>Application terminée.</strong> Vous pouvez fermer cet onglet.</p>\n</main></body></html>\n");
    repondre(s, "200 OK", "text/html; charset=utf-8", c.d, NULL);
    free(c.d);
}

/* ---------------------------------------------------------------- */
/* Traitement d'une requête                                         */
/* ---------------------------------------------------------------- */

typedef enum { SUITE, FINI_REPONDU, FINI_ANNULE, FINI_ARRET, PAGE_FINALE_SERVIE } Suite;

/* Traite une requête. q : la question en cours, ou NULL (fin du programme). */
static Suite traiter(Serveur *s, const char *donnees, size_t n, Question *q,
                     const char *erreur, const char *annulation) {
    Requete r;
    if (!lire_requete(donnees, n, &r)) {
        page_simple(s, "400 Bad Request", "Requête mal formée.");
        return SUITE;
    }
    Suite suite = SUITE;
    /* § 5, règle 3 : un nom d'hôte étranger est un détournement par le DNS */
    if (!r.hote || strcmp(r.hote, s->hote) != 0) {
        noter_refus(s, "nom d'hôte inattendu", r.hote);
        page_simple(s, "403 Forbidden", "Accès refusé.");
    } else if (strcmp(r.methode, "GET") == 0 && strncmp(r.cible, "/?jeton=", 8) == 0) {
        if (meme_jeton(r.cible + 8, strlen(r.cible + 8), s->jeton)) {
            char *c = grym_formater("Set-Cookie: %s=%s; HttpOnly; SameSite=Strict; Path=/\r\n", s->cookie, s->jeton);
            rediriger(s, c);
            free(c);
        } else {
            noter_refus(s, "jeton faux dans l'adresse", NULL);
            page_simple(s, "403 Forbidden", "Accès refusé : ouvrez l'adresse affichée dans le terminal.");
        }
    } else if (!authentifie(s, &r)) {
        noter_refus(s, r.cookie ? "cookie sans le bon jeton" : "aucun cookie", r.cible);
        page_simple(s, "403 Forbidden", "Accès refusé : ouvrez l'adresse affichée dans le terminal.");
    } else if (strcmp(r.methode, "GET") == 0 && strcmp(r.cible, "/style.css") == 0) {
        repondre(s, "200 OK", "text/css; charset=utf-8", STYLE, NULL);
    } else if (strcmp(r.methode, "GET") == 0 && strncmp(r.cible, "/image/", 7) == 0) {
        char *e = NULL;
        long id = strtol(r.cible + 7, &e, 10);
        struct Image *im = NULL;
        for (size_t k = 0; e && *e == '\0' && e != r.cible + 7 && k < s->nb_images; k++)
            if (s->images[k].id == id) im = &s->images[k];
        if (im) repondre_octets(s, "200 OK", im->type, im->octets, im->taille, NULL);
        else page_simple(s, "404 Not Found", "Image inconnue.");
    } else if (strcmp(r.methode, "GET") == 0 && strcmp(r.cible, "/") == 0) {
        if (q) page_question(s, q);
        else { page_finale(s, erreur, annulation); suite = PAGE_FINALE_SERVIE; }
    } else if (strcmp(r.methode, "POST") == 0 && strcmp(r.cible, "/reponse") == 0) {
        /* § 5, règle 3 : un envoi vient de la page de l'application, et d'elle seule */
        if (!r.origine || strcmp(r.origine, s->origine) != 0) {
            noter_refus(s, r.origine ? "origine inattendue" : "aucune origine", r.origine);
            page_simple(s, "403 Forbidden", "Accès refusé.");
        } else {
            Envoi e;
            if (!lire_envoi(&r, &e)) {
                page_simple(s, "400 Bad Request", "Envoi mal formé.");
            } else {
                Partie *pq = envoi_partie(&e, "q"), *pa = envoi_partie(&e, "action");
                char attendu[24];
                snprintf(attendu, sizeof attendu, "%ld", s->numero);
                if (!q || !pq || pq->fichier || strcmp(pq->valeur, attendu) != 0) {
                    free(s->avis);   /* décision 6 : une réponse à une question close ne répond à rien */
                    s->avis = grym_dupliquer("Cette question a déjà reçu sa réponse.");
                    rediriger(s, NULL);
                } else if (pa && !pa->fichier && strcmp(pa->valeur, "annuler") == 0) {
                    rediriger(s, NULL);
                    suite = FINI_ANNULE;
                } else {
                    int tout = 1;
                    for (size_t k = 0; k < q->n && suite == SUITE; k++) {
                        if (q->acceptes[k]) continue;
                        char nom[24];
                        snprintf(nom, sizeof nom, "c%lu", (unsigned long)k);
                        Partie *pv = envoi_partie(&e, nom);
                        snprintf(nom, sizeof nom, "v%lu", (unsigned long)k);
                        Partie *pvider = envoi_partie(&e, nom);
                        Champ *ch = &q->champs[k];
                        free(q->refus[k]);
                        q->refus[k] = NULL;
                        free(q->saisi[k]);
                        q->saisi[k] = grym_dupliquer(pv && !pv->fichier && !pv->mal ? pv->valeur : "");
                        if (pv && pv->mal) {   /* octet nul ou UTF-8 invalide : jamais transmis à la machine */
                            q->refus[k] = grym_dupliquer("Texte illisible : réécrivez-le.");
                            tout = 0;
                            continue;
                        }
                        ch->vider = ch->videable && pvider && !pvider->fichier && strcmp(pvider->valeur, "1") == 0;
                        if (est_fichier(ch) && pv && pv->fichier && pv->choisi && !ch->vider) {
                            /* le fichier choisi passe à la machine, sans jamais toucher le disque (§ 10) */
                            ch->fichier_recu = 1;
                            ch->octets = (unsigned char *)pv->valeur;
                            ch->taille = pv->taille;
                            ch->nom_fichier = pv->nom_fichier;
                            pv->valeur = NULL;
                            pv->nom_fichier = NULL;
                        }
                        ch->ligne = grym_dupliquer(ch->vider || est_fichier(ch) ? "" : q->saisi[k]);
                        char *message = NULL;
                        int res = q->valider(q->vcontexte, k, &message);
                        if (res < 0) { free(message); suite = FINI_ARRET; break; }
                        if (res > 0) { q->refus[k] = message ? message : grym_dupliquer("Réponse refusée."); tout = 0; }
                        else { free(message); q->acceptes[k] = 1; }
                    }
                    rediriger(s, NULL);
                    if (suite == SUITE && tout) suite = FINI_REPONDU;
                }
                envoi_liberer(&e);
            }
        }
    } else {
        page_simple(s, "404 Not Found", "Page inconnue.");
    }
    requete_liberer(&r);
    return suite;
}

/* ---------------------------------------------------------------- */
/* L'interface                                                      */
/* ---------------------------------------------------------------- */

/* Les images placées avant l'octet k quittent le fil ; les autres reculent de k. */
static void retirer_images(Serveur *s, size_t k) {
    size_t w = 0;
    for (size_t r = 0; r < s->nb_images; r++) {
        if (s->images[r].position < k) {
            free(s->images[r].octets);
            free(s->images[r].description);
            free(s->images[r].html);
            continue;
        }
        s->images[r].position -= k;
        s->images[w++] = s->images[r];
    }
    s->nb_images = w;
}

/* Ajoute la sortie de la machine à l'affichage, en gardant les LIGNES_MAX dernières lignes. */
static void absorber(Serveur *s, Chaine *sortie) {
    if (sortie->n) {
        chaine_ajouter(&s->affichage, sortie->d);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    size_t lignes = 0;
    for (size_t k = 0; k < s->affichage.n; k++) if (s->affichage.d[k] == '\n') lignes++;
    if (lignes <= LIGNES_MAX) return;
    size_t k = 0;
    for (size_t trop = lignes - LIGNES_MAX; trop && k < s->affichage.n; k++) if (s->affichage.d[k] == '\n') trop--;
    memmove(s->affichage.d, s->affichage.d + k, s->affichage.n - k + 1);
    s->affichage.n -= k;
    retirer_images(s, k);
}

static int serveur_disponible(void *contexte) { (void)contexte; return 1; }

static Issue serveur_formulaire(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                                Validation valider, void *vcontexte) {
    Serveur *s = contexte;
    absorber(s, sortie);
    s->numero++;
    Question q = { champs, n, grym_allouer((n ? n : 1) * sizeof(char *)), grym_allouer((n ? n : 1) * sizeof(char *)),
                   grym_allouer(n ? n : 1), valider, vcontexte };
    for (size_t k = 0; k < n; k++) { q.saisi[k] = NULL; q.refus[k] = NULL; q.acceptes[k] = 0; }
    Issue issue = ISSUE_FIN;
    for (;;) {
        size_t taille = 0;
        int interrompu = 0;
        char *d = s->t.recevoir(s->t.contexte, &taille, &interrompu);
        if (interrompu) { issue = ISSUE_INTERROMPU; break; }
        if (!d) { issue = ISSUE_FIN; break; }
        Suite suite = traiter(s, d, taille, &q, NULL, NULL);
        free(d);
        if (suite == FINI_REPONDU) { issue = ISSUE_REPONDU; break; }
        if (suite == FINI_ANNULE) { issue = ISSUE_ANNULE; break; }
        if (suite == FINI_ARRET) { issue = ISSUE_ARRET; break; }
    }
    *arret = 0;
    for (size_t k = 0; k < n; k++) {
        if (!q.acceptes[k] && *arret == 0) *arret = k;
        free(q.saisi[k]);
        free(q.refus[k]);
    }
    free(q.saisi);
    free(q.refus);
    free(q.acceptes);
    return issue;
}

static void serveur_effacer(void *contexte, Chaine *sortie) {
    Serveur *s = contexte;
    if (sortie->n) { sortie->n = 0; sortie->d[0] = '\0'; }   /* ce qui précède l'effacement disparaît */
    s->affichage.n = 0;
    if (s->affichage.d) s->affichage.d[0] = '\0';
    retirer_images(s, (size_t)-1);
}

static struct Image *nouveau_segment(Serveur *s) {
    if (s->nb_images == s->cap_images) {
        s->cap_images = s->cap_images ? s->cap_images * 2 : 8;
        struct Image *t = grym_allouer(s->cap_images * sizeof *t);
        if (s->nb_images) memcpy(t, s->images, s->nb_images * sizeof *t);
        free(s->images);
        s->images = t;
    }
    struct Image *im = &s->images[s->nb_images++];
    memset(im, 0, sizeof *im);
    im->position = s->affichage.n;
    im->id = ++s->prochaine_image;
    return im;
}

static const char *type_image(const char *format) {
    return strcmp(format, "PNG") == 0 ? "image/png" : strcmp(format, "JPEG") == 0 ? "image/jpeg"
         : strcmp(format, "GIF") == 0 ? "image/gif" : "image/webp";
}

/* « Afficher la photo. » : l'image prend place dans le fil (docs/v2.md, § 10). */
static void serveur_afficher_image(void *contexte, Chaine *sortie, const unsigned char *octets, size_t taille,
                                   const char *format, const char *description) {
    Serveur *s = contexte;
    absorber(s, sortie);
    struct Image *im = nouveau_segment(s);
    im->octets = grym_allouer(taille ? taille : 1);
    if (taille) memcpy(im->octets, octets, taille);
    im->taille = taille;
    im->type = type_image(format);
    im->description = grym_dupliquer(description);
}

/* « Afficher la fiche de p. » : un tableau dans le fil, une image montrée en image (grammaire, § 20). */
static void serveur_afficher_fiche(void *contexte, Chaine *sortie, const char *titre, const LigneFiche *l, size_t n) {
    Serveur *s = contexte;
    absorber(s, sortie);
    Chaine h = {0};
    chaine_ajouter(&h, "</pre><table class=\"fiche\"><caption>");
    echapper(&h, titre);
    chaine_ajouter(&h, "</caption>");
    for (size_t k = 0; k < n; k++) {
        chaine_ajouter(&h, "<tr><th>");
        echapper(&h, l[k].libelle);
        chaine_ajouter(&h, "</th><td>");
        if (l[k].format) {
            struct Image *im = nouveau_segment(s);
            im->cachee = 1;
            im->octets = grym_allouer(l[k].taille ? l[k].taille : 1);
            if (l[k].taille) memcpy(im->octets, l[k].octets, l[k].taille);
            im->taille = l[k].taille;
            im->type = type_image(l[k].format);
            im->description = grym_dupliquer(l[k].texte);
            char *img = grym_formater("<img src=\"/image/%ld\" alt=\"", im->id);
            chaine_ajouter(&h, img);
            free(img);
            echapper(&h, l[k].texte);
            chaine_ajouter(&h, "\">");
        } else {
            echapper(&h, l[k].texte);
        }
        chaine_ajouter(&h, "</td></tr>");
    }
    chaine_ajouter(&h, "</table><pre class=\"sortie\">");
    struct Image *seg = nouveau_segment(s);
    seg->html = chaine_rendre(&h);
    seg->description = grym_dupliquer(titre);
}

Interface serveur_interface(Serveur *s) {
    Interface i = { s, serveur_disponible, serveur_formulaire, serveur_effacer, 1, serveur_afficher_image,
                    serveur_afficher_fiche };
    return i;
}

void serveur_terminer(Serveur *s, Chaine *sortie, const char *erreur, const char *annulation) {
    absorber(s, sortie);
    /* Après la page finale, le navigateur demande encore sa feuille de style et ses images : on les sert
     * tant qu'il en demande (le transport réseau abandonne après ATTENTE_FIN secondes de silence). */
    for (;;) {
        size_t taille = 0;
        int interrompu = 0;
        char *d = s->t.recevoir(s->t.contexte, &taille, &interrompu);
        if (!d) return;
        Suite suite = traiter(s, d, taille, NULL, erreur, annulation);
        free(d);
        if (suite == PAGE_FINALE_SERVIE) s->fin_proche = 1;
    }
}

const char *serveur_adresse(const Serveur *s) { return s->adresse; }

Serveur *serveur_creer(int port, const char *jeton, const char *titre, Transport t) {
    Serveur *s = grym_allouer(sizeof *s);
    memset(s, 0, sizeof *s);
    s->port = port;
    snprintf(s->jeton, sizeof s->jeton, "%s", jeton);
    snprintf(s->cookie, sizeof s->cookie, "grym_%d", port);
    snprintf(s->hote, sizeof s->hote, "127.0.0.1:%d", port);
    snprintf(s->origine, sizeof s->origine, "http://127.0.0.1:%d", port);
    snprintf(s->adresse, sizeof s->adresse, "http://127.0.0.1:%d/?jeton=%s", port, s->jeton);
    s->titre = grym_dupliquer(titre);
    s->t = t;
    s->ecoute = s->client = PRISE_INVALIDE;
    return s;
}

void serveur_fermer(Serveur *s) {
    if (!s) return;
    if (s->client != PRISE_INVALIDE) fermer_prise(s->client);
    if (s->ecoute != PRISE_INVALIDE) fermer_prise(s->ecoute);
#ifdef _WIN32
    if (s->reseau) WSACleanup();
#endif
    retirer_images(s, (size_t)-1);
    free(s->images);
    free(s->titre);
    free(s->affichage.d);
    free(s->avis);
    free(s);
}

/* ---------------------------------------------------------------- */
/* Transport par le réseau                                          */
/* ---------------------------------------------------------------- */

/* 128 bits du générateur du système (§ 5, règle 2). */
static int jeton_aleatoire(char *hex) {
    unsigned char o[16];
#ifdef _WIN32
    if (BCryptGenRandom(NULL, o, sizeof o, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return 0;
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    size_t lu = fread(o, 1, sizeof o, f);
    fclose(f);
    if (lu != sizeof o) return 0;
#endif
    for (size_t k = 0; k < sizeof o; k++) snprintf(hex + 2 * k, 3, "%02x", o[k]);
    return 1;
}

/* Attend qu'une prise soit prête, par tranches d'une seconde, pour voir passer un Ctrl+C. */
static int attendre(Prise p, int secondes, int *interrompu) {
    for (int t = 0; secondes < 0 || t < secondes; t++) {
        if (grym_interruption) { *interrompu = 1; return 0; }
        fd_set ens;
        FD_ZERO(&ens);
        FD_SET(p, &ens);
        struct timeval tv = { 1, 0 };
        int r = select((int)p + 1, &ens, NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0 && !grym_interruption) return 0;
    }
    return 0;
}

static void envoyer_prise(void *contexte, const char *donnees, size_t taille) {
    Serveur *s = contexte;
    if (s->client == PRISE_INVALIDE) return;
    while (taille) {
        long e = (long)send(s->client, donnees, (int)taille, 0);
        if (e <= 0) break;
        donnees += e;
        taille -= (size_t)e;
    }
    fermer_prise(s->client);
    s->client = PRISE_INVALIDE;
}

static long longueur_annoncee(const char *d, size_t fin) {
    for (size_t k = 0; k + 15 < fin; k++) {
        if ((k == 0 || d[k - 1] == '\n') && egal_sans_casse(d + k, "content-length:", 15)) {
            const char *v = d + k + 15;
            while (*v == ' ') v++;
            char *e = NULL;
            long l = strtol(v, &e, 10);
            return e == v || l < 0 ? -2 : l;
        }
    }
    return 0;
}

static char *recevoir_prise(void *contexte, size_t *taille, int *interrompu) {
    Serveur *s = contexte;
    for (;;) {
        if (!attendre(s->ecoute, s->fin_proche ? ATTENTE_FIN : -1, interrompu)) return NULL;
        s->client = accept(s->ecoute, NULL, NULL);
        if (s->client == PRISE_INVALIDE) continue;
        char *d = grym_allouer(ENTETES_MAX + 1);
        size_t n = 0, cap = ENTETES_MAX, fin = 0;
        long corps = -1;
        int valide = 0;
        for (;;) {
            if (!attendre(s->client, ATTENTE_CLIENT, interrompu)) break;
            long r = (long)recv(s->client, d + n, (int)(cap - n), 0);
            if (r <= 0) break;
            n += (size_t)r;
            d[n] = '\0';
            if (!fin) {
                char *e = strstr(d, "\r\n\r\n");
                if (e) {
                    fin = (size_t)(e - d) + 4;
                    corps = longueur_annoncee(d, fin);
                    if (corps < 0 || corps > CORPS_MAX) { corps = -3; break; }
                    if (fin + (size_t)corps > cap) {
                        cap = fin + (size_t)corps;
                        char *t = grym_allouer(cap + 1);
                        memcpy(t, d, n + 1);
                        free(d);
                        d = t;
                    }
                } else if (n == cap) {
                    corps = -4;   /* en-têtes trop longs */
                    break;
                }
            }
            if (fin && n >= fin + (size_t)corps) { valide = 1; break; }
        }
        if (*interrompu) { free(d); fermer_prise(s->client); s->client = PRISE_INVALIDE; return NULL; }
        if (valide) { *taille = fin + (size_t)corps; return d; }
        free(d);
        if (corps == -3) page_simple(s, "413 Content Too Large", "Envoi trop volumineux.");
        else if (corps == -4) page_simple(s, "431 Request Header Fields Too Large", "En-têtes trop longs.");
        else { fermer_prise(s->client); s->client = PRISE_INVALIDE; }
    }
}

Serveur *serveur_ouvrir(int port, const char *titre, char **erreur) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { *erreur = grym_dupliquer("Réseau indisponible (WSAStartup)."); return NULL; }
#endif
    char jeton[33];
    if (!jeton_aleatoire(jeton)) { *erreur = grym_dupliquer("Aucun générateur aléatoire du système : pas de jeton."); return NULL; }
    Prise p = socket(AF_INET, SOCK_STREAM, 0);
    if (p == PRISE_INVALIDE) { *erreur = grym_dupliquer("Impossible de créer une prise réseau."); return NULL; }
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001);   /* 127.0.0.1, jamais le réseau (§ 5, règle 1) */
    a.sin_port = htons((unsigned short)port);
    if (bind(p, (struct sockaddr *)&a, sizeof a) != 0 || listen(p, 16) != 0) {
        fermer_prise(p);
        *erreur = port ? grym_formater("Le port %d est déjà pris : choisissez-en un autre avec --port.", port)
                       : grym_dupliquer("Impossible d'écouter sur 127.0.0.1.");
        return NULL;
    }
    socklen_t l = sizeof a;
    getsockname(p, (struct sockaddr *)&a, &l);
    Transport t = { NULL, recevoir_prise, envoyer_prise };
    Serveur *s = serveur_creer(ntohs(a.sin_port), jeton, titre, t);
    s->t.contexte = s;
    s->ecoute = p;
    s->reseau = 1;
    return s;
}
