#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* Tests du serveur local (docs/v2.md, § 5 et § 9) : chaque requête passe par un transport scripté,
 * sans réseau. Les règles de sécurité ont chacune leur attaque. */
#include "analyseur.h"
#include "compilateur.h"
#include "json.h"
#include "serveur.h"
#include "texte.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#endif

static int total = 0, echecs = 0;

#define PORT 4242
#define JETON "0123456789abcdef0123456789abcdef"
#define HOTE "Host: 127.0.0.1:4242\r\n"
#define COOKIE "Cookie: grym_4242=" JETON "\r\n"
#define ORIGINE "Origin: http://127.0.0.1:4242\r\n"

/* Transport scripté : les requêtes dans l'ordre ; chaque réponse est gardée. */
typedef struct {
    char **requetes;
    size_t n, i;
    char *reponses[64];
    size_t nr;
} Script;

static char *recevoir(void *contexte, size_t *taille, int *interrompu) {
    Script *s = contexte;
    *interrompu = 0;
    if (s->i == s->n) return NULL;
    *taille = strlen(s->requetes[s->i]);
    return grym_dupliquer(s->requetes[s->i++]);
}

static void envoyer(void *contexte, const char *donnees, size_t taille) {
    Script *s = contexte;
    if (s->nr < 64) s->reponses[s->nr++] = grym_formater("%.*s", (int)taille, donnees);
}

static char *get(const char *cible) {
    return grym_formater("GET %s HTTP/1.1\r\n" HOTE COOKIE "\r\n", cible);
}

static char *post(const char *corps) {
    return grym_formater("POST /reponse HTTP/1.1\r\n" HOTE COOKIE ORIGINE
                         "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n%s",
                         (unsigned long)strlen(corps), corps);
}

#define PART(nom, valeur) "--XyZ\r\nContent-Disposition: form-data; name=\"" nom "\"\r\n\r\n" valeur "\r\n"
#define PARTF(nom, fichier, octets) "--XyZ\r\nContent-Disposition: form-data; name=\"" nom "\"; filename=\"" fichier \
    "\"\r\nContent-Type: application/octet-stream\r\n\r\n" octets "\r\n"
#define PNG "\x89PNG\r\n\x1a\nimage"

static char *post_multi(const char *corps) {
    return grym_formater("POST /reponse HTTP/1.1\r\n" HOTE COOKIE ORIGINE
                         "Content-Type: multipart/form-data; boundary=XyZ\r\nContent-Length: %lu\r\n\r\n%s",
                         (unsigned long)strlen(corps), corps);
}

static char *evenement_multi(const char *corps) {   /* un écran avec une zone de fichier : l'envoi est en multipart */
    return grym_formater("POST /evenement HTTP/1.1\r\n" HOTE COOKIE ORIGINE
                         "Content-Type: multipart/form-data; boundary=XyZ\r\nContent-Length: %lu\r\n\r\n%s",
                         (unsigned long)strlen(corps), corps);
}

/* Encodage « application/x-www-form-urlencoded » d'une valeur. */
static char *enc(const char *v) {
    Chaine c = {0};
    char t[4];
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) { t[0] = (char)*p; t[1] = 0; }
        else if (*p == ' ') { t[0] = '+'; t[1] = 0; }
        else snprintf(t, sizeof t, "%%%02X", *p);
        chaine_ajouter(&c, t);
    }
    return chaine_rendre(&c);
}

/* Réponse à la question suivante : « c0=…&c1=… » à partir des valeurs brutes, séparées par « | ». */
static long question_suivante;
/* Le mot de passe des bases chiffrées (2.37), pour les essais ; NULL : l'oublier (la page le demande alors). */
static void mot_de_passe_essais(const char *m) {
#ifdef _WIN32
    _putenv_s("GRYM_MOT_DE_PASSE_BASE", m ? m : "");
#else
    if (m) setenv("GRYM_MOT_DE_PASSE_BASE", m, 1);
    else unsetenv("GRYM_MOT_DE_PASSE_BASE");
#endif
}
static int fichier_present(const char *nom) {
    FILE *f = fopen(nom, "rb");
    if (f) fclose(f);
    return f != NULL;
}
static char *reponse(const char *valeurs) {
    Chaine c = {0};
    char *debut = grym_formater("q=%ld", ++question_suivante);
    chaine_ajouter(&c, debut);
    free(debut);
    int k = 0;
    for (const char *p = valeurs; ; k++) {
        const char *f = strchr(p, '|');
        char *v = grym_formater("%.*s", (int)(f ? (size_t)(f - p) : strlen(p)), p);
        char *e = enc(v);
        char *morceau = grym_formater("&c%d=%s", k, e);
        chaine_ajouter(&c, morceau);
        free(morceau);
        free(e);
        free(v);
        if (!f) break;
        p = f + 1;
    }
    chaine_ajouter(&c, "&action=envoyer");
    char *corps = chaine_rendre(&c);
    char *r = post(corps);
    free(corps);
    return r;
}

static const char *dossier_des_exemples;

/* Exécute src servi par le transport scripté ; les réponses restent dans *sc. */
static void servir(const char *src, Script *sc) {
    Transport t = { sc, recevoir, envoyer };
    Serveur *s = serveur_creer(PORT, JETON, "essai.grym", t);
    Portee *portee = portee_creer();
    Machine *m = machine_creer();
    Interface i = serveur_interface(s);
    machine_interface(m, &i);
    if (dossier_des_exemples) machine_dossier(m, dossier_des_exemples);
    Programme p;
    Diagnostic d;
    Chaine sortie = {0};
    char *erreur = NULL;
    if (!analyser(src, strlen(src), portee, 0, &p, &d)) {
        erreur = grym_formater("ANALYSE %s", d.message);
        diagnostic_liberer(&d);
    } else {
        Module *b = compiler(&p, &d);
        if (!b || !machine_executer(m, b, &sortie, &d)) {
            erreur = grym_formater("Erreur, ligne %d : %s", d.ligne, d.message);
            diagnostic_liberer(&d);
        }
        module_detruire(b);
        programme_liberer(&p);
    }
    char *annule = erreur ? machine_annulation(m, 0) : NULL;
    serveur_terminer(s, &sortie, erreur, annule);
    free(annule);
    free(erreur);
    free(sortie.d);
    machine_detruire(m);
    portee_detruire(portee);
    serveur_fermer(s);
}

static void liberer(Script *sc) {
    for (size_t k = 0; k < sc->n; k++) free(sc->requetes[k]);
    free(sc->requetes);
    for (size_t k = 0; k < sc->nr; k++) free(sc->reponses[k]);
}

static void verifier(int ligne, const char *quoi, const char *reponse, const char *fragment, int present) {
    total++;
    int trouve = reponse && strstr(reponse, fragment) != NULL;
    if (trouve == present) return;
    echecs++;
    printf("ÉCHEC (test ligne %d) : %s\n  %s « %s »\n  réponse : %s\n", ligne, quoi,
           present ? "attendu" : "interdit", fragment, reponse ? reponse : "(aucune)");
}

static char *lire_tout(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *d = grym_allouer((size_t)n + 1);
    d[fread(d, 1, (size_t)n, f)] = '\0';
    fclose(f);
    return d;
}

/* Un exemple du dépôt, servi du début à la fin ; la dernière réponse est la page finale. */
static void exemple(int ligne, const char *nom, Script *sc, const char *const *fragments) {
    char *chemin = grym_formater("exemples/%s", nom);
    char *src = lire_tout(chemin);
    total++;
    if (!src) { echecs++; printf("ÉCHEC (test ligne %d) : %s introuvable\n", ligne, chemin); free(chemin); return; }
    dossier_des_exemples = "exemples";
    servir(src, sc);
    dossier_des_exemples = NULL;
    const char *fin = sc->nr ? sc->reponses[sc->nr - 1] : NULL;
    verifier(ligne, nom, fin, "Application terminée.", 1);
    verifier(ligne, nom, fin, "class=\"erreur\"", 0);
    for (size_t k = 0; fragments && fragments[k]; k++) verifier(ligne, nom, fin, fragments[k], 1);
    free(src);
    free(chemin);
}


static char *evenement(const char *corps) {
    return grym_formater("POST /evenement HTTP/1.1\r\n" HOTE COOKIE ORIGINE
                         "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %lu\r\n\r\n%s",
                         (unsigned long)strlen(corps), corps);
}

/* Les jetons de couleur du serveur sont ceux du thème de l'atelier (docs/web.md, § 2.2). */
static void verifier_jetons(void) {
    char *texte = lire_tout("src/atelier/theme/grymoir-jetons.json");
    total++;
    if (!texte) { echecs++; printf("ÉCHEC : src/atelier/theme/grymoir-jetons.json introuvable\n"); return; }
    char *erreur = NULL;
    Json *j = json_lire(texte, strlen(texte), &erreur);
    free(texte);
    if (!j) { echecs++; printf("ÉCHEC : jetons illisibles : %s\n", erreur); free(erreur); return; }
    const char *const modes[] = { "clair", "sombre" };
    for (int m = 0; m < 2; m++) {
        char *chemin = grym_formater("couleurs.%s", modes[m]);
        const Json *c = json_chemin(j, chemin);
        free(chemin);
        for (size_t k = 0; c && k < c->nb; k++) {
            const char *attendu = json_texte(c->elements[k]), *vu = serveur_jeton(m, 0, c->cles[k]);
            total++;
            if (!vu || !attendu || strcmp(vu, attendu) != 0) {
                echecs++;
                printf("ÉCHEC : jeton %s (%s) : %s dans le serveur, %s dans le thème\n", c->cles[k], modes[m],
                       vu ? vu : "absent", attendu ? attendu : "?");
            }
        }
        for (int a = 0; a < 5; a++) {
            chemin = grym_formater("accents.%s.%s", serveur_palette(a), modes[m]);
            const Json *acc = json_chemin(j, chemin);
            free(chemin);
            total++;
            if (!acc) { echecs++; printf("ÉCHEC : accent %s absent du thème\n", serveur_palette(a)); continue; }
            for (size_t k = 0; k < acc->nb; k++) {
                const char *attendu = json_texte(acc->elements[k]);
                if (!attendu) continue;   /* « contrastes » : un objet, pas une couleur */
                const char *vu = serveur_jeton(m, a, acc->cles[k]);
                total++;
                if (!vu || strcmp(vu, attendu) != 0) {
                    echecs++;
                    printf("ÉCHEC : jeton %s de %s (%s) : %s dans le serveur, %s dans le thème\n", acc->cles[k],
                           serveur_palette(a), modes[m], vu ? vu : "absent", attendu);
                }
            }
        }
    }
    /* l'ordre de la palette est celui du thème : le rang que donne la machine désigne la même couleur */
    const Json *ordre = json_chemin(j, "accents._ordre");
    for (int a = 0; ordre && a < 5 && (size_t)a < ordre->nb; a++) {
        total++;
        if (strcmp(json_texte(ordre->elements[a]), serveur_palette(a)) != 0) {
            echecs++;
            printf("ÉCHEC : palette, rang %d : %s dans le serveur, %s dans le thème\n", a, serveur_palette(a),
                   json_texte(ordre->elements[a]));
        }
    }
    json_liberer(j);
}


/* Un projet du dépôt, servi comme grym servir le fait : les « Utiliser » partent de son dossier ; sa base est
 * une base d'essai, effacée avant et après (docs/atelier.md, § 5 quater). */
static void servir_projet_base(const char *chemin, const char *base, Script *sc, int garder);
static void servir_projet(const char *chemin, const char *base, Script *sc) { servir_projet_base(chemin, base, sc, 0); }

/* garder : 1, la base d'une exécution précédente sert (un second lancement) ; 2, la base reste après (20 octobre 2026). */
static void servir_projet_base(const char *chemin, const char *base, Script *sc, int garder) {
    if (!(garder & 1)) {
        remove(base);
        char *cles = grym_formater("%s.clés", base);   /* une base chiffrée (2.37) : ses clés partent avec elle */
        remove(cles);
        free(cles);
    }
    char *src = lire_tout(chemin);
    total++;
    if (!src) { echecs++; printf("ÉCHEC : %s introuvable\n", chemin); return; }
    Transport t = { sc, recevoir, envoyer };
    Serveur *s = serveur_creer(PORT, JETON, "projet", t);
    Portee *portee = portee_creer();
    portee_fichier(portee, chemin);
    Machine *m = machine_creer();
    Interface i = serveur_interface(s);
    machine_interface(m, &i);
    machine_demande_mot_de_passe(m, serveur_demander_mot_de_passe, s);   /* comme grym servir (2.37) */
    const char *barre = strrchr(chemin, '/');
    char *dossier = grym_formater("%.*s", (int)(barre - chemin), chemin);
    machine_dossier(m, dossier);
    machine_base(m, base);
    Programme p;
    Diagnostic d;
    Chaine sortie = {0};
    char *erreur = NULL;
    if (!analyser(src, strlen(src), portee, 0, &p, &d)) {
        erreur = grym_formater("ANALYSE %s", d.message);
        diagnostic_liberer(&d);
    } else {
        Module *b = compiler(&p, &d);
        if (!b || !machine_executer(m, b, &sortie, &d)) {
            erreur = grym_formater("Erreur, ligne %d : %s", d.ligne, d.message);
            diagnostic_liberer(&d);
        }
        module_detruire(b);
        programme_liberer(&p);
    }
    char *annule = erreur ? machine_annulation(m, 0) : NULL;
    serveur_terminer(s, &sortie, erreur, annule);
    free(annule);
    free(erreur);
    free(sortie.d);
    free(dossier);
    free(src);
    machine_detruire(m);
    portee_detruire(portee);
    serveur_fermer(s);
    if (!(garder & 2)) {
        remove(base);
        char *cles = grym_formater("%s.clés", base);
        remove(cles);
        free(cles);
    }
}


/* Praxis et ses données d'essai (21 octobre 2026) : l'application livrée part vide ; les essais en font une copie,
 * « _essai_praxis », où les données d'essai (tests/praxis/essais.grym) sont branchées avant l'accueil. */
static int copier_fichier(const char *de, const char *vers) {
    FILE *a = fopen(de, "rb"), *b = a ? fopen(vers, "wb") : NULL;
    char tampon[65536];
    size_t n;
    int ok = a && b;
    while (ok && (n = fread(tampon, 1, sizeof tampon, a)) > 0) ok = fwrite(tampon, 1, n, b) == n;
    if (a) fclose(a);
    if (b) fclose(b);
    return ok;
}

static void copier_dossier(const char *de, const char *vers) {
#ifdef _WIN32
    _mkdir(vers);
#else
    mkdir(vers, 0755);
#endif
    DIR *d = opendir(de);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        const size_t l = strlen(e->d_name);
        if (strstr(e->d_name, ".grymd") || (l > 6 && !strcmp(e->d_name + l - 6, ".grymb"))) continue;   /* ni base ni clés */
        char a[1024], b[1024];
        snprintf(a, sizeof a, "%s/%s", de, e->d_name);
        snprintf(b, sizeof b, "%s/%s", vers, e->d_name);
        struct stat st;
        if (stat(a, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (strcmp(e->d_name, "Sauvegardes") && strcmp(e->d_name, "Exports")) copier_dossier(a, b);
        } else {
            copier_fichier(a, b);
        }
    }
    closedir(d);
}

static void preparer_praxis(void) {
    copier_dossier("projets/Praxis/Nicolas", "_essai_praxis");
    copier_fichier("tests/praxis/essais.grym", "_essai_praxis/essais.grym");
    char *src = lire_tout("_essai_praxis/Praxis.grym");
    if (!src) return;
    Chaine c = {0};
    const char *u = strstr(src, "Utiliser « actions ».\n"), *r = strstr(src, "Remarque : l'accueil de la maquette");
    if (!u || !r || r < u) { free(src); return; }
    u += strlen("Utiliser « actions ».\n");
    const char garde_u = *u;
    *(char *)u = 0;
    chaine_ajouter(&c, src);
    *(char *)u = garde_u;
    chaine_ajouter(&c, "Utiliser « essais ».\n");
    const char garde_r = *r;
    *(char *)r = 0;
    chaine_ajouter(&c, u);
    *(char *)r = garde_r;
    chaine_ajouter(&c, "Si le nombre de patients conservés est nul, remplir les essais.\n\n");
    chaine_ajouter(&c, r);
    FILE *f = fopen("_essai_praxis/Praxis.grym", "wb");
    if (f) { fwrite(c.d, 1, strlen(c.d), f); fclose(f); }
    free(c.d);
    free(src);
}

#define CONTIENT(r, f)     verifier(__LINE__, #r, r, f, 1)
#define NE_CONTIENT_PAS(r, f) verifier(__LINE__, #r, r, f, 0)
#define REQUETES(...) do { char *liste[] = { __VA_ARGS__ }; sc.n = sizeof liste / sizeof *liste; \
    sc.requetes = malloc(sizeof liste); memcpy(sc.requetes, liste, sizeof liste); } while (0)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    preparer_praxis();
    mot_de_passe_essais("essai-essai");   /* Praxis a sa base chiffrée (21 octobre 2026) */
    Script sc;

    /* Jeton : la première visite pose le cookie (nommé par le port) et nettoie l'adresse */
    memset(&sc, 0, sizeof sc);
    REQUETES(grym_dupliquer("GET /?jeton=" JETON " HTTP/1.1\r\n" HOTE "\r\n"),
             grym_dupliquer("GET /?jeton=ffffffffffffffffffffffffffffffff HTTP/1.1\r\n" HOTE "\r\n"),
             get("/"));
    servir("Afficher « bonjour ».", &sc);
    CONTIENT(sc.reponses[0], "HTTP/1.1 303 See Other");
    CONTIENT(sc.reponses[0], "Set-Cookie: grym_4242=" JETON "; HttpOnly; SameSite=Strict; Path=/");
    CONTIENT(sc.reponses[0], "Location: /\r\n");
    CONTIENT(sc.reponses[1], "403 Forbidden");
    NE_CONTIENT_PAS(sc.reponses[1], "Set-Cookie");
    CONTIENT(sc.reponses[2], "bonjour");
    CONTIENT(sc.reponses[2], "Application terminée.");
    liberer(&sc);

    /* Attaques : autre hôte (DNS), sans cookie, mauvais cookie, envoi sans ou avec une autre origine */
    memset(&sc, 0, sizeof sc);
    REQUETES(grym_dupliquer("GET / HTTP/1.1\r\nHost: evil.example:4242\r\n" COOKIE "\r\n"),
             grym_dupliquer("GET / HTTP/1.1\r\n" HOTE "\r\n"),
             grym_dupliquer("GET / HTTP/1.1\r\n" HOTE "Cookie: grym_4242=0123456789abcdef0123456789abcdee\r\n\r\n"),
             grym_dupliquer("GET / HTTP/1.1\r\n" HOTE "Cookie: grym_4243=" JETON "\r\n\r\n"),
             grym_dupliquer("POST /reponse HTTP/1.1\r\n" HOTE COOKIE "Content-Length: 11\r\n\r\nq=1&c0=12&"),
             grym_dupliquer("POST /reponse HTTP/1.1\r\n" HOTE COOKIE "Origin: http://evil.example\r\n"
                            "Content-Length: 10\r\n\r\nq=1&c0=12&"),
             grym_dupliquer("POST /reponse HTTP/1.1\r\n" HOTE COOKIE "Origin: null\r\n"
                            "Content-Length: 10\r\n\r\nq=1&c0=12&"),
             grym_dupliquer("GET / HTTP/1.1\r\n" HOTE HOTE COOKIE "\r\n"),
             grym_dupliquer("n'importe quoi\r\n\r\n"),
             get("/ailleurs"),
             get("/style.css"),
             post("q=1&c0=12&action=envoyer"),
             get("/"));
    servir("Le x vaut la réponse en nombre à « Âge ? ».\nAfficher x + 1.", &sc);
    for (int k = 0; k < 7; k++) CONTIENT(sc.reponses[k], "403 Forbidden");
    CONTIENT(sc.reponses[7], "400 Bad Request");
    CONTIENT(sc.reponses[8], "400 Bad Request");
    CONTIENT(sc.reponses[9], "404 Not Found");
    CONTIENT(sc.reponses[10], "text/css");
    CONTIENT(sc.reponses[11], "303 See Other");
    CONTIENT(sc.reponses[12], "13");
    /* chaque réponse porte la politique de contenu stricte */
    for (size_t k = 0; k < sc.nr; k++) CONTIENT(sc.reponses[k], "Content-Security-Policy: default-src 'none'");
    /* un script ne vient que de la page elle-même : jamais en ligne, jamais d'ailleurs */
    for (size_t k = 0; k < sc.nr; k++) CONTIENT(sc.reponses[k], "script-src 'self';");
    for (size_t k = 0; k < sc.nr; k++) NE_CONTIENT_PAS(sc.reponses[k], "unsafe-inline");
    for (size_t k = 0; k < sc.nr; k++) CONTIENT(sc.reponses[k], "X-Content-Type-Options: nosniff");
    /* same-origin, jamais no-referrer : sinon un navigateur envoie « Origin: null » avec chaque formulaire */
    for (size_t k = 0; k < sc.nr; k++) CONTIENT(sc.reponses[k], "Referrer-Policy: same-origin\r\n");
    liberer(&sc);

    /* Une question : la page, un refus qui garde la saisie, puis la bonne réponse */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"), post("q=1&c0=douze&action=envoyer"), get("/"), post("q=1&c0=12&action=envoyer"), get("/"));
    servir("Afficher « Bienvenue ».\nLe x vaut la réponse en nombre à « Âge ? ».\nAfficher x + 1.", &sc);
    CONTIENT(sc.reponses[0], "<pre class=\"sortie\">Bienvenue\n</pre>");
    CONTIENT(sc.reponses[0], "<input type=\"hidden\" name=\"q\" value=\"1\">");
    CONTIENT(sc.reponses[0], "<label for=\"c0\">Âge ?</label>");
    CONTIENT(sc.reponses[0], "autofocus");
    CONTIENT(sc.reponses[2], "« douze » n&#39;est pas un nombre.");
    CONTIENT(sc.reponses[2], "value=\"douze\"");
    NE_CONTIENT_PAS(sc.reponses[2], "Tape « . »");   /* une convention de la console */
    CONTIENT(sc.reponses[4], "Bienvenue\n13");
    liberer(&sc);

    /* Numéros : une réponse à une question close ne répond à rien (décision 6) */
    memset(&sc, 0, sizeof sc);
    REQUETES(post("q=1&c0=a&action=envoyer"), post("q=1&c0=b&action=envoyer"), get("/"),
             post("q=2&c0=c&action=envoyer"), get("/"));
    servir("Le x vaut la réponse à « Un ? ».\nLe y vaut la réponse à « Deux ? ».\nAfficher x puis y.", &sc);
    CONTIENT(sc.reponses[2], "Cette question a déjà reçu sa réponse.");
    CONTIENT(sc.reponses[2], "value=\"2\"");
    CONTIENT(sc.reponses[4], "a c");
    liberer(&sc);

    /* Annuler : un bouton, rattrapé par Essayer comme le point de la console */
    memset(&sc, 0, sizeof sc);
    REQUETES(post("q=1&c0=&action=annuler"), get("/"));
    servir("Essayer :\n    Le x vaut la réponse à « ? ».\nEn cas d'échec, afficher le motif de l'échec.", &sc);
    CONTIENT(sc.reponses[1], "Saisie annulée.");
    liberer(&sc);

    /* Échappement : rien d'affiché ni de saisi ne devient du HTML */
    memset(&sc, 0, sizeof sc);
    REQUETES(post("q=1&c0=%3Cb%3Egras%3C%2Fb%3E%22&action=envoyer"), get("/"));
    servir("Afficher « <script>alert(1)</script> ».\nLe x vaut la réponse à « ? ».\nAfficher x.", &sc);
    CONTIENT(sc.reponses[1], "&lt;script&gt;alert(1)&lt;/script&gt;");
    CONTIENT(sc.reponses[1], "&lt;b&gt;gras&lt;/b&gt;&quot;");
    NE_CONTIENT_PAS(sc.reponses[1], "<script>");
    NE_CONTIENT_PAS(sc.reponses[1], "<b>");
    liberer(&sc);

    /* Formulaire d'une entité : les champs acceptés restent, en lecture seule */
    memset(&sc, 0, sizeof sc);
    REQUETES(post("q=1&c0=Ana&c1=x&action=envoyer"), get("/"), post("q=1&c1=21.09.1990&action=envoyer"), get("/"));
    servir("Un contact, conservé, a : un nom (texte), unique, une naissance (date).\n"
           "Le c vaut un nouveau contact saisi.\nConserver c.\nAfficher nom du c puis naissance du c.", &sc);
    CONTIENT(sc.reponses[1], "name=\"c0\" value=\"Ana\" readonly");
    CONTIENT(sc.reponses[1], "n&#39;est pas une date");
    CONTIENT(sc.reponses[3], "Ana 21.09.1990");
    liberer(&sc);

    /* Modifier : valeur actuelle préremplie, case « vider » pour un champ facultatif */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"), post("q=1&c0=Ana&c1=21.09.1990&v1=1&action=envoyer"), get("/"));
    servir("Un contact, conservé, a : un nom (texte), une naissance (date), facultative.\n"
           "Le c vaut un nouveau contact :\n    Le nom vaut « Ana ».\n    La naissance vaut 01.01.2000.\nConserver c.\n"
           "Saisir à nouveau c.\nAfficher naissance du c.", &sc);
    CONTIENT(sc.reponses[0], "value=\"01.01.2000\"");
    CONTIENT(sc.reponses[0], "name=\"v1\" value=\"1\"> vider");
    CONTIENT(sc.reponses[2], "absent");
    liberer(&sc);

    /* Effacer l'écran vide l'affichage ; une erreur s'affiche dans la page finale */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"));
    servir("Afficher « avant ».\nEffacer l'écran.\nAfficher « après ».\nAfficher 1 ÷ 0.", &sc);
    NE_CONTIENT_PAS(sc.reponses[0], "avant");
    CONTIENT(sc.reponses[0], "après");
    CONTIENT(sc.reponses[0], "Erreur, ligne 4 : Division par zéro.");
    liberer(&sc);

    /* Plus de requêtes pendant une question : la fin de l'entrée, comme en console */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"));
    servir("Le x vaut la réponse à « Nom ? ».", &sc);
    total++;
    if (sc.nr != 1) { echecs++; printf("ÉCHEC : une seule réponse attendue\n"); }
    liberer(&sc);

    /* Corps mal encodé : refusé comme une réponse vide, jamais comme un octet nul */
    memset(&sc, 0, sizeof sc);
    REQUETES(post("q=1&c0=a%00b&action=envoyer"), post("q=1&c0=ok&action=envoyer"), get("/"));
    servir("Le x vaut la réponse à « ? ».\nAfficher « [ » suivi de x suivi de « ] ».", &sc);
    CONTIENT(sc.reponses[2], "[ok]");
    NE_CONTIENT_PAS(sc.reponses[2], "[a");
    liberer(&sc);

    /* --- v2.0-b (docs/v2.md, § 10) --- */
#define COMPOSITEURS "Un compositeur, conservé, a : un nom (texte), unique.\nUne pièce, conservée, a : un titre (texte), " \
    "un compositeur (compositeur), une date (date), facultative, un prix (nombre), une édition (vrai ou faux), " \
    "un tirage (vrai ou faux), facultatif.\nPour mettre un nom :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut nom.\n" \
    "    Conserver c.\nMettre « Zoé ».\nMettre « Bach ».\nMettre « élodie ».\nMettre « Oublié ».\n" \
    "Supprimer le compositeur conservé dont le nom est « Oublié ».\n"
    /* liens : les clés en suggestions, ordre du dictionnaire, corbeille exclue ; oui/non ; pavé numérique */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"), post("q=1&c0=T&c1=Zoé&c2=&c3=12,5&c4=non&c5=&action=envoyer"), get("/"));
    servir(COMPOSITEURS "La p vaut une nouvelle pièce saisie.\nConserver p.\n"
           "Afficher nom du compositeur de p puis prix de p puis édition de p puis tirage de p.", &sc);
    CONTIENT(sc.reponses[0], "<select id=\"c1\" name=\"c1\"><option value=\"\" selected></option><option value=\"Bach\">Bach</option>"
                             "<option value=\"élodie\">élodie</option><option value=\"Zoé\">Zoé</option></select>");
    NE_CONTIENT_PAS(sc.reponses[0], "<datalist");
    /* la mémoire de saisie du navigateur, rangée par nom de champ (c0, c1…), mélangerait toutes les questions */
    CONTIENT(sc.reponses[0], "<form class=\"question\" method=\"post\" action=\"/reponse\" autocomplete=\"off\">");
    NE_CONTIENT_PAS(sc.reponses[0], "Oublié");
    CONTIENT(sc.reponses[0], "name=\"c3\" value=\"\" inputmode=\"decimal\"");
    CONTIENT(sc.reponses[0], "<select id=\"c4\" name=\"c4\"><option value=\"\" selected></option><option value=\"oui\">oui</option>");
    CONTIENT(sc.reponses[0], "<select id=\"c5\" name=\"c5\"><option value=\"\" selected></option>");
    CONTIENT(sc.reponses[2], "Zoé 12,5 faux absent");
    liberer(&sc);

    /* modifier : le lien actuel est choisi ; dans la corbeille, il reste proposé et choisi, jamais perdu */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"), post("q=1&c0=T&c1=Oublié&c2=&c3=1&c4=oui&c5=&action=envoyer"), get("/"));
    servir(COMPOSITEURS "Rétablir le compositeur supprimé dont le nom est « Oublié ».\n"
           "La p vaut une nouvelle pièce :\n    Le titre vaut « T ».\n"
           "    Le compositeur vaut le compositeur conservé dont le nom est « Oublié ».\n"
           "    Le prix vaut 1.\n    L'édition vaut vrai.\nConserver p.\n"
           "Supprimer le compositeur conservé dont le nom est « Oublié ».\n"
           "Saisir à nouveau p.\nAfficher nom du compositeur de p.", &sc);
    CONTIENT(sc.reponses[0], "<select id=\"c1\" name=\"c1\"><option value=\"\"></option>"
                             "<option value=\"Oublié\" selected>Oublié</option><option value=\"Bach\">Bach</option>");
    liberer(&sc);

    /* plus de 1000 objets : le champ texte à suggestions revient, avec les 1000 premières clés */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"));
    servir("Un compositeur, conservé, a : un nom (texte), unique.\nUne pièce, conservée, a : un compositeur (compositeur).\n"
           "Pour peupler :\n    Pour chaque i de 1 à 1001 :\n        Le c vaut un nouveau compositeur :\n"
           "            Le nom vaut « C » suivi de i.\n        Conserver c.\n"
           "Peupler.\nLa p vaut une nouvelle pièce saisie.", &sc);
    CONTIENT(sc.reponses[0], "list=\"l0\" autocomplete=\"off\"");
    NE_CONTIENT_PAS(sc.reponses[0], "<select");
    liberer(&sc);

    /* téléversement : jamais sur le disque, le nom réduit à son dernier segment, l'image affichée à sa place */
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"),
             post_multi(PART("q", "1") PARTF("c0", "", "") PART("action", "envoyer") "--XyZ--\r\n"),
             post_multi(PART("q", "1") PARTF("c0", "notes.txt", "du texte") PART("action", "envoyer") "--XyZ--\r\n"),
             get("/"),
             post_multi(PART("q", "1") PARTF("c0", "../../etc/a.png", PNG) PART("action", "envoyer") "--XyZ--\r\n"),
             get("/"), get("/image/1"), get("/image/2"), get("/image/x"));
    servir("Une fiche, conservée, a : une photo (image).\nLa f vaut une nouvelle fiche saisie.\nConserver f.\n"
           "Afficher « Voici » puis photo de f puis « : » puis nom de fichier de photo de f.", &sc);
    CONTIENT(sc.reponses[0], "enctype=\"multipart/form-data\"");
    CONTIENT(sc.reponses[0], "<input type=\"file\" id=\"c0\" name=\"c0\" accept=\"image/png,image/jpeg,image/gif,image/webp\"");
    CONTIENT(sc.reponses[3], "n&#39;est pas une image");
    CONTIENT(sc.reponses[5], "Voici <img src=\"/image/1\" alt=\"une image PNG de 13 octets\"> : a.png");
    CONTIENT(sc.reponses[6], "Content-Type: image/png\r\n");
    CONTIENT(sc.reponses[6], "Content-Length: 13\r\n");
    CONTIENT(sc.reponses[6], "\r\n\r\n" PNG);
    CONTIENT(sc.reponses[7], "404 Not Found");
    CONTIENT(sc.reponses[8], "404 Not Found");
    liberer(&sc);

    /* aucun fichier choisi : une réponse est attendue ; modifier garde le fichier actuel */
    {
        FILE *f = fopen("_essai_contrat.txt", "wb");
        if (f) { fputs("contrat", f); fclose(f); }
    }
    memset(&sc, 0, sizeof sc);
    REQUETES(post_multi(PART("q", "1") PARTF("c0", "", "") PART("action", "envoyer") "--XyZ--\r\n"), get("/"),
             post_multi(PART("q", "1") PART("action", "annuler") "--XyZ--\r\n"),
             get("/"), post_multi(PART("q", "2") PARTF("c0", "", "") PART("action", "envoyer") "--XyZ--\r\n"), get("/"));
    servir("Une fiche, conservée, a : un contrat (fichier).\nLa f vaut une nouvelle fiche :\n"
           "    Le contrat vaut le fichier « _essai_contrat.txt ».\nConserver f.\n"
           "Essayer :\n    La g vaut une nouvelle fiche saisie.\nEn cas d'échec, afficher le motif de l'échec.\n"
           "Saisir à nouveau f.\nAfficher nom de fichier de contrat de f.", &sc);
    CONTIENT(sc.reponses[1], "Une réponse est attendue.");
    CONTIENT(sc.reponses[3], "Saisie annulée.");
    CONTIENT(sc.reponses[3], "<span class=\"actuel\">actuel : _essai_contrat.txt</span>");
    CONTIENT(sc.reponses[5], "_essai_contrat.txt\n</pre>");
    liberer(&sc);
    remove("_essai_contrat.txt");

    /* un envoi mal formé ; « Effacer l'écran » retire les images du fil */
    memset(&sc, 0, sizeof sc);
    REQUETES(grym_formater("POST /reponse HTTP/1.1\r\n" HOTE COOKIE ORIGINE
                           "Content-Type: multipart/form-data; boundary=XyZ\r\nContent-Length: 9\r\n\r\ncharabia!"),
             post_multi(PART("q", "1") PARTF("c0", "a.png", PNG) PART("action", "envoyer") "--XyZ--\r\n"),
             get("/"), get("/image/1"));
    servir("Une fiche, conservée, a : une photo (image).\nLa f vaut une nouvelle fiche saisie.\n"
           "Afficher photo de f.\nEffacer l'écran.\nAfficher « vide ».", &sc);
    CONTIENT(sc.reponses[0], "400 Bad Request");
    NE_CONTIENT_PAS(sc.reponses[2], "<img");
    CONTIENT(sc.reponses[3], "404 Not Found");
    liberer(&sc);

    /* --- La fiche d'un objet : un tableau, une image servie à part, tout échappé (§ 20) --- */
    {
        FILE *f = fopen("_essai_fiche.png", "wb");
        if (f) { fputs(PNG, f); fclose(f); }
    }
    memset(&sc, 0, sizeof sc);
    REQUETES(get("/"), get("/image/1"));
    servir("Un membre, conservé, a : un nom (texte), une photo (image), un parrain (membre), facultatif.\n"
           "Le m vaut un nouveau membre :\n    Le nom vaut « <b>Ana</b> ».\n    La photo vaut le fichier « _essai_fiche.png ».\n"
           "Afficher « avant ».\nAfficher la fiche de m.\nAfficher « après ».", &sc);
    CONTIENT(sc.reponses[0], "avant\n</pre><table class=\"fiche\"><caption>Membre</caption>");
    CONTIENT(sc.reponses[0], "<tr><th>Nom</th><td>&lt;b&gt;Ana&lt;/b&gt;</td></tr>");
    CONTIENT(sc.reponses[0], "<tr><th>Photo</th><td><img src=\"/image/1\" alt=\"une image PNG de 13 octets\"></td></tr>");
    CONTIENT(sc.reponses[0], "<tr><th>Parrain</th><td>absent</td></tr></table><pre class=\"sortie\">après");
    NE_CONTIENT_PAS(sc.reponses[0], "<b>Ana");
    CONTIENT(sc.reponses[1], "Content-Type: image/png");
    liberer(&sc);
    remove("_essai_fiche.png");


    /* --- W1 : les écrans dans le navigateur (docs/web.md, § 3 et § 5) --- */
    verifier_jetons();
    {
        const char *src =
            "Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative.\n"
            "Pour remplir un nom :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut nom.\n    Conserver c.\n"
            "Remplir « Liszt ».\nRemplir « <script>alert(1)</script> ».\n"
            "L'écran des compositeurs montre :\n    le texte « Nos compositeurs »,\n"
            "    la liste des compositeurs conservés, par nom,\n    un bouton « Nouveau »,\n    un bouton « Échouer »,\n"
            "    un bouton « Fermer ».\n"
            "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Ouvrir la fiche du compositeur.\n"
            "Quand on clique sur « Nouveau » dans l'écran des compositeurs :\n    Le c vaut un nouveau compositeur saisi.\n"
            "    Conserver c.\n"
            "Quand on clique sur « Échouer » dans l'écran des compositeurs :\n    Le e vaut un nouveau compositeur :\n"
            "        Le nom vaut « Fantôme ».\n    Conserver e.\n    Afficher 1 ÷ 0.\n"
            "Quand on clique sur « Fermer » dans l'écran des compositeurs :\n    Fermer l'écran.\n"
            "Ouvrir l'écran des compositeurs.\n"
            "Afficher le nombre de compositeurs conservés.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"),                                          /* 0 : l'écran, attente 1 */
                 evenement("q=1&choix=1.1"),                        /* 1 : Liszt → sa fiche par-dessus, attente 2 */
                 get("/"),                                          /* 2 : la fiche */
                 evenement("q=2&clic=3"),                           /* 3 : « Fermer » de la fiche, attente 3 */
                 get("/"),                                          /* 4 */
                 evenement("q=3&clic=2"),                           /* 5 : « Nouveau » : une question, n° 4 */
                 get("/"),                                          /* 6 : la question */
                 post("q=4&c0=Chopin&c1=&action=envoyer"),          /* 7 : attente 5 */
                 get("/"),                                          /* 8 */
                 evenement("q=5&clic=3"),                           /* 9 : « Échouer », attente 6 */
                 get("/"),                                          /* 10 */
                 evenement("q=1&clic=4"),                           /* 11 : un écran d'hier */
                 get("/"),                                          /* 12 */
                 grym_dupliquer("POST /evenement HTTP/1.1\r\n" HOTE COOKIE "Origin: http://evil.example\r\n"
                                "Content-Length: 10\r\n\r\nq=6&clic=4"),   /* 13 : autre origine */
                 evenement("q=6&fermer=1"),                         /* 14 : la croix : l'écran se ferme */
                 get("/"));                                         /* 15 : la page finale */
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<h1>Compositeurs</h1>");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Nos compositeurs</p>");
        CONTIENT(sc.reponses[0], "<form class=\"ecran\" method=\"post\" action=\"/evenement\" autocomplete=\"off\">"
                                 "<input type=\"hidden\" name=\"q\" value=\"1\">");
        CONTIENT(sc.reponses[0], "<th>Nom</th><th>Naissance</th>");
        /* par nom : « <script>… » avant « Liszt » ; échappé, jamais du HTML (docs/web.md, § 2.1) */
        CONTIENT(sc.reponses[0], "<button class=\"ligne\" name=\"choix\" value=\"1.0\">&lt;script&gt;alert(1)&lt;/script&gt;</button>");
        CONTIENT(sc.reponses[0], "<button class=\"ligne\" name=\"choix\" value=\"1.1\">Liszt</button>");
        NE_CONTIENT_PAS(sc.reponses[0], "<script>");
        CONTIENT(sc.reponses[0], "<div class=\"boutons\"><button name=\"clic\" value=\"2\">Nouveau</button>"
                                 "<button name=\"clic\" value=\"3\">Échouer</button><button name=\"clic\" value=\"4\">Fermer</button></div>");
        CONTIENT(sc.reponses[0], "<body class=\"accueil\">");
        NE_CONTIENT_PAS(sc.reponses[0], "<noscript>");   /* aucune zone : rien à valider */
        CONTIENT(sc.reponses[1], "303 See Other");
        /* la fiche, par-dessus : le fil rappelle l'écran du dessous */
        CONTIENT(sc.reponses[2], "<p class=\"fil\">Compositeurs</p>");
        CONTIENT(sc.reponses[2], "<h1>Liszt</h1>");
        CONTIENT(sc.reponses[2], "<p class=\"texte\">Nom : Liszt</p>");
        CONTIENT(sc.reponses[2], "<button name=\"clic\" value=\"3\">Fermer</button>");
        NE_CONTIENT_PAS(sc.reponses[2], "class=\"accueil\"");
        /* retour à l'écran : la ligne choisie le reste */
        CONTIENT(sc.reponses[4], "<tr class=\"choisie\"><td class=\"selection\"><input type=\"radio\" name=\"s1\" value=\"1\" "
                                 "aria-label=\"Sélectionner la ligne 2\" checked></td><td><button class=\"ligne\" name=\"choix\" value=\"1.1\">Liszt");
        NE_CONTIENT_PAS(sc.reponses[4], "class=\"fil\"");
        CONTIENT(sc.reponses[6], "<form class=\"question\"");
        CONTIENT(sc.reponses[8], "value=\"1.1\">Chopin</button>");
        /* Chopin s'insère au-dessus de Liszt : le choix suit Liszt, pas son ancien rang */
        CONTIENT(sc.reponses[8], "<tr class=\"choisie\"><td class=\"selection\"><input type=\"radio\" name=\"s1\" value=\"2\" "
                                 "aria-label=\"Sélectionner la ligne 3\" checked></td><td><button class=\"ligne\" name=\"choix\" value=\"1.2\">Liszt");
        /* l'événement raté : le message sous le titre, rien de conservé, l'écran reste ouvert */
        CONTIENT(sc.reponses[10], "<p class=\"message\" role=\"alert\">Division par zéro.</p>");
        NE_CONTIENT_PAS(sc.reponses[10], "Fantôme");
        CONTIENT(sc.reponses[11], "303 See Other");
        CONTIENT(sc.reponses[12], "<p class=\"avis\">Cet écran a changé depuis : voici son état actuel.</p>");
        CONTIENT(sc.reponses[13], "403 Forbidden");
        CONTIENT(sc.reponses[15], "Application terminée.");
        CONTIENT(sc.reponses[15], "3\n");
        NE_CONTIENT_PAS(sc.reponses[15], "class=\"erreur\"");
        liberer(&sc);
    }
    {   /* Zones : un envoi porte chaque zone modifiée, puis le bouton ; un changement refusé retient le bouton */
        const char *src =
            "Un compositeur, conservé, a : un nom (texte), unique, un pays (texte).\n"
            "Pour remplir un nom et un pays :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut nom.\n"
            "        Le pays vaut pays.\n    Conserver c.\n"
            "Remplir « Bach » et « Allemagne ».\nRemplir « Chopin » et « Pologne ».\nRemplir « Schumann » et « Allemagne ».\n"
            "L'écran de recherche montre :\n    un pays (texte), « Suisse » au départ,\n    un âge (nombre), facultatif,\n"
            "    la liste des compositeurs conservés dont le pays est le pays de l'écran, par nom,\n"
            "    un bouton « Supprimer »,\n    un bouton « Fermer ».\n"
            "Quand on ouvre l'écran de recherche :\n    Le pays de l'écran devient « Allemagne ».\n"
            "Quand on change le pays dans l'écran de recherche :\n    Afficher « pays » puis le pays de l'écran.\n"
            "Quand on clique sur « Supprimer » dans l'écran de recherche :\n"
            "    Si le compositeur choisi de l'écran est présent, supprimer le compositeur choisi de l'écran.\n"
            "Quand on clique sur « Fermer » dans l'écran de recherche :\n    Fermer l'écran.\n"
            "Quand on ferme l'écran de recherche :\n    Afficher « fermé » puis l'âge de l'écran.\n"
            "Ouvrir l'écran de recherche.\nAfficher le nombre de compositeurs conservés.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"),                                                    /* 0 */
                 evenement("q=1&z0=Pologne&z1=&valider=1"),                   /* 1 : Entrée dans une zone */
                 get("/"),                                                    /* 2 */
                 evenement("q=2&z0=Pologne&z1=&choix=2.0"),                   /* 3 : Chopin choisi */
                 evenement("q=3&z0=Pologne&z1=abc&clic=3"),                   /* 4 : refusé : pas de suppression */
                 get("/"),                                                    /* 5 */
                 evenement("q=4&z0=Pologne&z1=12%2C5&clic=3"),                /* 6 : l'âge, puis la suppression */
                 get("/"),                                                    /* 7 */
                 evenement("q=5&z0=Pologne&z1=12%2C5&valider=1"),             /* 8 : rien n'a changé : rien ne part */
                 evenement("q=5&z0=Pologne&z1=12%2C5&clic=4"),                /* 9 : Fermer */
                 get("/"));                                                   /* 10 */
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<label for=\"z0\">Pays<span class=\"requis\" title=\"Obligatoire\"> *</span></label>"
                                 "<input type=\"text\" id=\"z0\" name=\"z0\" value=\"Allemagne\" aria-required=\"true\">");
        CONTIENT(sc.reponses[0], "name=\"z1\" value=\"\" inputmode=\"decimal\">");
        CONTIENT(sc.reponses[0], "value=\"2.1\">Schumann</button>");
        CONTIENT(sc.reponses[0], "<button class=\"invisible\" name=\"valider\"");
        /* un menu changé valide les zones par le script ; sans script, par un bouton visible */
        CONTIENT(sc.reponses[0], "<script src=\"/grym.js\" defer></script>");
        CONTIENT(sc.reponses[0], "<noscript><div class=\"boutons\"><button name=\"valider\" value=\"1\">Valider les zones</button>");
        CONTIENT(sc.reponses[2], "value=\"2.0\">Chopin</button>");
        CONTIENT(sc.reponses[2], "<pre class=\"sortie\">pays Pologne\n</pre>");
        CONTIENT(sc.reponses[5], "<div class=\"resume-erreurs\" role=\"alert\"><p>Il y a un problème :</p>"
                                 "<ul><li><a href=\"#z1\">« abc » n&#39;est pas un nombre.</a></li></ul></div>");
        CONTIENT(sc.reponses[5], "<p class=\"erreur-zone\" id=\"z1e\">« abc » n&#39;est pas un nombre.</p>");
        CONTIENT(sc.reponses[5], "aria-describedby=\"z1e\" aria-invalid=\"true\"");
        CONTIENT(sc.reponses[5], "value=\"2.0\">Chopin</button>");   /* toujours là : le clic n'est pas parti */
        CONTIENT(sc.reponses[7], "<td class=\"vide\" colspan=\"3\">Aucune ligne.</td>");
        CONTIENT(sc.reponses[7], "name=\"z1\" value=\"12,5\"");
        NE_CONTIENT_PAS(sc.reponses[7], "class=\"message\"");
        CONTIENT(sc.reponses[10], "fermé 12,5\n2\n");
        liberer(&sc);
    }
    {   /* Une case à cocher : décochée, elle n'envoie rien ; « …p » dit qu'elle était là */
        const char *src =
            "L'écran d'essai montre :\n    un actif (vrai ou faux),\n    un bouton « OK ».\n"
            "Quand on change l'actif dans l'écran d'essai :\n    Afficher « actif » puis l'actif de l'écran.\n"
            "Quand on clique sur « OK » dans l'écran d'essai :\n    Fermer l'écran.\n"
            "Ouvrir l'écran d'essai.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0p=1&z0=oui&valider=1"), get("/"), evenement("q=2&z0p=1&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"hidden\" name=\"z0p\" value=\"1\"><label class=\"case\">"
                                 "<input type=\"checkbox\" name=\"z0\" value=\"oui\"> Actif</label>");
        CONTIENT(sc.reponses[2], "value=\"oui\" checked> Actif");
        CONTIENT(sc.reponses[4], "actif vrai\nactif faux\n");
        liberer(&sc);
    }
    {   /* États (§ 22.6), groupe et boutons radio (§ 22.7) : ce que la page montre, et ce qu'elle refuse d'envoyer */
        const char *src = "L'écran de saisie montre :\n"
                          "    le groupe « Statut » :\n"
                          "        un état (« actif », « suspendu » ou « retiré »), « actif » au départ,\n"
                          "    une remarque (texte), facultative, sur plusieurs lignes,\n"
                          "    un total (nombre), 0 au départ, en lecture seule,\n"
                          "    le texte « Caché », visible si faux,\n"
                          "    un bouton « Valider », actif si la remarque de l'écran est présente,\n"
                          "    un bouton « Fermer ».\n"
                          "Quand on clique sur « Valider » dans l'écran de saisie :\n    Afficher « validé ».\n"
                          "Quand on clique sur « Fermer » dans l'écran de saisie :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran de saisie.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z1=&clic=6"), get("/"), evenement("q=2&clic=7"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<fieldset class=\"groupe\"><legend>Statut</legend>");
        CONTIENT(sc.reponses[0], "</fieldset>");
        CONTIENT(sc.reponses[0], "<input type=\"radio\" name=\"z1\" value=\"actif\" checked aria-required=\"true\"> actif</label>");
        NE_CONTIENT_PAS(sc.reponses[0], "value=\"\">");   /* zone obligatoire : pas de « — » */
        CONTIENT(sc.reponses[0], "<textarea rows=\"4\" id=\"z3\" name=\"z3\">");
        CONTIENT(sc.reponses[0], "name=\"z4\" value=\"0\" inputmode=\"decimal\" readonly>");
        NE_CONTIENT_PAS(sc.reponses[0], "Caché");
        CONTIENT(sc.reponses[0], "<button name=\"clic\" value=\"6\" disabled>Valider</button>");
        NE_CONTIENT_PAS(sc.reponses[2], "validé");   /* un clic forgé sur un bouton inactif ne compte pas */
        CONTIENT(sc.reponses[4], "Application terminée.");
        liberer(&sc);
    }

    {   /* L'image (§ 22.8) : servie à part, à l'adresse de sa série ; une autre série ou un autre élément : 404 */
        const char *src = "L'écran x montre :\n    l'image « exemples/pixel.png »,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/ecran-image/1/0"), get("/ecran-image/2/0"), get("/ecran-image/1/1"), evenement("q=1&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<p class=\"image\"><img src=\"/ecran-image/1/0\" alt=\"\"></p>");
        CONTIENT(sc.reponses[1], "Content-Type: image/png");
        CONTIENT(sc.reponses[2], "404 Not Found");
        CONTIENT(sc.reponses[3], "404 Not Found");
        CONTIENT(sc.reponses[5], "Application terminée.");
        liberer(&sc);
    }

    {   /* Un son (§ 15.7) : un lecteur dans le fil et dans la fiche, servi avec son type ; une zone qui ouvre le micro */
        FILE *w = fopen("_essai_memo.wav", "wb");
        static const unsigned char WAV[] = { 'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
        if (w) { fwrite(WAV, 1, sizeof WAV, w); fclose(w); }
        const char *src = "Une visite, conservée, a :\n    un lieu (texte),\n    un mémo (son).\n"
                          "Le v vaut une nouvelle visite :\n    Le lieu vaut « Bulle ».\n    Le mémo vaut le fichier « _essai_memo.wav ».\n"
                          "Afficher le mémo de v.\nAfficher la fiche de v.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/image/1"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<audio controls preload=\"metadata\" src=\"/image/");
        CONTIENT(sc.reponses[0], "</audio></td>");   /* dans la fiche aussi */
        CONTIENT(sc.reponses[1], "audio/wav");
        CONTIENT(sc.reponses[0], "media-src 'self'");   /* sans elle, default-src 'none' bloquait tout lecteur */
        liberer(&sc);
        /* la fiche déduite (§ 22.3) : le lecteur aussi, servi comme les images de l'écran ; la page d'avant s'en va */
        const char *src3 = "Une visite, conservée, a :\n    un lieu (texte),\n    un mémo (son).\n"
                           "Le v vaut une nouvelle visite :\n    Le lieu vaut « Bulle ».\n    Le mémo vaut le fichier « _essai_memo.wav ».\n"
                           "Conserver v.\nOuvrir la fiche de v.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/ecran-image/1/1"), get("/ecran-image/1/0"), evenement("q=1&clic=3"), get("/"));
        servir(src3, &sc);
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Mémo : <audio controls preload=\"metadata\" src=\"/ecran-image/1/1\" "
                                 "aria-label=\"un fichier de 16 octets\"></audio></p>");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Lieu : Bulle</p>");
        CONTIENT(sc.reponses[1], "audio/wav");
        CONTIENT(sc.reponses[2], "404 Not Found");   /* un texte sans octets ne se sert pas */
        liberer(&sc);
        remove("_essai_memo.wav");
        const char *src2 = "L'écran x montre :\n    un mémo (son),\n    un bouton « OK ».\n"
                           "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=1"), get("/"));
        servir(src2, &sc);
        CONTIENT(sc.reponses[0], "accept=\"audio/*\" capture");
        liberer(&sc);
    }

    {   /* Le graphique par mois (§ 22.11) : les tâches créées dans le désordre, les mois dans l'ordre du calendrier,
         * une échéance absente à la fin */
        const char *src = "Une tâche, conservée, a :\n    un nom (texte),\n    une échéance (date), facultative.\n"
                          "Le a vaut une nouvelle tâche :\n    Le nom vaut « a ».\n    L'échéance vaut 03.11.2026.\nConserver a.\n"
                          "Le b vaut une nouvelle tâche :\n    Le nom vaut « b ».\nConserver b.\n"
                          "Le c vaut une nouvelle tâche :\n    Le nom vaut « c ».\n    L'échéance vaut 20.09.2026.\nConserver c.\n"
                          "Le d vaut une nouvelle tâche :\n    Le nom vaut « d ».\n    L'échéance vaut 28.11.2026.\nConserver d.\n"
                          "L'écran x montre :\n    le graphique des tâches conservées, par mois de l'échéance,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=1"), get("/"));
        servir(src, &sc);
        const char *sep = strstr(sc.reponses[0], "septembre 2026"), *nov = sep ? strstr(sep, "novembre 2026") : NULL;
        CONTIENT(sc.reponses[0], "septembre 2026");
        CONTIENT(sep ? sep : "", "novembre 2026");   /* après septembre */
        CONTIENT(nov ? nov : "", "—");                /* l'échéance absente, à la fin */
        NE_CONTIENT_PAS(sc.reponses[0], "2026-11");     /* la clé triable ne se montre pas */
        liberer(&sc);
    }

    {   /* La fiche d'un membre (§ 16.1) : le mot de passe s'y montre « •••••• », jamais son empreinte */
        const char *src = "Un membre, conservé, a :\n    un nom (texte),\n    un code (mot de passe).\n"
                          "Le m vaut un nouveau membre :\n    Le nom vaut « Anne ».\n    Le code vaut « s3cret ».\nAfficher la fiche de m.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "••••••");
        NE_CONTIENT_PAS(sc.reponses[0], "pbkdf2");
        NE_CONTIENT_PAS(sc.reponses[0], "s3cret");
        liberer(&sc);
    }

    {   /* Une zone (mot de passe) : masquée d'elle-même (§ 16.1) */
        const char *src = "L'écran x montre :\n    un code (mot de passe),\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=a&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"password\" id=\"z0\"");
        liberer(&sc);
    }

    {   /* Les étiquettes (§ 16.15) : une valeur permise colorée devient une étiquette dans une liste ; sans couleur, un texte.
         * La feuille porte leurs règles, et celles du résumé des erreurs (oubliées une fois) */
        const char *src = "Une cotisation, conservée, a :\n    un membre (texte),\n    un état (« payée » en vert, « en retard » en rouge ou « annulée »).\n"
                          "Le a vaut une nouvelle cotisation :\n    Le membre vaut « Anne ».\n    L'état vaut « payée ».\nConserver a.\n"
                          "Le b vaut une nouvelle cotisation :\n    Le membre vaut « Bruno ».\n    L'état vaut « en retard ».\nConserver b.\n"
                          "Le c vaut une nouvelle cotisation :\n    Le membre vaut « Cléa ».\n    L'état vaut « annulée ».\nConserver c.\n"
                          "L'écran x montre :\n    la liste des cotisations conservées, avec le membre et l'état,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), evenement("q=1&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<td><span class=\"etiquette etiquette-v\">payée</span></td>");
        CONTIENT(sc.reponses[0], "<td><span class=\"etiquette etiquette-r\">en retard</span></td>");
        CONTIENT(sc.reponses[0], "<td>annulée</td>");
        CONTIENT(sc.reponses[1], ".etiquette-v{background:#d7f0dd;color:#1e5b2e}");
        CONTIENT(sc.reponses[1], ".resume-erreurs{border-left:4px solid #b3261e;");
        CONTIENT(sc.reponses[1], ".zone .erreur-zone{color:#b3261e;");
        liberer(&sc);
    }

    {   /* Les fiches (§ 22.3, § 22.13, § 16.15) : l'état en étiquette, le courriel en lien d'écriture, le site en lien ;
         * dans la fiche affichée comme dans la fiche déduite. Un téléphone et un texte restent des textes. */
        const char *src = "Un contact, conservé, a :\n    un nom (texte),\n    un courriel (courriel),\n    un site (adresse web),\n"
                          "    un portable (téléphone),\n    un état (« actif » en vert ou « parti »).\n"
                          "Le a vaut un nouveau contact :\n    Le nom vaut « Anne ».\n    Le courriel vaut « anne+club@tilleul.ch ».\n"
                          "    Le site vaut « https://tilleul.ch/?a=1&b=2 ».\n    Le portable vaut « 079 123 45 67 ».\n"
                          "    L'état vaut « actif ».\nConserver a.\nAfficher la fiche de a.\nOuvrir la fiche de a.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=6"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[2], "<td><a href=\"mailto:anne+club@tilleul.ch\">anne+club@tilleul.ch</a></td>");
        CONTIENT(sc.reponses[2], "<td><a href=\"https://tilleul.ch/?a=1&amp;b=2\" target=\"_blank\" rel=\"noopener noreferrer\">");
        CONTIENT(sc.reponses[2], "<td><span class=\"etiquette etiquette-v\">actif</span></td>");
        CONTIENT(sc.reponses[2], "<td>079 123 45 67</td>");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Courriel : <a href=\"mailto:anne+club@tilleul.ch\">");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Site : <a href=\"https://tilleul.ch/?a=1&amp;b=2\"");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">État : <span class=\"etiquette etiquette-v\">actif</span></p>");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Portable : 079 123 45 67</p>");
        CONTIENT(sc.reponses[0], "<p class=\"texte\">Nom : Anne</p>");
        liberer(&sc);
    }

    {   /* Le résumé des erreurs (§ 22.6) : un événement qui lit deux zones obligatoires vides les signale toutes deux d'un
         * coup, en haut et sous chacune ; « Fermer », qui n'en lit aucune, n'est pas bloqué */
        const char *src = "L'écran x montre :\n    un nom (texte),\n    une ville (texte),\n    une note (texte), facultative,\n"
                          "    un bouton « OK »,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher le nom de l'écran puis la ville de l'écran.\n"
                          "    Afficher la note de l'écran.\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=3"), get("/"), evenement("q=2&clic=4"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[2], "<p>Il y a 2 problèmes :</p><ul><li><a href=\"#z0\">Remplis « Nom » : cette zone est obligatoire.</a></li>"
                                 "<li><a href=\"#z1\">Remplis « Ville » : cette zone est obligatoire.</a></li></ul>");
        CONTIENT(sc.reponses[2], "<p class=\"erreur-zone\" id=\"z1e\">Remplis « Ville » : cette zone est obligatoire.</p>");
        NE_CONTIENT_PAS(sc.reponses[2], "id=\"z2e\"");   /* la note, facultative, n'est pas en faute */
        CONTIENT(sc.reponses[4], "Application terminée.");   /* Fermer, qui ne lit aucune zone, passe */
        liberer(&sc);
    }

    {   /* L'unité (§ 22.6) : de part ou d'autre du champ ; tapée avec la valeur (« 12 CHF »), elle est ôtée */
        const char *src = "L'écran x montre :\n    un montant (nombre), avec l'unité « CHF » devant,\n"
                          "    une remise (nombre), facultative, avec l'unité « % »,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher « reçu : » puis le montant de l'écran + 1.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=12%20CHF&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<span class=\"avec-unite\"><span class=\"unite\" aria-hidden=\"true\">CHF</span><input type=\"text\" id=\"z0\"");
        CONTIENT(sc.reponses[0], "aria-label=\"Montant, CHF\"");
        CONTIENT(sc.reponses[0], "><span class=\"unite\" aria-hidden=\"true\">%</span></span>");
        CONTIENT(sc.reponses[2], "reçu : 13");
        liberer(&sc);
    }

    {   /* Courriel, téléphone (§ 16.1) : le bon clavier, sans bloquer l'envoi ; une forme fausse refusée, une juste acceptée */
        const char *src = "L'écran x montre :\n    un courriel (courriel),\n    un portable (téléphone), facultatif,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher « reçu : » puis le courriel de l'écran.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=anne"), get("/"), evenement("q=2&z0=anne%40tilleul.ch&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"text\" id=\"z0\" name=\"z0\" value=\"\" inputmode=\"email\" autocomplete=\"email\"");
        CONTIENT(sc.reponses[0], "<input type=\"tel\" id=\"z1\"");
        NE_CONTIENT_PAS(sc.reponses[0], "type=\"email\"");
        CONTIENT(sc.reponses[2], "« anne » n&#39;est pas un courriel (nom@domaine.ch).");
        CONTIENT(sc.reponses[4], "reçu : anne@tilleul.ch");
        liberer(&sc);
    }

    {   /* Le masque (§ 22.6) : un champ de mot de passe ; la valeur ne revient jamais dans la page ; vide, il n'efface rien */
        const char *src = "L'écran x montre :\n    un code (texte), masqué,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher « gardé : » puis le code de l'écran.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=s3cret"), get("/"), evenement("q=2&z0=&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"password\" id=\"z0\" name=\"z0\" value=\"\"");
        NE_CONTIENT_PAS(sc.reponses[2], "s3cret");   /* jamais renvoyée dans la page */
        CONTIENT(sc.reponses[2], "placeholder=\"••••••\"");
        CONTIENT(sc.reponses[4], "gardé : s3cret");   /* le champ renvoyé vide ne l'a pas effacée */
        liberer(&sc);
    }

    {   /* Les bornes (§ 22.6) : hors bornes, refusé avec la règle ; dedans, accepté ; un texte a sa longueur de frappe */
        const char *src = "L'écran x montre :\n    une quantité (nombre), de 1 à 99,\n    un code (texte), au plus 4 caractères,\n"
                          "    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher « reçu : » puis la quantité de l'écran.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=150&z1=1700"), get("/"), evenement("q=2&z0=12&z1=1700&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "name=\"z1\" value=\"\" aria-required=\"true\" maxlength=\"4\"");
        CONTIENT(sc.reponses[2], "« Quantité » va de 1 à 99.");
        CONTIENT(sc.reponses[4], "reçu : 12");
        liberer(&sc);
    }

    {   /* La recherche (§ 22.6) : demandée, un champ relié à ses propositions ; une saisie désigne l'objet. Puis automatique,
         * au-delà de vingt objets, sans rien écrire */
        const char *src = "Un membre, conservé, a :\n    un nom (texte), unique.\n"
                          "Le a vaut un nouveau membre :\n    Le nom vaut « Anne ».\nConserver a.\n"
                          "Le b vaut un nouveau membre :\n    Le nom vaut « Bruno ».\nConserver b.\n"
                          "L'écran x montre :\n    un membre (membre), avec recherche,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher « trouvé : » puis le nom du membre de l'écran.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=Bruno&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"text\" id=\"z0\" name=\"z0\" list=\"z0l\" value=\"\"");
        CONTIENT(sc.reponses[0], "<datalist id=\"z0l\"><option value=\"Anne\"><option value=\"Bruno\"></datalist>");
        CONTIENT(sc.reponses[2], "trouvé : Bruno");
        liberer(&sc);
        Chaine p = {0};
        chaine_ajouter(&p, "Un membre, conservé, a :\n    un nom (texte), unique.\n");
        for (int i = 1; i <= 21; i++) {
            char l[160];
            snprintf(l, sizeof l, "Le m%d vaut un nouveau membre :\n    Le nom vaut « M%02d ».\nConserver m%d.\n", i, i, i);
            chaine_ajouter(&p, l);
        }
        chaine_ajouter(&p, "L'écran x montre :\n    un membre (membre),\n    un bouton « OK ».\n"
                           "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n");
        char *src2 = chaine_rendre(&p);
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=1"), get("/"));
        servir(src2, &sc);
        CONTIENT(sc.reponses[0], "list=\"z0l\"");   /* 21 objets : la recherche d'elle-même */
        NE_CONTIENT_PAS(sc.reponses[0], "<select");
        liberer(&sc);
        free(src2);
    }

    {   /* L'indication (§ 22.6) : sous le libellé, désignée par le champ ; ses lettres (« mpsi ») ne deviennent pas des options */
        const char *src = "L'écran x montre :\n    un code (texte), avec l'indication « mpsi : 4 chiffres »,\n"
                          "    un actif (vrai ou faux), avec l'indication « coché : visible »,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=1700&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "</label><p class=\"indication\" id=\"z0i\">mpsi : 4 chiffres</p><input type=\"text\" id=\"z0\" name=\"z0\"");
        CONTIENT(sc.reponses[0], "aria-describedby=\"z0i\"");
        NE_CONTIENT_PAS(sc.reponses[0], "<textarea");                  /* le « m » n'a pas fait une zone sur plusieurs lignes */
        NE_CONTIENT_PAS(sc.reponses[0], "z0\" value=\"\" readonly"); /* ni le « s » une lecture seule */
        CONTIENT(sc.reponses[0], "<p class=\"indication\" id=\"z1i\">coché : visible</p>");
        CONTIENT(sc.reponses[2], "Application terminée.");
        liberer(&sc);
    }

    {   /* La taille du titre (§ 22.5) : une classe sur le titre, sa règle dans la feuille */
        const char *src = "L'écran x, « Recherche » en grand, montre :\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), evenement("q=1&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<h1 class=\"titre-grand\">Recherche</h1>");
        CONTIENT(sc.reponses[1], ".entete h1.titre-grand{font-size:40px;line-height:46px;font-weight:700}");   /* 2.17 */
        liberer(&sc);
    }

    {   /* Des choix multiples (§ 22.1) : une case par genre conservé ; cocher deux cases ; l'événement lit « Pour chaque genre
         * de l'écran » et « le nombre de genres de l'écran », en mémoire (un écran n'est jamais conservé) */
        const char *src = "Un genre, conservé, a :\n    un nom (texte), unique.\n"
                          "Le a vaut un nouveau genre :\n    Le nom vaut « baroque ».\nConserver a.\n"
                          "Le b vaut un nouveau genre :\n    Le nom vaut « sacré ».\nConserver b.\n"
                          "Le c vaut un nouveau genre :\n    Le nom vaut « fugue ».\nConserver c.\n"
                          "L'écran x montre :\n    des genres (genre),\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Pour chaque genre de l'écran :\n"
                          "        Afficher « choisi : » puis le nom du genre.\n    Afficher le nombre de genres de l'écran.\n"
                          "    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0p=1&z0=baroque&z0=fugue"), get("/"), evenement("q=2&z0p=1&z0=baroque&z0=fugue&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<div class=\"choix-multiples\" role=\"group\"><label class=\"case\"><input type=\"checkbox\" name=\"z0\" value=\"baroque\">");
        CONTIENT(sc.reponses[2], "value=\"baroque\" checked>");   /* la valeur revient cochée */
        CONTIENT(sc.reponses[2], "value=\"sacré\">");
        CONTIENT(sc.reponses[2], "value=\"fugue\" checked>");
        CONTIENT(sc.reponses[4], "choisi : baroque");
        CONTIENT(sc.reponses[4], "choisi : fugue");
        NE_CONTIENT_PAS(sc.reponses[4], "choisi : sacré");
        CONTIENT(sc.reponses[4], "2");
        liberer(&sc);
    }

    {   /* L'interrupteur (§ 22.6) : une case annoncée comme un interrupteur ; la bascule change la zone */
        const char *src = "L'écran x montre :\n    un actif (vrai ou faux), en interrupteur,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher l'actif de l'écran.\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), evenement("q=1&z0p=1&z0=oui&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<label class=\"case interrupteur\"><input type=\"checkbox\" role=\"switch\" name=\"z0\"");
        CONTIENT(sc.reponses[1], ".interrupteur input:checked{background:var(--accent)}");
        CONTIENT(sc.reponses[3], "vrai");
        liberer(&sc);
    }

    {   /* Une zone d'image : un champ de fichier (images seules), l'envoi en multipart ; le fichier choisi devient la valeur de
         * la zone, son nom s'affiche ; un fichier qui n'est pas une image est refusé, l'écran le dit */
        const char *src = "L'écran x montre :\n    une photo (image), facultative,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher la photo de l'écran.\n    Fermer l'écran.\n"
                          "Ouvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement_multi(PART("q", "1") PARTF("z0", "notes.txt", "pas une image") "--XyZ--\r\n"), get("/"),
                 evenement_multi(PART("q", "2") PARTF("z0", "plan.png", PNG) "--XyZ--\r\n"), get("/"),
                 evenement_multi(PART("q", "3") PARTF("z0", "", "") PART("clic", "1") "--XyZ--\r\n"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "enctype=\"multipart/form-data\"");
        CONTIENT(sc.reponses[0], "<input type=\"file\" id=\"z0\" name=\"z0\" accept=\"image/png,image/jpeg,image/gif,image/webp\"");
        CONTIENT(sc.reponses[2], "« notes.txt » n&#39;est pas une image (PNG, JPEG, GIF ou WebP).");
        CONTIENT(sc.reponses[4], "<span class=\"fichier-actuel\">une image PNG de 13 octets</span>");   /* le fichier choisi, gardé */
        CONTIENT(sc.reponses[6], "alt=\"une image PNG de 13 octets\"");   /* aucun nouveau choix : l'image reste */
        CONTIENT(sc.reponses[6], "Application terminée.");
        liberer(&sc);
    }

    {   /* Une heure comme valeur de départ (§ 14.7) : la zone s'ouvre à 8 h ; l'événement la lit */
        const char *src = "L'écran x montre :\n    un début (heure), 08:00 au départ,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran x :\n    Afficher le début de l'écran.\n    Fermer l'écran.\n"
                          "Ouvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=08:00&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<input type=\"text\" id=\"z0\" name=\"z0\" value=\"08:00\"");
        CONTIENT(sc.reponses[2], "8:00");
        liberer(&sc);
    }

    {   /* Le menu (§ 22.12) : en tête de page même écrit en dernier, un <details> par menu ; ses boutons déclenchent leurs
         * événements ; « actif si » s'y applique */
        const char *src = "L'écran x montre :\n    le texte « t »,\n    le menu « Membres » :\n"
                          "        un bouton « Bonjour »,\n        un bouton « Fermer », actif si vrai.\n"
                          "Quand on clique sur « Bonjour » dans l'écran x :\n    Afficher « salut ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=2"), get("/"), evenement("q=2&clic=3"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "</div>\n<nav class=\"menus\">\n<details class=\"menu\"><summary>Membres</summary>\n");   /* sous le titre */
        CONTIENT(sc.reponses[0], "<button name=\"clic\" value=\"2\">Bonjour</button>");
        CONTIENT(sc.reponses[0], "</details>\n</nav>\n<p class=\"texte\">t</p>");
        CONTIENT(sc.reponses[2], "salut");
        CONTIENT(sc.reponses[4], "Application terminée.");
        liberer(&sc);
    }

    {   /* Le graphique (§ 22.11) : compté par état, dans l'ordre d'apparition, « — » pour un état vide ; additionné en
         * secteurs ; un tableau pour les lecteurs d'écran ; pas de style dans la page */
        const char *src = "Un état, conservé, a :\n    un nom (texte), unique.\n"
                          "Une tâche, conservée, a :\n    un titre (texte),\n    un état (état), facultatif,\n    une durée (nombre).\n"
                          "Le a vaut un nouvel état :\n    Le nom vaut « à faire ».\nConserver a.\n"
                          "Le f vaut un nouvel état :\n    Le nom vaut « fait ».\nConserver f.\n"
                          "Le t1 vaut une nouvelle tâche :\n    Le titre vaut « un ».\n    L'état vaut a.\n    La durée vaut 3.\nConserver t1.\n"
                          "Le t2 vaut une nouvelle tâche :\n    Le titre vaut « deux ».\n    L'état vaut f.\n    La durée vaut 3.\nConserver t2.\n"
                          "Le t3 vaut une nouvelle tâche :\n    Le titre vaut « trois ».\n    L'état vaut a.\n    La durée vaut 0.\nConserver t3.\n"
                          "Le t4 vaut une nouvelle tâche :\n    Le titre vaut « quatre ».\n    La durée vaut 0.\nConserver t4.\n"
                          "L'écran x montre :\n    le graphique des tâches conservées, par état,\n"
                          "    le graphique des tâches conservées, par état, avec la somme de la durée, en secteurs,\n"
                          "    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<table class=\"lecteur\"><tr><th>à faire</th><td>2</td></tr><tr><th>fait</th><td>1</td></tr>"
                                 "<tr><th>—</th><td>1</td></tr></table>");
        CONTIENT(sc.reponses[0], "<tr><th>à faire</th><td>3</td></tr><tr><th>fait</th><td>3</td></tr><tr><th>—</th><td>0</td></tr>");
        CONTIENT(sc.reponses[0], "<path d=\"M100,100 L100.00,10.00 A90,90 0 0,1 100.00,190.00 Z\"");   /* la moitié : de midi à six heures */
        CONTIENT(sc.reponses[0], "<rect x=\"140\" y=\"10\" width=\"260.0\" height=\"20\"");   /* la plus grande barre : toute la largeur */
        NE_CONTIENT_PAS(sc.reponses[0], "style=");
        CONTIENT(sc.reponses[2], "Application terminée.");
        liberer(&sc);
    }

    {   /* L'image calculée (§ 22.8) : celle de la ligne choisie ; absente, rien ; son adresse porte une version qui change
         * avec ses octets ; une version périmée : 404 */
        const char *src = "Un compositeur, conservé, a :\n    un nom (texte), unique,\n    un portrait (image), facultatif.\n"
                          "Le b vaut un nouveau compositeur :\n    Le nom vaut « Bach ».\n    Le portrait vaut le fichier « exemples/pixel.png ».\n"
                          "Conserver b.\n"
                          "Le c vaut un nouveau compositeur :\n    Le nom vaut « Chopin ».\nConserver c.\n"
                          "L'écran x montre :\n    la liste des compositeurs conservés, par nom,\n"
                          "    l'image du portrait du compositeur choisi de l'écran,\n    un bouton « Fermer ».\n"
                          "Quand on choisit un compositeur dans l'écran x :\n    Afficher le nom du compositeur.\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&choix=0.0"), get("/"), get("/ecran-image/1/1/1"), get("/ecran-image/1/1/0"),
                 evenement("q=2&choix=0.1"), get("/"), evenement("q=3&clic=2"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<p class=\"image\"></p>");
        CONTIENT(sc.reponses[2], "<img src=\"/ecran-image/1/1/1\" alt=\"\">");
        CONTIENT(sc.reponses[3], "Content-Type: image/png");
        CONTIENT(sc.reponses[4], "404 Not Found");
        CONTIENT(sc.reponses[6], "<p class=\"image\"></p>");
        CONTIENT(sc.reponses[8], "Application terminée.");
        liberer(&sc);
    }

    {   /* La position libre (§ 22.9) : chaque élément dans sa boîte, placée par /ecran-style/série ; pas de style dans
         * la page ; les boutons du bloc ne se rangent pas en ligne ; une autre série : 404 */
        const char *src = "L'écran x montre :\n    en position libre :\n"
                          "        un nom (texte), facultatif, à 96 de la gauche et 16 du haut, large de 240,\n"
                          "        un bouton « A », à 8 de la gauche et 72 du haut,\n"
                          "        un bouton « Fermer », à 120 de la gauche et 72 du haut, haut de 40,\n    le texte « t ».\n"
                          "Quand on clique sur « A » dans l'écran x :\n    Afficher 1.\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/ecran-style/1"), get("/ecran-style/2"), evenement("q=1&clic=3"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<link rel=\"stylesheet\" href=\"/ecran-style/1\">");
        CONTIENT(sc.reponses[0], "<div class=\"libre\">");
        CONTIENT(sc.reponses[0], "<div class=\"place\" id=\"p1\"><div class=\"zone\">");
        CONTIENT(sc.reponses[0], "<div class=\"place\" id=\"p2\"><button name=\"clic\" value=\"2\">A</button></div>");
        NE_CONTIENT_PAS(sc.reponses[0], "class=\"boutons\"><button name=\"clic\" value=\"2\"");
        NE_CONTIENT_PAS(sc.reponses[0], "style=");
        CONTIENT(sc.reponses[1], "Content-Type: text/css");
        CONTIENT(sc.reponses[1], "#p1{margin:16px 0 0 96px;width:240px}");
        CONTIENT(sc.reponses[1], "#p3{margin:72px 0 0 120px;height:40px}");
        CONTIENT(sc.reponses[1], "@media (max-width:400px)");
        CONTIENT(sc.reponses[2], "404 Not Found");
        CONTIENT(sc.reponses[4], "Application terminée.");
        liberer(&sc);
    }

    {   /* Les onglets (§ 22.10) : la barre, les panneaux, le premier actif ; le choix voyage dans « o0 » et se retient
         * d'un envoi à l'autre ; une zone d'un onglet caché garde sa valeur */
        const char *src = "L'écran x montre :\n    les onglets :\n        l'onglet « Identité » :\n            un nom (texte), facultatif,\n"
                          "        l'onglet « Notes » :\n            le texte « n »,\n    un bouton « Lire »,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Lire » dans l'écran x :\n    Afficher le nom de l'écran.\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&o0=1&z2=Ada&clic=8"), get("/"), evenement("q=2&clic=9"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<div class=\"onglets\"><input type=\"hidden\" name=\"o0\" value=\"0\"><div class=\"barre\" role=\"tablist\">"
                                 "<button type=\"button\" role=\"tab\" data-i=\"0\" aria-selected=\"true\" class=\"actif\">Identité</button>"
                                 "<button type=\"button\" role=\"tab\" data-i=\"1\" aria-selected=\"false\">Notes</button></div>");
        CONTIENT(sc.reponses[0], "<section class=\"panneau actif\" role=\"tabpanel\" data-i=\"0\">");
        CONTIENT(sc.reponses[2], "<input type=\"hidden\" name=\"o0\" value=\"1\">");
        CONTIENT(sc.reponses[2], "<section class=\"panneau actif\" role=\"tabpanel\" data-i=\"1\">");
        CONTIENT(sc.reponses[2], "Ada");
        CONTIENT(sc.reponses[4], "Application terminée.");
        liberer(&sc);
    }

    {   /* Un bloc placé dans un bloc libre (1.81) : sa boîte l'entoure et se ferme avec lui ; il remplit sa taille */
        const char *src = "L'écran x montre :\n    en position libre :\n"
                          "        le groupe « G », à 8 de la gauche et 0 du haut, large de 300 :\n            le texte « a »,\n"
                          "        un bouton « Fermer », à 8 de la gauche et 120 du haut.\n"
                          "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/ecran-style/1"), evenement("q=1&clic=4"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<div class=\"place\" id=\"p1\"><fieldset class=\"groupe\"><legend>G</legend>\n"
                                 "<p class=\"texte\">a</p>\n</fieldset>\n</div>\n<div class=\"place\" id=\"p4\">");
        CONTIENT(sc.reponses[1], "#p1{margin:0px 0 0 8px;width:300px}");
        CONTIENT(sc.reponses[1], "#p1>fieldset{box-sizing:border-box;width:100%;height:100%}");
        CONTIENT(sc.reponses[3], "Application terminée.");
        liberer(&sc);
    }

    {   /* Le thème : la couleur choisie dans la feuille, clair et sombre ; le logo servi à /logo */
        const char *src = "Les écrans ont la couleur verte et le logo « exemples/pixel.png ».\n"
                          "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), get("/logo"), get("/grym.js"), evenement("q=1&clic=0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<img src=\"/logo\" alt=\"\"><h1>Accueil</h1>");
        char *clair = grym_formater("--accent:%s;", serveur_jeton(0, 1, "accent"));
        char *sombre = grym_formater("--accent:%s;", serveur_jeton(1, 1, "accent"));
        CONTIENT(sc.reponses[1], clair);
        CONTIENT(sc.reponses[1], sombre);
        CONTIENT(sc.reponses[1], "@media (prefers-color-scheme:dark)");
        free(clair);
        free(sombre);
        CONTIENT(sc.reponses[2], "Content-Type: image/png");
        CONTIENT(sc.reponses[3], "Content-Type: text/javascript");
        CONTIENT(sc.reponses[3], "button[name=valider]");
        CONTIENT(sc.reponses[5], "Application terminée.");
        liberer(&sc);
    }


    {   /* Le thème libre (2.13) : les jetons dérivés dans la feuille, sans mode sombre (un fond est donné), la police
           servie à /police et déclarée, le logo SVG servi en image/svg+xml, les polices permises par la CSP */
        const char *src = "Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police "
                          "« vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf » et le logo « vendor/lucide/check.svg ».\n"
                          "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), get("/logo"), get("/police"), evenement("q=1&clic=0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[1], "--fond:#E8E2D6;");
        CONTIENT(sc.reponses[1], "--accent:#46433B;");
        CONTIENT(sc.reponses[1], "--accent-doux:#CEC9BD;");
        NE_CONTIENT_PAS(sc.reponses[1], "prefers-color-scheme:dark");
        CONTIENT(sc.reponses[1], "@font-face{font-family:\"Police de l'application\";src:url(/police)}");
        CONTIENT(sc.reponses[2], "Content-Type: image/svg+xml");
        CONTIENT(sc.reponses[3], "Content-Type: font/ttf");
        CONTIENT(sc.reponses[3], "font-src 'self'");
        CONTIENT(sc.reponses[5], "Application terminée.");
        liberer(&sc);
    }

    {   /* 2.19 : une liste en cartes : un bouton de choix par ligne, la première colonne en grand, la deuxième en nom */
        const char *src = "Une séance, conservée, a :\n    une heure (heure),\n    un nom (texte),\n    un soin (texte).\n"
                          "Pour semer :\n    La s vaut une nouvelle séance :\n        L'heure vaut 9:00.\n        Le nom vaut « Anne ».\n"
                          "        Le soin vaut « EPI ».\n    Conserver la s.\n"
                          "Si le nombre de séances conservées est nul, semer.\n"
                          "L'écran d'accueil montre :\n    le texte « Aujourd'hui », en petites capitales,\n"
                          "    la liste des séances conservées, par heure, avec l'heure, le nom et le soin, en cartes.\n"
                          "Quand on choisit une séance dans l'écran d'accueil :\n    Fermer l'écran.\nOuvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&choix=1.0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<p class=\"texte capitales\">Aujourd&#39;hui</p>");   /* 2.20 */
        CONTIENT(sc.reponses[0], "<div class=\"cartes\"><button class=\"carte\" name=\"choix\" value=\"1.0\"><span class=\"tete\">"
                                 "<span class=\"grand\">09:00</span><span class=\"etiquettes\"></span></span><span class=\"nom\">Anne</span>"
                                 "<span class=\"detail\">EPI</span></button></div>");
        CONTIENT(sc.reponses[2], "Application terminée.");
        liberer(&sc);
    }

    {   /* 2.17 : un bouton à côté du titre, dans l'en-tête, avant la croix ; pas une seconde fois plus bas */
        const char *src = "L'écran d'accueil, « Bonjour » en grand, montre :\n    un bouton « Nouvelle », principal, à côté du titre,\n"
                          "    un bouton « Fermer ».\n"
                          "Quand on clique sur « Nouvelle » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\nOuvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<h1 class=\"titre-grand\">Bonjour</h1><button name=\"clic\" value=\"0\" class=\"principal\">Nouvelle</button><button name=\"fermer\"");
        NE_CONTIENT_PAS(sc.reponses[0], "<div class=\"boutons\"><button name=\"clic\" value=\"0\"");
        CONTIENT(sc.reponses[2], "Application terminée.");
        liberer(&sc);
    }

    {   /* 2.16 : la signature en bandeau, servie à /signature ; le logo ne se pose plus près du titre */
        const char *src = "Les écrans ont le logo « exemples/pixel.png » et la signature « vendor/lucide/check.svg ».\n"
                          "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\nOuvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/signature"), evenement("q=1&clic=0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<header class=\"bandeau\"><img src=\"/signature\" alt=\"\"></header>");
        NE_CONTIENT_PAS(sc.reponses[0], "<img src=\"/logo\"");
        CONTIENT(sc.reponses[1], "Content-Type: image/svg+xml");
        CONTIENT(sc.reponses[3], "Application terminée.");
        liberer(&sc);
    }

    {   /* 2.14, 2.15 : le bouton principal et les tuiles, une grille, l'image servie comme celle d'un écran */
        const char *src = "L'écran d'accueil montre :\n    un bouton « Nouvelle », principal,\n"
                          "    un bouton « Clients », en tuile, avec la description « Fiches », avec l'image « exemples/pixel.png »,\n"
                          "    un bouton « Soins », en tuile.\n"
                          "Quand on clique sur « Nouvelle » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Quand on clique sur « Clients » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Quand on clique sur « Soins » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), get("/style.css"), evenement("q=1&clic=0"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<button name=\"clic\" value=\"0\" class=\"principal\">Nouvelle</button>");
        CONTIENT(sc.reponses[0], "<div class=\"tuiles\"><button class=\"tuile\" name=\"clic\" value=\"1\"><span class=\"tete\">"
                                 "<span class=\"titre\">Clients</span><img src=\"/ecran-image/");
        CONTIENT(sc.reponses[0], "<span class=\"description\">Fiches</span></button><button class=\"tuile\" name=\"clic\" value=\"2\">");
        CONTIENT(sc.reponses[1], ".tuiles{display:grid;");
        CONTIENT(sc.reponses[3], "Application terminée.");
        liberer(&sc);
    }

    {   /* L'application « Gestion de projet » (projets/), de bout en bout dans le navigateur */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=5"),                                       /* 0 : Bilan */
                 get("/"),                                                      /* 1 */
                 evenement("q=2&clic=1"),                                       /* 2 : Tâches */
                 get("/"),                                                      /* 3 */
                 evenement("q=3&z0=Site+de+l%27association&s1=1&clic=5"),       /* 4 : « Écrire les textes », Terminer */
                 get("/"),                                                      /* 5 */
                 evenement("q=4&z0=Site+de+l%27association&clic=4"),            /* 6 : Commencer, sans ligne */
                 get("/"),                                                      /* 7 */
                 evenement("q=5&fermer=1"),                                     /* 8 : retour à l'accueil */
                 evenement("q=6&clic=2"),                                       /* 9 : En retard */
                 get("/"),                                                      /* 10 */
                 evenement("q=7&clic=1"),                                       /* 11 : Fermer */
                 evenement("q=8&clic=5"),                                       /* 12 : Bilan */
                 evenement("q=9&clic=6"),                                       /* 13 : Fermer l'accueil */
                 get("/"));                                                     /* 14 */
        servir_projet("projets/Gestion de projet/Gestion de projet.grym", "_essai_gestion.grymd", &sc);
        CONTIENT(sc.reponses[1], "Site de l&#39;association : 1 tâche(s) terminée(s) sur 3, fin prévue le ");
        CONTIENT(sc.reponses[1], "en retard : Écrire les textes (5 jour(s))");
        /* le projet pré-choisi à l'ouverture ; la liste le suit */
        CONTIENT(sc.reponses[3], "<option value=\"Site de l&#39;association\" selected>");
        CONTIENT(sc.reponses[3], "value=\"1.1\">Écrire les textes</button>");
        NE_CONTIENT_PAS(sc.reponses[3], "class=\"message\"");
        /* sélectionnée par le bouton radio, terminée par le bouton, sans ouvrir sa fiche */
        CONTIENT(sc.reponses[5], "Écrire les textes</button></td><td>Louis Bapst</td><td>Terminée</td>");
        CONTIENT(sc.reponses[5], "<h1>Tâches d&#39;un projet</h1>");
        /* la ligne terminée a changé de contenu : plus rien n'est sélectionné, et « Commencer » le refuse */
        CONTIENT(sc.reponses[7], "<p class=\"message\" role=\"alert\">Choisis d&#39;abord une tâche dans la liste.</p>");
        NE_CONTIENT_PAS(sc.reponses[7], " checked>");
        CONTIENT(sc.reponses[10], "<td class=\"vide\" colspan=\"5\">Aucune ligne.</td>");
        CONTIENT(sc.reponses[14], "2 tâche(s) terminée(s) sur 3");
        NE_CONTIENT_PAS(sc.reponses[14], "class=\"erreur\"");
        CONTIENT(sc.reponses[14], "Application terminée.");
        liberer(&sc);
    }

    {   /* « Modifier » d'une fiche déduite vérifie les règles (§ 16.14, 2.22) : refusée, la saisie s'annule et la fiche
           le dit ; acceptée, elle est conservée */
        const char *src = "Un cabinet, conservé, a :\n    un nom (texte), unique,\n    un numéro GLN (texte), facultatif.\n"
            "Chaque cabinet conservé dont le numéro GLN est présent vérifie :\n    la longueur du numéro GLN du cabinet = 13,\n"
            "    sinon « Le GLN » puis le numéro GLN du cabinet puis « est faux. ».\n"
            "Si le nombre de cabinets conservés est nul :\n    Le n vaut un nouveau cabinet :\n        Le nom vaut « N ».\n    Conserver le n.\n"
            "Le c vaut le cabinet conservé.\nOuvrir la fiche du c.\nAfficher le numéro GLN du c.\n";
        memset(&sc, 0, sizeof sc);
        question_suivante = 1;
        char *faux = reponse("N|123");
        question_suivante = 3;
        char *juste = reponse("N|1234567890123");
        REQUETES(get("/"), evenement("q=1&clic=2"), faux, get("/"), evenement("q=3&clic=2"), juste, get("/"),
                 evenement("q=5&fermer=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[3], "<p class=\"message\" role=\"alert\">Le GLN 123 est faux.</p>");
        CONTIENT(sc.reponses[3], "<p class=\"texte\">Numéro GLN : </p>");   /* rien n'est resté de la saisie refusée */
        CONTIENT(sc.reponses[6], "<p class=\"texte\">Numéro GLN : 1234567890123</p>");
        NE_CONTIENT_PAS(sc.reponses[6], "role=\"alert\"");
        CONTIENT(sc.reponses[8], "1234567890123\n");
        CONTIENT(sc.reponses[8], "Application terminée.");
        NE_CONTIENT_PAS(sc.reponses[8], "class=\"erreur\"");
        liberer(&sc);
    }

    {   /* « Nouvelle … » sous le tableau d'une fiche : la règle de l'entité créée se vérifie aussi (2.22) */
        const char *src = "Un client, conservé, a :\n    un nom (texte), unique.\n"
            "Une séance, conservée, a :\n    un client (client),\n    un prix (nombre).\n"
            "Chaque séance conservée vérifie :\n    le prix de la séance ≥ 0,\n    sinon « Le prix est négatif. ».\n"
            "Si le nombre de clients conservés est nul :\n    Le n vaut un nouveau client :\n        Le nom vaut « Ana ».\n    Conserver le n.\n"
            "Le c vaut le client conservé.\nOuvrir la fiche du c.\nAfficher le nombre de séances conservées.\n";
        memset(&sc, 0, sizeof sc);
        question_suivante = 1;
        char *negatif = reponse("-5");
        REQUETES(get("/"), evenement("q=1&clic=3"), get("/"), negatif, get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[2], "<label for=\"c0\">Prix</label>");
        CONTIENT(sc.reponses[4], "<p class=\"message\" role=\"alert\">Le prix est négatif.</p>");
        CONTIENT(sc.reponses[4], "<td class=\"vide\" colspan=\"2\">Aucune ligne.</td>");   /* la séance n'est pas restée */
        liberer(&sc);
    }

    {   /* un événement qui ferme son écran puis viole une règle : annulé, l'écran reste ouvert avec le motif */
        const char *src = "Un cabinet, conservé, a :\n    un nom (texte), unique,\n    un numéro GLN (texte), facultatif.\n"
            "Chaque cabinet conservé dont le numéro GLN est présent vérifie :\n    la longueur du numéro GLN du cabinet = 13,\n"
            "    sinon « Le GLN est faux. ».\n"
            "Si le nombre de cabinets conservés est nul :\n    Le n vaut un nouveau cabinet :\n        Le nom vaut « N ».\n    Conserver le n.\n"
            "L'écran d'accueil montre :\n    un numéro GLN (texte), facultatif,\n    un bouton « Enregistrer ».\n"
            "Quand on clique sur « Enregistrer » dans l'écran d'accueil :\n    Le c vaut le cabinet conservé.\n"
            "    Le numéro GLN du c devient le numéro GLN de l'écran.\n    Fermer l'écran.\n"
            "Ouvrir l'écran d'accueil.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&z0=123&clic=1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[1], "<p class=\"message\" role=\"alert\">Le GLN est faux.</p>");
        CONTIENT(sc.reponses[1], "<h1>Accueil</h1>");
        liberer(&sc);
    }

    {   /* Cabinet (N2a) : le cabinet, un seul, créé au lancement ; son écran depuis celui des soins ; ses sigles ;
           la règle du GLN vérifiée à « Enregistrer » */
        memset(&sc, 0, sizeof sc);
#define CAB_ZONES "z2=Nicolas+Seydoux&z3=Rue+X&z4=1&z5=&z6=1700&z7=Fribourg&z8=Suisse&z11=&z12=&z13=Z123456&z15=&z16="
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=7"), get("/"), evenement("q=3&clic=0"), get("/"),
                 evenement("q=4&" CAB_ZONES "&z14=760100000000&clic=27"), get("/"),
                 evenement("q=5&" CAB_ZONES "&z14=7601000000000&clic=27"), get("/"),
                 evenement("q=6&clic=0"), get("/"), evenement("q=7&clic=28"), evenement("q=8&clic=1"), get("/"));
#undef CAB_ZONES
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet3.grymd", &sc);
        CONTIENT(sc.reponses[2], "<h1>Configuration</h1>");   /* le cabinet, les soins, les rubriques */
        CONTIENT(sc.reponses[13], "<th>Code tarifaire</th>");
        CONTIENT(sc.reponses[4], "<h1>Le cabinet</h1>");
        CONTIENT(sc.reponses[4], "value=\"Nicolas Seydoux\"");
        CONTIENT(sc.reponses[4], ">Numéro RCC</label>");   /* le sigle garde ses capitales (2.21) */
        CONTIENT(sc.reponses[4], ">Numéro ASCA</label>");
        CONTIENT(sc.reponses[4], "name=\"z18\" value=\"oui\" aria-describedby=\"z18i\"> Facturation des absences</label>");   /* l'interrupteur, éteint au départ (20 octobre 2026) */
        NE_CONTIENT_PAS(sc.reponses[4], "name=\"z18\" value=\"oui\" checked");
        CONTIENT(sc.reponses[6], "Le numéro GLN 760100000000 n&#39;a pas treize chiffres.");
        CONTIENT(sc.reponses[6], "<h1>Le cabinet</h1>");   /* l'écran reste ouvert, rien n'est écrit */
        CONTIENT(sc.reponses[8], "<h1>Configuration</h1>");
        CONTIENT(sc.reponses[10], "value=\"7601000000000\"");
        CONTIENT(sc.reponses[10], "value=\"Fribourg\"");
        NE_CONTIENT_PAS(sc.reponses[10], "class=\"erreur\"");
        liberer(&sc);
        remove("_essai_cabinet3.grymd");
    }

    {   /* Cabinet : la comptabilité de l'année (recettes, frais, résultat) ; un frais saisi entre dans les comptes ; une
           quittance, un document, ne se fabrique pas dans le navigateur (19 octobre 2026). Le frais est daté du jour et
           les recettes lues à l'écran : l'essai vaut à toute date, même en janvier, quand les séances d'essai tombent
           dans l'année d'avant. */
        char jour[11], ev_frais[200];
        const time_t maintenant = time(NULL);
        strftime(jour, sizeof jour, "%d.%m.%Y", localtime(&maintenant));
        snprintf(ev_frais, sizeof ev_frais, "q=5&z1=%s&z2=80&z3=Huiles+de+massage&z4=Mat%%C3%%A9riel&z5=carte&clic=Enregistrer", jour);
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=6"), get("/"), evenement("q=3&clic=7"), evenement("q=4&clic=3"), get("/"),
                 evenement(ev_frais), get("/"),
                 evenement("q=6&clic=4"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet8.grymd", &sc);
        CONTIENT(sc.reponses[1], "Comptabilité</span></span><span class=\"description\">Encaisser, factures, rappels, frais</span>");
        CONTIENT(sc.reponses[3], "<p class=\"texte\">Frais : CHF 0,00</p>");
        CONTIENT(sc.reponses[6], "Libellé");
        CONTIENT(sc.reponses[8], "<td>Huiles de massage</td>");
        CONTIENT(sc.reponses[10], "<p class=\"texte\">Frais : CHF 80,00</p>");
        {   /* le résultat : les recettes lues avant, moins les 80 du frais */
            const char *r = sc.reponses[3] ? strstr(sc.reponses[3], "Recettes encaissées : CHF ") : NULL;
            long recettes = -1;
            if (r) sscanf(r + strlen("Recettes encaissées : CHF "), "%ld", &recettes);
            CONTIENT(sc.reponses[3], "Recettes encaissées : CHF ");
            char attendu[80];
            snprintf(attendu, sizeof attendu, "<p class=\"texte\">Résultat : CHF %ld,00</p>", recettes - 80);   /* les prix d'essai sont ronds */
            CONTIENT(sc.reponses[10], attendu);
        }
        liberer(&sc);
        remove("_essai_cabinet8.grymd");
        /* les listes suivent l'année choisie (21 octobre 2026) : celle de la séance payée il y a quatre jours, choisie
           dans le menu de la Comptabilité, pour valoir aussi les premiers jours de janvier */
        char ev_annee[40], titre[40];
        {
            const time_t il_y_a = time(NULL) - 4 * 86400;
            char an[5];
            strftime(an, sizeof an, "%Y", localtime(&il_y_a));
            snprintf(ev_annee, sizeof ev_annee, "q=3&z0=%s&clic=6", an);   /* l'année, puis « Quittances » */
            snprintf(titre, sizeof titre, "<h1>Quittances %s</h1>", an);
        }
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=6"), evenement(ev_annee), get("/"),
                 evenement("q=4&choix=3.0"), evenement("q=5&clic=5"), get("/"),
                 evenement("q=6&z1=cl%C3%A9ment"), get("/"), evenement("q=7&z1=Personne"), get("/"), evenement("q=8&z1="), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet9.grymd", &sc);
        CONTIENT(sc.reponses[4], titre);
        CONTIENT(sc.reponses[4], "<td>Sophie Clément</td>");   /* payée par virement il y a quatre jours, sans quittance */
        CONTIENT(sc.reponses[4], "<td>Anne Rochat</td>");
        /* un nom de patient, en minuscules : toutes les années, ses seules séances ; un nom inconnu : rien ; vide : l'année */
        CONTIENT(sc.reponses[9], "<h1>Quittances, toutes les années</h1>");
        CONTIENT(sc.reponses[9], "<td>Sophie Clément</td>");
        NE_CONTIENT_PAS(sc.reponses[9], "<td>Anne Rochat</td>");
        NE_CONTIENT_PAS(sc.reponses[11], "<td>Sophie Clément</td>");
        CONTIENT(sc.reponses[13], titre);
        CONTIENT(sc.reponses[13], "<td>Anne Rochat</td>");
        CONTIENT(sc.reponses[7], "Un document se fabrique dans l&#39;atelier ou dans une application fabriquée.");
        liberer(&sc);
        remove("_essai_cabinet9.grymd");
    }

    {   /* Cabinet : une séance offerte, cochée, s'enregistre « offert » ; une rubrique de frais ajoutée puis renommée
           (19 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=0"),
                 evenement("q=3&z2=Anne+Rochat&z4=EPI&z5=19.10.2026&z6=&z7=&z8=oui&z8p=1&z9=%C3%A0+payer&z12=0&clic=19"),
                 evenement("q=4&clic=4"), evenement("q=5&choix=2.0"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet10.grymd", &sc);
        CONTIENT(sc.reponses[5], "<h1>Patient</h1>");
        CONTIENT(sc.reponses[5], ">offert</span>");
        liberer(&sc);
        remove("_essai_cabinet10.grymd");
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=7"), evenement("q=3&clic=2"), get("/"),
                 evenement("q=4&z2=Supervision&clic=3"), get("/"), evenement("q=5&choix=0.7"), get("/"),
                 evenement("q=6&z0=Supervision+clinique&clic=2"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet11.grymd", &sc);
        CONTIENT(sc.reponses[3], ">Loyer</button>");   /* les rubriques de départ */
        CONTIENT(sc.reponses[5], ">Supervision</button>");
        CONTIENT(sc.reponses[7], "<h1>Rubrique</h1>");
        CONTIENT(sc.reponses[9], ">Supervision clinique</button>");
        NE_CONTIENT_PAS(sc.reponses[9], ">Supervision</button>");
        liberer(&sc);
        remove("_essai_cabinet11.grymd");
    }

    {   /* Cabinet : les rappels, par facture échue ; dessous, les séances dues depuis trente jours sans facture, que
           « Facturer » facture ; « Rappel » sans facture choisie est refusé ; la facture et le journal sont des
           documents : refusés dans le navigateur, sans rien numéroter ni dater (20 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=6"), evenement("q=3&clic=8"), get("/"), evenement("q=4&clic=4"), get("/"),
                 evenement("q=5&s8=0&clic=9"), get("/"), evenement("q=6&clic=5"), evenement("q=7&clic=10"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet12.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>Rappels</h1>");
        CONTIENT(sc.reponses[3], "Factures échues");
        CONTIENT(sc.reponses[3], "<td>Marc Dupraz</td>");          /* ses deux séances faites il y a 45 et 38 jours */
        NE_CONTIENT_PAS(sc.reponses[3], "<td>Anne Rochat</td>");   /* sa séance à payer date d'hier */
        NE_CONTIENT_PAS(sc.reponses[3], "<td>Claire Monney</td>"); /* son absence : le cabinet ne facture pas les absences */
        CONTIENT(sc.reponses[5], "Choisis d&#39;abord une facture dans la liste.");
        CONTIENT(sc.reponses[7], "Un document se fabrique dans l&#39;atelier ou dans une application fabriquée.");
        CONTIENT(sc.reponses[10], "Un document se fabrique dans l&#39;atelier ou dans une application fabriquée.");
        liberer(&sc);
        remove("_essai_cabinet12.grymd");
    }

    {   /* Praxis 2, chantier A (23 octobre 2026) : un soin sur ordonnance se facture à l'assurance ; sans ordonnance qui la
           couvre, sa séance est refusée ; l'ordonnance saisie dans la page du patient, la séance s'y rattache, payée
           « assurance », et l'ordonnance en compte une sur neuf */
        memset(&sc, 0, sizeof sc);
        const char *seance = "z2=Anne+Rochat&z4=EPI&z5=05.10.2026&z6=&z7=&z9=%C3%A0+payer&z12=130&clic=19";
        char q11[200], q18[200];
        snprintf(q11, sizeof q11, "q=11&%s", seance);
        snprintf(q18, sizeof q18, "q=18&%s", seance);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Configuration"), evenement("q=3&clic=1"),
                 evenement("q=4&choix=0.0"), evenement("q=5&z0=EPI&z1=130&z2=70&z3=60&z4=1003&z5=oui&z5p=1&z6=patient&clic=9"),
                 evenement("q=6&clic=Fermer"), evenement("q=7&clic=Fermer"), evenement("q=8&clic=Patients"),
                 evenement("q=9&z0=Anne+Rochat&clic=-1"), evenement("q=10&clic=Nouvelle+s%C3%A9ance"),
                 evenement(q11), get("/"),
                 evenement("q=12&clic=Fermer"), evenement("q=13&clic=Ordonnances"), evenement("q=14&clic=Nouvelle+ordonnance"),
                 evenement("q=15&z0=01.10.2026&z1=Dre+Morel&z2=&z3=&z4=9&z5=maladie&z7=&clic=Enregistrer"), get("/"),
                 evenement("q=16&clic=Fermer"), evenement("q=17&clic=Nouvelle+s%C3%A9ance"), evenement(q18), get("/"),
                 evenement("q=19&clic=Ordonnances"), evenement("q=20&choix=1.0"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_ordonnance.grymd", &sc);
        CONTIENT(sc.reponses[11], "Pas d&#39;ordonnance valable pour Anne Rochat à cette date");
        CONTIENT(sc.reponses[16], ">Dre Morel</td>");
        CONTIENT(sc.reponses[20], "<h1>Patient</h1>");
        CONTIENT(sc.reponses[20], ">assurance</");
        NE_CONTIENT_PAS(sc.reponses[20], "class=\"erreur\"");
        CONTIENT(sc.reponses[23], "Séances : 1 sur 9.");
        liberer(&sc);
        remove("_essai_ordonnance.grymd");
        remove("_essai_ordonnance.grymd.clés");
    }

    {   /* « l'écran des ordonnances » et « l'écran de l'ordonnance » : deux écrans, pas un (23 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&choix=0.0"), get("/"), evenement("q=2&clic=Fermer"), get("/"));
        servir("Une ordonnance, conservée, a :\n    un médecin (texte), unique.\n"
               "La o vaut une nouvelle ordonnance :\n    Le médecin vaut « Dre Morel ».\nConserver la o.\n"
               "L'ouverte vaut la o.\n"
               "L'écran des ordonnances, « Liste », montre :\n    la liste des ordonnances conservées, avec le médecin,\n    un bouton « Quitter ».\n"
               "L'écran de l'ordonnance, « Une », montre :\n    le texte le médecin de l'ouverte,\n    un bouton « Fermer ».\n"
               "Quand on choisit une ordonnance dans l'écran des ordonnances :\n    L'ouverte devient l'ordonnance choisie de l'écran.\n"
               "    Ouvrir l'écran de l'ordonnance.\n"
               "Quand on clique sur « Fermer » dans l'écran de l'ordonnance :\n    Fermer l'écran.\n"
               "Quand on clique sur « Quitter » dans l'écran des ordonnances :\n    Fermer l'écran.\n"
               "Ouvrir l'écran des ordonnances.\n", &sc);
        CONTIENT(sc.reponses[2], "<h1>Une</h1>");
        NE_CONTIENT_PAS(sc.reponses[2], "class=\"erreur\"");
        CONTIENT(sc.reponses[4], "<h1>Liste</h1>");
        NE_CONTIENT_PAS(sc.reponses[4], "class=\"erreur\"");
        liberer(&sc);
    }

    {   /* un calcul appelé depuis le texte d'un écran ouvert par-dessus une liste (23 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&choix=0.0"), get("/"), evenement("q=2&clic=Fermer"), get("/"));
        servir("Une fiche, conservée, a :\n    un nom (texte), unique.\n"
               "Une trace, conservée, a :\n    un parent (fiche),\n    une valeur (nombre).\n"
               "La f vaut une nouvelle fiche :\n    Le nom vaut « A ».\nConserver la f.\n"
               "L'usage d'une fiche :\n    Le x vaut la fiche.\n"
               "    Rendre \"lignes : \" suivi du nombre de traces conservées dont le parent = le x.\n"
               "La choisie vaut la f.\n"
               "L'écran des fiches montre :\n    la liste des fiches conservées, avec le nom,\n    un bouton « Quitter ».\n"
               "L'écran de la fiche montre :\n    le texte l'usage de la choisie,\n    un bouton « Fermer ».\n"
               "Quand on choisit une fiche dans l'écran des fiches :\n    La choisie devient la fiche choisie de l'écran.\n"
               "    Ouvrir l'écran de la fiche.\n"
               "Quand on clique sur « Fermer » dans l'écran de la fiche :\n    Fermer l'écran.\n"
               "Quand on clique sur « Quitter » dans l'écran des fiches :\n    Fermer l'écran.\n"
               "Ouvrir l'écran des fiches.\n", &sc);
        CONTIENT(sc.reponses[2], "lignes : 0");
        NE_CONTIENT_PAS(sc.reponses[2], "class=\"erreur\"");
        CONTIENT(sc.reponses[4], ">A</button>");
        NE_CONTIENT_PAS(sc.reponses[4], "class=\"erreur\"");
        liberer(&sc);
    }

    {   /* après une liste à colonnes « … et c », un texte calculé est l'élément suivant, pas une colonne (2.50) */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"));
        servir("Une séance, conservée, a :\n    une date (date),\n    un prix (nombre).\n"
               "La s vaut une nouvelle séance :\n    La date vaut 01.10.2026.\n    Le prix vaut 12.\nConserver la s.\n"
               "La note vaut « valeur du point : 1,10 ».\n"
               "L'écran x montre :\n    la liste des séances conservées, avec la date et le prix,\n    le texte la note,\n    un bouton « Fermer ».\n"
               "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n", &sc);
        CONTIENT(sc.reponses[0], "valeur du point : 1,10");
        NE_CONTIENT_PAS(sc.reponses[0], "class=\"erreur\"");
        liberer(&sc);
    }

    {   /* les choix filtrés d'une zone (2.48) : « (patient dont l'archivage est absent) » ne propose pas l'archivé */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"));
        servir("Un patient, conservé, a :\n    un nom (texte), unique,\n    un archivage (date), facultatif.\n"
               "Le a vaut un nouveau patient :\n    Le nom vaut « Anne ».\nConserver le a.\n"
               "Le b vaut un nouveau patient :\n    Le nom vaut « Bruno ».\nL'archivage du b devient 01.01.2026.\nConserver le b.\n"
               "L'écran x montre :\n    un patient (patient dont l'archivage est absent), facultatif,\n    un bouton « Fermer ».\n"
               "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n", &sc);
        CONTIENT(sc.reponses[0], ">Anne<");
        NE_CONTIENT_PAS(sc.reponses[0], "Bruno");
        liberer(&sc);
    }

    {   /* une colonne qui traverse trois objets : la séance, son soin, la position du soin (23 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"));
        servir("Une position, conservée, a : un code (texte), unique.\n"
               "Un soin, conservé, a : un nom (texte), unique, une position (position), facultative.\n"
               "Une séance, conservée, a : une date (date), un soin (soin).\n"
               "La p vaut une nouvelle position :\n    Le code vaut « 7601 ».\nConserver la p.\n"
               "Le s vaut un nouveau soin :\n    Le nom vaut « Ergo ».\nLa position du s devient la p.\nConserver le s.\n"
               "Le t vaut un nouveau soin :\n    Le nom vaut « Sans ».\nConserver le t.\n"
               "La a vaut une nouvelle séance :\n    La date vaut 01.10.2026.\n    Le soin vaut le s.\nConserver la a.\n"
               "La b vaut une nouvelle séance :\n    La date vaut 02.10.2026.\n    Le soin vaut le t.\nConserver la b.\n"
               "L'écran x montre :\n    la liste des séances conservées, par date, avec la date, le code de la position du soin et le nom du soin.\n"
               "Ouvrir l'écran x.\n", &sc);
        CONTIENT(sc.reponses[0], "<td>7601</td><td>Ergo</td>");
        CONTIENT(sc.reponses[0], "<td></td><td>Sans</td>");
        liberer(&sc);
    }

    {   /* Praxis 2, chantier B (23 octobre 2026) : « Facturer à l'assurance » vérifie d'abord tout ce que la facture tiers
           payant doit montrer ; tout y est (essais.grym), il ne reste que le PDF, que seuls l'atelier et une application
           fabriquée savent faire (le parcours complet : test_atelier) ; la liste des factures aux assurances, vide */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Patients"), evenement("q=3&z0=Claire+Monney&clic=-1"),
                 evenement("q=4&clic=Ordonnances"), evenement("q=5&clic=Nouvelle+ordonnance"),
                 evenement("q=6&z0=01.10.2026&z1=Dre+Morel&z2=&z3=&z4=9&z5=maladie&z7=&clic=Enregistrer"),
                 evenement("q=7&clic=Fermer"), evenement("q=8&clic=Nouvelle+s%C3%A9ance"),
                 evenement("q=9&z2=Claire+Monney&z4=Ergoth%C3%A9rapie&z5=05.10.2026&z6=&z7=60&z9=%C3%A0+payer&z12=105%2C6&clic=19"),
                 evenement("q=10&clic=Ordonnances"), evenement("q=11&choix=1.0"),
                 evenement("q=12&clic=Facturer+%C3%A0+l%27assurance"), get("/"),
                 evenement("q=13&clic=Fermer"), evenement("q=14&clic=Fermer"), evenement("q=15&clic=Fermer"), evenement("q=16&clic=Fermer"),
                 get("/"), evenement("q=17&clic=Assurances"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_tiers_payant.grymd", &sc);
        CONTIENT(sc.reponses[12], "Un document se fabrique dans l&#39;atelier ou dans une application fabriquée.");
        CONTIENT(sc.reponses[17], ">Assurances<");
        CONTIENT(sc.reponses[19], "<h1>Factures aux assurances</h1>");
        liberer(&sc);
        remove("_essai_tiers_payant.grymd");
        remove("_essai_tiers_payant.grymd.clés");
    }

    {   /* le nom et le prénom (24 octobre 2026) : saisis à part, réunis dans la clé « Prénom Nom » ; un homonyme est refusé */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Patients"), evenement("q=3&clic=4"),
                 evenement("q=4&z1=Thi%C3%A9mard&z2=Alain&z3=Rue+de+Gruy%C3%A8re&z4=65&z5=&z7=1630&z8=Bulle&z6=Suisse&z19=&clic=22"),
                 get("/"), evenement("q=5&clic=4"),
                 evenement("q=6&z1=Thi%C3%A9mard&z2=Alain&z3=&z4=&z5=&z7=&z8=&z6=Suisse&z19=&clic=22"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_prenom.grymd", &sc);
        CONTIENT(sc.reponses[4], ">Alain Thiémard</button>");
        CONTIENT(sc.reponses[7], "Il y a déjà un patient « Alain Thiémard »");
        liberer(&sc);
        remove("_essai_prenom.grymd");
        remove("_essai_prenom.grymd.clés");
    }

    {   /* quitter (24 octobre 2026) : « Fermer l'application » ferme l'accueil, et une copie chiffrée part dans « Sauvegardes » */
        memset(&sc, 0, sizeof sc);
        system("rm -rf _essai_praxis/Sauvegardes");
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Fermer+l%27application"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_quitter.grymd", &sc);
        NE_CONTIENT_PAS(sc.reponses[2], "class=\"erreur\"");
        int coffre = 0;
        DIR *dd = opendir("_essai_praxis/Sauvegardes");
        if (dd) {
            for (struct dirent *x; (x = readdir(dd)) != NULL;) coffre |= strstr(x->d_name, ".coffre") != NULL;
            closedir(dd);
        }
        total++;
        if (!coffre) { echecs++; printf("ÉCHEC (ligne %d) : pas de copie chiffrée dans _essai_praxis/Sauvegardes\n", __LINE__); }
        liberer(&sc);
        system("rm -rf _essai_praxis/Sauvegardes");
        remove("_essai_quitter.grymd");
        remove("_essai_quitter.grymd.clés");
    }

    {   /* la version neutre (projets/Praxis/Praxis, 23 octobre 2026) : un cabinet sans nom propre, sans soin, Praxis 2 éteint */
        memset(&sc, 0, sizeof sc);
        copier_dossier("projets/Praxis/Praxis", "_essai_neutre_projet");
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=Configuration"), get("/"));
        servir_projet("_essai_neutre_projet/Praxis.grym", "_essai_neutre.grymd", &sc);
        CONTIENT(sc.reponses[1], ">Patients<");
        NE_CONTIENT_PAS(sc.reponses[1], "class=\"erreur\"");
        NE_CONTIENT_PAS(sc.reponses[1], "Nicolas");
        CONTIENT(sc.reponses[3], "<h1>Configuration</h1>");
        NE_CONTIENT_PAS(sc.reponses[3], ">Rendez-vous<");
        liberer(&sc);
        remove("_essai_neutre.grymd");
        remove("_essai_neutre.grymd.clés");
    }

    {   /* Praxis 1 chez Nicolas (23 octobre 2026) : les deux interrupteurs de la fiche du cabinet éteints, plus rien de
           Praxis 2 ne se voit : ni Rendez-vous, SMS, Assureurs, Tarif dans la Configuration, ni Assurances ni Agenda OneDoc
           sur l'accueil, ni l'assurance et les ordonnances dans la page du patient */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Configuration"), evenement("q=3&clic=0"),
                 evenement("q=4&z2=Cabinet+essai&z3=Rue+X&z4=1&z5=&z6=1700&z7=Fribourg&z8=Suisse&z11=&z12=&z13=&z14=&z15=&z16="
                           "&z19p=1&z20p=1&clic=27"), get("/"),
                 evenement("q=5&clic=Fermer"), get("/"), evenement("q=6&clic=Patients"), evenement("q=7&z0=Anne+Rochat&clic=-1"),
                 get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_eteint.grymd", &sc);
        CONTIENT(sc.reponses[4], "<h1>Configuration</h1>");
        NE_CONTIENT_PAS(sc.reponses[4], ">Rendez-vous<");
        NE_CONTIENT_PAS(sc.reponses[4], ">SMS<");
        CONTIENT(sc.reponses[4], ">Assureurs<");   /* 24 octobre 2026 : les caisses se saisissent partout */
        NE_CONTIENT_PAS(sc.reponses[4], ">Tarif des assurances<");
        NE_CONTIENT_PAS(sc.reponses[6], ">Assurances<");
        NE_CONTIENT_PAS(sc.reponses[6], ">Agenda OneDoc<");
        CONTIENT(sc.reponses[9], "<h1>Patient</h1>");
        NE_CONTIENT_PAS(sc.reponses[9], ">Ordonnances<");
        CONTIENT(sc.reponses[9], "uméro d&#39;assuré");   /* 24 octobre 2026 : l'assurance se note partout ; seule sa facturation reste éteinte */
        liberer(&sc);
        remove("_essai_eteint.grymd");
        remove("_essai_eteint.grymd.clés");
    }

    {   /* Praxis 2, les SMS (23 octobre 2026) : la page « SMS » ; sans identifiant ni clé, rien ne part ; la clé enregistrée
           ne se relit pas ; le navigateur de grym n'envoie pas, il le dit */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Configuration"), evenement("q=3&clic=SMS"), get("/"),
                 evenement("q=4&clic=V%C3%A9rifier+le+compte"), get("/"),
                 evenement("q=5&z0=essai-cle&z1=secret-api-9&z2=Praxis&z3=&clic=Enregistrer"), get("/"),
                 evenement("q=6&z0=essai-cle&z1=&z2=Praxis&z3=&clic=V%C3%A9rifier+le+compte"), get("/"),
                 evenement("q=7&z0=essai-cle&z1=&z2=AnExpediteurTropLong&z3=&clic=Enregistrer"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_sms.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>SMS</h1>");
        CONTIENT(sc.reponses[3], "Aucun essai encore.");
        CONTIENT(sc.reponses[5], "Enregistre d&#39;abord l&#39;identifiant et la clé.");
        CONTIENT(sc.reponses[7], "Enregistré.");
        NE_CONTIENT_PAS(sc.reponses[7], "secret-api-9");
        CONTIENT(sc.reponses[9], "Un envoi sur internet se fait dans l&#39;atelier ou dans une application fabriquée.");
        CONTIENT(sc.reponses[11], "L&#39;expéditeur a 11 caractères au plus.");
        liberer(&sc);
        remove("_essai_sms.grymd");
        remove("_essai_sms.grymd.clés");
    }

    {   /* Le cabinet de Lorianne (projets/Praxis/Lorianne, 22 octobre 2026) : sa copie se fonde à son nom, à Bulle, avec
           l'agenda OneDoc et le choix d'agenda allumé ; aucun soin d'avance ; la fiche du cabinet a l'interrupteur */
        memset(&sc, 0, sizeof sc);
        copier_dossier("projets/Praxis/Lorianne", "_essai_lorianne_projet");
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=Configuration"), get("/"),
                 evenement("q=3&clic=Le+cabinet"), get("/"), evenement("q=4&clic=Fermer"), evenement("q=5&clic=Soins"), get("/"));
        servir_projet("_essai_lorianne_projet/Praxis.grym", "_essai_lorianne.grymd", &sc);
        CONTIENT(sc.reponses[1], "Bonjour Lorianne");
        CONTIENT(sc.reponses[1], ">Agenda OneDoc<");
        CONTIENT(sc.reponses[3], ">Rendez-vous<");
        CONTIENT(sc.reponses[5], "value=\"Grand-Rue\"");
        CONTIENT(sc.reponses[5], "value=\"Lorianne Clément\"");
        CONTIENT(sc.reponses[5], "> Choix d&#39;agenda</label>");
        NE_CONTIENT_PAS(sc.reponses[8], ">EPI</button>");
        liberer(&sc);
        remove("_essai_lorianne.grymd");
        remove("_essai_lorianne.grymd.clés");
    }

    {   /* Praxis 2 : l'agenda, Praxis ou OneDoc, choisi dans la Configuration (« Rendez-vous ») ; OneDoc sans adresse est
           refusé ; avec, l'accueil montre la tuile « Agenda OneDoc », qui ouvre l'adresse dans le navigateur du système,
           ce que le navigateur de grym ne sait pas faire (22 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=Configuration"),
                 evenement("q=3&clic=Rendez-vous"), get("/"),
                 evenement("q=4&z0=OneDoc&z1=&clic=Enregistrer"), get("/"),
                 evenement("q=5&z0=OneDoc&z1=https%3A%2F%2Fpro.onedoc.ch%2Fagenda&clic=Enregistrer"),
                 evenement("q=6&clic=Fermer"), get("/"), evenement("q=7&clic=Agenda+OneDoc"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet24.grymd", &sc);
        NE_CONTIENT_PAS(sc.reponses[1], ">Agenda OneDoc<");   /* « Praxis » au départ : pas de tuile */
        CONTIENT(sc.reponses[4], "<h1>Rendez-vous</h1>");
        CONTIENT(sc.reponses[4], ">Adresse de l&#39;agenda</label>");
        CONTIENT(sc.reponses[6], "Avec OneDoc, donne l&#39;adresse de l&#39;agenda OneDoc.");
        CONTIENT(sc.reponses[9], ">Agenda OneDoc<");
        CONTIENT(sc.reponses[11], "Une adresse web s&#39;ouvre depuis l&#39;atelier ou une application fabriquée.");
        liberer(&sc);
        remove("_essai_cabinet24.grymd");
    }

    {   /* Praxis : « Patient à ouvrir », une zone à recherche en tête de l'écran des patients ; choisir un patient ouvre sa
           page, et la zone se vide au retour ; un patient archivé est refusé (22 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Patients"), get("/"),
                 evenement("q=3&z0=Louis+Bapst&clic=-1"), get("/"), evenement("q=4&clic=Fermer"), get("/"),
                 evenement("q=5&choix=2.0"), evenement("q=6&clic=Archiver"),
                 evenement("q=7&z0=Anne+Rochat&clic=-1"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet23.grymd", &sc);
        CONTIENT(sc.reponses[2], ">Patient à ouvrir</label>");
        CONTIENT(sc.reponses[2], "Une partie du nom : choisis, la fiche s&#39;ouvre");
        CONTIENT(sc.reponses[4], "<h1>Patient</h1>");
        CONTIENT(sc.reponses[4], "value=\"Louis Bapst\"");
        CONTIENT(sc.reponses[6], ">Louis Bapst</button>");          /* de retour dans la liste */
        NE_CONTIENT_PAS(sc.reponses[6], "value=\"Louis Bapst\" selected");
        CONTIENT(sc.reponses[10], "Ce patient est archivé : la Configuration le montre, sous Archives.");
        liberer(&sc);
        remove("_essai_cabinet23.grymd");
    }

    {   /* Praxis : archiver un patient le retire de la liste des patients ; « Archives », dans la Configuration, le
           montre et le désarchive ; sans pièce, il se supprime encore (21 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Patients"), evenement("q=3&choix=2.0"), get("/"),
                 evenement("q=4&clic=Archiver"), get("/"), evenement("q=5&clic=Fermer"), evenement("q=6&clic=Configuration"),
                 evenement("q=7&clic=Archives"), get("/"), evenement("q=8&s0=0&clic=D%C3%A9sarchiver"), get("/"),
                 evenement("q=9&clic=Fermer"), evenement("q=10&clic=Fermer"), evenement("q=11&clic=Patients"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet21.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>Patient</h1>");
        CONTIENT(sc.reponses[3], ">Archiver</button>");
        CONTIENT(sc.reponses[3], "value=\"Anne Rochat\"");
        NE_CONTIENT_PAS(sc.reponses[5], ">Anne Rochat</button>");   /* archivée : hors de la liste des patients */
        CONTIENT(sc.reponses[5], ">Louis Bapst</button>");
        CONTIENT(sc.reponses[9], "<h1>Archives</h1>");
        CONTIENT(sc.reponses[9], ">Anne Rochat</button>");
        NE_CONTIENT_PAS(sc.reponses[11], ">Anne Rochat</button>");   /* désarchivée : plus dans les archives */
        CONTIENT(sc.reponses[15], ">Anne Rochat</button>");          /* et de retour dans la liste */
        liberer(&sc);
        remove("_essai_cabinet21.grymd");
    }

    {   /* Cabinet : l'IBAN des QR-factures, avec ou sans espaces ; vingt et un caractères (2.27) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=7"), evenement("q=3&clic=0"), get("/"),
                 evenement("q=4&z17=CH52+0483+5012&clic=27"), get("/"),
                 evenement("q=5&z17=CH44+3199+9123+0008+8901+2&clic=27"), get("/"),
                 evenement("q=6&z17=CH53+0483+5012+3456+7100+0&clic=27"), get("/"),
                 evenement("q=7&z17=CH52+0483+5012+3456+7100+0&clic=27"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet13.grymd", &sc);
        CONTIENT(sc.reponses[3], "value=\"CH52 0483 5012 3456 7100 0\"");   /* celui des données d'essai */
        CONTIENT(sc.reponses[5], "Un IBAN suisse compte 21 caractères : CH, puis 19 chiffres et lettres.");
        /* un QR-IBAN : la QR-facture de contrôle le refuse (2.29) ; une clé fausse aussi */
        CONTIENT(sc.reponses[7], "est un QR-IBAN ; il exige une référence QR (QRR)");
        CONTIENT(sc.reponses[7], "<h1>Le cabinet</h1>");
        CONTIENT(sc.reponses[9], "sa clé de contrôle ne correspond pas");
        CONTIENT(sc.reponses[11], "<h1>Configuration</h1>");   /* accepté, l'écran s'est fermé */
        liberer(&sc);
        remove("_essai_cabinet13.grymd");
    }

    {   /* Cabinet : « Facture » dans « À encaisser » ; un document, refusé dans le navigateur sans rien numéroter ; une
           séance offerte n'a ni facture ni quittance (20 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=6"), evenement("q=3&clic=5"), get("/"), evenement("q=4&s0=0&clic=6"), get("/"),
                 evenement("q=5&clic=7"), evenement("q=6&clic=12"), evenement("q=7&clic=4"), evenement("q=8&choix=2.0"),
                 evenement("q=9&choix=23.0"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet14.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>À encaisser</h1>");
        CONTIENT(sc.reponses[3], ">Facture</button>");
        CONTIENT(sc.reponses[5], "Un document se fabrique dans l&#39;atelier ou dans une application fabriquée.");
        CONTIENT(sc.reponses[11], ">Facture ou quittance</button>");
        liberer(&sc);
        remove("_essai_cabinet14.grymd");
        memset(&sc, 0, sizeof sc);   /* la séance offerte de Marc Dupraz, il y a quinze jours (après celle, prévue, du jour) */
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"), evenement("q=3&choix=2.4"), evenement("q=4&choix=23.1"), get("/"),
                 evenement("q=5&clic=17"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet15.grymd", &sc);
        CONTIENT(sc.reponses[4], "value=\"Marc Dupraz\" selected");
        CONTIENT(sc.reponses[4], "name=\"z10\" value=\"oui\" checked");
        CONTIENT(sc.reponses[6], "Une séance offerte n&#39;a ni facture ni quittance.");
        liberer(&sc);
        remove("_essai_cabinet15.grymd");
    }

    {   /* Cabinet : une séance prévue n'a pas encore de facture ; « Supprimer » une séance sans pièce ; la sauvegarde,
           rappelée à l'accueil tant qu'aucune n'est faite ; l'écran des sauvegardes : une copie chiffrée tout de suite (sans
           dossier choisi : « Sauvegardes », à côté du programme), une restauration sans copie choisie, un changement de
           mot de passe sans le bon actuel, refusés (20 octobre 2026) */
        char jour[11], copie[300];
        const time_t maintenant = time(NULL);
        strftime(jour, sizeof jour, "%Y-%m-%d", localtime(&maintenant));
        (void)jour;   /* « Praxis-AAAA-MM-JJ-HHMM.coffre », une par jour passé (24 octobre 2026) : trouvée après coup */
        copie[0] = '\0';
        system("rm -rf _essai_praxis/Sauvegardes");
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), get("/"), evenement("q=2&clic=5"), evenement("q=3&choix=0.0"),
                 get("/"), evenement("q=4&clic=17"), get("/"), evenement("q=5&clic=19"), get("/"), evenement("q=6&fermer=1"),
                 evenement("q=7&clic=7"), evenement("q=8&clic=4"), get("/"), evenement("q=9&z2=&clic=4"), get("/"),
                 evenement("q=10&clic=10"), get("/"), evenement("q=11&z12=mauvais-mot&z13=nouveau-mot&z14=nouveau-mot&clic=15"), get("/"),
                 evenement("q=12&fermer=1"), evenement("q=13&fermer=1"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet17.grymd", &sc);
        CONTIENT(sc.reponses[1], "Aucune sauvegarde encore : Configuration, puis Sauvegardes.");
        CONTIENT(sc.reponses[4], "<h1>Séance</h1>");
        CONTIENT(sc.reponses[6], "Une séance prévue se facture une fois faite : change d&#39;abord son statut.");
        CONTIENT(sc.reponses[8], "<h1>Séances prévues</h1>");   /* supprimée, la page s'est fermée */
        CONTIENT(sc.reponses[12], "<h1>Sauvegardes</h1>");
        CONTIENT(sc.reponses[14], "Copie chiffrée faite dans Sauvegardes.");
        CONTIENT(sc.reponses[16], "Choisis d&#39;abord la copie à restaurer.");
        CONTIENT(sc.reponses[18], "Le mot de passe actuel est faux.");
        CONTIENT(sc.reponses[21], "Dernière sauvegarde le ");
        {
            char prefixe[40];
            snprintf(prefixe, sizeof prefixe, "Praxis-%s-", jour);
            DIR *dd = opendir("_essai_praxis/Sauvegardes");
            for (struct dirent *x; dd && (x = readdir(dd)) != NULL;)
                if (!strncmp(x->d_name, prefixe, strlen(prefixe)) && strstr(x->d_name, ".coffre"))
                    snprintf(copie, sizeof copie, "_essai_praxis/Sauvegardes/%s", x->d_name);
            if (dd) closedir(dd);
        }
        FILE *c = fopen(copie, "rb");
        char tete[12] = "";
        if (c && fread(tete, 1, 12, c) != 12) tete[0] = 0;
        total++;
        if (!c || memcmp(tete, "GRYMCOFFRE2\n", 12) != 0) { echecs++; printf("ÉCHEC (test ligne %d) : la sauvegarde chiffrée %s\n", __LINE__, copie); }
        if (c) fclose(c);
        system("rm -rf _essai_praxis/Sauvegardes");
        liberer(&sc);
        remove("_essai_cabinet17.grymd");
    }

    {   /* Cabinet : tout se supprime et se rétablit : un patient supprimé va dans la corbeille avec ses séances ;
           rétabli depuis la Configuration, il revient avec elles (20 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"), evenement("q=3&choix=2.0"), get("/"), evenement("q=4&clic=29"), get("/"),
                 evenement("q=5&fermer=1"), evenement("q=6&clic=7"), evenement("q=7&clic=3"), get("/"),
                 evenement("q=8&s1=0&clic=R%C3%A9tablir+le+patient"), get("/"), evenement("q=9&clic=Fermer"), evenement("q=10&fermer=1"),
                 evenement("q=11&clic=4"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet18.grymd", &sc);
        CONTIENT(sc.reponses[3], "value=\"Anne Rochat\"");
        CONTIENT(sc.reponses[3], ">Supprimer</button>");
        NE_CONTIENT_PAS(sc.reponses[5], ">Anne Rochat</button>");   /* plus dans la liste des patients */
        CONTIENT(sc.reponses[9], "<h1>Corbeille</h1>");
        CONTIENT(sc.reponses[9], ">Anne Rochat</button>");
        CONTIENT(sc.reponses[9], "<td>Anne Rochat</td>");           /* ses séances, parties avec elle */
        NE_CONTIENT_PAS(sc.reponses[11], "Anne Rochat");
        CONTIENT(sc.reponses[15], ">Anne Rochat</button>");        /* rétablie, de retour parmi les patients */
        liberer(&sc);
        remove("_essai_cabinet18.grymd");
    }

    {   /* Praxis : « Effacer le patient », depuis la corbeille, le supprime définitivement : il n'y est plus, et un homonyme
           peut s'inscrire (21 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=Patients"), evenement("q=3&choix=2.0"),
                 evenement("q=4&clic=Supprimer"), evenement("q=5&clic=Fermer"), evenement("q=6&clic=Configuration"),
                 evenement("q=7&clic=Corbeille"), get("/"), evenement("q=8&s1=0&clic=Effacer+le+patient"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet22.grymd", &sc);
        CONTIENT(sc.reponses[7], "<h1>Corbeille</h1>");
        CONTIENT(sc.reponses[7], ">Anne Rochat</button>");
        CONTIENT(sc.reponses[7], ">Effacer le patient</button>");
        NE_CONTIENT_PAS(sc.reponses[9], "Anne Rochat");   /* ni elle, ni ses séances */
        NE_CONTIENT_PAS(sc.reponses[9], "class=\"erreur\"");
        liberer(&sc);
        remove("_essai_cabinet22.grymd");
    }

    {   /* La base chiffrée (2.37) : grym servir demande le mot de passe par une page, deux champs masqués la première
           fois, jamais renvoyés ; trop court, ou deux saisies qui diffèrent, refusés ; choisi, le programme s'exécute */
#ifdef _WIN32
        _mkdir("_essai_chiffre");
#else
        mkdir("_essai_chiffre", 0755);
#endif
        FILE *f = fopen("_essai_chiffre/app.grym", "wb");
        if (f) {
            fputs("La base est chiffrée.\nUne note, conservée, a :\n    un texte (texte).\n"
                  "Afficher « notes : » puis le nombre de notes conservées.\n", f);
            fclose(f);
        }
        remove("_essai_chiffre.grymd.clés");
        mot_de_passe_essais(NULL);   /* la page, cette fois */
        question_suivante = 0;
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), post("q=1&c0=court&c1=court"), get("/"), post("q=1&c0=essai-essai&c1=essai-autre"), get("/"),
                 post("q=1&c1=essai-essai"), get("/"));
        servir_projet_base("_essai_chiffre/app.grym", "_essai_chiffre.grymd", &sc, 2);
        CONTIENT(sc.reponses[0], "choisis leur mot de passe");
        CONTIENT(sc.reponses[0], "type=\"password\" id=\"c0\"");
        CONTIENT(sc.reponses[0], "type=\"password\" id=\"c1\"");
        CONTIENT(sc.reponses[2], "Au moins huit caractères.");
        NE_CONTIENT_PAS(sc.reponses[2], "court");   /* jamais renvoyé dans la page */
        CONTIENT(sc.reponses[4], "Les deux saisies diffèrent.");
        CONTIENT(sc.reponses[6], "notes : 0");
        liberer(&sc);
        total++;
        if (!fichier_present("_essai_chiffre.grymd.clés")) { echecs++; printf("ÉCHEC (test ligne %d) : les clés\n", __LINE__); }
        remove("_essai_chiffre.grymd");
        remove("_essai_chiffre.grymd.clés");
        remove("_essai_chiffre/app.grym");
        remove("_essai_chiffre");
        mot_de_passe_essais("essai-essai");
    }

    {   /* Praxis, base chiffrée (21 octobre 2026) : au premier lancement, la bienvenue et le dossier des sauvegardes, puis
           l'accueil ; rouvert, l'accueil tout de suite (le mot de passe, l'exécution l'a demandé) ; la base, illisible */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=Continuer"), get("/"));
        servir_projet_base("_essai_praxis/Praxis.grym", "_essai_cabinet19.grymd", &sc, 2);
        CONTIENT(sc.reponses[0], "<h1>Bienvenue</h1>");
        CONTIENT(sc.reponses[0], "Les données des patients sont chiffrées");
        NE_CONTIENT_PAS(sc.reponses[0], "Anne Rochat");
        CONTIENT(sc.reponses[2], "Bonjour Nicolas");
        liberer(&sc);
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"));
        servir_projet_base("_essai_praxis/Praxis.grym", "_essai_cabinet19.grymd", &sc, 3);
        CONTIENT(sc.reponses[0], "Bonjour Nicolas");
        NE_CONTIENT_PAS(sc.reponses[0], "<h1>Bienvenue</h1>");
        liberer(&sc);
        total++;
        {
            FILE *b = fopen("_essai_cabinet19.grymd", "rb");
            char t[16] = "";
            if (b) { if (fread(t, 1, 16, b) != 16) t[0] = 0; fclose(b); }
            if (!b || memcmp(t, "SQLite format 3", 15) == 0) { echecs++; printf("ÉCHEC (test ligne %d) : la base de Praxis n'est pas chiffrée\n", __LINE__); }
        }
        remove("_essai_cabinet19.grymd");
        remove("_essai_cabinet19.grymd.clés");
    }

    {   /* Cabinet : la recherche des patients (nom, téléphone, localité) ; l'export CSV des comptes de l'année, dans Exports,
           à côté du programme (20 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"), get("/"),
                 evenement("q=3&z1=rochat&clic=-1"), get("/"), evenement("q=4&z1=Romont&clic=-1"), get("/"),
                 evenement("q=5&fermer=1"), evenement("q=6&clic=6"), evenement("q=7&clic=11"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet20.grymd", &sc);
        CONTIENT(sc.reponses[2], ">Louis Bapst</button>");
        CONTIENT(sc.reponses[4], ">Anne Rochat</button>");
        NE_CONTIENT_PAS(sc.reponses[4], ">Louis Bapst</button>");
        CONTIENT(sc.reponses[6], ">Claire Monney</button>");      /* par sa localité */
        NE_CONTIENT_PAS(sc.reponses[6], ">Anne Rochat</button>");
        CONTIENT(sc.reponses[10], "Exporté dans Exports : Recettes ");
        liberer(&sc);
        remove("_essai_cabinet20.grymd");
        int recettes = 0, frais = 0;
        DIR *d = opendir("_essai_praxis/Exports");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                char chemin[600];
                snprintf(chemin, sizeof chemin, "_essai_praxis/Exports/%s", e->d_name);
                if (!strncmp(e->d_name, "Recettes ", 9)) recettes++;
                if (!strncmp(e->d_name, "Frais ", 6)) frais++;
                if (e->d_name[0] != '.') remove(chemin);
            }
            closedir(d);
        }
        remove("_essai_praxis/Exports");
        total++;
        if (recettes != 1 || frais != 1) { echecs++; printf("ÉCHEC (test ligne %d) : l'export CSV (%d, %d)\n", __LINE__, recettes, frais); }
    }

    {   /* « Faire comme un clic sur « B ». » (2.36) : le corps d'un autre événement de clic, dans la même transaction ;
           un clic par libellé, « clic=Deux fois », comme par rang */
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&clic=1"), get("/"), evenement("q=2&clic=Deux+fois"), get("/"));
        servir("Le total vaut 0.\nL'écran x montre :\n    le texte \"Total : \" suivi du total,\n    un bouton « Ajouter »,\n"
               "    un bouton « Deux fois ».\nQuand on clique sur « Ajouter » dans l'écran x :\n    Le total devient le total + 1.\n"
               "Quand on clique sur « Deux fois » dans l'écran x :\n    Faire comme un clic sur « Ajouter ».\n"
               "    Faire comme un clic sur « Ajouter ».\nOuvrir l'écran x.\n", &sc);
        CONTIENT(sc.reponses[0], "Total : 0");
        CONTIENT(sc.reponses[2], "Total : 1");
        CONTIENT(sc.reponses[4], "Total : 3");
        liberer(&sc);
    }

    {   /* Cabinet : l'écran des factures (vide tant qu'aucune n'est faite dans l'atelier) (20 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=6"), evenement("q=3&clic=9"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet16.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>Factures 20");   /* l'année des comptes, celle du jour */
        CONTIENT(sc.reponses[3], "<th>État</th>");
        liberer(&sc);
        remove("_essai_cabinet16.grymd");
    }

    {   /* Le texte calculé (2.24) : relu à chaque attente, après chaque événement */
        const char *src = "L'écran x montre :\n    une ville (texte), facultative,\n"
            "    le texte \"Ville : \" suivi de la ville de l'écran, en petites capitales,\n    un bouton « Fini ».\n"
            "Quand on ouvre l'écran x :\n    La ville de l'écran devient « Bulle ».\n"
            "Quand on clique sur « Fini » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
        memset(&sc, 0, sizeof sc);
        REQUETES(get("/"), evenement("q=1&z0=Fribourg&clic=-1"), get("/"));
        servir(src, &sc);
        CONTIENT(sc.reponses[0], "<p class=\"texte capitales\">Ville : Bulle</p>");
        CONTIENT(sc.reponses[2], "<p class=\"texte capitales\">Ville : Fribourg</p>");
        liberer(&sc);
        /* Cabinet : le surtitre daté de l'accueil */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet7.grymd", &sc);
        CONTIENT(sc.reponses[1], "<p class=\"texte capitales\">Aujourd&#39;hui, ");
        liberer(&sc);
        remove("_essai_cabinet7.grymd");
    }

    {   /* Cabinet : la page d'une séance (commentaire tapé, enregistré) ; son patient s'ouvre sur sa page, jamais sur la
           fiche déduite ; la page d'un soin (19 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"), evenement("q=3&choix=2.0"), evenement("q=4&choix=23.0"), get("/"),
                 evenement("q=5&clic=2"), get("/"), evenement("q=6&clic=27"),
                 evenement("q=7&z1=Anne+Rochat&z3=EPI&z5=09%3A00&z6=60&z7=70&z8=faite&z9=Twint"
                           "&z12=Nuque+d%C3%A9tendue&clic=16"),
                 evenement("q=8&choix=23.0"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet5.grymd", &sc);
        CONTIENT(sc.reponses[4], "<h1>Séance</h1>");
        CONTIENT(sc.reponses[4], ">Commentaire dicté</label>");
        CONTIENT(sc.reponses[4], "<option value=\"Anne Rochat\" selected>Anne Rochat</option>");
        CONTIENT(sc.reponses[6], "<h1>Patient</h1>");        /* « Voir le patient » : la page, pas la fiche déduite */
        CONTIENT(sc.reponses[6], "value=\"Anne Rochat\"");
        CONTIENT(sc.reponses[10], "Nuque détendue</textarea>");
        CONTIENT(sc.reponses[10], "name=\"z6\" value=\"60\"");
        NE_CONTIENT_PAS(sc.reponses[10], "class=\"erreur\"");
        liberer(&sc);
        remove("_essai_cabinet5.grymd");
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=7"), evenement("q=3&clic=1"), evenement("q=4&choix=0.0"), get("/"),
                 evenement("q=5&z0=EPI&z1=130&z2=70&z3=60&z4=1003&z6=patient&clic=9"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet6.grymd", &sc);
        CONTIENT(sc.reponses[4], "<h1>Soin</h1>");
        CONTIENT(sc.reponses[6], "<td>1003</td>");   /* le code tarifaire, dans la liste des soins */
        liberer(&sc);
        remove("_essai_cabinet6.grymd");
    }

    {   /* Cabinet : la page d'un patient ; l'anamnèse s'y corrige, une nouvelle séance le reçoit d'office */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"), evenement("q=3&choix=2.0"), get("/"),
                 evenement("q=4&z1=Anne+Rochat&z9=079+000+00+01&z10=&z11=&z3=Route+de+Riaz&z4=12a&z5=&z7=1630&z8=Bulle"
                           "&z6=Suisse&z19=Migraines+depuis+2019&clic=25"),
                 evenement("q=5&choix=2.0"), get("/"), evenement("q=6&clic=26"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet4.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>Patient</h1>");
        CONTIENT(sc.reponses[3], "value=\"Anne Rochat\"");
        /* l'ajout, dicté ou tapé, rejoint l'anamnèse (vide jusque-là) à l'enregistrement ; relue à la réouverture */
        CONTIENT(sc.reponses[6], "Migraines depuis 2019</textarea>");
        CONTIENT(sc.reponses[8], ">Nouvelle séance</h1>");
        CONTIENT(sc.reponses[8], "<option value=\"Anne Rochat\" selected>Anne Rochat</option>");
        NE_CONTIENT_PAS(sc.reponses[8], "class=\"erreur\"");
        liberer(&sc);
        remove("_essai_cabinet4.grymd");
    }

    {   /* Cabinet : un nouveau patient depuis l'écran de séance, par l'écran d'inscription, aussitôt choisi ;
           l'anamnèse à côté des champs (19 octobre 2026) */
        memset(&sc, 0, sizeof sc);
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=0"), evenement("q=3&clic=3"), get("/"),
                 evenement("q=4&z1=Zo%C3%A9+Neuve&z9=079+000+00+09&z10=&z11=&z3=Rue+du+Pont&z4=1&z5=&z7=1630&z8=Bulle"
                           "&z6=Suisse&z19=Douleurs+lombaires&clic=22"), get("/"));
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet2.grymd", &sc);
        CONTIENT(sc.reponses[3], "<h1>Nouveau patient</h1>");
        CONTIENT(sc.reponses[3], ">Anamnèse</label>");
        CONTIENT(sc.reponses[5], "<option value=\"Zoé Neuve\" selected>Zoé Neuve</option>");
        CONTIENT(sc.reponses[5], ">Nouvelle séance</h1>");   /* on est revenu à la séance */
        liberer(&sc);
        remove("_essai_cabinet2.grymd");
    }

    {   /* L'application « Cabinet » (projets/) : un client saisi, deux séances, le tarif réduit dès la deuxième,
           la fiche du client et son historique */
        memset(&sc, 0, sizeof sc);
        /* l'écran d'inscription : le pays garde sa valeur de départ, « Suisse » */
        char *formulaire = evenement("q=4&z1=Zo%C3%A9+Essai&z9=079+000+00+00&z10=&z11=&z3=Route+de+Riaz&z4=12a&z5=&z7=1630"
                                     "&z8=Bulle&z6=Suisse&z19=&clic=22");
        REQUETES(evenement("q=1&clic=Continuer"), evenement("q=2&clic=4"),                                       /* 0 : Clients */
                 evenement("q=3&clic=4"),                                       /* 1 : Nouveau client */
                 get("/"),                                                      /* 2 : l'écran d'inscription */
                 formulaire,                                                    /* 3 */
                 get("/"),                                                      /* 4 : la liste des clients */
                 evenement("q=5&fermer=1"),                                     /* 5 : retour à l'accueil */
                 evenement("q=6&clic=0"),                                       /* 6 : Nouvelle séance */
                 /* 7 : client et soin choisis, prix laissé vide : il se calcule avant « Enregistrer » */
                 evenement("q=7&z2=Zo%C3%A9+Essai&z4=EPI&z5=01.10.2026&z6=&z7=&z9=%C3%A0+payer&z12=&clic=19"),
                 evenement("q=8&clic=0"),                                       /* 8 : Nouvelle séance */
                 evenement("q=9&z2=Zo%C3%A9+Essai&z4=EPI&z5=2026-10-08&z6=14%3A30&z7=50&z9=Twint&z12=&clic=19"),   /* 9 */
                 evenement("q=10&clic=0"),                                       /* 10 : Nouvelle séance */
                 evenement("q=11&z2=&z4=EPI&z5=08.10.2026&z6=&z7=&z9=Twint&z12=50&clic=19"),   /* 11 : sans client */
                 get("/"),                                                      /* 12 */
                 evenement("q=12&fermer=1"),                                    /* 13 */
                 evenement("q=13&clic=4"),                                      /* 14 : Clients */
                 evenement("q=14&choix=2.6"),                                   /* 15 : la fiche d'Anne */
                 get("/"));                                                     /* 16 */
        servir_projet("_essai_praxis/Praxis.grym", "_essai_cabinet.grymd", &sc);
        /* l'inscription demande le téléphone et le courriel, des textes vérifiés (§ 22.13) */
        CONTIENT(sc.reponses[3], ">Téléphone</label>");
        CONTIENT(sc.reponses[3], ">Courriel</label>");
        /* l'adresse postale structurée, comme la QR-facture l'exige (rue, numéro, numéro postal, localité, pays) */
        CONTIENT(sc.reponses[3], ">Numéro postal</label>");
        CONTIENT(sc.reponses[13], "<fieldset class=\"groupe inverse\"><legend>Prix proposé</legend>");   /* 2.18 */
        CONTIENT(sc.reponses[17], "Route de Riaz");
        CONTIENT(sc.reponses[17], "1630");
        CONTIENT(sc.reponses[17], "Suisse");
        CONTIENT(sc.reponses[5], "Zoé Essai</button></td><td>079 000 00 00</td>");   /* après les six clients d'essai (essais.grym) */
        CONTIENT(sc.reponses[13], "Remplis « Patient » : cette zone est obligatoire.");
        /* la date : le calendrier du navigateur, en « 2026-10-08 » ; la séance enregistrée avec ce format est au
           08.10.2026 dans la page du patient (plus bas) */
        CONTIENT(sc.reponses[13], "<input type=\"date\" id=\"z5\" name=\"z5\" value=\"2026-10-08\"");
        /* la page du patient : ses données dans les zones, l'anamnèse à droite ; ses séances, la plus récente en tête,
           la première au tarif, la suivante au tarif réduit, avec leur durée */
        CONTIENT(sc.reponses[17], "value=\"Route de Riaz\"");
        CONTIENT(sc.reponses[17], ">Anamnèse</label>");
        CONTIENT(sc.reponses[17], "value=\"23.0\">08.10.2026</button></td><td>14:30</td><td>EPI</td><td>50</td><td>70</td>");
        CONTIENT(sc.reponses[17], "value=\"23.1\">01.10.2026</button></td><td></td><td>EPI</td><td></td><td>130</td>");
        CONTIENT(sc.reponses[17], "<span class=\"etiquette etiquette-o\">à payer</span>");
        NE_CONTIENT_PAS(sc.reponses[17], "value=\"23.2\"");
        CONTIENT(sc.reponses[17], ">Nouvelle séance</button>");
        NE_CONTIENT_PAS(sc.reponses[17], "class=\"erreur\"");
        liberer(&sc);
        remove("_essai_cabinet.grymd");
    }

    /* --- v2.0 : chaque exemple du dépôt tourne dans le navigateur --- */
    {
        static const char *const SANS_QUESTION[] = { "aptitudes.grym", "boucles.grym", "conserver.grym", "dates.grym",
            "entites.grym", "facture.grym", "fichiers.grym", "formules.grym", "methodes.grym", "objets.grym",
            "partitions.grym", "rabais.grym", "registre.grym" };
        for (size_t k = 0; k < sizeof SANS_QUESTION / sizeof *SANS_QUESTION; k++) {
            memset(&sc, 0, sizeof sc);
            REQUETES(get("/"));
            exemple(__LINE__, SANS_QUESTION[k], &sc, NULL);
            liberer(&sc);
        }
        remove("exemples/copie.png");   /* fichiers.grym l'écrit, et n'écrase jamais */

        question_suivante = 0;
        memset(&sc, 0, sizeof sc);
        REQUETES(reponse("1"), reponse("Ana|079|"), reponse("2"), reponse("3"), reponse("Ana"), reponse("0"), get("/"));
        static const char *const SAISIE[] = { "Ajouté : Ana", "1 trouvé(s).", "Au revoir.", NULL };
        exemple(__LINE__, "saisie.grym", &sc, SAISIE);
        liberer(&sc);

        question_suivante = 0;
        memset(&sc, 0, sizeof sc);
        REQUETES(reponse("1"), reponse("Johann Sebastian Bach|"),
                 reponse("2"), reponse("BWV 1079|L'Offrande musicale|Johann Sebastian Bach|1747"), reponse("baroque"), reponse(""),
                 reponse("3"), reponse("P-1|BWV 1079|Bärenreiter|"), reponse("violon"), reponse("2"), reponse(""),
                 reponse("4"), reponse("5"), reponse("Johann Sebastian Bach"), reponse("6"), reponse("P-1"),
                 reponse("9"), reponse("P-1"), reponse("P-1|BWV 1079|Henle|"),
                 reponse("7"), reponse("P-1"), reponse("8"), reponse("P-1"), reponse("0"), get("/"));
        static const char *const PARTOTHEQUE[] = { "Compositeur ajouté : Johann Sebastian Bach", "Œuvre ajoutée : L&#39;Offrande musicale",
            "Partition ajoutée : P-1", "BWV 1079     L&#39;Offrande musicale            Johann Sebastian Bach (1747)",
            "1 œuvre(s) de Johann Sebastian Bach", "L&#39;Offrande musicale chez Bärenreiter", "- violon × 2",
            "Modifiée : P-1", "Dans la corbeille : P-1", "Rétablie : P-1", "Au revoir.", NULL };
        exemple(__LINE__, "partotheque.grym", &sc, PARTOTHEQUE);
        liberer(&sc);
    }
#ifndef _WIN32
    {   /* tout exemple du dépôt figure ci-dessus : un exemple nouveau doit y entrer */
        int n = 0;
        DIR *d = opendir("exemples");
        for (struct dirent *e; d && (e = readdir(d)); ) {
            size_t l = strlen(e->d_name);
            if (l > 5 && strcmp(e->d_name + l - 5, ".grym") == 0) n++;
        }
        if (d) closedir(d);
        total++;
        if (n != 15) { echecs++; printf("ÉCHEC : %d exemples .grym dans exemples/, 15 attendus ici\n", n); }
    }
#endif

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
