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

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
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
static void servir_projet(const char *chemin, const char *base, Script *sc) {
    remove(base);
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
    remove(base);
}

#define CONTIENT(r, f)     verifier(__LINE__, #r, r, f, 1)
#define NE_CONTIENT_PAS(r, f) verifier(__LINE__, #r, r, f, 0)
#define REQUETES(...) do { char *liste[] = { __VA_ARGS__ }; sc.n = sizeof liste / sizeof *liste; \
    sc.requetes = malloc(sizeof liste); memcpy(sc.requetes, liste, sizeof liste); } while (0)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
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
        CONTIENT(sc.reponses[1], ".entete h1.titre-grand{font-size:2.2em}");
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
