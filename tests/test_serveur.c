/* Tests du serveur local (docs/v2.md, § 5 et § 9) : chaque requête passe par un transport scripté,
 * sans réseau. Les règles de sécurité ont chacune leur attaque. */
#include "analyseur.h"
#include "compilateur.h"
#include "serveur.h"
#include "texte.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
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

/* Exécute src servi par le transport scripté ; les réponses restent dans *sc. */
static void servir(const char *src, Script *sc) {
    Transport t = { sc, recevoir, envoyer };
    Serveur *s = serveur_creer(PORT, JETON, "essai.grym", t);
    Portee *portee = portee_creer();
    Machine *m = machine_creer();
    Interface i = serveur_interface(s);
    machine_interface(m, &i);
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
    NE_CONTIENT_PAS(sc.reponses[2], "Tapez « . »");   /* une convention de la console */
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
    CONTIENT(sc.reponses[0], "list=\"l1\" autocomplete=\"off\"");
    CONTIENT(sc.reponses[0], "<datalist id=\"l1\"><option value=\"Bach\"><option value=\"élodie\"><option value=\"Zoé\"></datalist>");
    NE_CONTIENT_PAS(sc.reponses[0], "Oublié");
    CONTIENT(sc.reponses[0], "name=\"c3\" value=\"\" inputmode=\"decimal\"");
    CONTIENT(sc.reponses[0], "<select id=\"c4\" name=\"c4\"><option value=\"\" selected></option><option value=\"oui\">oui</option>");
    CONTIENT(sc.reponses[0], "<select id=\"c5\" name=\"c5\"><option value=\"\" selected></option>");
    CONTIENT(sc.reponses[2], "Zoé 12,5 faux absent");
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

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}
