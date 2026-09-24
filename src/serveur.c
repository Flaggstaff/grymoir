/* GrymoiR : l'interface par le navigateur, servie en local (v2.0-a).
 * Spécification : docs/v2.md (révision 0.4), § 4, § 5 et § 9 ; docs/vm.md, § 13.
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
#define CORPS_MAX (1024 * 1024)   /* octets de corps en v2.0-a */
#define LIGNES_MAX 1000           /* affichage gardé dans la page */
#define ATTENTE_CLIENT 5          /* secondes : un client muet est congédié */

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
    int reseau;           /* ouvert par serveur_ouvrir : prises et WSAStartup à rendre */
    Chaine affichage;     /* depuis le dernier effacement, plafonné à LIGNES_MAX lignes */
    long numero;          /* numéro de la question courante (décision 6) */
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

/* Valeur du paramètre nom dans un corps « a=1&b=2 », décodée ; NULL si absent, ou mal formé (*mal). */
static char *parametre_mal(const char *corps, const char *nom, int *mal) {
    size_t ln = strlen(nom);
    for (const char *p = corps; p && *p; ) {
        const char *fin = strchr(p, '&');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        if (l > ln && strncmp(p, nom, ln) == 0 && p[ln] == '=') {
            char *v = decoder(p + ln + 1, l - ln - 1);
            if (!v && mal) *mal = 1;
            return v;
        }
        p = fin ? fin + 1 : NULL;
    }
    return NULL;
}

static char *parametre(const char *corps, const char *nom) { return parametre_mal(corps, nom, NULL); }

/* ---------------------------------------------------------------- */
/* Requêtes                                                         */
/* ---------------------------------------------------------------- */

typedef struct {
    char methode[8];
    char *cible;          /* « / », « /?jeton=… », « /reponse », « /style.css » */
    char *hote, *cookie, *origine;
    const char *corps;
    size_t taille_corps;
} Requete;

static void requete_liberer(Requete *r) {
    free(r->cible);
    free(r->hote);
    free(r->cookie);
    free(r->origine);
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
                     : ln == 6 && egal_sans_casse(p, "origin", 6) ? &r->origine : NULL;
        if (cible && *cible) { free(valeur); requete_liberer(r); return 0; }   /* en-tête en double : refus */
        if (cible) *cible = valeur;
        else free(valeur);
    }
    r->corps = fin_entetes + 4;
    r->taille_corps = n - (size_t)(r->corps - d);
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
    "button{font:inherit;padding:.3rem .9rem;margin-right:.5rem}";

static void repondre(Serveur *s, const char *statut, const char *type, const char *corps, const char *en_plus) {
    Chaine r = {0};
    char *entete = grym_formater(
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'self'; img-src 'self'; form-action 'self'; "
        "frame-ancestors 'none'; base-uri 'none'\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "%s\r\n",
        statut, type, (unsigned long)strlen(corps), en_plus ? en_plus : "");
    chaine_ajouter(&r, entete);
    chaine_ajouter(&r, corps);
    free(entete);
    s->t.envoyer(s->t.contexte, r.d, r.n);
    free(r.d);
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
    if (s->affichage.n) {
        chaine_ajouter(c, "<pre class=\"sortie\">");
        echapper(c, s->affichage.d);
        chaine_ajouter(c, "</pre>\n");
    }
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
    char *num = grym_formater("%ld", s->numero);
    chaine_ajouter(&c, "<form method=\"post\" action=\"/reponse\"><input type=\"hidden\" name=\"q\" value=\"");
    chaine_ajouter(&c, num);
    chaine_ajouter(&c, "\">\n");
    free(num);
    int focus = 0;
    for (size_t k = 0; k < q->n; k++) {
        const Champ *ch = &q->champs[k];
        char *id = grym_formater("c%lu", (unsigned long)k);
        chaine_ajouter(&c, "<p class=\"champ\"><label for=\"");
        chaine_ajouter(&c, id);
        chaine_ajouter(&c, "\">");
        echapper(&c, ch->libelle);
        chaine_ajouter(&c, "</label> <input type=\"text\" id=\"");
        chaine_ajouter(&c, id);
        chaine_ajouter(&c, "\" name=\"");
        chaine_ajouter(&c, id);
        chaine_ajouter(&c, "\" value=\"");
        echapper(&c, q->saisi[k] ? q->saisi[k] : ch->valeur ? ch->valeur : "");
        chaine_ajouter(&c, "\"");
        if (q->acceptes[k]) chaine_ajouter(&c, " readonly");
        else if (!focus) { chaine_ajouter(&c, " autofocus"); focus = 1; }
        chaine_ajouter(&c, ">");
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
        page_simple(s, "403 Forbidden", "Accès refusé.");
    } else if (strcmp(r.methode, "GET") == 0 && strncmp(r.cible, "/?jeton=", 8) == 0) {
        if (meme_jeton(r.cible + 8, strlen(r.cible + 8), s->jeton)) {
            char *c = grym_formater("Set-Cookie: %s=%s; HttpOnly; SameSite=Strict; Path=/\r\n", s->cookie, s->jeton);
            rediriger(s, c);
            free(c);
        } else {
            page_simple(s, "403 Forbidden", "Accès refusé : ouvrez l'adresse affichée dans le terminal.");
        }
    } else if (!authentifie(s, &r)) {
        page_simple(s, "403 Forbidden", "Accès refusé : ouvrez l'adresse affichée dans le terminal.");
    } else if (strcmp(r.methode, "GET") == 0 && strcmp(r.cible, "/style.css") == 0) {
        repondre(s, "200 OK", "text/css; charset=utf-8", STYLE, NULL);
    } else if (strcmp(r.methode, "GET") == 0 && strcmp(r.cible, "/") == 0) {
        if (q) page_question(s, q);
        else { page_finale(s, erreur, annulation); suite = PAGE_FINALE_SERVIE; }
    } else if (strcmp(r.methode, "POST") == 0 && strcmp(r.cible, "/reponse") == 0) {
        /* § 5, règle 3 : un envoi vient de la page de l'application, et d'elle seule */
        if (!r.origine || strcmp(r.origine, s->origine) != 0) {
            page_simple(s, "403 Forbidden", "Accès refusé.");
        } else {
            char *corps = grym_formater("%.*s", (int)r.taille_corps, r.corps);
            char *numero = parametre(corps, "q");
            char *action = parametre(corps, "action");
            char attendu[24];
            snprintf(attendu, sizeof attendu, "%ld", s->numero);
            if (!q || !numero || strcmp(numero, attendu) != 0) {
                free(s->avis);   /* décision 6 : une réponse à une question close ne répond à rien */
                s->avis = grym_dupliquer("Cette question a déjà reçu sa réponse.");
                rediriger(s, NULL);
            } else if (action && strcmp(action, "annuler") == 0) {
                rediriger(s, NULL);
                suite = FINI_ANNULE;
            } else {
                int tout = 1;
                for (size_t k = 0; k < q->n && suite == SUITE; k++) {
                    if (q->acceptes[k]) continue;
                    char nom[24];
                    snprintf(nom, sizeof nom, "c%lu", (unsigned long)k);
                    int mal = 0;
                    char *v = parametre_mal(corps, nom, &mal);
                    snprintf(nom, sizeof nom, "v%lu", (unsigned long)k);
                    char *vider = parametre(corps, nom);
                    free(q->saisi[k]);
                    q->saisi[k] = v ? v : grym_dupliquer("");
                    free(q->refus[k]);
                    q->refus[k] = NULL;
                    if (mal) {   /* octet nul ou UTF-8 invalide : jamais transmis à la machine */
                        free(vider);
                        q->refus[k] = grym_dupliquer("Texte illisible : réécrivez-le.");
                        tout = 0;
                        continue;
                    }
                    q->champs[k].vider = q->champs[k].videable && vider && strcmp(vider, "1") == 0;
                    free(vider);
                    q->champs[k].ligne = grym_dupliquer(q->champs[k].vider ? "" : q->saisi[k]);
                    char *message = NULL;
                    int res = q->valider(q->vcontexte, k, &message);
                    if (res < 0) { free(message); suite = FINI_ARRET; break; }
                    if (res > 0) { q->refus[k] = message ? message : grym_dupliquer("Réponse refusée."); tout = 0; }
                    else { free(message); q->acceptes[k] = 1; }
                }
                rediriger(s, NULL);
                if (suite == SUITE && tout) suite = FINI_REPONDU;
            }
            free(numero);
            free(action);
            free(corps);
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
}

Interface serveur_interface(Serveur *s) {
    Interface i = { s, serveur_disponible, serveur_formulaire, serveur_effacer };
    return i;
}

void serveur_terminer(Serveur *s, Chaine *sortie, const char *erreur, const char *annulation) {
    absorber(s, sortie);
    for (;;) {
        size_t taille = 0;
        int interrompu = 0;
        char *d = s->t.recevoir(s->t.contexte, &taille, &interrompu);
        if (!d) return;
        Suite suite = traiter(s, d, taille, NULL, erreur, annulation);
        free(d);
        if (suite == PAGE_FINALE_SERVIE) return;
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
        if (!attendre(s->ecoute, -1, interrompu)) return NULL;
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
