/* Tests de l'arithmétique décimale, du bytecode et de la machine virtuelle, GrymoiR v0.2. */
#include "analyseur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "vm.h"
#include "empreinte.h"
#include "coffre.h"
#include "base.h"
#include "date.h"
#include "imprimeur.h"
#include "sqlite3.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static int total = 0, echecs = 0;

/* Exécute une source ; renvoie la sortie sans le dernier saut de ligne,
 * ou « ERREUR l:c message ». */
static char *executer_source(Portee *portee, Machine *m, const char *src, int interactif) {
    Programme p;
    Diagnostic d;
    if (!analyser(src, strlen(src), portee, interactif, &p, &d)) {
        char *msg = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return msg;
    }
    Chaine s = {0};
    Module *b = compiler(&p, &d);
    int ok = b && machine_executer(m, b, &s, &d);
    module_detruire(b);
    programme_liberer(&p);
    char *r = chaine_rendre(&s);
    if (!ok) {
        free(r);
        char *m = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return m;
    }
    size_t l = strlen(r);
    if (l && r[l - 1] == '\n') r[l - 1] = '\0';
    return r;
}

static void signaler(int l, const char *src, const char *attendu, const char *obtenu);

/* Réponses scriptées (grammaire, § 17) : la question et ce qui attend vont dans la sortie,
 * comme à l'écran, puis la ligne suivante du script est rendue. */
typedef struct { const char *const *lignes; size_t n, i; } Script;

static char *reponses(void *contexte, Chaine *sortie, const char *question) {
    Script *sc = contexte;
    chaine_ajouter(sortie, question);
    chaine_ajouter(sortie, " ");
    if (sc->i == sc->n) return NULL;
    return grym_dupliquer(sc->lignes[sc->i++]);
}

static char *faux_transcrire(void *contexte, const unsigned char *octets, size_t taille, char **erreur);

static void verifier_saisie(int l, const char *src, const char *const *lignes, size_t n, const char *attendu) {
    total++;
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    Script sc = { lignes, n, 0 };
    machine_lecteur(m, reponses, &sc);
    char *r = executer_source(p, m, src, 0);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(l, src, attendu, r);
    free(r);
    machine_detruire(m);
    portee_detruire(p);
}

static void signaler(int l, const char *src, const char *attendu, const char *obtenu) {
    echecs++;
    printf("ÉCHEC (test ligne %d)\n  source  : %s\n  attendu : %s\n  obtenu  : %s\n",
           l, src, attendu, obtenu);
}

/* Programme complet (mode fichier). Un attendu qui commence par « ~ » est un fragment. */
static void verifier(int l, const char *src, const char *attendu, int interactif) {
    total++;
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_transcripteur(m, faux_transcrire);   /* § 15.8 : un faux transcripteur, sans Whisper */
    char *r = executer_source(p, m, src, interactif);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(l, src, attendu, r);
    free(r);
    machine_detruire(m);
    portee_detruire(p);
}

#define CALC(expr, att)  verifier(__LINE__, expr, att, 1)
#define PROG(src, att)   verifier(__LINE__, src, att, 0)

/* Fichier d'essai, créé dans le dossier courant et retiré à la fin. */
static void creer_fichier(const char *nom, const void *octets, size_t n) {
    FILE *f = fopen(nom, "wb");
    if (f) { fwrite(octets, 1, n, f); fclose(f); }
}

/* Format d'une base (charte, art. 13) : PRAGMA user_version, ou −1 si illisible. */
static long format_base(const char *chemin) {
    sqlite3 *db = NULL;
    long v = -1;
    if (sqlite3_open_v2(chemin, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
            v = (long)sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return v;
}

static void sql_direct(const char *chemin, const char *sql) {
    sqlite3 *db = NULL;
    if (sqlite3_open(chemin, &db) == SQLITE_OK) sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
}

/* Un nombre lu directement dans une base (la première colonne de la première ligne), -1 sinon. */
static long sql_nombre(const char *chemin, const char *sql) {
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    long v = -1;
    if (sqlite3_open_v2(chemin, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK
        && sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        v = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return v;
}

/* Réponses scriptées qui, à la question « casser ? », font écrire un autre programme dans la base (une autre
 * connexion) : la ligne de montant 5 passe à -4, ce que la règle interdit. */
typedef struct { Script sc; const char *base; } ScriptCasseur;

static char *reponses_casseur(void *contexte, Chaine *sortie, const char *question) {
    ScriptCasseur *c = contexte;
    if (strcmp(question, "casser ?") == 0)
        sql_direct(c->base, "UPDATE \"e ligne\" SET \"c montant\" = '-4' WHERE \"c montant\" = '5';");
    return reponses(&c->sc, sortie, question);
}

/* Nombre d'index de ce nom dans la base : 1 s'il existe. */
static long index_existe(const char *chemin, const char *nom) {
    sqlite3 *db = NULL;
    long n = -1;
    if (sqlite3_open_v2(chemin, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master WHERE type = 'index' AND name = ?", -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_text(st, 1, nom, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_ROW) n = (long)sqlite3_column_int64(st, 0);
        }
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return n;
}

/* Lance src sur la base chemin ; rend la sortie (à libérer). */
static char *lancer_sur(const char *chemin, const char *src) {
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_base(m, chemin);
    char *r = executer_source(p, m, src, 0);
    machine_detruire(m);
    portee_detruire(p);
    return r;
}

/* Une interface « page » pour les tests (docs/vm.md, § 13) : tous les champs d'un coup, puis seuls les
 * champs refusés à nouveau, comme un navigateur qui renvoie la page. Réponses spéciales : « ANNULER »,
 * « VIDER ». Le journal note chaque page, ses champs et ses refus. */
typedef struct {
    const char *const *reponses;
    size_t n, i;
    Chaine journal;
    int effacements;
} Page;

/* Un faux transcripteur (§ 15.8) : il rend « transcription de N octets », pour vérifier que la machine l'appelle au
 * bon moment et range son texte au bon endroit, sans Whisper. */
static char *faux_transcrire(void *contexte, const unsigned char *octets, size_t taille, char **erreur) {
    (void)contexte; (void)octets; (void)erreur;
    return grym_formater("transcription de %lu octets", (unsigned long)taille);
}

static int page_disponible(void *contexte) { (void)contexte; return 1; }

static Issue page_formulaire(void *contexte, Chaine *sortie, Champ *champs, size_t n, size_t *arret,
                             Validation valider, void *vcontexte) {
    Page *p = contexte;
    (void)sortie;
    char *acceptes = calloc(n ? n : 1, 1);
    int refus;
    chaine_ajouter(&p->journal, "page");
    do {
        refus = 0;
        for (size_t k = 0; k < n; k++) {
            if (acceptes[k]) continue;
            chaine_ajouter(&p->journal, " ");
            chaine_ajouter(&p->journal, champs[k].libelle);
            if (champs[k].valeur) { chaine_ajouter(&p->journal, "="); chaine_ajouter(&p->journal, champs[k].valeur); }
            if (champs[k].videable) chaine_ajouter(&p->journal, "(videable)");
            if (p->i >= p->n) { free(acceptes); *arret = k; return ISSUE_FIN; }
            const char *r = p->reponses[p->i++];
            if (strcmp(r, "ANNULER") == 0) { free(acceptes); *arret = k; return ISSUE_ANNULE; }
            champs[k].vider = strcmp(r, "VIDER") == 0;
            champs[k].ligne = grym_dupliquer(champs[k].vider ? "" : r);
            char *message = NULL;
            int v = valider(vcontexte, k, &message);
            if (v < 0) { free(message); free(acceptes); *arret = k; return ISSUE_ARRET; }
            if (v > 0) {
                chaine_ajouter(&p->journal, " !");
                chaine_ajouter(&p->journal, message);
                refus = 1;
            } else {
                acceptes[k] = 1;
            }
            free(message);
        }
    } while (refus);
    free(acceptes);
    return ISSUE_REPONDU;
}

static void page_effacer(void *contexte, Chaine *sortie) { (void)sortie; ((Page *)contexte)->effacements++; }

/* Exécute src avec l'interface page ; rend la sortie suivie du journal (à libérer). */
static char *avec_page(const char *src, const char *const *reponses, size_t n, int *effacements) {
    Page pg = { reponses, n, 0, {0}, 0 };
    Interface i = { &pg, page_disponible, page_formulaire, page_effacer, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_interface(m, &i);
    char *r = executer_source(p, m, src, 0);
    machine_detruire(m);
    portee_detruire(p);
    char *j = chaine_rendre(&pg.journal);
    char *tout = grym_formater("%s\n%s", r, j);
    free(r);
    free(j);
    if (effacements) *effacements = pg.effacements;
    return tout;
}

static int fichier_existe(const char *nom) {
    FILE *f = fopen(nom, "rb");
    if (f) fclose(f);
    return f != NULL;
}

#define APT_M "Une chose horodatée a : une date.\nUne chose numérotée a : un numéro.\nUne personne a : un nom.\n"


/* ---------------------------------------------------------------- */
/* La base chiffrée (2.37, § 16.18)                                  */
/* ---------------------------------------------------------------- */

static char *lancer_chiffre(const char *chemin, const char *mdp, const char *src) {
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_base(m, chemin);
    machine_mot_de_passe_base(m, mdp);
    char *r = executer_source(p, m, src, 0);
    machine_detruire(m);
    portee_detruire(p);
    return r;
}

/* Vrai si le fichier contient ce texte en clair. */
static int contient_en_clair(const char *chemin, const char *texte) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return 0;
    static char tout[1 << 20];
    const size_t n = fread(tout, 1, sizeof tout, f), l = strlen(texte);
    fclose(f);
    for (size_t i = 0; i + l <= n; i++) if (memcmp(tout + i, texte, l) == 0) return 1;
    return 0;
}

static void verifier_chiffre(int ligne, const char *chemin, const char *mdp, const char *src, const char *attendu) {
    char *r = lancer_chiffre(chemin, mdp, src);
    total++;
    const int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) { echecs++; signaler(ligne, src, attendu, r); }
    free(r);
}

static void effacer_base(const char *b) {
    const char *suffixes[] = { "", ".clés", ".restaurer", ".restaurer.clés", "-wal", "-shm" };
    for (size_t k = 0; k < sizeof suffixes / sizeof *suffixes; k++) {
        char *c = grym_formater("%s%s", b, suffixes[k]);
        remove(c);
        free(c);
    }
}

static void essais_base_chiffree(void) {
#define NOTE "Une note, conservée, a :\n    un texte (texte).\n"
#define CH "La base est chiffrée.\n" NOTE
    const char *b = "_essai_chiffree.grymd";
    effacer_base(b);
    /* la première fois : les clés naissent, la base est chiffrée, rien de lisible dans le fichier */
    verifier_chiffre(__LINE__, b, "essai-essai",
                     CH "La n vaut une nouvelle note :\n    Le texte vaut « secret-anamnese ».\nConserver la n.\n"
                        "Afficher le nombre de notes conservées.", "1");
    total++;
    if (!fichier_existe("_essai_chiffree.grymd.clés") || contient_en_clair(b, "secret-anamnese") || contient_en_clair(b, "SQLite format 3")) {
        echecs++;
        printf("ÉCHEC (ligne %d) : la base n'est pas chiffrée, ou ses clés manquent\n", __LINE__);
    }
    /* le bon mot de passe l'ouvre ; un autre, ou aucun, non */
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Afficher le texte de la note conservée dont le texte contient « secret ».", "secret-anamnese");
    verifier_chiffre(__LINE__, b, "autre-mot", CH "Afficher le nombre de notes conservées.", "~Mot de passe faux.");
    verifier_chiffre(__LINE__, b, NULL, CH "Afficher le nombre de notes conservées.", "~sans son mot de passe, rien ne s'ouvre");
    /* sans la déclaration, la base chiffrée ne se lit pas */
    verifier_chiffre(__LINE__, b, NULL, NOTE "Afficher le nombre de notes conservées.", "~écris « La base est chiffrée. » en tête du programme");
    /* la sauvegarde : un coffre de format 2, rien de lisible ; « avec le mot de passe » refusé sur une base chiffrée */
    remove("_essai_chiffree_copie.coffre");
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Sauvegarder la base dans « _essai_chiffree_copie.coffre ».\nAfficher « fait ».", "fait");
    total++;
    {
        FILE *f = fopen("_essai_chiffree_copie.coffre", "rb");
        char tete[12] = "";
        if (f) { if (fread(tete, 1, 12, f) != 12) tete[0] = 0; fclose(f); }
        if (memcmp(tete, "GRYMCOFFRE2\n", 12) != 0 || contient_en_clair("_essai_chiffree_copie.coffre", "secret-anamnese")) {
            echecs++;
            printf("ÉCHEC (ligne %d) : le coffre de format 2\n", __LINE__);
        }
    }
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Sauvegarder la base dans « x.coffre » avec le mot de passe « m ».",
                     "~sa sauvegarde l'est d'office, avec ses clés");
    /* le mot de passe change : l'ancien ne sert plus, le nouveau ouvre ; un faux actuel est refusé */
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Changer le mot de passe de la base pour « nouveau-mot », l'ancien étant « faux ».",
                     "~Le mot de passe actuel est faux.");
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Changer le mot de passe de la base pour « nouveau-mot », l'ancien étant « essai-essai ».\n"
                                                 "Afficher « changé ».", "changé");
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Afficher 1.", "~Mot de passe faux.");
    verifier_chiffre(__LINE__, b, "nouveau-mot", CH "Afficher le nombre de notes conservées.", "1");
    /* la restauration du coffre d'avant : rien en clair en attente ; elle s'ouvre avec le mot de passe de sa copie */
    verifier_chiffre(__LINE__, b, "nouveau-mot", CH "La m vaut une nouvelle note :\n    Le texte vaut « deuxième ».\nConserver la m.\n"
                                                 "Restaurer la base depuis « _essai_chiffree_copie.coffre » avec le mot de passe « essai-essai ».\n"
                                                 "Afficher « prête ».", "prête");
    total++;
    if (contient_en_clair("_essai_chiffree.grymd.restaurer", "secret-anamnese")) { echecs++; printf("ÉCHEC (ligne %d) : attente en clair\n", __LINE__); }
    verifier_chiffre(__LINE__, b, "nouveau-mot", CH "Afficher 1.", "~elle s'ouvre avec le mot de passe de sa copie");
    verifier_chiffre(__LINE__, b, "essai-essai", CH "Afficher le nombre de notes conservées.", "1");
    total++;
    if (contient_en_clair(b, "secret-anamnese")) { echecs++; printf("ÉCHEC (ligne %d) : la base restaurée est en clair\n", __LINE__); }
    /* une base en clair, d'avant la déclaration, se chiffre sur place à la première ouverture */
    const char *c = "_essai_clair.grymd";
    effacer_base(c);
    verifier_chiffre(__LINE__, c, NULL, NOTE "La n vaut une nouvelle note :\n    Le texte vaut « secret-anamnese ».\nConserver la n.\nAfficher 1.", "1");
    total++;
    if (!contient_en_clair(c, "secret-anamnese")) { echecs++; printf("ÉCHEC (ligne %d) : la base d'essai devrait être en clair\n", __LINE__); }
    verifier_chiffre(__LINE__, c, "essai-essai", CH "Afficher le nombre de notes conservées.", "1");
    total++;
    if (contient_en_clair(c, "secret-anamnese")) { echecs++; printf("ÉCHEC (ligne %d) : la base n'a pas été chiffrée sur place\n", __LINE__); }
    /* la phrase : une seule fois, au premier niveau ; « Changer » sur une base qui n'est pas chiffrée */
    verifier_chiffre(__LINE__, NULL, NULL, "La base est chiffrée.\nLa base est chiffrée.", "~se déclare une seule fois");
    verifier_chiffre(__LINE__, NULL, NULL, "Si vrai :\n    La base est chiffrée.", "~au premier niveau");
    effacer_base(c);
    verifier_chiffre(__LINE__, c, NULL, NOTE "Changer le mot de passe de la base pour « a », l'ancien étant « b ».", "~n'est pas chiffrée");
    effacer_base(b);
    effacer_base(c);
    remove("_essai_chiffree_copie.coffre");
    {   /* les « avant restauration » laissés par l'essai */
        DIR *d = opendir(".");
        struct dirent *e;
        while (d && (e = readdir(d)))
            if (!strncmp(e->d_name, "_essai_chiffree.grymd avant restauration", 40)) remove(e->d_name);
        if (d) closedir(d);
    }
    /* Un secret (§ 22.17, 2.40) : rangé tel quel dans une base chiffrée, illisible dans le fichier ; sa présence se teste,
       la fiche le cache, le lire est refusé, et il ne se vérifie pas comme un mot de passe */
#define CAB "La base est chiffrée.\nUn cabinet, conservé, a :\n    un nom (texte), unique,\n    une clé (secret), facultative.\n"
    const char *s = "_essai_secret.grymd";
    effacer_base(s);
    verifier_chiffre(__LINE__, s, "essai-essai",
                     CAB "Le c vaut un nouveau cabinet :\n    Le nom vaut « Praxis ».\n    La clé vaut « cle-api-4417 ».\n"
                         "Conserver le c.\nSi la clé du c est présente, afficher « présente ».\nAfficher la fiche du c.",
                     "présente\nCabinet Praxis\n  Nom  Praxis\n  Clé  ••••••");
    total++;
    if (contient_en_clair(s, "cle-api-4417")) {
        echecs++;
        printf("ÉCHEC (ligne %d) : un secret se lit dans le fichier de la base\n", __LINE__);
    }
    verifier_chiffre(__LINE__, s, "essai-essai", CAB "Afficher la clé du cabinet conservé.",
                     "~« clé » est un secret : il ne se lit que pour être envoyé.");
    verifier_chiffre(__LINE__, s, "essai-essai", CAB "Si « cle-api-4417 » correspond à la clé du cabinet conservé, afficher « oui ».",
                     "~« clé » est un secret : il ne se lit que pour être envoyé.");
    verifier_chiffre(__LINE__, s, "essai-essai", CAB "La clé du cabinet conservé devient « autre ».\n"
                                                 "Si la clé du cabinet conservé est présente, afficher « gardée ».", "gardée");
    effacer_base(s);
#undef CAB
#undef CH
#undef NOTE
}

static char *lancer_chiffre_dans(const char *dossier, const char *chemin, const char *mdp, const char *src) {
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_dossier(m, dossier);
    machine_base(m, chemin);
    machine_mot_de_passe_base(m, mdp);
    char *r = executer_source(p, m, src, 0);
    machine_detruire(m);
    portee_detruire(p);
    return r;
}

static unsigned char *lire_fichier_entier(const char *chemin, size_t *n) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return NULL;
    static unsigned char t[1 << 16];
    *n = fread(t, 1, sizeof t, f);
    fclose(f);
    unsigned char *r = malloc(*n ? *n : 1);
    memcpy(r, t, *n);
    return r;
}

#define VRAI(c, quoi) do { total++; if (!(c)) { echecs++; printf("ÉCHEC (ligne %d) : %s\n", __LINE__, quoi); } } while (0)

/* Le secours (étape 3) : la clé de secours du développeur ; sa partie publique dans le projet donne à chaque base une
 * enveloppe de secours ; la partie secrète remet un mot de passe à neuf, sur la base ou depuis un coffre. */
static void essais_secours(void) {
#define CH "La base est chiffrée.\nUne note, conservée, a :\n    un texte (texte).\n"
    char *e = NULL;
    unsigned char pub[32], sec[32], pub2[32], sec2[32];
    size_t nf = 0, nf2 = 0;
    VRAI(!secours_creer("court", pub, &nf, &e) && e && strstr(e, "douze"), "mot de passe de secours trop court refusé");
    free(e); e = NULL;
    unsigned char *fichier = secours_creer("superadmin-essai", pub, &nf, &e);
    unsigned char *fichier2 = secours_creer("un-autre-developpeur", pub2, &nf2, &e);
    VRAI(fichier && fichier2 && nf == 141, "la clé de secours naît (141 octets)");
    unsigned char lue[32];
    VRAI(fichier && secours_publique(fichier, nf, lue) && memcmp(lue, pub, 32) == 0, "sa partie publique se lit sans mot de passe");
    VRAI(fichier && !secours_ouvrir(fichier, nf, "faux-faux-faux", sec, &e), "mot de passe de secours faux refusé");
    free(e); e = NULL;
    VRAI(fichier && secours_ouvrir(fichier, nf, "superadmin-essai", sec, &e), "le bon mot de passe ouvre la clé de secours");
    VRAI(fichier2 && secours_ouvrir(fichier2, nf2, "un-autre-developpeur", sec2, &e), "la seconde clé s'ouvre");
    char h[65];
    secours_hex_ecrire(pub, h);
    VRAI(secours_hex_lire(h, lue) && memcmp(lue, pub, 32) == 0, "la clé publique en hexadécimal, aller et retour");
#ifdef _WIN32
    _mkdir("_essai_secours");
#else
    mkdir("_essai_secours", 0755);
#endif
    const char *b = "_essai_secours/app.grymd";
    effacer_base(b);
    remove("_essai_secours/clé-de-secours.pub");
    /* une base née sans clé publique : pas d'enveloppe de secours */
    char *r = lancer_chiffre_dans("_essai_secours", b, "mot-de-nicolas", CH "La n vaut une nouvelle note :\n    Le texte vaut « a ».\nConserver la n.\nAfficher 1.");
    free(r);
    size_t nc = 0;
    unsigned char *cles = lire_fichier_entier("_essai_secours/app.grymd.clés", &nc);
    VRAI(cles && !cles_ont_secours(cles, nc), "sans clé publique, pas d'enveloppe de secours");
    free(cles);
    VRAI(!base_secourir(b, sec, "neuf-mot-de-passe", &e) && e && strstr(e, "pas d'enveloppe de secours"), "le secours refusé, et dit pourquoi");
    free(e); e = NULL;
    /* la clé publique posée dans le projet : l'enveloppe s'ajoute à l'ouverture suivante */
    FILE *f = fopen("_essai_secours/clé-de-secours.pub", "wb");
    if (f) { fputs(h, f); fputc('\n', f); fclose(f); }
    r = lancer_chiffre_dans("_essai_secours", b, "mot-de-nicolas", CH "Afficher le nombre de notes conservées.");
    VRAI(r && !strcmp(r, "1"), "la base s'ouvre comme avant");
    free(r);
    cles = lire_fichier_entier("_essai_secours/app.grymd.clés", &nc);
    VRAI(cles && cles_ont_secours(cles, nc), "l'enveloppe de secours ajoutée");
    free(cles);
    /* une sauvegarde, faite avec l'enveloppe de secours */
    remove("_essai_secours/copie.coffre");
    r = lancer_chiffre_dans("_essai_secours", b, "mot-de-nicolas", CH "Sauvegarder la base dans « copie.coffre ».\nAfficher « fait ».");
    VRAI(r && !strcmp(r, "fait"), "la sauvegarde");
    free(r);
    /* Nicolas oublie : une autre clé de secours ne sert à rien ; la bonne remet un mot de passe neuf */
    VRAI(!base_secourir(b, sec2, "neuf-mot-de-passe", &e) && e && strstr(e, "n'est pas celle de cette base"), "une autre clé refusée");
    free(e); e = NULL;
    VRAI(base_secourir(b, sec, "neuf-mot-de-passe", &e), "le secours remet un mot de passe");
    r = lancer_chiffre_dans("_essai_secours", b, "mot-de-nicolas", CH "Afficher 1.");
    VRAI(r && strstr(r, "Mot de passe faux."), "l'ancien ne sert plus");
    free(r);
    r = lancer_chiffre_dans("_essai_secours", b, "neuf-mot-de-passe", CH "Afficher le nombre de notes conservées.");
    VRAI(r && !strcmp(r, "1"), "le nouveau ouvre la base, ses données intactes");
    free(r);
    VRAI(fichier_existe("_essai_secours/app.grymd.clés avant secours"), "les anciennes clés gardées");
    /* l'ordinateur perdu, le mot de passe oublié : depuis le coffre, une base neuve */
    remove("_essai_secours/sauvee.grymd");
    remove("_essai_secours/sauvee.grymd.clés");
    VRAI(base_secourir_coffre("_essai_secours/copie.coffre", sec, "mot-retrouve", "_essai_secours/sauvee.grymd", &e), "le secours depuis un coffre");
    VRAI(!base_secourir_coffre("_essai_secours/copie.coffre", sec, "mot-retrouve", "_essai_secours/sauvee.grymd", &e) && e && strstr(e, "jamais écrasé"),
         "jamais par-dessus une base");
    free(e); e = NULL;
    r = lancer_chiffre_dans("_essai_secours", "_essai_secours/sauvee.grymd", "mot-retrouve", CH "Afficher le texte de la note conservée dont le texte est « a ».");
    VRAI(r && !strcmp(r, "a"), "la base sauvée s'ouvre avec le mot de passe retrouvé");
    free(r);
    VRAI(!contient_en_clair("_essai_secours/sauvee.grymd", "SQLite format 3"), "la base sauvée est chiffrée");
    free(fichier);
    free(fichier2);
    effacer_base(b);
    effacer_base("_essai_secours/sauvee.grymd");
    remove("_essai_secours/app.grymd.clés avant secours");
    remove("_essai_secours/copie.coffre");
    remove("_essai_secours/clé-de-secours.pub");
    remove("_essai_secours");
#undef CH
}

/* ---------------------------------------------------------------- */
/* Fichiers utilisés (grammaire, § 21)                              */
/* ---------------------------------------------------------------- */

static void ecrire_source(const char *nom, const char *texte) { creer_fichier(nom, texte, strlen(texte)); }

/* Analyse le fichier `chemin` avec ceux qu'il utilise, le compile, passe le module par le format .grymb
 * (il doit tourner sans les sources), puis l'exécute. Sortie, ou « ERREUR [fichier:]l:c message ». */
static char *lancer_fichier(const char *chemin) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return grym_dupliquer("ERREUR fichier absent");
    static char tampon[65536];
    size_t n = fread(tampon, 1, sizeof tampon - 1, f);
    fclose(f);
    tampon[n] = '\0';
    Portee *portee = portee_creer();
    portee_fichier(portee, chemin);
    Programme p;
    Diagnostic d;
    size_t l = strlen(chemin);
    int compact = l > 6 && strcmp(chemin + l - 6, ".grymc") == 0;
    int ok = compact ? analyser_compact(tampon, n, portee, &p, &d) : analyser(tampon, n, portee, 0, &p, &d);
    portee_detruire(portee);
    if (!ok) {
        char *r = d.fichier ? grym_formater("ERREUR %s:%d:%d %s", d.fichier, d.ligne, d.colonne, d.message)
                            : grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return r;
    }
    Module *b = compiler(&p, &d);
    programme_liberer(&p);
    size_t taille = 0;
    unsigned char *octets = module_serialiser(b, &taille);
    module_detruire(b);
    char *erreur = NULL;
    b = module_lire(octets, taille, &erreur);
    free(octets);
    if (!b) { char *r = grym_formater("ERREUR relecture : %s", erreur); free(erreur); return r; }
    Machine *m = machine_creer();
    Chaine s = {0};
    ok = machine_executer(m, b, &s, &d);
    module_detruire(b);
    machine_detruire(m);
    char *r = chaine_rendre(&s);
    if (!ok) {
        free(r);
        r = d.fichier ? grym_formater("ERREUR %s:%d:%d %s", d.fichier, d.ligne, d.colonne, d.message)
                      : grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return r;
    }
    size_t lr = strlen(r);
    if (lr && r[lr - 1] == '\n') r[lr - 1] = '\0';
    return r;
}

static void verifier_fichier(int ligne, const char *chemin, const char *attendu) {
    total++;
    char *r = lancer_fichier(chemin);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(ligne, chemin, attendu, r);
    free(r);
}
#define FICHIER(chemin, att) verifier_fichier(__LINE__, chemin, att)

static void essais_utiliser(void) {
    static const char *const FICHIERS[] = {
        "_u_donnees.grym", "_u_prog.grym", "_u_tard.grym", "_u_exec.grym", "_u_b.grym", "_u_faux.grym",
        "_u_c.grym", "_u_x.grym", "_u_y.grym", "_u_d.grym", "_u_self.grym", "_u_absent.grym", "_u_ext.grym",
        "_u_deux.grym", "_u_deux.grymc", "_u_e.grym", "_u_dup.grym", "_u_div.grym", "_u_h.grym",
        "_u_haut.grym", "_u_gauche.grym", "_u_droite.grym", "_u_bas.grym", "_u_compact.grymc", "_u_k.grym",
        "tests/_u_commun.grym", "tests/_u_fiches.grym", "_u_sous.grym", "_u_prog.grymc", "_u_texte.grym",
        "_u_gdon.grym", "_u_gprog.grym", "_u_regle.grym", "_u_regle_prog.grym", "_u_regle_prog.grymd"
    };
    ecrire_source("_u_donnees.grym", "Remarque : les données.\nUn compositeur, conservé, a :\n    un nom (texte), unique.\n"
                                     "Le carré d'un nombre vaut nombre × nombre.\n");
    /* l'essentiel : une remarque, puis « Utiliser », puis le programme, qui voit classes et formules */
    ecrire_source("_u_prog.grym", "Remarque : le programme.\nUtiliser « _u_donnees ».\nAfficher le carré de 7.\n"
                                  "Le c vaut un nouveau compositeur :\n    Le nom vaut « Bach ».\nAfficher nom du c.\n");
    FICHIER("_u_prog.grym", "49\nBach");
    /* une règle déclarée dans un fichier utilisé vaut dans le programme qui l'utilise (§ 16.14, § 21) */
    ecrire_source("_u_regle.grym", "Un stock, conservé, a :\n    une quantité (nombre).\n"
                                   "Chaque stock conservé vérifie :\n    la quantité du stock ≥ 0,\n    sinon « Stock négatif. ».\n");
    ecrire_source("_u_regle_prog.grym", "Utiliser « _u_regle ».\nLe s vaut un nouveau stock :\n    La quantité vaut −1.\n"
                                        "Conserver le s.\n");
    FICHIER("_u_regle_prog.grym", "~Stock négatif.");
    /* formater un fichier qui en utilise un autre garde les genres qui y sont déclarés (§ 12, § 21) */
    {
        ecrire_source("_u_gdon.grym", "Une facture, conservée, a :\n    une échéance (date), facultative.\n"
                                      "La remise d'un montant vaut montant ÷ 10.\n");
        const char *src = "Utiliser « _u_gdon ».\n\nLa f vaut une nouvelle facture.\n"
                          "Si l'échéance de la f est absente, afficher la remise de 50.\n";
        ecrire_source("_u_gprog.grym", src);
        Portee *pt = portee_creer();
        portee_fichier(pt, "_u_gprog.grym");
        Programme pg;
        Diagnostic dg;
        total++;
        if (!analyser(src, strlen(src), pt, 0, &pg, &dg)) {
            signaler(__LINE__, src, "(analyse)", dg.message);
            diagnostic_liberer(&dg);
        } else {
            char *canon = imprimer_litteraire(&pg);
            if (strcmp(canon, src) != 0) signaler(__LINE__, src, src, canon);
            free(canon);
            programme_liberer(&pg);
        }
        portee_detruire(pt);
    }
    /* en tête du fichier seulement */
    ecrire_source("_u_tard.grym", "Afficher 1.\nUtiliser « _u_donnees ».\n");
    FICHIER("_u_tard.grym", "ERREUR 2:1 « Utiliser » se place en tête du fichier, avant toute autre phrase.");
    /* un fichier utilisé ne contient que des déclarations */
    ecrire_source("_u_exec.grym", "Le carré d'un n vaut n × n.\nLe x vaut 1.\n");
    ecrire_source("_u_b.grym", "Utiliser « _u_exec ».\n");
    FICHIER("_u_b.grym", "ERREUR 1:10 « _u_exec.grym » contient une phrase qui s'exécute (ligne 2) : "
                         "seul un fichier de déclarations s'utilise.");
    /* une erreur dans un fichier utilisé désigne ce fichier */
    ecrire_source("_u_faux.grym", "Un client a :\n    un nom,\n    un nom.\n");
    ecrire_source("_u_c.grym", "Utiliser « _u_faux ».\n");
    FICHIER("_u_c.grym", "ERREUR _u_faux.grym:3:8 Champ « nom » déjà nommé.");
    /* utilisation circulaire, et fichier qui s'utilise lui-même */
    ecrire_source("_u_x.grym", "Utiliser « _u_y ».\n");
    ecrire_source("_u_y.grym", "Utiliser « _u_x ».\n");
    ecrire_source("_u_d.grym", "Utiliser « _u_x ».\n");
    FICHIER("_u_d.grym", "~Utilisation circulaire : « _u_x.grym » s'utilise lui-même");
    ecrire_source("_u_self.grym", "Utiliser « _u_self ».\n");
    FICHIER("_u_self.grym", "ERREUR 1:10 Un fichier ne s'utilise pas lui-même.");
    /* fichier introuvable, extension écrite, deux formes du même nom */
    ecrire_source("_u_absent.grym", "Utiliser « _u_rien ».\n");
    FICHIER("_u_absent.grym", "ERREUR 1:10 « _u_rien » introuvable : ni « _u_rien.grym » ni « _u_rien.grymc ».");
    ecrire_source("_u_ext.grym", "Utiliser « _u_donnees.grym ».\n");
    FICHIER("_u_ext.grym", "~Écris le fichier sans extension : « Utiliser « _u_donnees ». »");
    ecrire_source("_u_deux.grymc", "# vide\n");
    ecrire_source("_u_texte.grym", "Utiliser « _u_deux ».\n");
    ecrire_source("_u_deux.grym", "Remarque : vide.\n");
    FICHIER("_u_texte.grym", "~« _u_deux » désigne à la fois « _u_deux.grym » et « _u_deux.grymc »");
    /* un conflit nomme le fichier de l'autre déclaration */
    ecrire_source("_u_dup.grym", "Un compositeur a : un nom.\n");
    ecrire_source("_u_e.grym", "Utiliser « _u_donnees ».\nUtiliser « _u_dup ».\n");
    FICHIER("_u_e.grym", "ERREUR _u_dup.grym:1:4 La classe « compositeur » existe déjà : déclarée dans "
                         "« _u_donnees.grym », ligne 2.");
    /* une erreur d'exécution dans une formule d'un fichier utilisé, après passage par le bytecode */
    ecrire_source("_u_div.grym", "Le quotient d'un a et d'un b vaut a ÷ b.\n");
    ecrire_source("_u_h.grym", "Utiliser « _u_div ».\nAfficher le quotient de 1 et de 0.\n");
    FICHIER("_u_h.grym", "ERREUR _u_div.grym:1:37 Division par zéro.");
    /* un losange : deux fichiers utilisent le même, lu une seule fois */
    ecrire_source("_u_bas.grym", "Un point a : un x.\n");
    ecrire_source("_u_gauche.grym", "Utiliser « _u_bas ».\nLe double d'un n vaut n × 2.\n");
    ecrire_source("_u_droite.grym", "Utiliser « _u_bas ».\nLe triple d'un n vaut n × 3.\n");
    ecrire_source("_u_haut.grym", "Utiliser « _u_gauche ».\nUtiliser « _u_droite ».\nLe p vaut un nouveau point :\n"
                                  "    Le x vaut le double du triple de 2.\nAfficher x de p.\n");
    FICHIER("_u_haut.grym", "12");
    /* un fichier utilisé en forme compacte */
    ecrire_source("_u_compact.grymc", "_calcul _le cube(_un n) << n × n × n\n");
    ecrire_source("_u_k.grym", "Utiliser « _u_compact ».\nAfficher le cube de 3.\n");
    FICHIER("_u_k.grym", "27");
    /* le chemin part du dossier du fichier qui utilise */
    ecrire_source("tests/_u_commun.grym", "Le double d'un n vaut n × 2.\n");
    ecrire_source("tests/_u_fiches.grym", "Utiliser « _u_commun ».\nLe triple d'un n vaut n × 3.\n");
    ecrire_source("_u_sous.grym", "Utiliser « tests/_u_fiches ».\nAfficher le double du triple de 5.\n");
    FICHIER("_u_sous.grym", "30");
    /* un programme principal en forme compacte */
    ecrire_source("_u_prog.grymc", "_utiliser « _u_donnees »\n_afficher carré(4)\n");
    FICHIER("_u_prog.grymc", "16");

    {   /* l'impression garde la phrase, jamais les déclarations du fichier utilisé ; aller et retour compacts */
        total += 2;
        Portee *portee = portee_creer();
        portee_fichier(portee, "_u_prog.grym");
        const char *src = "Remarque : le programme.\nUtiliser « _u_donnees ».\nAfficher le carré de 7.\n";
        Programme p;
        Diagnostic d;
        if (analyser(src, strlen(src), portee, 0, &p, &d)) {
            char *l = imprimer_litteraire(&p), *c = imprimer_compact(&p);
            if (strcmp(l, src) != 0) signaler(__LINE__, src, src, l);
            const char *ac = "# le programme.\n_utiliser « _u_donnees »\n_afficher carré(7)\n";
            if (strcmp(c, ac) != 0) signaler(__LINE__, src, ac, c);
            free(l);
            free(c);
            programme_liberer(&p);
        } else {
            signaler(__LINE__, src, "analyse", d.message);
            diagnostic_liberer(&d);
            echecs++;
        }
        portee_detruire(portee);
    }
    {   /* boucle interactive : « Utiliser » depuis le dossier courant ; une seconde fois ne fait rien */
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r1 = executer_source(p, m, "Utiliser « _u_donnees ».\nAfficher le carré de 3.", 1);
        char *r2 = executer_source(p, m, "Utiliser « _u_donnees ».\nAfficher le carré de 4.", 1);
        if (strcmp(r1, "9") != 0 || strcmp(r2, "16") != 0) signaler(__LINE__, "interactif", "9 / 16", r2);
        free(r1);
        free(r2);
        machine_detruire(m);
        portee_detruire(p);
    }
    /* « utiliser » ne commence pas le nom d'une action */
    PROG("Pour utiliser un x :\n    Afficher x.\n", "ERREUR 1:6 « utiliser » commence une construction du langage : il ne peut pas commencer le nom d'une action.");
    for (size_t k = 0; k < sizeof FICHIERS / sizeof *FICHIERS; k++) remove(FICHIERS[k]);
}


/* Essai de migration (docs/atelier.md, A2-c) : le rapport, ou « ERREUR message ». La base ne change pas. */
static char *essai_migration(const char *chemin, const char *src) {
    Portee *p = portee_creer();
    Programme prog;
    Diagnostic d;
    if (!analyser(src, strlen(src), p, 0, &prog, &d)) {
        char *r = grym_formater("ERREUR analyse %s", d.message);
        diagnostic_liberer(&d);
        portee_detruire(p);
        return r;
    }
    Module *b = compiler(&prog, &d);
    programme_liberer(&prog);
    portee_detruire(p);
    Machine *m = machine_creer();
    machine_base(m, chemin);
    Chaine rapport = {0}, sortie = {0};
    machine_essai_migration(m, &rapport);
    int ok = machine_executer(m, b, &sortie, &d);
    module_detruire(b);
    machine_detruire(m);
    free(sortie.d);
    char *r = chaine_rendre(&rapport);
    if (!ok) {
        free(r);
        r = grym_formater("ERREUR %s", d.message);
        diagnostic_liberer(&d);
    }
    return r;
}

static void essais_migration(void) {
    const char *base = "_essai_migration.grymd";
    remove(base);
    const char *v1 = "Un compositeur, conservé, a : un nom (texte), unique.\n";
    const char *v2 = "Un compositeur, conservé, a : un nom (texte), unique, un pays (texte), « Suisse » au départ, "
                     "une naissance (date), facultative.\n";
    const char *v3 = "Un compositeur, conservé, a : un nom (texte), unique, un âge (nombre).\n";
    struct { const char *src, *attendu; int ligne; } cas[] = {
        { v1, "La base n'existe pas encore : le premier lancement la créera, avec 1 table.\n", __LINE__ },
    };
    for (size_t k = 0; k < sizeof cas / sizeof *cas; k++) {
        total++;
        char *r = essai_migration(base, cas[k].src);
        if (strcmp(r, cas[k].attendu) != 0) signaler(cas[k].ligne, cas[k].src, cas[k].attendu, r);
        free(r);
    }
    total++;
    FILE *f = fopen(base, "rb");   /* l'essai n'a pas créé la base */
    if (f) { fclose(f); signaler(__LINE__, v1, "pas de base", "base créée"); }
    char *r = lancer_sur(base, "Un compositeur, conservé, a : un nom (texte), unique.\n"
                                "Pour remplir :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n"
                                "    Conserver c.\n    Le d vaut un nouveau compositeur :\n        Le nom vaut « Liszt ».\n"
                                "    Conserver d.\nRemplir.\n");
    free(r);
    struct { const char *src, *attendu; int ligne; } suite[] = {
        { v1, "La base est à jour : rien ne changera.\n", __LINE__ },
        { v2, "« compositeur » : champ « pays » ajouté ; 2 compositeurs reçoivent « Suisse ».\n"
              "« compositeur » : champ « naissance » ajouté ; 2 compositeurs le reçoivent absent.\n", __LINE__ },
        { v1, "La base est à jour : rien ne changera.\n", __LINE__ },   /* l'essai précédent n'a rien écrit */
        { v3, "ERREUR « âge » est nouveau, et 2 compositeurs sont déjà conservés : donne-lui une valeur de départ, "
              "après son type : « (nombre), … au départ ».", __LINE__ },
        { "Un point a : un x.\n", "Aucune entité conservée : aucune base.\n", __LINE__ },
    };
    for (size_t k = 0; k < sizeof suite / sizeof *suite; k++) {
        total++;
        char *x = essai_migration(base, suite[k].src);
        if (strcmp(x, suite[k].attendu) != 0) signaler(suite[k].ligne, suite[k].src, suite[k].attendu, x);
        free(x);
    }
    /* Renommages (§ 16.7, 2.23) : « anciennement « client » » garde les valeurs, les liaisons et les liens */
    {
        const char *ren = "_essai_renommage.grymd";
        remove(ren);
        char *r0 = lancer_sur(ren,
            "Un genre, conservé, a : un nom (texte), unique.\n"
            "Un client, conservé, a : un nom (texte), unique, une photo (fichier), facultative, des genres (genre).\n"
            "Une séance, conservée, a : un client (client), un prix (nombre).\n"
            "Le g vaut un nouveau genre :\n    Le nom vaut « calme ».\nConserver g.\n"
            "Le c vaut un nouveau client :\n    Le nom vaut « Ana ».\nConserver c.\nLes genres du c gagnent g.\n"
            "Le s vaut une nouvelle séance :\n    Le client vaut c.\n    Le prix vaut 130.\nConserver s.\n");
        free(r0);
        const char *v2 =
            "Un genre, conservé, a : un nom (texte), unique.\n"
            "Un patient, conservé, anciennement « client », a : un nom (texte), unique, "
            "un document (fichier), facultatif, anciennement « photo », des humeurs (genre), anciennement « genres ».\n"
            "Une séance, conservée, a : un patient (patient), anciennement « client », un prix (nombre).\n";
        total++;
        char *x = essai_migration(ren, v2);
        const char *attendu = "« client » devient « patient » : 1 objet gardé.\n"
                              "« patient » : « photo » devient « document », 1 valeur gardée.\n"
                              "« patient » : « genres » devient « humeurs », 1 liaison gardée.\n"
                              "« séance » : « client » devient « patient », 1 valeur gardée.\n";
        if (strcmp(x, attendu) != 0) signaler(__LINE__, v2, attendu, x);
        free(x);
        total++;   /* l'essai n'a rien changé : le même rapport, une seconde fois */
        x = essai_migration(ren, v2);
        if (strcmp(x, attendu) != 0) signaler(__LINE__, v2, attendu, x);
        free(x);
        char *lire = grym_formater("%sPour chaque séance conservée :\n"
                                   "    Afficher le nom du patient de la séance puis le prix de la séance.\n"
                                   "Afficher le nombre d'humeurs du patient conservé.\n"
                                   "Afficher le nombre de patients conservés dont le nom est « Ana ».\n", v2);
        for (int fois = 0; fois < 2; fois++) {   /* la seconde fois, il n'y a plus rien à renommer */
            total++;
            char *y = lancer_sur(ren, lire);
            if (strcmp(y, "Ana 130\n1\n1") != 0) signaler(__LINE__, lire, "Ana 130\n1\n1", y);
            free(y);
        }
        total++;   /* la base est à jour */
        x = essai_migration(ren, v2);
        if (strcmp(x, "La base est à jour : rien ne changera.\n") != 0) signaler(__LINE__, v2, "à jour", x);
        free(x);
        total++;   /* un nouvel objet se conserve sous le nouveau nom, et le lien suit */
        char *nouveau = grym_formater("%sLe p vaut un nouveau patient :\n    Le nom vaut « Bea ».\nConserver p.\n"
                                      "Le s vaut une nouvelle séance :\n    Le patient vaut p.\n    Le prix vaut 70.\n"
                                      "Conserver s.\nAfficher la somme des prix des séances conservées.\n", v2);
        char *y = lancer_sur(ren, nouveau);
        if (strcmp(y, "200") != 0) signaler(__LINE__, nouveau, "200", y);
        free(y);
        free(nouveau);
        free(lire);
        total++;   /* l'ancien nom, oublié, mais sans aucune valeur : il part sans rien emporter (2.36) */
        x = essai_migration(ren, "Un genre, conservé, a : un nom (texte), unique.\n"
            "Un patient, conservé, a : un nom (texte), unique, un dossier (fichier), facultatif, des humeurs (genre).\n"
            "Une séance, conservée, a : un patient (patient), un prix (nombre).\n");
        if (!strstr(x, "champ « document » retiré ; aucune valeur n'est perdue"))
            signaler(__LINE__, "document vide oublié", "retiré ; aucune valeur n'est perdue", x);
        free(x);
        remove(ren);
    }
    remove(base);
}


/* ---------------------------------------------------------------- */
/* Écrans (grammaire, § 22), pilotés par script                      */
/* ---------------------------------------------------------------- */

/* Une interface d'écrans pour les essais : les événements viennent d'un script (« clic Nouveau », « choix 2 »
 * pour la deuxième ligne de la première liste, « fermer »), les formulaires de la page ci-dessus.
 * Le journal note chaque ouverture, chaque liste montrée, chaque erreur et chaque fermeture. */
typedef struct {
    Page page;
    const char *const *evenements;
    size_t n, i;
    const ElementEcran *elements;
    size_t nb_elements;
    /* écrans empilés : ceux du dessous, pour les retrouver à la fermeture (§ 22.3) */
    ElementEcran *pile[8];
    size_t nb_pile[8];
    size_t profondeur;
} Scene;

static void scene_ouvrir(void *contexte, Chaine *sortie, const char *titre, const ElementEcran *el, size_t n) {
    Scene *s = contexte;
    (void)sortie;
    /* une copie : l'interface ne garde rien de la machine après l'appel ; les textes restent ceux de la machine,
       vivants tant que l'écran est ouvert */
    if (s->profondeur < 8) {
        s->pile[s->profondeur] = malloc(n * sizeof *el + 1);
        memcpy(s->pile[s->profondeur], el, n * sizeof *el);
        s->nb_pile[s->profondeur] = n;
        s->profondeur++;
    }
    s->elements = el;
    s->nb_elements = n;
    chaine_ajouter(&s->page.journal, "[ouvrir ");
    chaine_ajouter(&s->page.journal, titre);
    for (size_t k = 0; k < n; k++) {
        if (el[k].sorte == ELEMENT_LIBRE) { chaine_ajouter(&s->page.journal, " ; libre {"); continue; }   /* § 22.9 */
        if (el[k].sorte >= ELEMENT_COTE_A_COTE && el[k].sorte <= ELEMENT_GROUPE) {   /* les blocs de disposition */
            chaine_ajouter(&s->page.journal, el[k].sorte == ELEMENT_COTE_A_COTE ? " ; côte à côte {"
                                             : el[k].sorte == ELEMENT_L_UN_SOUS_L_AUTRE ? " ; l'un sous l'autre {"
                                             : el[k].sorte == ELEMENT_GROUPE ? " ; groupe " : " ; }");
            if (el[k].sorte == ELEMENT_GROUPE) { chaine_ajouter(&s->page.journal, el[k].texte); chaine_ajouter(&s->page.journal, " {"); }
            continue;
        }
        chaine_ajouter(&s->page.journal, el[k].sorte == ELEMENT_LISTE ? " ; liste " : el[k].sorte == ELEMENT_BOUTON ? " ; bouton "
                                         : el[k].sorte == ELEMENT_ZONE ? " ; zone " : el[k].sorte == ELEMENT_IMAGE ? " ; image " : " ; texte ");
        chaine_ajouter(&s->page.journal, el[k].texte);
        if (el[k].sorte == ELEMENT_IMAGE) {   /* § 22.8 : le format et la taille, lus à l'ouverture */
            char *x = grym_formater(" (%s %lu)", el[k].format ? el[k].format : "?", (unsigned long)el[k].taille);
            chaine_ajouter(&s->page.journal, x);
            free(x);
        }
        if (el[k].sorte == ELEMENT_ZONE) {
            chaine_ajouter(&s->page.journal, " (");
            chaine_ajouter(&s->page.journal, el[k].type);
            if (el[k].facultatif) chaine_ajouter(&s->page.journal, ", facultatif");
            if (el[k].plusieurs_lignes) chaine_ajouter(&s->page.journal, ", lignes");
            if (el[k].lecture_seule) chaine_ajouter(&s->page.journal, ", lecture seule");
            for (size_t c = 0; c < el[k].nb_choix; c++) {
                chaine_ajouter(&s->page.journal, c ? "|" : " : ");
                chaine_ajouter(&s->page.journal, el[k].choix[c]);
            }
            chaine_ajouter(&s->page.journal, ")");
        }
        if (el[k].place) {   /* § 22.9 : « @gauche,haut largeur×hauteur » */
            char *x = grym_formater(" @%d,%d %dx%d", el[k].gauche, el[k].haut, el[k].largeur, el[k].hauteur);
            chaine_ajouter(&s->page.journal, x);
            free(x);
        }
        for (size_t c = 0; c < el[k].nb_colonnes; c++) {
            chaine_ajouter(&s->page.journal, c ? ", " : " (");
            chaine_ajouter(&s->page.journal, el[k].colonnes[c]);
            if (c + 1 == el[k].nb_colonnes) chaine_ajouter(&s->page.journal, ")");
        }
    }
    chaine_ajouter(&s->page.journal, "]\n");
}

static void scene_lignes(void *contexte, size_t element, const char *const *cellules, size_t nb_lignes) {
    Scene *s = contexte;
    size_t nc = s->elements[element].nb_colonnes;
    chaine_ajouter(&s->page.journal, "[lignes");
    for (size_t r = 0; r < nb_lignes; r++) {
        chaine_ajouter(&s->page.journal, r ? " / " : " ");
        for (size_t c = 0; c < nc; c++) {
            if (c) chaine_ajouter(&s->page.journal, "|");
            chaine_ajouter(&s->page.journal, cellules[r * nc + c]);
        }
    }
    chaine_ajouter(&s->page.journal, "]\n");
}

static void scene_valeurs(void *contexte, const char *const *v, size_t n) {
    Scene *s = contexte;
    int zones = 0;
    for (size_t k = 0; k < n; k++) zones |= v[k] != NULL;
    if (!zones) return;   /* un écran sans zone : rien à noter */
    chaine_ajouter(&s->page.journal, "[valeurs");
    for (size_t k = 0; k < n; k++)
        if (v[k]) {
            chaine_ajouter(&s->page.journal, " ");
            chaine_ajouter(&s->page.journal, s->elements[k].texte);
            chaine_ajouter(&s->page.journal, "=");
            chaine_ajouter(&s->page.journal, v[k]);
        }
    chaine_ajouter(&s->page.journal, "]\n");
}

static Issue scene_attendre(void *contexte, Chaine *sortie, Evenement *e) {
    Scene *s = contexte;
    static long choisies[16];
    static char texte[128];
    if (sortie->n) {   /* ce que l'événement précédent a affiché */
        chaine_ajouter(&s->page.journal, sortie->d);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    e->sorte = EVENEMENT_FERMETURE;
    if (s->i >= s->n) return ISSUE_REPONDU;
    const char *x = s->evenements[s->i++];
    if (strcmp(x, "INTERROMPRE") == 0) return ISSUE_INTERROMPU;
    /* « … @2 » : la deuxième ligne de la première liste est sélectionnée au moment de l'événement */
    for (size_t k = 0; k < 16; k++) choisies[k] = -1;
    e->choisies = choisies;
    e->nb_choisies = s->nb_elements < 16 ? s->nb_elements : 16;
    const char *arobase = strchr(x, '@');
    if (arobase)
        for (size_t k = 0; k < s->nb_elements && k < 16; k++)
            if (s->elements[k].sorte == ELEMENT_LISTE) { choisies[k] = atol(arobase + 1) - 1; break; }
    char evt[128];
    snprintf(evt, sizeof evt, "%.*s", (int)(arobase ? arobase - x - 1 : (long)strlen(x)), x);
    x = evt;
    if (strncmp(x, "change ", 7) == 0) {   /* « change Pays=Pologne » */
        const char *egal = strchr(x, '=');
        for (size_t k = 0; egal && k < s->nb_elements; k++)
            if (s->elements[k].sorte == ELEMENT_ZONE && strncmp(s->elements[k].texte, x + 7, (size_t)(egal - x - 7)) == 0
                && strlen(s->elements[k].texte) == (size_t)(egal - x - 7)) {
                e->sorte = EVENEMENT_CHANGEMENT;
                e->element = k;
                snprintf(texte, sizeof texte, "%s", egal + 1);
                e->texte = texte;
            }
        return ISSUE_REPONDU;
    }
    if (strncmp(x, "clic ", 5) == 0) {
        for (size_t k = 0; k < s->nb_elements; k++)
            if (s->elements[k].sorte == ELEMENT_BOUTON && strcmp(s->elements[k].texte, x + 5) == 0) {
                e->sorte = EVENEMENT_CLIC;
                e->element = k;
            }
    } else if (strncmp(x, "choix ", 6) == 0) {
        for (size_t k = 0; k < s->nb_elements; k++)
            if (s->elements[k].sorte == ELEMENT_LISTE) {
                e->sorte = EVENEMENT_CHOIX;
                e->element = k;
                e->ligne = (size_t)atoi(x + 6) - 1;
                break;
            }
    }
    return ISSUE_REPONDU;
}

static void scene_erreur(void *contexte, Chaine *sortie, const char *message) {
    Scene *s = contexte;
    (void)sortie;
    chaine_ajouter(&s->page.journal, "[erreur ");
    chaine_ajouter(&s->page.journal, message);
    chaine_ajouter(&s->page.journal, "]\n");
}

static void scene_fermer(void *contexte, Chaine *sortie) {
    Scene *s = contexte;
    if (sortie->n) {
        chaine_ajouter(&s->page.journal, sortie->d);
        sortie->n = 0;
        sortie->d[0] = '\0';
    }
    chaine_ajouter(&s->page.journal, "[fermer]\n");
    if (s->profondeur) {   /* l'écran du dessous reprend */
        free(s->pile[--s->profondeur]);
        s->elements = s->profondeur ? s->pile[s->profondeur - 1] : NULL;
        s->nb_elements = s->profondeur ? s->nb_pile[s->profondeur - 1] : 0;
    }
}

/* Exécute src avec des écrans pilotés par script ; rend le journal suivi de la sortie finale. */
static void scene_apparence(void *contexte, int couleur, const unsigned char *logo, size_t taille, const char *format) {
    Scene *s = contexte;
    (void)logo;
    char *l = grym_formater("[apparence %d%s%s %lu]\n", couleur, format ? " " : "", format ? format : "", (unsigned long)taille);
    chaine_ajouter(&s->page.journal, l);
    free(l);
}

/* Le thème libre (2.13) : chaque jeton, clair puis sombre ; « clair imposé » ; la taille de la police */
static void scene_theme(void *contexte, const char *const *noms, const char *const *clair, const char *const *sombre,
                        size_t n, int clair_impose, const unsigned char *police, size_t taille_police) {
    Scene *s = contexte;
    (void)police;
    chaine_ajouter(&s->page.journal, "[thème");
    for (size_t k = 0; k < n; k++) {
        char *x = grym_formater(" %s=%s%s%s", noms[k], clair[k], sombre[k] ? "/" : "", sombre[k] ? sombre[k] : "");
        chaine_ajouter(&s->page.journal, x);
        free(x);
    }
    char *x = grym_formater("%s police %lu]\n", clair_impose ? " clair-imposé" : "", (unsigned long)taille_police);
    chaine_ajouter(&s->page.journal, x);
    free(x);
}

/* La signature (2.16) : son format et sa taille */
static void scene_signature(void *contexte, const unsigned char *octets, size_t taille, const char *format) {
    Scene *s = contexte;
    (void)octets;
    char *x = grym_formater("[signature %s %lu]\n", format, (unsigned long)taille);
    chaine_ajouter(&s->page.journal, x);
    free(x);
}

/* « actif si », « visible si » (§ 22.6) : journalisés seulement quand un élément est inactif ou caché */
static void scene_etats(void *contexte, const unsigned char *etats, size_t n) {
    Scene *s = contexte;
    int rien = 1;
    for (size_t k = 0; k < n; k++) rien &= !etats[k];
    if (rien) return;
    chaine_ajouter(&s->page.journal, "[états");
    for (size_t k = 0; k < n; k++) {
        if (!etats[k]) continue;
        char *x = grym_formater(" %s%s%s", s->elements && k < s->nb_elements ? s->elements[k].texte : "?",
                                etats[k] & 1 ? " inactif" : "", etats[k] & 2 ? " caché" : "");
        chaine_ajouter(&s->page.journal, x);
        free(x);
    }
    chaine_ajouter(&s->page.journal, "]\n");
}

/* Une image calculée (§ 22.8) : « [image k PNG 69] », ou « [image k absente] ». */
static void scene_image(void *contexte, size_t k, const unsigned char *octets, size_t taille, const char *format) {
    Scene *s = contexte;
    char *x = octets ? grym_formater("[image %lu %s %lu]\n", (unsigned long)k, format ? format : "?", (unsigned long)taille)
                     : grym_formater("[image %lu absente]\n", (unsigned long)k);
    chaine_ajouter(&s->page.journal, x);
    free(x);
}

static char *avec_ecrans(const char *src, const char *const *ev, size_t nev, const char *const *rep, size_t nrep) {
    Scene sc = { { rep, nrep, 0, {0}, 0 }, ev, nev, 0, NULL, 0, {0}, {0}, 0 };
    Interface i = { &sc, page_disponible, page_formulaire, page_effacer, 0, NULL, NULL,
                    scene_ouvrir, scene_lignes, scene_attendre, scene_erreur, scene_fermer, scene_valeurs,
                    scene_apparence, scene_etats, scene_image, NULL, NULL, NULL, NULL, scene_theme, scene_signature,
                    NULL, NULL };
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    machine_interface(m, &i);
    char *r = executer_source(p, m, src, 0);
    machine_detruire(m);
    portee_detruire(p);
    while (sc.profondeur) free(sc.pile[--sc.profondeur]);
    char *j = chaine_rendre(&sc.page.journal);
    char *tout = grym_formater("%s%s", j ? j : "", r);
    free(r);
    free(j);
    return tout;
}

#define ECRANS(src, ev, rep, att) do { \
        total++; \
        char *r_ = avec_ecrans(src, ev, sizeof ev / sizeof *ev, rep, sizeof rep / sizeof *rep); \
        if (strcmp(r_, att) != 0) signaler(__LINE__, src, att, r_); \
        free(r_); \
    } while (0)

static void essais_etats_ecran(void) {   /* § 22.6 */
    const char *src =
        "L'écran de saisie montre :\n"
        "    un nom (texte), facultatif,\n"
        "    une remarque (texte), facultative, sur plusieurs lignes,\n"
        "    un total (nombre), 0 au départ, en lecture seule,\n"
        "    le texte « Nom manquant », visible si le nom de l'écran est absent,\n"
        "    un bouton « Valider », actif si le nom de l'écran est présent,\n"
        "    un bouton « Fermer ».\n"
        "Quand on change le total dans l'écran de saisie :\n    Afficher « total changé ».\n"
        "Quand on clique sur « Valider » dans l'écran de saisie :\n"
        "    Le total de l'écran devient 3.\n    Afficher le nom de l'écran puis le total de l'écran.\n"
        "Quand on clique sur « Fermer » dans l'écran de saisie :\n    Fermer l'écran.\n"
        "Ouvrir l'écran de saisie.\n";
    /* « Valider » inactif tant que le nom manque : le clic ne compte pas ; le total en lecture seule ne change pas */
    const char *ev[] = { "clic Valider", "change Total=5", "change Nom=Ada", "clic Valider", "clic Fermer" };
    const char *rep[] = { "" };
    ECRANS(src, ev, rep,
           "[ouvrir Saisie ; zone Nom (texte, facultatif) ; zone Remarque (texte, facultatif, lignes) ; zone Total (nombre, lecture seule) ; "
           "texte Nom manquant ; bouton Valider ; bouton Fermer]\n"
           "[valeurs Nom= Remarque= Total=0]\n"
           "[états Valider inactif]\n"                 /* le clic sur « Valider » et le total en lecture seule : ignorés */
           "[valeurs Nom=Ada Remarque= Total=0]\n"
           "[états Nom manquant caché]\n"
           "[valeurs Nom=Ada Remarque= Total=3]\n"
           "[états Nom manquant caché]\n"
           "Ada 3\n"
           "[valeurs Nom=Ada Remarque= Total=3]\n"
           "[états Nom manquant caché]\n"
           "[fermer]\n");
    /* groupe titré et zone à valeurs permises (§ 22.7) : la valeur hors liste est refusée, l'écran reste ouvert */
    const char *src3 = "L'écran de saisie montre :\n    le groupe « Statut » :\n"
                       "        un état (« actif », « suspendu » ou « retiré »), « actif » au départ,\n    un bouton « OK ».\n"
                       "Quand on change l'état dans l'écran de saisie :\n    Afficher l'état de l'écran.\n"
                       "Quand on clique sur « OK » dans l'écran de saisie :\n    Fermer l'écran.\nOuvrir l'écran de saisie.\n";
    const char *ev3[] = { "change État=suspendu", "change État=perdu", "clic OK" };
    ECRANS(src3, ev3, rep,
           "[ouvrir Saisie ; groupe Statut { ; zone État (texte : actif|suspendu|retiré) ; } ; bouton OK]\n"
           "[valeurs État=actif]\n[valeurs État=suspendu]\nsuspendu\n"
           "[erreur « perdu » n'est pas une valeur permise de « état » : « actif », « suspendu » ou « retiré ».]\n"
           "[valeurs État=suspendu]\n[valeurs État=suspendu]\n[fermer]\n");
    /* image calculée (§ 22.8) : relue et passée à l'interface avant chaque attente ; une valeur qui n'est pas une
     * image arrête le programme */
    const char *src5 = "L'écran x montre :\n    l'image du fichier « exemples/pixel.png »,\n    un bouton « OK ».\n"
                       "Quand on clique sur « OK » dans l'écran x :\n    Afficher 1.\nOuvrir l'écran x.\n";
    const char *ev5[] = { "clic OK", "fermer" };
    ECRANS(src5, ev5, rep, "[ouvrir X ; image  (? 0) ; bouton OK]\n[image 0 PNG 69]\n[image 0 PNG 69]\n1\n[fermer]\n");
    /* position libre (§ 22.9) : la machine passe la place et la taille de chaque élément du bloc */
    const char *src4 = "L'écran de saisie montre :\n    en position libre :\n"
                       "        un nom (texte), à 96 de la gauche et 16 du haut, large de 240,\n"
                       "        un bouton « OK », à 96 de la gauche et 72 du haut, haut de 30,\n    le texte « fin ».\n"
                       "Quand on clique sur « OK » dans l'écran de saisie :\n    Fermer l'écran.\nOuvrir l'écran de saisie.\n";
    const char *ev4[] = { "clic OK" };
    ECRANS(src4, ev4, rep,
           "[ouvrir Saisie ; libre { ; zone Nom (texte) @96,16 240x0 ; bouton OK @96,72 0x30 ; } ; texte fin]\n"
           "[valeurs Nom=]\n[valeurs Nom=]\n[fermer]\n");
    /* une zone obligatoire vide (§ 22.6) : absente pour une condition ; dans un événement, un message pour l'utilisateur,
       l'écran reste ouvert */
    const char *src2 = "L'écran x montre :\n    un nom (texte),\n    le texte « Nom ? », visible si le nom de l'écran est absent,\n"
                       "    un bouton « OK »,\n    un bouton « Fermer ».\n"
                       "Quand on clique sur « OK » dans l'écran x :\n    Afficher le nom de l'écran.\n"
                       "Quand on clique sur « Fermer » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
    const char *ev2[] = { "clic OK", "change Nom=Ada", "clic OK", "clic Fermer" };
    ECRANS(src2, ev2, rep,
           "[ouvrir X ; zone Nom (texte) ; texte Nom ? ; bouton OK ; bouton Fermer]\n[valeurs Nom=]\n"
           "[erreur Remplis « Nom » : cette zone est obligatoire.]\n[valeurs Nom=]\n"
           "[valeurs Nom=Ada]\n[états Nom ? caché]\n[valeurs Nom=Ada]\n[états Nom ? caché]\nAda\n"
           "[valeurs Nom=Ada]\n[états Nom ? caché]\n[fermer]\n");
}

/* l'image (§ 22.8) : lue à l'ouverture, depuis le dossier du programme ; disparue depuis l'analyse, l'écran ne s'ouvre pas */
static void essai_image(void) {
    const char *rep[] = { "" };
    const char *src = "L'écran x montre :\n    l'image « exemples/pixel.png »,\n    un bouton « OK ».\n"
                      "Quand on clique sur « OK » dans l'écran x :\n    Fermer l'écran.\nOuvrir l'écran x.\n";
    const char *ev[] = { "clic OK" };
    ECRANS(src, ev, rep, "[ouvrir X ; image exemples/pixel.png (PNG 69) ; bouton OK]\n[fermer]\n");
}

/* une case à cocher obligatoire jamais touchée vaut faux : c'est ce qu'elle montre (§ 22.6) */
static void essai_case_vide(void) {
    const char *rep[] = { "" };
    const char *src = "L'écran x montre :\n    un accord (vrai ou faux),\n    un bouton « OK ».\n"
                      "Quand on clique sur « OK » dans l'écran x :\n    Afficher l'accord de l'écran.\n    Fermer l'écran.\nOuvrir l'écran x.\n";
    const char *ev[] = { "clic OK" };
    ECRANS(src, ev, rep, "[ouvrir X ; zone Accord (vrai ou faux) ; bouton OK]\n[valeurs Accord=non]\n[valeurs Accord=non]\nfaux\n[fermer]\n");
}

static void essais_ecrans(void) {
    const char *compositeurs =
        "Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative, une photo (image), facultative.\n"
        "Pour remplir :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Liszt ».\n    Conserver c.\n"
        "    Le d vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n        La naissance vaut 31.03.1685.\n"
        "    Conserver d.\n"
        "Remplir.\n"
        "L'écran des compositeurs montre :\n    le texte « Nos compositeurs »,\n"
        "    la liste des compositeurs conservés, par nom,\n    un bouton « Nouveau »,\n    un bouton « Échouer »,\n"
        "    un bouton « Fermer ».\n"
        "Quand on choisit un compositeur dans l'écran des compositeurs :\n    Afficher « choisi » puis nom du compositeur.\n"
        "Quand on clique sur « Nouveau » dans l'écran des compositeurs :\n    Le c vaut un nouveau compositeur saisi.\n"
        "    Conserver c.\n"
        "Quand on clique sur « Échouer » dans l'écran des compositeurs :\n    Le e vaut un nouveau compositeur :\n"
        "        Le nom vaut « Fantôme ».\n    Conserver e.\n    Afficher 1 ÷ 0.\n"
        "Quand on clique sur « Fermer » dans l'écran des compositeurs :\n    Fermer l'écran.\n"
        "Ouvrir l'écran des compositeurs.\n"
        "Afficher le nombre de compositeurs conservés.\n";
    /* Ouvrir, choisir la première ligne (Bach : tri par nom), ajouter Chopin, un clic qui échoue, fermer.
     * Les colonnes : les champs simples, sans l'image ; un champ absent reste vide ; la liste suit l'ajout ;
     * l'événement raté n'a rien conservé (« Fantôme » n'apparaît pas), et l'écran est resté ouvert. */
    {
        const char *ev[] = { "choix 1", "clic Nouveau", "clic Échouer", "clic Fermer" };
        const char *rep[] = { "Chopin", "", "" };
        ECRANS(compositeurs, ev, rep,
               "[ouvrir Compositeurs ; texte Nos compositeurs ; liste compositeur (Nom, Naissance) ; bouton Nouveau ; "
               "bouton Échouer ; bouton Fermer]\n"
               "[lignes Bach|31.03.1685 / Liszt|]\n"
               "[lignes Bach|31.03.1685 / Liszt|]\n"
               "choisi Bach\n"
               "page Nom Naissance Photo[lignes Bach|31.03.1685 / Chopin| / Liszt|]\n"
               "[erreur Division par zéro.]\n"
               "[lignes Bach|31.03.1685 / Chopin| / Liszt|]\n"
               "[lignes Bach|31.03.1685 / Chopin| / Liszt|]\n"
               "[fermer]\n"
               "3");
    }
    /* La croix de la fenêtre ferme l'écran ; le programme continue */
    {
        const char *ev[] = { "fermer" };
        const char *rep[] = { "" };
        ECRANS(compositeurs, ev, rep,
               "[ouvrir Compositeurs ; texte Nos compositeurs ; liste compositeur (Nom, Naissance) ; bouton Nouveau ; "
               "bouton Échouer ; bouton Fermer]\n[lignes Bach|31.03.1685 / Liszt|]\n[fermer]\n2");
    }
    /* Interrompre pendant l'attente : ce qui a été validé reste, le reste est annulé */
    {
        const char *ev[] = { "clic Nouveau", "INTERROMPRE" };
        const char *rep[] = { "Chopin", "", "" };
        char *r = avec_ecrans(compositeurs, ev, 2, rep, 3);
        total++;
        if (!strstr(r, "Interrompu (Ctrl+C).")) signaler(__LINE__, compositeurs, "…Interrompu…", r);
        free(r);
    }
    /* L'apparence (§ 22.5) : la couleur et le logo passent à l'interface avant le premier écran ; sans logo, NULL */
    {
        const char *src = "Les écrans ont la couleur violette et le logo « exemples/pixel.png ».\n"
                          "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran d'accueil.\n";
        const char *ev[] = { "clic Fermer" };
        const char *rep[] = { "" };
        char *r = avec_ecrans(src, ev, 1, rep, 1);
        total++;
        if (strncmp(r, "[apparence 3 PNG ", 17) != 0 || !strstr(r, "]\n[ouvrir")) signaler(__LINE__, src, "[apparence 3 PNG …]\n[ouvrir …", r);
        free(r);
        const char *src2 = "Les écrans ont la couleur grise.\n"
                           "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                           "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                           "Ouvrir l'écran d'accueil.\n";
        r = avec_ecrans(src2, ev, 1, rep, 1);
        total++;
        if (strncmp(r, "[apparence 4 0]\n[ouvrir", 23) != 0) signaler(__LINE__, src2, "[apparence 4 0]\n[ouvrir …", r);
        free(r);
        /* 2.13 : le logo SVG, puis les jetons dérivés, le clair imposé par le fond, la taille de la police */
        const char *src3 = "Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police "
                           "« vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf » et le logo « vendor/lucide/check.svg ».\n"
                           "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                           "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                           "Ouvrir l'écran d'accueil.\n";
        r = avec_ecrans(src3, ev, 1, rep, 1);
        total++;
        if (strncmp(r, "[apparence -1 SVG ", 18) != 0
            || !strstr(r, "[thème fond=#E8E2D6 surface=#F5F2ED surface-alt=#DDD6CB survol=#EFEBE2 separateur=#D1CBC0 "
                          "fond-desactive=#E0DACF danger-doux=#E4D3C8 accent=#46433B accent-survol=#3E3B34 "
                          "accent-enfonce=#35332D sur-accent=#FFFFFF accent-doux=#CEC9BD clair-imposé police ")
            || !strstr(r, "]\n[ouvrir"))
            signaler(__LINE__, src3, "[apparence -1 SVG …]\n[thème fond=#E8E2D6 … clair-imposé police …]\n[ouvrir …", r);
        free(r);
        /* 2.16 : la signature, après le thème */
        const char *src5 = "Les écrans ont la couleur « #46433B », le logo « vendor/lucide/check.svg » et la signature « exemples/pixel.png ».\n"
                           "L'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                           "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\nOuvrir l'écran d'accueil.\n";
        r = avec_ecrans(src5, ev, 1, rep, 1);
        total++;
        if (!strstr(r, "police 0]\n[signature PNG ") || !strstr(r, "]\n[ouvrir")) signaler(__LINE__, src5, "[thème …]\n[signature PNG …]\n[ouvrir …", r);
        free(r);
        /* sans fond : le mode sombre reçoit la couleur éclaircie */
        const char *src4 = "Les écrans ont la couleur « #46433B ».\nL'écran d'accueil montre :\n    un bouton « Fermer ».\n"
                           "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\nOuvrir l'écran d'accueil.\n";
        r = avec_ecrans(src4, ev, 1, rep, 1);
        total++;
        if (!strstr(r, "[thème accent=#46433B/#878580 accent-survol=#3E3B34/#999793 accent-enfonce=#35332D/#ABAAA6 "
                       "sur-accent=#FFFFFF/#1B1613 accent-doux=#D9D3CB/#24201C police 0]"))
            signaler(__LINE__, src4, "[thème accent=#46433B/#878580 …]", r);
        free(r);
    }
    /* Le titre donné ; un nom en « d' » */
    {
        const char *src = "L'écran d'accueil montre :\n    le texte « Bonjour »,\n    un bouton « Fermer ».\n"
                          "Quand on clique sur « Fermer » dans l'écran d'accueil :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran d'accueil.\n"
                          "L'écran de fin, « Au revoir », montre :\n    le texte « Adieu »,\n    un bouton « OK ».\n"
                          "Quand on clique sur « OK » dans l'écran de fin :\n    Fermer l'écran.\n"
                          "Ouvrir l'écran de fin.\n";
        const char *ev[] = { "clic Fermer", "clic OK" };
        const char *rep[] = { "" };
        ECRANS(src, ev, rep, "[ouvrir Accueil ; texte Bonjour ; bouton Fermer]\n[fermer]\n"
                             "[ouvrir Au revoir ; texte Adieu ; bouton OK]\n[fermer]\n");
    }
    /* A3-b : zones de saisie, « de l'écran », ligne choisie, changement, ouverture, fermeture */
    {
        const char *src =
            "Un compositeur, conservé, a : un nom (texte), unique, un pays (texte).\n"
            "Pour remplir un nom et un pays :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut nom.\n"
            "        Le pays vaut pays.\n    Conserver c.\n"
            "Remplir « Bach » et « Allemagne ».\nRemplir « Chopin » et « Pologne ».\nRemplir « Sweelinck » et « Pays-Bas ».\n"
            "Remplir « Schumann » et « Allemagne ».\n"
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
        const char *ev[] = { "clic Supprimer @2", "change Pays=Pologne", "change Âge=abc", "change Âge=12,5",
                             "clic Supprimer", "clic Fermer" };
        const char *rep[] = { "" };
        ECRANS(src, ev, rep,
               "[ouvrir Recherche ; zone Pays (texte) ; zone Âge (nombre, facultatif) ; liste compositeur (Nom, Pays) ; "
               "bouton Supprimer ; bouton Fermer]\n"
               "[lignes Bach|Allemagne / Schumann|Allemagne]\n"
               "[valeurs Pays=Allemagne Âge=]\n"
               "[lignes Bach|Allemagne]\n"
               "[valeurs Pays=Allemagne Âge=]\n"
               "[lignes Chopin|Pologne]\n"
               "[valeurs Pays=Pologne Âge=]\n"
               "pays Pologne\n"
               "[erreur « abc » n'est pas un nombre.]\n"
               "[lignes Chopin|Pologne]\n"
               "[valeurs Pays=Pologne Âge=]\n"
               "[lignes Chopin|Pologne]\n"
               "[valeurs Pays=Pologne Âge=12,5]\n"
               "[lignes Chopin|Pologne]\n"
               "[valeurs Pays=Pologne Âge=12,5]\n"
               "[lignes Chopin|Pologne]\n"
               "[valeurs Pays=Pologne Âge=12,5]\n"
               "fermé 12,5\n[fermer]\n"
               "3");
    }
    PROG("L'écran de recherche montre :\n    un pays (texte),\n    un bouton « OK ».\n"
         "Quand on clique sur « OK » dans l'écran de recherche :\n    Fermer l'écran.\n"
         "Quand on change la ville dans l'écran de recherche :\n    Afficher 1.\n",
         "~L'écran « de recherche » n'a pas de zone « ville »");
    PROG("L'écran de recherche montre :\n    un pays (texte),\n    un bouton « OK ».\n"
         "Quand on clique sur « OK » dans l'écran de recherche :\n    Fermer l'écran.\n"
         "Afficher le pays de l'écran.\n", "~« l'écran » ne se lit que dans un événement");
    PROG("L'écran de recherche montre :\n    une photo (image).\n", "");   /* une zone d'image est acceptée */
    /* les mots de passe (§ 16.1) : SHA-256 et PBKDF2-HMAC-SHA256, contre les valeurs de Python (hashlib) */
    {
        static const char *const textes[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                                             "Le cœur a ses raisons que la raison ne connaît point, et ce texte dépasse soixante-quatre octets."};
        static const char *const attendus[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "4bad8c467ed1b8023657a316d1874d1021d65566463784da58465725351fba99"};
        for (int i = 0; i < 4; i++) {
            unsigned char o[32];
            char h[65];
            sha256((const unsigned char *)textes[i], strlen(textes[i]), o);
            for (int j = 0; j < 32; j++) snprintf(h + 2 * j, 3, "%02x", o[j]);
            if (strcmp(h, attendus[i]) != 0) { echecs++; printf("ÉCHEC sha256 %d : %s\n", i, h); }
            total++;
        }
        struct { const char *p, *s; unsigned long c; size_t n; const char *attendu; } k[] = {
            {"password", "salt", 1, 32, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"}, {"password", "salt", 2, 32, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"},
            {"password", "salt", 4096, 32, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"}, {"passwd", "salt", 1, 64, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"},
            {"élodie-2026", "sel-aléatoire", 1000, 40, "1a09b665eca2a23b0b145d763360630a21df65925980b3e6307eb3c162685c37303cd523109a1e46"},
            {"un mot de passe vraiment très long, plus long que les soixante-quatre octets du bloc de HMAC", "s", 3, 32,
             "417d07b8994f59f818365e26bb380b3e2b5c2c3df93961946012c79d126eed7c"}};
        for (int i = 0; i < 6; i++) {
            unsigned char o[64];
            char h[129];
            pbkdf2_sha256((const unsigned char *)k[i].p, strlen(k[i].p), (const unsigned char *)k[i].s, strlen(k[i].s), k[i].c, o, k[i].n);
            for (size_t j = 0; j < k[i].n; j++) snprintf(h + 2 * j, 3, "%02x", o[j]);
            if (strcmp(h, k[i].attendu) != 0) { echecs++; printf("ÉCHEC pbkdf2 %d : %s\n", i, h); }
            total++;
        }
        char *e1 = empreinte_creer("s3cret"), *e2 = empreinte_creer("s3cret");
        total += 4;
        if (!e1 || strncmp(e1, "pbkdf2-sha256$600000$", 21) != 0) { echecs++; printf("ÉCHEC empreinte : forme\n"); }
        if (!e1 || !e2 || strcmp(e1, e2) == 0) { echecs++; printf("ÉCHEC empreinte : le sel ne change pas\n"); }
        if (!empreinte_verifier("s3cret", e1) || empreinte_verifier("s3creT", e1)) { echecs++; printf("ÉCHEC empreinte : vérification\n"); }
        if (empreinte_verifier("s3cret", "pbkdf2-sha256$600000$zz$00")) { echecs++; printf("ÉCHEC empreinte : abîmée acceptée\n"); }
        free(e1);
        free(e2);
    }
    PROG("Un membre, conservé, a :\n    un nom (texte), unique,\n    un code (mot de passe).\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Anne ».\n    Le code vaut « s3cret ».\nConserver m.\n"
         "Pour chaque membre conservé :\n    Si « s3cret » correspond au code du membre, afficher « bon ».\n"
         "    Si « autre » correspond au code du membre, afficher « mauvais ».\n"
         "Afficher « fin ».\n", "~bon\nfin");   /* le bon passe, le mauvais non */
    PROG("Un membre, conservé, a :\n    un code (mot de passe).\nLe m vaut un nouveau membre :\n    Le code vaut « s3cret ».\n"
         "Afficher le code de m.\n", "~« code » est un mot de passe : il ne se lit pas, il se vérifie");
    /* courriel, téléphone, adresse web (§ 16.1) : des textes rangés en base, cherchés comme tels ; leur forme vérifiée */
    PROG("Un contact, conservé, a :\n    un courriel (courriel), unique,\n    un portable (téléphone), facultatif,\n"
         "    un site (adresse web), facultatif.\n"
         "Le c vaut un nouveau contact :\n    Le courriel vaut « anne@tilleul.ch ».\n    Le portable vaut « +41 26 912 34 56 ».\n"
         "    Le site vaut « https://tilleul.ch ».\nConserver c.\n"
         "Pour chaque contact conservé dont le courriel contient « tilleul » :\n"
         "    Afficher le courriel du contact puis « / » puis le portable du contact.\n", "~anne@tilleul.ch / +41 26 912 34 56");
    PROG("Un contact, conservé, a :\n    un courriel (courriel).\nLe c vaut un nouveau contact :\n"
         "    Le courriel vaut « anne » suivi de « @ ».\nConserver c.\n", "~« anne@ » n'est pas un courriel (nom@domaine.ch).");
    PROG("Un contact, conservé, a :\n    un portable (téléphone).\nLe c vaut un nouveau contact :\n"
         "    Le portable vaut « 12 ».\nConserver c.\n", "~« 12 » n'est pas un numéro de téléphone");
    PROG("Un contact, conservé, a :\n    un site (adresse web).\nLe c vaut un nouveau contact :\n"
         "    Le site vaut « tilleul.ch ».\nConserver c.\n", "~« tilleul.ch » n'est pas une adresse web (https://exemple.ch).");
    /* La fiche montre les champs « plusieurs » (§ 16.13, § 22.3) : un bouton par élément, vers sa fiche ; vide : « aucun » */
    {
        const char *src3 =
            "Un genre, conservé, a : un nom (texte), unique.\n"
            "Une œuvre, conservée, a : un titre (texte), unique, des genres (genre), des solistes (genre).\n"
            "Pour classer un nom :\n    Le g vaut un nouveau genre :\n        Le nom vaut nom.\n    Conserver g.\n"
            "Classer « baroque ».\nClasser « sacré ».\n"
            "L'o vaut une nouvelle œuvre :\n    Le titre vaut « Messe ».\nConserver o.\n"
            "Les genres de o gagnent le genre conservé dont le nom est « baroque ».\n"
            "Les genres de o gagnent le genre conservé dont le nom est « sacré ».\n"
            "Ouvrir la fiche de o.\nAfficher « fin ».\n";
        const char *ev[] = { "clic sacré", "clic Fermer", "clic Fermer" };
        const char *rep[] = { "" };
        char *r = avec_ecrans(src3, ev, 3, rep, 1);
        const char *att = "[ouvrir Messe ; texte Titre : Messe ; texte Genres : ; côte à côte { ; bouton baroque ; bouton sacré ; } ; "
                          "texte Solistes : aucun ; bouton Modifier ; bouton Fermer]\n"
                          "[ouvrir sacré ; texte Nom : sacré ; bouton Modifier ; bouton Fermer]\n[fermer]\n[fermer]\nfin";
        total++;
        if (strcmp(r, att) != 0) signaler(__LINE__, src3, att, r);
        free(r);
    }
    /* A3-c : la fiche déduite (champs, lien vers la fiche du lien, Modifier) et les écrans empilés */
    {
        const char *src2 =
            "Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative.\n"
            "Une œuvre, conservée, a : un titre (texte), unique, un compositeur (compositeur).\n"
            "Pour remplir :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n"
            "        La naissance vaut 31.03.1685.\n    Conserver c.\n"
            "    Le o vaut une nouvelle œuvre :\n        Le titre vaut « Messe ».\n        Le compositeur vaut c.\n"
            "    Conserver o.\n"
            "Remplir.\n"
            "L'écran des détails montre :\n    le texte « Empilé. »,\n    un bouton « Retour ».\n"
            "Quand on clique sur « Retour » dans l'écran des détails :\n    Fermer l'écran.\n"
            "L'écran des œuvres montre :\n    la liste des œuvres conservées, par titre,\n    un bouton « Détails »,\n"
            "    un bouton « Fermer ».\n"
            "Quand on choisit une œuvre dans l'écran des œuvres :\n    Ouvrir la fiche de l'œuvre.\n"
            "Quand on clique sur « Détails » dans l'écran des œuvres :\n    Ouvrir l'écran des détails.\n"
            "    Afficher « revenu ».\n"
            "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
            "Ouvrir l'écran des œuvres.\nAfficher le titre de l'œuvre conservée dont le titre ≠ « x ».\n";
        /* la fiche de la Messe ; son lien vers la fiche de Bach, par-dessus ; Fermer ; Modifier (« Requiem ») ;
           Fermer ; l'écran des détails, empilé ; Retour ; Fermer */
        const char *ev[] = { "choix 1", "clic Compositeur : Bach", "clic Fermer", "clic Modifier", "clic Fermer",
                             "clic Détails", "clic Retour", "clic Fermer" };
        const char *rep[] = { "Requiem", "" };
        ECRANS(src2, ev, rep,
               "[ouvrir Œuvres ; liste œuvre (Titre, Compositeur) ; bouton Détails ; bouton Fermer]\n"
               "[lignes Messe|Bach]\n"
               "[ouvrir Messe ; texte Titre : Messe ; bouton Compositeur : Bach ; bouton Modifier ; bouton Fermer]\n"
               "[ouvrir Bach ; texte Nom : Bach ; texte Naissance : 31.03.1685 ; texte Œuvres ; liste œuvre (Titre) ; "
               "bouton Nouvelle œuvre ; bouton Modifier ; bouton Fermer]\n"
               "[lignes Messe]\n"
               "[fermer]\n"
               "page Titre=Messe Compositeur=Bach[fermer]\n"
               "[ouvrir Requiem ; texte Titre : Requiem ; bouton Compositeur : Bach ; bouton Modifier ; bouton Fermer]\n"
               "[fermer]\n"
               "[lignes Requiem|Bach]\n"
               "[ouvrir Détails ; texte Empilé. ; bouton Retour]\n"
               "[fermer]\n"
               "[lignes Requiem|Bach]\n"
               "revenu\n"
               "[lignes Requiem|Bach]\n"
               "[fermer]\n"
               "Requiem");
    }
    /* § 22.6 : un événement qui teste lui-même la présence d'une zone obligatoire s'en charge : elle n'est pas exigée
       d'emblée ; la lire vide sans test l'arrête toujours (« OK » sans ville) */
    {
        const char *src =
            "L'écran de saisie montre :\n    un nom (texte),\n    une ville (texte),\n    un bouton « OK ».\n"
            "Quand on change le nom dans l'écran de saisie :\n"
            "    Si le nom de l'écran est présent et la ville de l'écran est présente, afficher le nom de l'écran puis la ville de l'écran.\n"
            "    Afficher « nom changé ».\n"
            "Quand on clique sur « OK » dans l'écran de saisie :\n    Afficher la ville de l'écran.\n    Fermer l'écran.\n"
            "Ouvrir l'écran de saisie.\n";
        const char *ev[] = { "change Nom=Anne", "clic OK", "change Ville=Bulle", "change Nom=Bea", "clic OK" };
        const char *rep[] = { "" };
        ECRANS(src, ev, rep,
               "[ouvrir Saisie ; zone Nom (texte) ; zone Ville (texte) ; bouton OK]\n[valeurs Nom= Ville=]\n"
               "[valeurs Nom=Anne Ville=]\nnom changé\n"
               "[erreur Remplis « Ville » : cette zone est obligatoire.]\n[valeurs Nom=Anne Ville=]\n"
               "[valeurs Nom=Anne Ville=Bulle]\n[valeurs Nom=Bea Ville=Bulle]\nBea Bulle\nnom changé\n"
               "[valeurs Nom=Bea Ville=Bulle]\nBulle\n[fermer]\n");
    }
    /* § 22.3 : les tableaux d'une fiche, un par lien qui désigne l'objet ; tri par date, la plus récente en tête ;
       total des colonnes (nombre) ; une ligne vers sa fiche, et retour ; la ligne « Total » ne mène nulle part ;
       « Nouvelle séance », le client déjà rempli ; « hors des fiches » ; deux liens : deux tableaux nommés */
    {
        const char *src =
            "Un soin, conservé, a : un nom (texte), unique.\n"
            "Un client, conservé, a : un nom (texte), unique.\n"
            "Une séance, conservée, a : un client (client), un soin (soin), une date (date), un prix (nombre), "
            "un état (« payée » en vert ou « due » en rouge).\n"
            "Une note, conservée, hors des fiches, a : un client (client), un texte (texte).\n"
            "Un parrainage, conservé, a : un parrain (client), un filleul (client), une année (nombre entier).\n"
            "Pour remplir :\n"
            "    Le s vaut un nouveau soin :\n        Le nom vaut « EPI ».\n    Conserver s.\n"
            "    Le c vaut un nouveau client :\n        Le nom vaut « Anne ».\n    Conserver c.\n"
            "    La a vaut une nouvelle séance :\n        Le client vaut c.\n        Le soin vaut s.\n"
            "        La date vaut 01.09.2026.\n        Le prix vaut 130.\n        L'état vaut « payée ».\n    Conserver a.\n"
            "    La b vaut une nouvelle séance :\n        Le client vaut c.\n        Le soin vaut s.\n"
            "        La date vaut 15.09.2026.\n        Le prix vaut 70.\n        L'état vaut « due ».\n    Conserver b.\n"
            "    La n vaut une nouvelle note :\n        Le client vaut c.\n        Le texte vaut « confidentiel ».\n"
            "    Conserver n.\n"
            "Remplir.\n"
            "Le c vaut le client conservé dont le nom est « Anne ».\n"
            "Ouvrir la fiche de c.\n"
            "Afficher le nombre de séances conservées.\n";
        const char *ev[] = { "choix 1", "clic Fermer", "choix 3", "clic Nouvelle séance", "clic Fermer" };
        const char *rep[] = { "EPI", "20.09.2026", "70", "payée", "" };
        ECRANS(src, ev, rep,
               "[ouvrir Anne ; texte Nom : Anne ; texte Séances ; liste séance (Soin, Date, Prix, État) ; "
               "bouton Nouvelle séance ; texte Parrainages (parrain) ; liste parrainage (Filleul, Année) ; "
               "bouton Nouveau parrainage (parrain) ; texte Parrainages (filleul) ; liste parrainage (Parrain, Année) ; "
               "bouton Nouveau parrainage (filleul) ; bouton Modifier ; bouton Fermer]\n"
               "[lignes EPI|15.09.2026|70|\x1drdue / EPI|01.09.2026|130|\x1dvpayée / \x1cTotal||200|]\n"
               "[lignes]\n[lignes]\n"
               "[ouvrir une séance ; bouton Client : Anne ; bouton Soin : EPI ; texte Date : 15.09.2026 ; "
               "texte Prix : 70 ; texte État : due ; bouton Modifier ; bouton Fermer]\n"
               "[fermer]\n"
               "[lignes EPI|15.09.2026|70|\x1drdue / EPI|01.09.2026|130|\x1dvpayée / \x1cTotal||200|]\n"
               "[lignes]\n[lignes]\n"
               "page Soin Date Prix État (« payée » ou « due »)"
               "[lignes EPI|20.09.2026|70|\x1dvpayée / EPI|15.09.2026|70|\x1drdue / EPI|01.09.2026|130|\x1dvpayée / "
               "\x1cTotal||270|]\n"
               "[lignes]\n[lignes]\n"
               "[fermer]\n"
               "3");
    }
    PROG("Un soin a : un tarif de suivi.\nLe s vaut un nouveau soin :\n    Le tarif de suivi vaut 70.\n"
         "Afficher « suivi » suivi de « : » puis le tarif de suivi du s.\n", "suivi: 70");
    /* 2.15 : une tuile ; sa description et son image ne vont que sur une tuile ; l'image existe */
    PROG("L'écran x montre :\n    un bouton « B », avec la description « d ».\nOuvrir l'écran x.\n", "~Une description ou une image va sur une tuile");
    PROG("L'écran x montre :\n    un bouton « B », en tuile, avec l'image « absente.png ».\nOuvrir l'écran x.\n", "~Fichier « absente.png » introuvable");
    PROG("L'écran x montre :\n    un bouton « B », en tuile, en tuile.\nOuvrir l'écran x.\n", "~« en tuile » est déjà dit.");
    /* 2.18 : un groupe en couleur inversée ; l'accord */
    PROG("L'écran x montre :\n    le groupe « G », en couleur inversé :\n        le texte « t ».\nOuvrir l'écran x.\n", "~Accord : « inversée » (la couleur).");
    /* 2.17 : à côté du titre ; pas une tuile */
    PROG("L'écran x montre :\n    un bouton « B », en tuile, à côté du titre.\nOuvrir l'écran x.\n", "~Une tuile ne se met pas à côté du titre.");
    /* 2.14 : un bouton principal ; seul un bouton l'est */
    PROG("L'écran x montre :\n    le texte « t », principal.\nOuvrir l'écran x.\n", "~Seul un bouton est principal");
    PROG("L'écran x montre :\n    un bouton « B », principale.\nOuvrir l'écran x.\n", "~Accord : « principal » (le bouton).");
    PROG("Un point, hors des fiches, a : un x.\n",
         "ERREUR 1:11 Seule une entité se tient hors des fiches : « Une note, conservée, hors des fiches, a : ».");
    PROG("Une personne, conservée, a : un nom (texte).\nUne cliente, conservée, hors des fiches, est une personne.\n"
         "Une cliente a : un code (texte).\n", "");
    PROG("Un point a : un x.\nLe p vaut un nouveau point.\nOuvrir la fiche de p.\n",
         "ERREUR 0:0 Ce programme ouvre des écrans ou des documents : lance-le dans une fenêtre, avec grym-atelier.");
    /* A3-d : colonnes choisies (le champ d'un lien compris), blocs de disposition, titre donné */
    {
        const char *src =
            "Un compositeur, conservé, a : un nom (texte), unique, une date de naissance (date), facultative.\n"
            "Une œuvre, conservée, a : un titre (texte), unique, un compositeur (compositeur), une année (nombre entier), facultative.\n"
            "Pour remplir :\n    Le c vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n"
            "        La date de naissance vaut 31.03.1685.\n    Conserver c.\n"
            "    Le o vaut une nouvelle œuvre :\n        Le titre vaut « Messe ».\n        Le compositeur vaut c.\n"
            "        L'année vaut 1749.\n    Conserver o.\n"
            "Remplir.\n"
            "L'écran des œuvres, « Catalogue », montre :\n    côte à côte :\n"
            "        la liste des compositeurs conservés, par nom,\n        l'un sous l'autre :\n"
            "            la liste des œuvres conservées, par titre, avec le titre, la date de naissance du compositeur et l'année,\n"
            "            le texte « Double-clic : la fiche »,\n    un bouton « Fermer ».\n"
            "Quand on choisit une œuvre dans l'écran des œuvres :\n    Afficher titre de l'œuvre.\n"
            "Quand on clique sur « Fermer » dans l'écran des œuvres :\n    Fermer l'écran.\n"
            "Ouvrir l'écran des œuvres.\n";
        const char *ev[] = { "choix 9", "clic Fermer" };   /* une ligne qui n'existe pas : rien, l'écran reste ouvert */
        const char *rep[] = { "" };
        ECRANS(src, ev, rep,
               "[ouvrir Catalogue ; côte à côte { ; liste compositeur (Nom, Date de naissance) ; l'un sous l'autre { ; "
               "liste œuvre (Titre, Date de naissance du compositeur, Année) ; texte Double-clic : la fiche ; } ; } ; "
               "bouton Fermer]\n"
               "[lignes Bach|31.03.1685]\n[lignes Messe|31.03.1685|1749]\n"
               "[lignes Bach|31.03.1685]\n[lignes Messe|31.03.1685|1749]\n"
               "[lignes Bach|31.03.1685]\n[lignes Messe|31.03.1685|1749]\n[fermer]\n");
    }
    PROG("Une œuvre, conservée, a : un titre (texte), unique.\n"
         "L'écran des œuvres montre :\n    la liste des œuvres conservées, avec le prix,\n    un bouton « OK ».\n",
         "~Colonne « le prix » : un champ de « œuvre »");
    /* Les erreurs d'analyse */
    PROG("L'écran d'accueil montre :\n    un bouton « OK ».\nAfficher 1.\n",
         "ERREUR 2:5 Le bouton « OK » de l'écran « d'accueil » n'a pas de « Quand on clique sur « OK » dans l'écran d'accueil : ».");
    PROG("L'écran d'accueil montre :\n    un bouton « OK ».\n"
         "Quand on clique sur « Ok » dans l'écran d'accueil :\n    Afficher 1.\n",
         "ERREUR 3:21 L'écran « d'accueil » n'a pas de bouton « Ok » : « OK ».");
    PROG("Ouvrir l'écran d'accueil.\n", "~Écran « d'accueil » inconnu");
    PROG("Fermer l'écran.\n", "ERREUR 1:1 « Fermer l'écran. » ne s'emploie que dans un événement « Quand on … ».");
    PROG("L'écran d'accueil montre :\n    le texte « a »,\n    une image.\n", "~Type attendu entre parenthèses");
    PROG("L'écran d'accueil montre :\n    le texte « a »,\n    la photo.\n", "~Élément d'écran attendu");
    PROG("L'écran d'accueil montre :\n    un bouton « OK »,\n    un bouton « OK ».\n", "~Le bouton « OK » existe déjà");
    PROG("L'écran d'accueil montre :\n    le texte « a ».\nL'écran d'accueil montre :\n    le texte « b ».\n", "~existe déjà");
    PROG("L'écran d'accueil montre :\n    le texte « a ».\nEssayer :\n    Ouvrir l'écran d'accueil.\n"
         "En cas d'échec :\n    Afficher 1.\n", "~ne s'ouvre pas dans « Essayer »");
    PROG("Pour ouvrir un x :\n    Afficher x.\n", "~« ouvrir » commence une construction du langage");
    /* en console : refusé avant toute exécution, rien n'est affiché */
    PROG("Afficher 1.\nL'écran d'accueil montre :\n    le texte « a ».\nOuvrir l'écran d'accueil.\n",
         "ERREUR 0:0 Ce programme ouvre des écrans ou des documents : lance-le dans une fenêtre, avec grym-atelier.");
}

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- Décimal exact (§ 3.2) --- */
    CALC("1,1 + 2,2", "3,3");
    CALC("0,1 + 0,2", "0,3");
    CALC("12,50 × 3", "37,50");
    CALC("100 − 100,00", "0,00");
    CALC("0,5 × 0,5", "0,25");
    CALC("1 − 3", "−2");
    CALC("−0", "0");
    CALC("1'234'567,891", "1'234'567,891");
    CALC("−1'000", "−1000");   /* un entier de quatre chiffres ne se groupe pas (§ 4.1) */
    CALC("999 + 1", "1000");
    CALC("9999 + 1", "10'000");   /* à partir de cinq chiffres, il se groupe */
    CALC("1000,00", "1'000,00");   /* un montant, avec décimales, aussi */
    CALC("123456", "123'456");
    CALC("0,001", "0,001");
    CALC("99999999999999999999 + 1", "100'000'000'000'000'000'000");

    /* --- Division --- */
    CALC("1 ÷ 3", "0,3333333333333333333333333333");
    CALC("2 ÷ 3", "0,6666666666666666666666666667");
    CALC("10 ÷ 3", "3,333333333333333333333333333");
    CALC("1 ÷ 7 × 7", "1,0000000000000000000000000003");   /* ÷ arrondit, × reste exact */
    CALC("1 ÷ 8", "0,125");
    CALC("10 ÷ 4", "2,5");
    CALC("6 ÷ 3", "2");
    CALC("1,0 ÷ 2", "0,5");
    CALC("12,50 ÷ 5", "2,50");
    CALC("1 ÷ 1024", "0,0009765625");
    CALC("−7 ÷ 2", "−3,5");
    CALC("100 ÷ 0,5", "200");
    CALC("0 ÷ 5", "0");
    CALC("1 ÷ 0", "ERREUR 1:3 Division par zéro.");
    CALC("1 ÷ (2 − 2)", "ERREUR 1:3 Division par zéro.");

    /* --- Puissance (§ 3.1) --- */
    CALC("2 ^ 10", "1024");
    CALC("2 ^ −1", "0,5");
    CALC("2 ^ −2", "0,25");
    CALC("3 ^ −1", "0,3333333333333333333333333333");
    CALC("−2 ^ 2", "−4");
    CALC("(−2) ^ 2", "4");
    CALC("(−2) ^ 3", "−8");
    CALC("2 ^ 3 ^ 2", "512");
    CALC("2 ^ 2,0", "4");
    CALC("1,5 ^ 2", "2,25");
    CALC("0 ^ 0", "1");
    CALC("1 ^ 1000000000000", "1");
    CALC("(−1) ^ 1000000000001", "−1");
    CALC("0 ^ −1", "ERREUR 1:3 Division par zéro.");
    CALC("2 ^ 0,5", "~Exposant non entier");
    CALC("10 ^ 2000", "~Nombre trop grand");
    CALC("2 ^ 1000000000000", "~Nombre trop grand");

    /* --- Programmes --- */
    PROG("Remarque : premier programme GrymoiR\n"
         "Le prix unitaire vaut 12,50.\n"
         "La quantité vaut 3.\n"
         "Le total vaut prix unitaire × quantité.\n"
         "Le total devient total + 1'000.\n"
         "Afficher « Total à payer : » puis le total.",
         "Total à payer : 1'037,50");
    PROG("Le x vaut 1.\nLe x devient x + 1.\nLe x devient x × 10.\nAfficher x.", "20");
    PROG("Afficher « a ».\nAfficher 1 puis 2 puis « b ».", "a\n1 2 b");
    PROG("Afficher “Bonjour”.", "Bonjour");
    PROG("Le taux vaut 7,7.\nLe montant vaut 250.\nAfficher le montant × le taux ÷ 100.",
         "19,25");
    PROG("Le x vaut 5.\nAfficher x.\nLe y vaut x ÷ 0.\nAfficher y.", "ERREUR 3:13 Division par zéro.");

    /* --- Conditions (grammaire, § 5) --- */
    PROG("Le x vaut 5.\nSi x > 3, afficher « grand ». Sinon, afficher « petit ».", "grand");
    PROG("Le x vaut 2.\nSi x > 3, afficher « grand ». Sinon, afficher « petit ».", "petit");
    PROG("Afficher 1,0 = 1 puis 2 > 3 puis 0 est nul puis −1 est négatif puis 0 est positif.",
         "vrai faux vrai vrai faux");
    PROG("Afficher 3 ≤ 3 puis 3 < 3 puis 3 ≥ 4 puis 3 ≠ 3,00 puis 2 n'est pas inférieur à 1.",
         "vrai faux faux faux vrai");
    PROG("Afficher vrai et faux puis vrai ou faux puis (faux ou faux) et vrai.",
         "faux vrai faux");
    PROG("Le t vaut vrai.\nLe u vaut vrai.\nAfficher t = u puis t ≠ u.", "vrai faux");
    PROG("Le total vaut 120.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "10");
    PROG("Le total vaut 70.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "5");
    PROG("Le total vaut 10.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "0");
    PROG("Le x vaut 5.\nSi x > 0 :\n  Si x > 3 :\n    Afficher 1.\n  Sinon :\n    Afficher 2.\n  Afficher 3.\n"
         "Afficher 4.", "1\n3\n4");
    /* court-circuit : le second membre n'est pas calculé */
    PROG("Le x vaut 0.\nSi x ≠ 0 et 1 ÷ x > 1, afficher 1. Sinon, afficher 2.", "2");
    PROG("Le x vaut 0.\nSi x = 0 ou 1 ÷ x > 1, afficher 3.", "3");
    PROG("Le t vaut 2 > 1.\nSi le t, afficher « oui ».\nAfficher le t est faux.", "oui\nfaux");
    PROG("Le [frais et port] vaut 7,50.\nAfficher [frais et port] × 2.", "15,00");
    /* erreurs d'exécution */
    PROG("Le x vaut 3.\nSi x, afficher 1.", "ERREUR 2:4 Condition ni vraie ni fausse : la valeur est un nombre.");
    PROG("Le t vaut vrai.\nAfficher t + 1.", "~ADDITION impossible : un des opérandes est un booléen.");
    PROG("Le t vaut vrai.\nLe u vaut faux.\nAfficher t < u.", "~Seuls deux nombres ou deux dates se comparent par ordre");
    PROG("Le t vaut vrai.\nAfficher 1 = t.", "~Comparaison impossible entre un nombre et un booléen.");
    PROG("Le t vaut 3.\nAfficher t et vrai.", "~Condition ni vraie ni fausse");

    /* --- Formules (grammaire, § 9 ; docs/vm.md, § 3) --- */
    PROG("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 7 puis le carré de −1,5.", "49 2,25");
    PROG("La moyenne d'un premier nombre et d'un second nombre vaut (premier nombre + second nombre) ÷ 2.\n"
         "Afficher la moyenne de 4 et de 6 puis la moyenne de 1 et de 2.", "5 1,5");
    PROG("La valeur absolue d'un nombre :\n    Si nombre est négatif, rendre −nombre.\n    Rendre nombre.\n"
         "Afficher la valeur absolue de −5 puis la valeur absolue de 3.", "5 3");
    PROG("La factorielle d'un nombre :\n    Si nombre ≤ 1, rendre 1.\n    Rendre nombre × la factorielle de (nombre − 1).\n"
         "Afficher la factorielle de 20.", "2'432'902'008'176'640'000");
    PROG("Le carré d'un nombre vaut nombre × nombre.\nLa somme des carrés d'un a et d'un b vaut carré de a + carré de b.\n"
         "Afficher la somme des carrés de 3 et de 4.", "25");
    PROG("Le double d'un nombre :\n    Le résultat vaut nombre × 2.\n    Le résultat devient résultat + 0.\n"
         "    Rendre résultat.\nAfficher le double de 21.", "42");
    PROG("Le total vaut 0.\nPour ajouter un montant :\n    Le total devient total + montant.\n"
         "Ajouter 5.\nAjouter 2,50.\nAfficher le total.", "7,50");
    PROG("Pour saluer :\n    Afficher « Bonjour ».\nSaluer.\nSaluer.", "Bonjour\nBonjour");
    /* § 9.2 : un argument logique entre parenthèses (2.38 ; avant, « Parenthèse fermante manquante ») */
    PROG("Pour montrer un a et un b et un c :\n    Afficher a puis b puis c.\nLe f vaut 3.\nLe p vaut 10.\n"
         "Montrer 1 et 2 et (le f > 1 et le p ≤ 15).\nMontrer (f = 3 ou p = 0) et (f + 1) × 2 et (f > 5).",
         "1 2 vrai\nvrai 8 faux");
    PROG("Pour payer un montant et une remise :\n    Afficher montant − remise.\nPayer 10 et 2.", "8");
    PROG("Le compteur vaut 0.\nPour compter un nombre :\n    Si nombre > 0 :\n        Le compteur devient compteur + 1.\n"
         "        Compter nombre − 1.\nCompter 50.\nAfficher le compteur.", "50");
    PROG("La boucle d'un nombre vaut la boucle de nombre + 1.\nAfficher la boucle de 1.",
         "~Trop d'appels imbriqués : plus de 1000.");
    PROG("L'inverse d'un nombre vaut 1 ÷ nombre.\nAfficher 1.\nAfficher l'inverse de 0.", "ERREUR 1:30 Division par zéro.");
    /* une action qui échoue n'écrit rien (charte, art. 7) */
    PROG("Le total vaut 1.\nPour casser :\n    Le total devient 99.\n    Le total devient total ÷ 0.\nCasser.",
         "ERREUR 4:28 Division par zéro.");

    /* --- Boucles et Selon (grammaire, § 10) --- */
    PROG("Le x vaut 3.\nTant que x > 0 :\n    Afficher x.\n    Le x devient x − 1.", "3\n2\n1");
    PROG("Répéter 3 fois, afficher « Bip ».", "Bip\nBip\nBip");
    PROG("Répéter 0 fois, afficher « jamais ».\nAfficher « fin ».", "fin");
    PROG("Répéter 2,00 fois, afficher 1.", "1\n1");
    PROG("Répéter 2,5 fois, afficher 1.",
         "ERREUR 1:9 Nombre de tours invalide : un entier positif ou nul est attendu, pas 2,5.");
    PROG("Répéter −1 fois, afficher 1.", "~pas −1");
    PROG("Pour chaque mois de 1 à 4, afficher mois.", "1\n2\n3\n4");
    PROG("Pour chaque n de 3 à 1, afficher n.", "3\n2\n1");
    PROG("Pour chaque n de 1 à 1, afficher n.", "1");
    PROG("Pour chaque n de 1 à 10 par pas de 4, afficher n.", "1\n5\n9");
    PROG("Pour chaque n de 10 à 1 par pas de −4, afficher n.", "10\n6\n2");
    PROG("Pour chaque n de 1 à 3 par pas de −1, afficher n.\nAfficher « rien ».", "rien");
    PROG("Le total vaut 0.\nPour chaque t de 0 à 1 par pas de 0,1, le total devient total + 1.\nAfficher le total.", "11");
    PROG("Pour chaque t de 0 à 0,5 par pas de 0,25, afficher t.", "0\n0,25\n0,50");
    PROG("Pour chaque n de 1 à 3 par pas de 0, afficher n.", "ERREUR 1:1 Pas nul : la boucle ne finirait jamais.");
    PROG("Pour chaque i de 1 à 5 :\n    Si i = 2, passer au tour suivant.\n    Si i = 4, sortir de la boucle.\n    Afficher i.",
         "1\n3");
    PROG("Le x vaut 0.\nTant que vrai :\n    Le x devient x + 1.\n    Si x = 3, sortir de la boucle.\nAfficher x.", "3");
    PROG("Le x vaut 0.\nTant que x < 5 :\n    Le x devient x + 1.\n    Si x < 3, passer au tour suivant.\n    Afficher x.",
         "3\n4\n5");
    PROG("Le n vaut 3.\nRépéter n fois :\n    Le n devient n + 1.\n    Si n > 10, sortir de la boucle.\nAfficher n.", "6");
    /* boucles imbriquées : Sortir ne quitte que la plus proche */
    PROG("Pour chaque i de 1 à 3 :\n    Pour chaque j de 1 à 3 :\n        Si j > i, sortir de la boucle.\n"
         "        Afficher i × 10 + j.", "11\n21\n22\n31\n32\n33");
    PROG("La somme d'un n :\n    Le total vaut 0.\n    Pour chaque i de 1 à n, le total devient total + i.\n"
         "    Rendre total.\nAfficher la somme de 100.", "5050");
    PROG("Le x vaut 7.\nSelon x :\n    Cas 1 ou 2 :\n        Afficher « un ou deux ».\n    Cas de 3 à 9 :\n"
         "        Afficher « chiffre ».\n    Autrement :\n        Afficher « autre ».", "chiffre");
    PROG("Le x vaut 10.\nSelon x :\n    Cas négatif, afficher « négatif ».\n    Cas supérieur ou égal à 10, afficher « grand ».\n"
         "    Cas 10, afficher « jamais atteint ».", "grand");
    PROG("Le x vaut 2.\nSelon x :\n    Cas 1, afficher 1.\nAfficher « fin ».", "fin");
    PROG("Le t vaut vrai.\nSelon le t :\n    Cas vrai, afficher « oui ».\n    Cas faux, afficher « non ».", "oui");
    PROG("Le carré d'un n vaut n × n.\nSelon le carré de 3 :\n    Cas 9, afficher « neuf ».", "neuf");
    PROG("Le x vaut 1.\nSelon x :\n    Cas vrai, afficher 1.", "~Comparaison impossible entre un nombre et un booléen.");
    /* interruption (Ctrl+C) : l'exécution s'arrête au premier saut arrière */
    {
        grym_interruption = 1;
        PROG("Le x vaut 0.\nTant que vrai, le x devient x + 1.", "~Interrompu (Ctrl+C).");
        grym_interruption = 0;
    }

    /* --- Objets (grammaire, § 13) --- */
    PROG("Un client a :\n    un nom,\n    un solde.\nLe c vaut un nouveau client :\n    Le nom vaut « Dupont ».\n"
         "    Le solde vaut 100.\nLe solde du c devient solde du c − 30.\nAfficher le nom du c puis le solde du c puis c.",
         "Dupont 70 un client");
    PROG("Un client a : un solde.\nPour créditer un client et un montant :\n    Le solde du client devient solde du client + montant.\n"
         "Le solde initial d'un client vaut solde du client × 2.\nLe c vaut un nouveau client :\n    Le solde vaut 5.\n"
         "Créditer c et 10.\nAfficher solde du c puis solde initial de c.", "15 30");
    PROG("Un client a : un nom.\nLe a vaut un nouveau client.\nLe b vaut a.\nLe c vaut un nouveau client.\n"
         "Afficher a = b puis a = c puis a ≠ c.", "vrai faux vrai");
    PROG("Un client a : un nom.\nLe b vaut un nouveau client.\nLe nom du b devient « x ».\nLe a vaut b.\n"
         "Le nom du a devient « y ».\nAfficher nom du b.", "y");
    PROG("Un client a : un solde.\nLe c vaut un nouveau client.\nAfficher le solde du c.",
         "ERREUR 3:10 Le champ « solde » n'a pas de valeur.");
    PROG("Un client a : un nom.\nUne facture a : un montant.\nLe c vaut un nouveau client.\nAfficher montant du c.",
         "ERREUR 4:10 Un client n'a pas de champ « montant ».");
    PROG("Un client a : un nom.\nLe x vaut 3.\nAfficher le nom du x.",
         "ERREUR 3:10 « nom » : la valeur n'est pas un objet, c'est un nombre.");
    PROG("Un nœud a : un suivant, une valeur.\nLe premier vaut un nouveau nœud :\n    La valeur vaut 1.\n"
         "Le courant vaut premier.\nPour chaque i de 2 à 5 :\n    Le suivant du courant devient un nouveau nœud :\n"
         "        La valeur vaut i.\n    Le courant devient suivant du courant.\n"
         "Afficher valeur du suivant du suivant du premier.", "3");

    /* --- Boucle interactive : une saisie ratée n'a aucun effet (§ 3.3, docs/vm.md § 6) --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Le a vaut 1.", "Le a devient 5. Le b vaut 1 ÷ 0.", "a", "b",
                                  "Le a devient a + 1. Le a devient a × 10. Le a devient a ÷ 0.", "a",
                                  "Si vrai :\n    Le a devient 7.\n    Le a devient a ÷ 0.", "a" };
        const char *attendus[] = { "", "~Division par zéro", "1", "~« b » inconnu",
                                   "~Division par zéro", "1", "~Division par zéro", "1" };
        /* Comme la boucle de grym : la machine annule ses écritures, la portée est restaurée. */
        for (int i = 0; i < 8; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                            : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Héritage --- */
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 42.\n"
         "Le nom du m devient « Anna ».\nAfficher nom du m puis licence du m puis m.", "Anna 42 un membre");
    PROG("Un objet a : un nom.\nUn outil est un objet. Un outil a : un usage.\nUn marteau est un outil. Un marteau a : un poids.\n"
         "Le h vaut un nouveau marteau :\n    Le nom vaut « M ».\n    L'usage vaut « clouer ».\n    Le poids vaut 2.\n"
         "Afficher nom du h puis usage du h puis poids du h.", "M clouer 2");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn invité est une personne.\n"
         "Pour saluer une personne :\n    Afficher « Bonjour » puis nom de personne.\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\nLe i vaut un nouvel invité :\n    Le nom vaut « Bo ».\n"
         "Saluer m.\nSaluer i.", "Bonjour Ana\nBonjour Bo");
    PROG("Une personne a : un nom.\nUne ville a : un nom.\nUn membre est une personne. Un membre a : une ville.\n"
         "Le m vaut un nouveau membre.\nLa ville du m devient une nouvelle ville :\n    Le nom vaut « Bulle ».\n"
         "Afficher nom de la ville du m.", "Bulle");

    /* --- Méthodes : la version la plus précise, selon la classe réelle --- */
    PROG("Une personne a : un nom.\nUn membre est une personne. Un membre a : une licence.\nUn invité est une personne.\n"
         "Pour saluer une personne :\n    Afficher « Bonjour » puis nom de la personne.\n"
         "Pour saluer un membre :\n    Afficher « Salut » puis nom du membre puis licence du membre.\n"
         "La description d'une personne vaut « personne ».\nLa description d'un invité vaut « invité ».\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 7.\n"
         "Le i vaut un nouvel invité :\n    Le nom vaut « Bo ».\nSaluer m.\nSaluer i.\n"
         "Afficher la description de m puis la description de i.",
         "Salut Ana 7\nBonjour Bo\npersonne invité");
    PROG("Un objet a : un nom.\nUn outil est un objet.\nUn marteau est un outil.\n"
         "Le poids d'un objet vaut 1.\nLe poids d'un outil vaut 2.\nLe h vaut un nouveau marteau.\nAfficher poids de h.", "2");
    PROG("Une personne a : un nom.\nPour saluer une personne :\n    Afficher 1.\nSaluer 3.",
         "ERREUR 4:1 « saluer » choisit sa version selon la classe de son premier argument : "
         "celui-ci n'est pas un objet, c'est un nombre.");
    PROG("Une personne a : un nom.\nUne ville a : un nom.\nPour saluer une personne :\n    Afficher 1.\n"
         "La v vaut une nouvelle ville.\nSaluer v.", "ERREUR 6:1 Aucune version de « saluer » pour une ville.");

    /* --- Aptitudes : champs apportés, versions, ordre de choix --- */
    PROG(APT_M "Un membre est une personne horodatée.\nUn membre a : une licence.\n"
         "Pour dater une chose horodatée :\n    La date de la chose devient « lundi ».\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 3.\n"
         "Dater m.\nAfficher nom du m puis date du m puis licence du m.", "Ana lundi 3");
    PROG(APT_M "Un document est une chose horodatée et numérotée.\n"
         "Le d vaut un nouveau document :\n    La date vaut 1.\n    Le numéro vaut 2.\nAfficher date du d puis numéro du d puis d.",
         "1 2 un document");
    /* la classe l'emporte sur ses aptitudes, qui l'emportent sur la classe parente */
    PROG(APT_M "Un employé est une personne horodatée.\nUn cadre est un employé.\n"
         "La sorte d'une personne vaut « personne ».\nLa sorte d'une chose horodatée vaut « horodatée ».\n"
         "La sorte d'un cadre vaut « cadre ».\n"
         "Le e vaut un nouvel employé.\nLe c vaut un nouveau cadre.\nLa p vaut une nouvelle personne.\n"
         "Afficher sorte de e puis sorte de c puis sorte de p.", "horodatée cadre personne");
    PROG(APT_M "Un membre est une personne horodatée et numérotée.\n"
         "Pour décrire une chose horodatée :\n    Afficher 1.\nPour décrire une chose numérotée :\n    Afficher 2.\n"
         "Pour décrire un membre :\n    Afficher 3.\nLe m vaut un nouveau membre.\nDécrire m.", "3");

    /* --- Dates (§ 14) --- */
    PROG("La facture vaut 21.09.2026.\nL'échéance vaut facture + 30.\n"
         "Afficher échéance puis échéance − facture puis facture − 5 puis 1 + facture.",
         "21.10.2026 30 16.09.2026 22.09.2026");
    PROG("Afficher 29.02.2024 + 365 puis 28.02.2023 + 1 puis 31.12.2026 + 1 puis 01.03.2024 − 1.",
         "28.02.2025 01.03.2023 01.01.2027 29.02.2024");
    PROG("Afficher 01.01.2027 − 01.01.2026 puis 01.01.2025 − 01.01.2024 puis 01.01.2026 − 21.09.2026.",
         "365 366 −263");
    PROG("Afficher 21.09.2026 + 3,00 puis 21.09.2026 < 22.09.2026 puis 21.09.2026 = 21.09.2026 puis 1.3.2026.",
         "24.09.2026 vrai vrai 01.03.2026");
    PROG("Pour chaque j de 30.12.2026 à 02.01.2027, afficher j.", "30.12.2026\n31.12.2026\n01.01.2027\n02.01.2027");
    PROG("Pour chaque j de 03.01.2026 à 01.01.2026, afficher j.", "03.01.2026\n02.01.2026\n01.01.2026");
    PROG("Pour chaque j de 01.01.2026 à 15.01.2026 par pas de 7, afficher j.", "01.01.2026\n08.01.2026\n15.01.2026");
    PROG("Le jour vaut 14.07.2026.\nSelon jour :\n    Cas de 01.07.2026 à 31.08.2026, afficher « été ».\n"
         "    Autrement, afficher « autre ».", "été");
    PROG("Afficher aujourd'hui = aujourd'hui puis aujourd'hui > 01.01.2026.", "vrai vrai");
    /* Mois, années, semaines, jours (§ 14.2) : ramenés au dernier jour du mois */
    PROG("La d vaut 31.01.2026.\nAfficher d + 1 mois puis d + 1 mois − 1 mois puis d + 13 mois puis d + −1 mois.",
         "28.02.2026 28.01.2026 28.02.2027 31.12.2025");
    PROG("Afficher 31.01.2024 + 1 mois puis 29.02.2028 + 1 an puis 29.02.2028 − 4 ans puis 15.03.2026 + 2 années.",
         "29.02.2024 28.02.2029 29.02.2024 15.03.2028");
    PROG("Afficher 31.01.2026 + 2 semaines puis 31.01.2026 − 10 jours puis 31.01.2026 + 0 jour puis 31.01.2026 + 3,00 mois.",
         "14.02.2026 21.01.2026 31.01.2026 30.04.2026");
    PROG("Le n vaut 3.\nAfficher 31.01.2026 + n mois puis 31.01.2026 + 2 × n mois puis 31.01.2026 + (n + 1) mois.",
         "30.04.2026 31.07.2026 31.05.2026");
    PROG("Afficher l'année de 21.09.2026 + 10 ans puis l'année de 21.09.2026 − 1 an.", "2036 2025");
    /* Arrondi (§ 3.4) : au plus proche, la moitié loin de zéro ; vers le bas, vers le haut, au pair */
    PROG("Le brut vaut 12,35.\nAfficher l'arrondi de brut × 1,081 à 0,05.", "13,35");
    PROG("Afficher l'arrondi de 2,5 à 1 puis l'arrondi de −2,5 à 1 puis l'arrondi de 2,49 à 1.", "3 −3 2");
    PROG("Afficher l'arrondi de 2,5 à 1 au pair puis l'arrondi de 3,5 à 1 au pair puis l'arrondi de 2,25 à 0,1 au pair.",
         "2 4 2,2");
    PROG("Afficher l'arrondi de 2,1 à 1 vers le haut puis l'arrondi de −2,1 à 1 vers le haut.", "3 −2");
    PROG("Afficher l'arrondi de 2,9 à 1 vers le bas puis l'arrondi de −2,1 à 1 vers le bas.", "2 −3");
    PROG("Afficher l'arrondi de 1234,5678 à 0,01 puis l'arrondi de 1234 à 100 puis l'arrondi de 0,001 à 0,01.",
         "1'234,57 1200 0,00");
    PROG("Afficher l'arrondi de 1 ÷ 3 à 0,01 puis l'arrondi de 7,03 à 0,05 puis l'arrondi de 7,024 à 0,05.",
         "0,33 7,05 7,00");
    PROG("Le pas vaut 0,05.\nLe brut vaut 2,02.\nAfficher l'arrondi du brut au pas + 1.", "3,00");
    PROG("Afficher l'arrondi de 0,4 à 1 = 0 puis l'arrondi de 10 ÷ 4 à 1 × 2.", "vrai 6");
    PROG("L'arrondi d'un nombre vaut nombre + 1.\nAfficher l'arrondi de 3.", "4");
    PROG("Un x a : un arrondi.\nLe o vaut un nouveau x :\n    L'arrondi vaut 4.\nAfficher l'arrondi de o.", "4");
    PROG("Afficher l'arrondi de 3 à 0.", "ERREUR 1:10 Un arrondi se fait à un pas positif (0,05, 1, 10…), pas à 0.");
    PROG("Afficher l'arrondi de 3 à −1.", "ERREUR 1:10 Un arrondi se fait à un pas positif (0,05, 1, 10…), pas à −1.");
    PROG("Afficher l'arrondi de « a » à 1.", "ERREUR 1:10 On n'arrondit qu'un nombre, pas un texte.");
    PROG("Afficher l'arrondi de 2 à « a ».", "ERREUR 1:10 Le pas d'un arrondi est un nombre, pas un texte.");
    /* Appeler une autre version (§ 13.8) */
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\nUn cadet est un membre.\nUne ville a : un nom.\n" "Pour saluer une personne :\n    Afficher « Bonjour » puis nom de la personne.\n"
         "Pour saluer un membre :\n    Saluer le membre comme une personne.\n    Afficher « Licence : » puis licence du membre.\n"
         "Le titre d'une personne vaut « M. » suivi de nom de la personne.\n"
         "Le titre d'un membre vaut « membre » suivi du titre du membre comme une personne.\n"
         "Le c vaut un nouveau cadet :\n    Le nom vaut « Noé ».\n    La licence vaut « A-1 ».\n"
         "Saluer c.\nAfficher le titre de c puis le titre de c comme une personne.\nSaluer c comme une personne.",
         "Bonjour Noé\nLicence : A-1\nmembreM.Noé M.Noé\nBonjour Noé");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\nUn cadet est un membre.\nUne ville a : un nom.\n" "Pour saluer une personne :\n    Afficher 1.\nLe v vaut une nouvelle ville :\n    Le nom vaut « Bulle ».\n"
         "Saluer v comme une personne.", "ERREUR 10:1 « saluer » comme une personne : une ville n'en est pas une.");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\nUn cadet est un membre.\nUne ville a : un nom.\n" "Pour saluer une personne :\n    Afficher 1.\nLe p vaut une nouvelle personne :\n    Le nom vaut « a ».\n"
         "Saluer p comme un membre.", "ERREUR 10:1 « saluer » comme un membre : une personne n'en est pas un.");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\nUn cadet est un membre.\nUne ville a : un nom.\n" "Pour saluer un membre :\n    Afficher 1.\nLe c vaut un nouveau cadet :\n    Le nom vaut « a ».\n"
         "    La licence vaut « b ».\nSaluer c comme une personne.", "ERREUR 11:1 Aucune version de « saluer » pour une personne.");
    PROG("Pour saluer :\n    Afficher 1.\nUne personne a : un nom.\nSaluer comme une personne.",
         "ERREUR 4:8 « saluer » n'est pas une méthode : « comme une personne » ne choisit aucune version (§ 13.6).");
    /* Ensembles (§ 13.9) */
    PROG("Les couleurs valent « rouge », « vert » et « bleu ».\nLes couleurs gagnent « rouge ».\nLes couleurs gagnent « jaune ».\n"
         "Les couleurs perdent « vert ».\nAfficher les couleurs puis le nombre de couleurs.\n"
         "Afficher les couleurs jointes par « / ».\nSi « bleu » est parmi les couleurs, afficher « oui ».\n"
         "Si « vert » n'est pas parmi les couleurs, afficher « plus de vert ».\n",
         "rouge, bleu, jaune 3\nrouge/bleu/jaune\noui\nplus de vert");
    PROG("Les couleurs valent « a » et « b ».\nLes autres valent les couleurs.\nLes autres gagnent « c ».\n"
         "Afficher le nombre de couleurs puis le nombre d'autres.\nPour chaque couleur parmi les autres, afficher couleur.\n"
         "Les vides valent un ensemble vide.\nAfficher le nombre de vides.\nLes vides deviennent 1, 2 et 2,0.\nAfficher les vides.",
         "2 3\na\nb\nc\n0\n1, 2");
    PROG("Une personne a : un nom.\nLes invités valent un ensemble vide.\nPour chaque n de 1 à 3 :\n"
         "    Le p vaut une nouvelle personne :\n        Le nom vaut « P » suivi de n.\n    Les invités gagnent p.\n"
         "    Les invités gagnent p.\nAfficher le nombre d'invités.\nPour chaque invité parmi les invités, afficher nom de invité.",
         "3\nP1\nP2\nP3");
    PROG("Le total d'un nombre vaut nombre.\nLa somme de l'ensemble d'un n vaut n + 1.\nLes x valent 1.\nLes x valent 2.",
         "~« x » existe déjà");
    PROG("Les x valent 1.\nAfficher 3 est parmi les x.", "faux");
    PROG("Le t vaut 1.\nLes t deviennent 2.", "ERREUR 2:5 « t » n'est pas un ensemble.");
    /* Exemples (§ 9.6) : ignorés à l'exécution, vérifiés en mode exemples */
    PROG("Le carré d'un nombre vaut nombre × nombre.\nExemple : le carré de 2 vaut 5.\nAfficher 1.", "1");
    compilateur_mode_exemples(1);
    PROG("Le carré d'un nombre vaut nombre × nombre.\nExemple : le carré de 7 vaut 49.\nExemple : le carré de 2 vaut 5.\n"
         "Exemple : le carré de « a » vaut 1.\nExemple : « a » en majuscules vaut « A ».\nAfficher 1.\nLe x vaut 2.",
         "✓ ligne 2 : le carré de 7 vaut 49\n✗ ligne 3 : le carré de 2 vaut 5 ; obtenu : 4\n"
         "✗ ligne 4 : le carré de « a » vaut 1 ; erreur : MULTIPLICATION impossible : un des opérandes est un texte.\n"
         "✓ ligne 5 : « a » en majuscules vaut « A »");
    compilateur_mode_exemples(0);
    PROG("Le x vaut 3.\nExemple : x vaut 3.", "~« x » n'est pas visible dans un calcul");
    PROG("Le carré d'un nombre :\n    Exemple : 1 vaut 1.\n    Rendre nombre.", "~Un exemple se place au premier niveau");
    PROG("Exemple : 1 + 1.", "~Un exemple dit ce que vaut une expression");
    /* Écart entre deux dates (§ 14.6), hasard (§ 3.6) */
    PROG("Afficher le nombre de mois entre 31.01.2026 et 28.02.2026 puis le nombre de mois entre 31.01.2026 et 27.02.2026.\n"
         "Afficher le nombre d'années entre 21.09.1990 et 20.09.2026 puis le nombre d'ans entre 21.09.1990 et 21.09.2026.\n"
         "Afficher le nombre de jours entre 01.01.2026 et 01.03.2026 puis le nombre de semaines entre 01.01.2026 et 01.03.2026.\n"
         "Afficher le nombre de mois entre 15.03.2026 et 14.01.2026 puis le nombre d'années entre 21.09.2026 et 21.09.1990.\n"
         "Afficher le nombre d'années entre 29.02.2024 et 28.02.2025 puis le nombre de jours entre 01.01.2026 et 01.01.2026 + 3.",
         "1 0\n35 36\n59 8\n−2 −36\n1 3");
    PROG("Afficher le nombre de mois entre 3 et 01.01.2026.", "ERREUR 1:10 Un écart se mesure entre deux dates, pas avec un nombre.");
    PROG("Afficher le nombre d'mois entre 01.01.2026 et 01.02.2026.", "~« le nombre de mois entre … »");
    PROG("Les vus valent un ensemble vide.\nLe juste vaut vrai.\nRépéter 600 fois :\n    Le n vaut un nombre au hasard de 6 à 1.\n"
         "    Si n < 1 ou n > 6, le juste devient faux.\n    Les vus gagnent n.\nAfficher juste puis le nombre de vus.\n"
         "Les faces valent « pile » et « face ».\nAfficher un élément au hasard parmi les faces est parmi les faces.",
         "vrai 6\nvrai");
    PROG("Le dé d'un n vaut un nombre au hasard de 1 à n.", "~Un calcul ne dépend pas du hasard");
    /* Heures (§ 14.7) */
    PROG("Une réunion, conservée, a : un titre (texte), un début (heure), une fin (heure), facultative.\n"
         "La r vaut une nouvelle réunion :\n    Le titre vaut « Comité ».\n    Le début vaut 19:30.\nConserver r.\n"
         "La s vaut une nouvelle réunion :\n    Le titre vaut « Tir ».\n    Le début vaut 8:05.\nConserver s.\n"
         "Pour chaque réunion conservée, par début, afficher début de la réunion puis titre de la réunion.\n"
         "Afficher le nombre de réunions conservées dont le début est supérieur à 12:00.",
         "08:05 Tir\n19:30 Comité\n1");
    PROG("Afficher 23:50 + 15 minutes puis 8:05 − 2 heures puis 14:30 − 8:05 puis le nombre d'heures entre 8:05 et 19:30.\n"
         "Afficher 8:05 < 19:30 puis 12:00 = 12:00 puis « 7:45 » en heure + 1 heure puis le nombre de minutes entre 19:30 et 8:05.",
         "00:05 06:05 385 11\nvrai vrai 08:45 −685");
    PROG("Le t vaut maintenant.\nAfficher t ≥ 0:00 et t ≤ 23:59.", "vrai");
    PROG("Afficher 24:00.", "~L'heure « 24:00 » n'existe pas : de 0:00 à 23:59.");
    PROG("Un rendez-vous a : un titre.", "ERREUR 1:10 « rendez-vous » : un nom ne prend pas de trait d'union, « - » est une "
         "soustraction (§ 1.4). Écris « rendez vous », en deux mots.");
    PROG("Le total vaut 3.\nLe remise vaut 1.\nAfficher total-remise puis total-1.", "2 2");
    PROG("Afficher 8:05 + 1.", "~ADDITION impossible avec une heure");
    PROG("Afficher 8:05 + 1 jour.", "~Une heure se décale de minutes ou d'heures, pas de jours.");
    PROG("Afficher 01.01.2026 + 3 minutes.", "~Des minutes s'ajoutent à une heure, pas à une date.");
    PROG("Afficher « 25:00 » en heure.", "~« 25:00 » n'est pas une heure : écris heures:minutes (14:30).");
    PROG("L'arrivée d'une h vaut maintenant.", "~Un calcul ne dépend pas de l'heure");
    PROG("Afficher un nombre au hasard de 1 à 2,5.", "~Un nombre au hasard se tire entre deux nombres entiers");
    PROG("Les vides valent un ensemble vide.\nAfficher un élément au hasard parmi les vides.",
         "ERREUR 2:10 Un élément au hasard ne se tire pas d'un ensemble vide.");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\nUn cadet est un membre.\nUne ville a : un nom.\n" "Pour saluer une personne :\n    Afficher 1.\nLe p vaut une nouvelle personne :\n    Le nom vaut « a ».\n"
         "Saluer p comme un personne.", "ERREUR 10:16 Accord : « comme une personne ».");
    /* Reste, partie entière, valeur absolue (§ 3.5) */
    PROG("Afficher le reste de 17 par 5 puis le reste de −7 par 3 puis le reste de 7 par −3 puis le reste de 7,5 par 2.",
         "2 −1 1 1,5");
    PROG("Le n vaut 10.\nAfficher le reste du n par 3 puis le reste de n × 2 + 1 par 4 puis le reste de 17 par 5 + 1.", "1 1 3");
    PROG("Afficher la valeur absolue de −3,50 puis la partie entière de −2,7 puis la partie entière de 2,7 puis "
         "la partie entière de (10 ÷ 4).", "3,50 −2 2 2");
    PROG("Le n vaut 10.\nAfficher la valeur absolue de n − 20 puis la valeur absolue de (n − 20).", "−10 10");
    PROG("Le reste vaut 4.\nAfficher le reste puis le reste + 1.", "4 5");
    PROG("La valeur absolue d'un nombre vaut 7.\nAfficher la valeur absolue de −1.", "7");
    PROG("Afficher le reste de 3 par 0.", "ERREUR 1:10 Division par zéro.");
    PROG("Afficher le reste de « a » par 2.", "ERREUR 1:10 Le reste se calcule entre deux nombres, pas avec un texte.");
    PROG("Afficher le reste de 3.", "ERREUR 1:23 Le reste se calcule par un diviseur : « le reste de 17 par 5 ».");
    /* Textes (§ 4.5) : longueur, majuscules, conversions, contient */
    PROG("Le nom vaut « Élodie Müller ».\nAfficher la longueur du nom puis nom en majuscules puis nom en minuscules.",
         "13 ÉLODIE MÜLLER élodie müller");
    PROG("Afficher la longueur de « été » puis « œuvre » en majuscules puis la longueur de « ».", "3 ŒUVRE 0");
    PROG("Le nom vaut « Élodie Müller ».\nSi le nom contient « MUL », afficher « a ».\n"
         "Si le nom commence par « elo » et nom finit par « ER », afficher « b ».\n"
         "Si le nom ne contient pas « x », afficher « c ».\nSi le nom ne commence pas par « m », afficher « d ».\n"
         "Si le nom ne finit pas par « r », afficher « e ».", "a\nb\nc\nd");
    PROG("Afficher « ŒUVRE » contient « œu » puis « abc » contient « » puis « ab » contient « abc ».", "vrai vrai faux");
    PROG("Le code vaut « 1'234,50 ».\nAfficher code en nombre + 1 puis « −3 » en nombre entier puis « 21.09.2026 » en date + 1.",
         "1'235,50 −3 22.09.2026");
    PROG("Afficher « Oui » en vrai ou faux puis « 1747 » en année puis (« a » suivi de « b ») en majuscules.", "vrai 1747 AB");
    PROG("Un mur a : une longueur.\nLe m vaut un nouveau mur :\n    La longueur vaut 4.\nAfficher la longueur du m.", "4");
    PROG("La longueur d'un texte vaut 7.\nAfficher la longueur de « a ».", "7");
    PROG("Afficher « abc » en nombre.", "ERREUR 1:18 « abc » n'est pas un nombre.");
    PROG("Afficher « 2,5 » en nombre entier.", "ERREUR 1:18 « 2,5 » n'est pas un nombre entier.");
    PROG("Afficher « » en date.", "ERREUR 1:14 Un texte vide n'est pas une date.");
    PROG("Afficher 3 en majuscules.", "ERREUR 1:12 « en majuscules » s'applique à un texte, pas à un nombre.");
    PROG("Si 3 contient « 3 », afficher 1.", "ERREUR 1:6 « contient » compare deux textes, pas un nombre.");
    PROG("Pour chaque e du 31.01.2026 au 31.05.2026 par pas de 1 mois, afficher e.",
         "31.01.2026\n28.02.2026\n31.03.2026\n30.04.2026\n31.05.2026");
    PROG("Pour chaque e du 31.12.2026 au 31.01.2026 par pas de −3 mois, afficher e.",
         "31.12.2026\n30.09.2026\n30.06.2026\n31.03.2026");
    PROG("Pour chaque e du 29.02.2024 au 01.03.2028 par pas de 2 ans, afficher e.", "29.02.2024\n28.02.2026\n29.02.2028");
    PROG("Pour chaque e du 01.01.2026 au 20.01.2026 par pas de 1 semaine, afficher e.",
         "01.01.2026\n08.01.2026\n15.01.2026");
    PROG("Pour chaque e du 01.01.2026 au 01.03.2026 par pas de 1 mois :\n    Si e > 01.01.2026, passer au tour suivant.\n"
         "    Afficher e.", "01.01.2026");
    PROG("Pour chaque e du 01.01.2026 au 01.03.2026 par pas de 0 mois, afficher e.",
         "ERREUR 1:1 Pas nul : la boucle ne finirait jamais.");
    PROG("Pour chaque e de 1 à 5 par pas de 1 mois, afficher e.", "ERREUR 1:37 Des mois s'ajoutent à une date, pas à un nombre.");
    PROG("Afficher 3 + 2 mois.", "ERREUR 1:12 Des mois s'ajoutent à une date, pas à un nombre.");
    PROG("Afficher 31.01.2026 + 1,5 mois.", "ERREUR 1:21 Une date se décale d'un nombre entier de mois.");
    PROG("Afficher 31.01.2026 + « a » semaines.", "ERREUR 1:21 Une date se décale d'un nombre entier de semaines.");
    PROG("Afficher 31.12.9999 + 1 mois.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 01.01.0001 − 1 an.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 31.01.2026 + 99999999 mois.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher l'année de 21.09.2026 + 1 mois.", "ERREUR 1:32 Une année se décale d'années, pas de mois.");
    PROG("Afficher l'année de 21.09.2026 + 9000 ans.", "ERREUR 1:32 Année hors du calendrier : de 1 à 9999.");
    PROG("Afficher 21.09.2026 + 21.09.2026.", "ERREUR 1:21 On n'additionne pas deux dates.");
    PROG("Afficher 21.09.2026 + 0,5.", "ERREUR 1:21 Une date se décale d'un nombre entier de jours.");
    PROG("Afficher 31.12.9999 + 1.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 01.01.0001 − 1.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 21.09.2026 + 99999999.", "ERREUR 1:21 Une date se décale d'un nombre entier de jours.");
    PROG("Afficher 3 − 21.09.2026.", "ERREUR 1:12 On ne soustrait pas une date d'un nombre.");
    PROG("Afficher 21.09.2026 + « a ».", "ERREUR 1:21 ADDITION impossible entre une date et un texte.");
    PROG("Afficher 21.09.2026 < 3.", "ERREUR 1:21 Comparaison impossible entre une date et un nombre.");
    PROG("Afficher −21.09.2026.", "ERREUR 1:10 Opposé impossible : la valeur est une date.");
    PROG("Répéter 21.09.2026 fois, afficher 1.",
         "~Nombre de tours invalide : un entier positif ou nul est attendu, pas une date.");
    {
        total++;
        char *attendu = date_suisse(date_aujourdhui());
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, "Afficher aujourd'hui.", 0);
        if (strcmp(r, attendu) != 0) signaler(__LINE__, "Afficher aujourd'hui.", attendu, r);
        free(r);
        free(attendu);
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Fichiers (§ 15) --- */
    {
        static const unsigned char PNG[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13 };
        static const unsigned char JPEG[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 16 };
        static const unsigned char WEBP[] = { 'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P' };
        static const unsigned char FAUX_WEBP[] = { 'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E' };
        creer_fichier("_essai.png", PNG, sizeof PNG);
        creer_fichier("_essai.jpg", JPEG, sizeof JPEG);
        creer_fichier("_essai.gif", "GIF89a....", 10);
        creer_fichier("_essai.webp", WEBP, sizeof WEBP);
        creer_fichier("_essai.wav", FAUX_WEBP, sizeof FAUX_WEBP);
        creer_fichier("_essai.txt", "bonjour", 7);
        creer_fichier("_essai1.bin", "x", 1);
        static char gros[2345678];
        memset(gros, 'x', sizeof gros);
        creer_fichier("_essai_gros.bin", gros, sizeof gros);
        creer_fichier("_essai_1000.bin", gros, 1000);
        creer_fichier("_essai_1050.bin", gros, 1050);
        creer_fichier("_essai_999.bin", gros, 999);
        remove("_essai_copie.png");
        remove("_essai_copie2.png");
        remove("_essai_rate.png");

        PROG("La photo vaut le fichier « _essai.png ».\n"
             "Afficher photo puis taille de la photo puis format de la photo puis nom de fichier de la photo.",
             "une image PNG de 12 octets 12 PNG _essai.png");
        PROG("Afficher format du fichier « _essai.jpg » puis format du fichier « _essai.gif » "
             "puis format du fichier « _essai.webp » puis format du fichier « _essai.wav ».",
             "JPEG GIF WebP WAV");   /* un WAV n'est pas une WebP, mais un son (§ 15.7) */
        PROG("Afficher le fichier « _essai.txt » puis le fichier « _essai1.bin ».",
             "un fichier de 7 octets un fichier de 1 octet");
        PROG("Afficher le fichier « _essai_999.bin ».\nAfficher le fichier « _essai_1000.bin ».\n"
             "Afficher le fichier « _essai_1050.bin ».\nAfficher le fichier « _essai_gros.bin ».",
             "un fichier de 999 octets\nun fichier de 1 Ko\nun fichier de 1,1 Ko\nun fichier de 2,3 Mo");
        PROG("Afficher le fichier « _essai.png » = le fichier « _essai.png » puis "
             "le fichier « _essai.png » = le fichier « _essai.jpg ».", "vrai faux");
        PROG("Le chemin vaut « _essai.txt ».\nAfficher taille du fichier (chemin).", "7");
        PROG("Afficher le fichier « _absent.png ».", "ERREUR 1:10 Fichier « _absent.png » introuvable ou illisible.");
        PROG("Afficher le fichier (3).", "ERREUR 1:10 Le chemin d'un fichier est un texte, pas un nombre.");
        PROG("La photo vaut le fichier « _essai.png ».\nAfficher poids de la photo.", "ERREUR 2:10 « poids de la photo » inconnu.");
        PROG("Une personne a : un nom, une photo.\nLa p vaut une nouvelle personne :\n"
             "    La photo vaut le fichier « _essai.jpg ».\nAfficher photo de la p puis format de la photo de la p.",
             "une image JPEG de 6 octets JPEG");
        /* écriture différée : rien n'est écrit si l'exécution échoue */
        PROG("Enregistrer le fichier « _essai.png » dans « _essai_rate.png ».\nAfficher 1 ÷ 0.",
             "ERREUR 2:12 Division par zéro.");
        total++;
        if (fichier_existe("_essai_rate.png")) signaler(__LINE__, "écriture annulée", "absent", "présent");
        PROG("Enregistrer le fichier « _essai.png » dans « _essai_copie.png ».\nAfficher « écrit ».", "écrit");
        PROG("Afficher le fichier « _essai_copie.png » = le fichier « _essai.png ».", "vrai");
        PROG("Enregistrer le fichier « _essai.txt » dans « _essai_copie.png ».",
             "ERREUR 1:1 « _essai_copie.png » existe déjà : il n'est jamais écrasé.");
        PROG("Enregistrer le fichier « _essai.txt » dans « _essai_copie2.png ».\n"
             "Enregistrer le fichier « _essai.txt » dans « _essai_copie2.png ».",
             "ERREUR 2:1 « _essai_copie2.png » est déjà enregistré par cette exécution.");
        total++;
        if (fichier_existe("_essai_copie2.png")) signaler(__LINE__, "écriture annulée", "absent", "présent");
        PROG("Enregistrer 3 dans « _essai_x ».", "ERREUR 1:1 Seul un fichier s'enregistre : la valeur est un nombre.");
        PROG("La photo vaut le fichier « _essai.png ».\nAfficher photo < photo.",
             "~Seuls deux nombres ou deux dates se comparent par ordre");

        const char *essais[] = { "_essai.png", "_essai.jpg", "_essai.gif", "_essai.webp", "_essai.wav", "_essai.txt",
                                 "_essai1.bin", "_essai_gros.bin", "_essai_1000.bin", "_essai_1050.bin",
                                 "_essai_999.bin", "_essai_copie.png" };
        for (size_t k = 0; k < sizeof essais / sizeof *essais; k++) remove(essais[k]);
    }

    /* --- Entités : typage strict avant toute écriture (§ 16.2) --- */
    PROG("Un client, conservé, a : un nom (texte), un âge (nombre entier), un solde (nombre), un actif (vrai ou faux), "
         "une inscription (date).\nLe c vaut un nouveau client :\n    Le nom vaut « Ana ».\n    L'âge vaut 30.\n"
         "    Le solde vaut 12,50.\n    L'actif vaut vrai.\n    L'inscription vaut 21.09.2026.\n"
         "Afficher nom du c puis âge du c puis solde du c puis actif du c puis inscription du c.",
         "Ana 30 12,50 vrai 21.09.2026");
    PROG("Un client, conservé, a : un âge (nombre entier).\nLe v vaut 2,5.\nLe c vaut un nouveau client :\n    L'âge vaut v.",
         "ERREUR 4:5 Le champ « âge » attend un nombre entier, pas 2,5.");
    PROG("Un client, conservé, a : un nom (texte).\nLe c vaut un nouveau client.\nLe x vaut 3.\nLe nom du c devient x.",
         "ERREUR 4:1 Le champ « nom » attend un texte, pas 3.");
    PROG("Un client, conservé, a : un parrain (client).\nUne personne a : un nom.\nLe p vaut une nouvelle personne.\n"
         "Le c vaut un nouveau client.\nLe parrain du c devient p.",
         "ERREUR 5:1 Le champ « parrain » attend un client, pas une personne.");
    PROG("Un client, conservé, a : un parrain (client).\nUn membre, conservé, est un client.\n"
         "Le m vaut un nouveau membre.\nLe c vaut un nouveau client.\nLe parrain du c devient m.\nAfficher parrain du c.",
         "un membre");
    PROG("Une chose datée a : une date (date).\nUn document, conservé, est une chose datée.\nLe x vaut « lundi ».\n"
         "Le d vaut un nouveau document.\nLa date du d devient x.",
         "ERREUR 5:1 Le champ « date » attend une date, pas un texte.");
    /* « suivi de » écrit un nombre comme « Afficher » : le séparateur de milliers fait partie du texte (§ 4) */
    PROG("Afficher « P- » suivi de 49999 puis « P- » suivi de 2026.", "P-49'999 P-2026");
    PROG("Un client, conservé, a : un nom (texte).\nUne personne a : un nom.\nLe p vaut une nouvelle personne.\n"
         "Le nom du p devient 3.\nAfficher nom du p.", "3");
    {
        static const unsigned char PNG[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        creer_fichier("_essai_img.png", PNG, sizeof PNG);
        creer_fichier("_essai_doc.pdf", "%PDF-1.7", 8);
        PROG("Un client, conservé, a : une photo (image), un contrat (fichier).\nLe c vaut un nouveau client :\n"
             "    La photo vaut le fichier « _essai_img.png ».\n    Le contrat vaut le fichier « _essai_doc.pdf ».\n"
             "Afficher photo du c puis contrat du c.", "une image PNG de 8 octets un fichier de 8 octets");
        PROG("Un client, conservé, a : une photo (image).\nLe c vaut un nouveau client :\n"
             "    La photo vaut le fichier « _essai_doc.pdf ».",
             "ERREUR 3:5 « _essai_doc.pdf » n'est pas une image (PNG, JPEG, GIF ou WebP).");
        /* un son (§ 15.7) : reconnu à sa signature ; un autre fichier refusé */
        static const unsigned char WAV[] = { 'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
        creer_fichier("_essai_son.wav", WAV, sizeof WAV);
        creer_fichier("_essai_son.mp3", "ID3\x04\x00\x00\x00\x00\x00\x00", 10);
        creer_fichier("_essai_son.ogg", "OggS\x00\x02\x00\x00", 8);
        creer_fichier("_essai_son.flac", "fLaC\x00\x00\x00\x22", 8);
        PROG("Une visite, conservée, a : un mémo (son).\n"
             "Le a vaut une nouvelle visite :\n    Le mémo vaut le fichier « _essai_son.wav ».\n"
             "Le b vaut une nouvelle visite :\n    Le mémo vaut le fichier « _essai_son.mp3 ».\n"
             "Le c vaut une nouvelle visite :\n    Le mémo vaut le fichier « _essai_son.ogg ».\n"
             "Le d vaut une nouvelle visite :\n    Le mémo vaut le fichier « _essai_son.flac ».\n"
             "Afficher le format du mémo de a puis le format du mémo de b puis le format du mémo de c puis le format du mémo de d.",
             "WAV MP3 Ogg FLAC");
        PROG("Une visite, conservée, a : un mémo (son).\nLe v vaut une nouvelle visite :\n"
             "    Le mémo vaut le fichier « _essai_doc.pdf ».",
             "~« _essai_doc.pdf » n'est pas un son (WAV, MP3, M4A, Ogg, WebM ou FLAC).");
        PROG("Une visite, conservée, a : un mémo (son).\nLe v vaut une nouvelle visite :\n"
             "    Le mémo vaut le fichier « _essai_img.png ».",
             "~« _essai_img.png » n'est pas un son");   /* une image n'est pas un son */
        /* la transcription (§ 15.8) : le texte arrive dans la note, survit à la base, reste modifiable */
        PROG("Une visite, conservée, a :\n    un lieu (texte),\n    un mémo (son),\n    une note (texte), facultative, transcrite du mémo.\n"
             "Le v vaut une nouvelle visite :\n    Le lieu vaut « Bulle ».\n    Le mémo vaut le fichier « _essai_son.wav ».\n"
             "Afficher la note de v.\nConserver v.\n"
             "Pour chaque visite conservée :\n    Afficher la note de la visite.\n    La note de la visite devient « corrigée ».\n"
             "Pour chaque visite conservée :\n    Afficher la note de la visite.",
             "transcription de 16 octets\ntranscription de 16 octets\ncorrigée");
        remove("_essai_son.wav");
        remove("_essai_son.mp3");
        remove("_essai_son.ogg");
        remove("_essai_son.flac");
        remove("_essai_img.png");
        remove("_essai_doc.pdf");
    }

    /* --- La base : conserver, modifier, supprimer (§ 16.3), base en mémoire --- */
#define CLIENT "Un client, conservé, a : un nom (texte), un parrain (client), une licence (texte), unique.\n"
#define ANA "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A-1 ».\n    Le parrain vaut a.\n"
    PROG(CLIENT ANA "Afficher « avant ».", "ERREUR 5:21 « a » inconnu.");   /* un objet ne se désigne qu'une fois créé */
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A-1 ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe nom du a devient « Anna ».\nAfficher nom du a puis nom du parrain du a.",
         "Anna Anna");
    PROG("Une personne a : un nom.\nLa p vaut une nouvelle personne.\nConserver p.",
         "ERREUR 3:1 « personne » n'est pas une entité : ses objets ne se conservent pas. "
         "Déclare « Une personne, conservée, a : ».");
    PROG("Conserver 3.", "ERREUR 1:1 Seul un objet se conserve : la valeur est un nombre.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\nConserver a.",
         "ERREUR 4:1 Le champ « parrain » n'a pas de valeur : un client incomplet ne se conserve pas.");
    PROG(CLIENT "Le b vaut un nouveau client :\n    Le nom vaut « Bo ».\n    La licence vaut « B ».\n"
         "Le parrain du b devient b.\nLe a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "    Le parrain vaut b.\nConserver a.",
         "ERREUR 10:1 Le champ « parrain » désigne un client qui n'est pas conservé : conserve-le d'abord.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nConserver a.",
         "ERREUR 7:1 Un client déjà conservé ne se conserve pas deux fois.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « A ».\n    Le parrain vaut a.\nConserver b.",
         "ERREUR 11:1 « licence » est unique : un autre client conservé a déjà « A ».");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nLa licence du b devient « A ».",
         "ERREUR 12:1 « licence » est unique : un autre client conservé a déjà « A ».");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nSupprimer a définitivement.",
         "ERREUR 12:1 Ce client est encore désigné par le champ « parrain » d'un client.");
    /* la suppression simple ne casse rien : toujours permise, même désigné (§ 16.12) */
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nSupprimer a.\n"
         "Afficher le nombre de clients conservés puis nom du parrain du b.", "1 Ana");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a définitivement.\nSupprimer a.",
         "ERREUR 8:1 Un client qui n'est pas conservé ne se supprime pas.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a.\nSupprimer a.",
         "ERREUR 8:1 Ce client est déjà supprimé : « Supprimer … définitivement » l'efface de la base.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a définitivement.\nConserver a.\nAfficher nom du a.", "Ana");
    PROG("Une adhésion, conservée, a : un numéro (nombre entier).\nLa x vaut une nouvelle adhésion :\n"
         "    Le numéro vaut 99999999999999999999.\nConserver x.",
         "ERREUR 4:1 Le champ « numéro » est trop grand pour la base : un nombre entier y tient entre "
         "−9'223'372'036'854'775'808 et 9'223'372'036'854'775'807.");
    PROG("Une facture, conservée, a : un montant (nombre).\nUn avoir, conservé, est une facture.\n"
         "Un contrat, conservé, a : une facture (facture).\nL'a vaut un nouvel avoir :\n    Le montant vaut −5.\n"
         "Conserver a.\nLe c vaut un nouveau contrat :\n    La facture vaut a.\nConserver c.\nSupprimer a définitivement.",
         "ERREUR 10:1 Cet avoir est encore désigné par le champ « facture » d'un contrat.");
    /* une saisie ratée rend à l'objet son état « non conservé » */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Un client, conservé, a : un nom (texte).",
                                  "Le c vaut un nouveau client :\n    Le nom vaut « Ana ».",
                                  "Conserver c. Afficher 1 ÷ 0.", "Conserver c. Afficher « conservé »." };
        const char *attendus[] = { "", "", "~Division par zéro", "conservé" };
        for (int i = 0; i < 4; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }
    /* --- Retrouver (§ 16.4), base en mémoire --- */
#define RC "Un client, conservé, a : un nom (texte), un solde (nombre), un actif (vrai ou faux), un parrain (client), " \
           "une inscription (date), un rang (nombre entier).\n" \
           "Pour créer un nom et un solde et un actif et une inscription et un rang :\n" \
           "    Le c vaut un nouveau client :\n        Le nom vaut nom.\n        Le solde vaut solde.\n" \
           "        L'actif vaut actif.\n        L'inscription vaut inscription.\n        Le rang vaut rang.\n" \
           "    Le parrain du c devient c.\n    Conserver c.\nLe oui vaut vrai.\nLe non vaut faux.\n" \
           "Créer « Zoé » et −12,50 et oui et 01.03.2026 et 3.\nCréer « émile » et 10 et oui et 15.01.2025 et 1.\n" \
           "Créer « Ana » et 9 et non et 21.09.2026 et 10.\nCréer « Bob » et −3 et oui et 01.01.2026 et 2.\n" \
           "Créer « Élodie » et 100,000000000000000001 et oui et 31.12.2025 et 20.\n"
    PROG(RC "Pour chaque client conservé, par nom, afficher nom du client.", "Ana\nBob\nÉlodie\némile\nZoé");
    PROG(RC "Pour chaque client conservé, afficher nom du client.", "Zoé\némile\nAna\nBob\nÉlodie");
    PROG(RC "Pour chaque client conservé dont le nom contient « E », par nom, afficher nom du client.", "Élodie\némile\nZoé");
    PROG(RC "Afficher le nombre de clients conservés dont le nom commence par « é » puis le nombre de clients conservés "
         "dont le nom ne finit pas par « E ».", "2 2");
    /* Moyenne, plus grand, plus petit, objet au champ extrême (§ 16.4) */
    PROG(RC "Afficher la moyenne des rangs des clients conservés puis le plus grand solde des clients conservés "
         "puis le plus petit rang des clients conservés.", "7,2 100,000000000000000001 1");
    PROG(RC "Afficher la moyenne des soldes des clients conservés dont l'actif est vrai.", "23,62500000000000000025");
    PROG(RC "Afficher nom du client conservé au plus grand solde puis nom du client conservé au plus petit rang dont "
         "l'actif est faux.", "Élodie Ana");
    PROG(RC "Afficher la moyenne des soldes des clients conservés dont le rang est supérieur à 100.",
         "~Aucun client conservé n'a de « solde » : pas de moyenne.");
    PROG(RC "Afficher nom du client conservé au plus grand solde dont le rang est supérieur à 100.",
         "~Aucun client conservé répondant à la condition n'a de « solde ».");
    PROG(RC "Afficher la plus grand solde des clients conservés.", "~Accord : « le plus grand solde ».");
    PROG(RC "Afficher la moyenne des noms des clients conservés.",
         "~« nom » est un champ texte : « la moyenne des » s'applique à un nombre.");
    /* Exporter (§ 15.4) : un tableau CSV, écrit à la fin de l'exécution */
    remove("_exp.csv");
    PROG(RC "Exporter les clients conservés dont le rang est inférieur à 3, par nom, avec le nom, le solde, l'actif "
         "et le nom du parrain dans « _exp.csv ».", "");
    {
        FILE *f = fopen("_exp.csv", "rb");
        char t[512] = {0};
        size_t n = f ? fread(t, 1, sizeof t - 1, f) : 0;
        if (f) fclose(f);
        const char *att = "\xEF\xBB\xBF" "Nom;Solde;Actif;Nom du parrain\r\nBob;-3;vrai;Bob\r\némile;10;vrai;émile\r\n";
        total++;
        if (n != strlen(att) || memcmp(t, att, n) != 0) signaler(__LINE__, "export", att, t);
    }
    /* Un rapport (§ 15.6) : le total dans un CSV ; une page HTML, avec titre, nombres à droite, total, date d'édition */
    /* les marques invisibles de relecture (§ 15.8) ne passent pas dans un tableur */
    remove("_marques.csv");
    PROG("Une note, conservée, a :\n    un texte (texte).\nLe n vaut une nouvelle note :\n"
         "    Le texte vaut « \xe2\x81\xa4Pour \xe2\x81\xa2" "deux\xe2\x81\xa3 francs ».\nConserver n.\n"
         "Exporter les notes conservées dans « _marques.csv ».", "");
    {
        FILE *f = fopen("_marques.csv", "rb");
        char t[256] = {0};
        size_t nl = f ? fread(t, 1, sizeof t - 1, f) : 0;
        if (f) fclose(f);
        const char *att = "\xEF\xBB\xBF" "Texte\r\nPour deux francs\r\n";
        total++;
        if (nl != strlen(att) || memcmp(t, att, nl) != 0) signaler(__LINE__, "marques retirées", att, t);
        remove("_marques.csv");
    }
    remove("_tot.csv");
    PROG(RC "Exporter les clients conservés dont le rang est inférieur à 3, par nom, avec le nom et le solde, "
         "avec le total du solde, dans « _tot.csv ».", "");
    {
        FILE *f = fopen("_tot.csv", "rb");
        char t[512] = {0};
        size_t n = f ? fread(t, 1, sizeof t - 1, f) : 0;
        if (f) fclose(f);
        const char *att = "\xEF\xBB\xBF" "Nom;Solde\r\nBob;-3\r\némile;10\r\nTotal;7\r\n";
        total++;
        if (n != strlen(att) || memcmp(t, att, n) != 0) signaler(__LINE__, "export avec total", att, t);
        remove("_tot.csv");
    }
    remove("_rap.html");
    PROG(RC "Exporter les clients conservés dont le rang est inférieur à 3, par nom, avec le nom et le solde, "
         "avec le titre « Clients & soldes », avec le total du solde, dans « _rap.html ».", "");
    {
        FILE *f = fopen("_rap.html", "rb");
        char t[4096] = {0};
        if (f) { size_t n = fread(t, 1, sizeof t - 1, f); t[n] = 0; fclose(f); }
        const char *attendus[] = {"<h1>Clients &amp; soldes</h1>", "<th align=\"left\">Nom</th><th class=\"n\" align=\"right\">Solde</th>",
                                  "<td>émile</td><td class=\"n\" align=\"right\">10</td>",
                                  "<tr class=\"total\"><td><b>Total</b></td><td class=\"n\" align=\"right\"><b>7</b></td></tr>",
                                  "<p class=\"edite\">Édité le ", "thead{display:table-header-group}"};
        for (size_t i = 0; i < sizeof attendus / sizeof *attendus; i++) {
            total++;
            if (!strstr(t, attendus[i])) signaler(__LINE__, "rapport html", attendus[i], t);
        }
        remove("_rap.html");
    }
    PROG(RC "Exporter les clients conservés dans « _rap.pdf ».", "~Un PDF se fabrique dans l'atelier ou dans une application fabriquée");
    PROG(RC "Exporter les clients conservés, avec le nom, avec le total du solde, dans « _x.csv ».",
         "~Le total porte sur une colonne exportée : ajoute « solde » aux colonnes.");
    PROG(RC "Exporter les clients conservés, avec le titre 3, dans « _x.csv ».", "~Titre attendu");
    PROG(RC "Exporter les clients conservés dans « _exp.csv ».", "~« _exp.csv » existe déjà : il n'est jamais écrasé.");
    remove("_exp.csv");
    PROG(RC "Exporter les clients conservés dans « _exp.csv ».\nExporter les clients conservés dans « _exp.csv ».",
         "~« _exp.csv » est déjà enregistré par cette exécution.");
    {
        FILE *f = fopen("_exp.csv", "rb");
        total++;
        if (f) { fclose(f); signaler(__LINE__, "export raté", "aucun fichier", "un fichier écrit"); }
    }
    PROG(RC "Exporter les clients conservés, avec le nom et le prix dans « _exp.csv ».", "~Colonne « le prix »");
    PROG("Pour exporter les clients :\n    Afficher 1.\nExporter les clients.", "1");
    /* Lire un tableau (§ 15.5) */
    {
        FILE *f = fopen("_imp.csv", "wb");
        if (f) {
            fputs("\xEF\xBB\xBFNom;Cotisation;Ville\r\n\"M\xC3\xBCller; \xC3\x89lodie\";1234,50;Bulle\r\nBob;-3;\r\n\r\n", f);
            fclose(f);
        }
        f = fopen("_imp2.csv", "wb");
        if (f) { fputs("a,b\n1,2,3\n", f); fclose(f); }
        f = fopen("_imp3.csv", "wb");
        if (f) { fputs("x\ty\n\"1\"\"\"\t\"2\n", f); fclose(f); }
    }
    PROG("Un membre, conservé, a : un nom (texte), unique, une cotisation (nombre), une ville (texte), facultative.\n"
         "Pour chaque ligne du tableau « _imp.csv » :\n    Le m vaut un nouveau membre :\n"
         "        Le nom vaut la colonne « Nom » de la ligne.\n"
         "        La cotisation vaut la colonne « cotisation » de la ligne en nombre.\n"
         "    La ville du m devient la colonne « Ville » de la ligne.\n    Conserver m.\n    Afficher ligne.\n"
         "Pour chaque membre conservé, par nom, afficher nom du membre puis cotisation du membre puis ville du membre.",
         "Müller; Élodie ; 1234,50 ; Bulle\nBob ; -3 ; \nBob −3 absent\nMüller; Élodie 1'234,50 Bulle");
    PROG("Pour chaque l du tableau « _imp.csv », afficher la colonne « Pays » de l.",
         "ERREUR 1:49 Aucune colonne « Pays » dans ce tableau : Nom, Cotisation, Ville.");
    PROG("Pour chaque l du tableau « _imp2.csv », afficher l.", "ERREUR 1:1 « _imp2.csv », ligne 2 : 3 cellules pour 2 colonnes.");
    PROG("Pour chaque l du tableau « _imp3.csv », afficher l.", "ERREUR 1:1 « _imp3.csv » : un guillemet n'est pas refermé.");
    PROG("Le t vaut « a ».\nAfficher la colonne « x » de t.", "~« la colonne » se lit dans une ligne de tableau, pas dans un texte.");
    PROG("Le n de la ligne d'un texte vaut 1.\nLa colonne d'un x vaut 2.\nAfficher la colonne de 3.", "2");
    remove("_imp.csv");
    remove("_imp2.csv");
    remove("_imp3.csv");
    /* Valeurs permises (§ 16.15) */
    PROG("Une tâche, conservée, a : un titre (texte), un état (« à faire », « en cours » ou « fini »).\n" "Le t vaut une nouvelle tâche :\n    Le titre vaut « a ».\n    L'état vaut « à faire ».\n"
         "L'état du t devient « en cours ».\nConserver t.\nAfficher état de t puis le nombre de tâches conservées dont "
         "l'état est « en cours ».", "en cours 1");
    PROG("Une tâche, conservée, a : un titre (texte), un état (« à faire », « en cours » ou « fini »).\n" "Le e vaut « fermé ».\nLe t vaut une nouvelle tâche :\n    Le titre vaut « a ».\n    L'état vaut e.",
         "~« fermé » n'est pas une valeur permise de « état » : « à faire », « en cours » ou « fini ».");
    PROG("Une tâche, conservée, a : un titre (texte), un état (« à faire », « en cours » ou « fini »).\n" "Le t vaut une nouvelle tâche :\n    Le titre vaut « a ».\n    L'état vaut « fermé ».",
         "ERREUR 4:17 « fermé » n'est pas une valeur permise de « état » : « à faire », « en cours » ou « fini ».");
    PROG("Une tâche, conservée, a : un titre (texte), un état (« à faire », « en cours » ou « fini »).\n" "Afficher le nombre de tâches conservées dont l'état est « fermée ».",
         "ERREUR 2:57 « fermée » n'est pas une valeur permise de « état » : « à faire », « en cours » ou « fini ».");
    PROG("Une tâche, conservée, a : un état (« a »).", "~Au moins deux valeurs permises");
    PROG("Une tâche, conservée, a : un état (« a », « b », « a »).", "~« a » est déjà dans la liste.");
    PROG(RC "Pour chaque client conservé dont le solde contient « 1 », afficher 1.",
         "~contient, commence par et finit par s'appliquent à un texte");
    PROG(RC "Pour chaque client conservé, par solde décroissant, afficher solde du client.",
         "100,000000000000000001\n10\n9\n−3\n−12,50");
    PROG(RC "Pour chaque client conservé dont le solde est négatif et l'actif est vrai, par solde décroissant, "
         "afficher nom du client.", "Bob\nZoé");
    PROG(RC "Afficher le nombre de clients conservés dont le solde > 9 puis le nombre de clients conservés dont le solde ≥ 9 "
         "puis le nombre de clients conservés dont le solde = 10,00 puis le nombre de clients conservés.", "2 3 1 5");
    PROG(RC "Afficher le nombre de clients conservés dont l'inscription < 01.01.2026 puis "
         "le nombre de clients conservés dont le rang ≥ 2,5 puis le nombre de clients conservés dont l'actif est faux.",
         "2 3 1");
    PROG(RC "Pour chaque client conservé dont le nom > « b », par nom, afficher nom du client.", "Bob\nÉlodie\némile\nZoé");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\nAfficher solde du a puis parrain du a = a puis "
         "a = le client conservé dont le rang est 10.", "9 vrai vrai");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\nLe solde du a devient 1000.\n"
         "Afficher le nombre de clients conservés dont le solde ≥ 1000.", "1");
    PROG(RC "Afficher le client conservé dont le solde > 5.",
         "ERREUR 18:10 3 clients conservés répondent à cette condition : « le client conservé dont … » en attend un seul.");
    PROG(RC "Afficher le client conservé dont le nom est « Personne ».",
         "ERREUR 18:10 Aucun client conservé ne répond à cette condition.");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\n"
         "Afficher le nombre de clients conservés dont le parrain est a puis le nombre de clients conservés dont le parrain n'est pas a.",
         "1 4");
    PROG(RC "Le x vaut « 9 ».\nAfficher le nombre de clients conservés dont le solde = x.",
         "ERREUR 19:10 Le champ « solde » se compare à un nombre.");
    /* la liste d'une boucle est figée à son début ; sortir, passer */
    PROG(RC "Pour chaque client conservé, par nom :\n    Si nom du client = « Bob », passer au tour suivant.\n"
         "    Si nom du client = « émile », sortir de la boucle.\n    Créer « N » et 0 et oui et 01.01.2026 et 0.\n"
         "    Afficher nom du client.\nAfficher le nombre de clients conservés.", "Ana\nÉlodie\n7");
    PROG(RC "Le compte vaut 0.\nPour chaque client conservé :\n    Le d vaut un nouveau client :\n"
         "        Le nom vaut « X ».\n        Le solde vaut 0.\n        L'actif vaut vrai.\n        L'inscription vaut 01.01.2026.\n"
         "        Le rang vaut 0.\n    Le parrain du d devient d.\n    Conserver d.\n    Le compte devient compte + 1.\n"
         "Afficher compte puis le nombre de clients conservés.", "5 10");
    /* héritage : un membre est un client */
    PROG("Un client, conservé, a : un nom (texte).\nUn membre, conservé, est un client. Un membre a : une cotisation (nombre).\n"
         "Le c vaut un nouveau client :\n    Le nom vaut « C ».\nConserver c.\nLe m vaut un nouveau membre :\n"
         "    Le nom vaut « M ».\n    La cotisation vaut 5.\nConserver m.\n"
         "Pour chaque client conservé, par nom, afficher nom du client puis client.\n"
         "Pour chaque membre conservé dont le nom est « M », afficher cotisation du membre.",
         "C un client\nM un membre\n5");
    /* --- Champs facultatifs (§ 16.9) --- */
#define FAC "Un compositeur, conservé, a : un nom (texte), unique, un maître (compositeur), facultatif.\n" \
            "Une partition, conservée, a : un titre (texte), un arrangeur (compositeur), facultatif, " \
            "une édition (date), facultative, un tirage (nombre entier), facultatif.\n"
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nConserver p.\n"
         "Afficher arrangeur de p puis édition de p puis arrangeur de p est absent puis tirage de p est présent.",
         "absent absent vrai faux");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher tirage de p + 1.",
         "ERREUR 5:22 Le champ « tirage » est absent : vérifie-le d'abord avec « est présent ».");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher nom de l'arrangeur de p.",
         "ERREUR 5:10 Le champ « arrangeur » est absent : vérifie-le d'abord avec « est présent ».");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher édition de p = 01.01.2026.",
         "~Le champ « édition » est absent");
    PROG(FAC "Le c vaut un nouveau compositeur.\nLe nom du c devient absent.",
         "ERREUR 4:1 Le champ « nom » n'est pas facultatif : il ne devient pas absent.");
    /* deux objets neufs qui se désignent l'un l'autre : possible grâce au champ facultatif */
    PROG(FAC "Le a vaut un nouveau compositeur :\n    Le nom vaut « A ».\nConserver a.\n"
         "Le b vaut un nouveau compositeur :\n    Le nom vaut « B ».\n    Le maître vaut a.\nConserver b.\n"
         "Le maître du a devient b.\nAfficher nom du maître du maître du a.", "A");
    PROG(FAC "Pour créer un titre et une édition :\n    La p vaut une nouvelle partition :\n        Le titre vaut titre.\n"
         "    Si édition est présente, l'édition de p devient édition.\n    Conserver p.\n"
         "Créer « C » et 01.01.2000.\nCréer « A » et absente.\nCréer « B » et 01.01.1990.\n"
         "Pour chaque partition conservée, par édition, afficher titre de la partition.\n"
         "Pour chaque partition conservée, par édition décroissant, afficher titre de la partition.\n"
         "Afficher le nombre de partitions conservées dont l'édition est absente puis "
         "le nombre de partitions conservées dont l'édition < 01.01.2020.",
         "B\nC\nA\nC\nB\nA\n1 2");
    {   /* migration : un champ facultatif s'ajoute à une entité qui a déjà des objets */
        remove("_essai_fac.grymd");
        const char *etapes[][2] = {
            { "Un livre, conservé, a : un titre (texte).\nLe l vaut un nouveau livre :\n    Le titre vaut « X ».\nConserver l.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un auteur (texte), facultatif, un parrain (livre), facultatif.\n"
              "Afficher auteur du livre conservé dont le titre est « X » puis le nombre de livres conservés dont l'auteur est absent.\n",
              "absent 1" },
            { "Un livre, conservé, a : un titre (texte), facultatif, un auteur (texte), facultatif, un parrain (livre), facultatif.\n",
              "~« titre » ne peut pas devenir facultatif" },
        };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_fac.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_fac.grymd");
    }

    /* --- Relations inverses (§ 16.10) --- */
#define INV "Une personne, conservée, a : un nom (texte), unique.\n" \
            "Un membre, conservé, est une personne.\n" \
            "Une chanson, conservée, a : un titre (texte), un compositeur (personne), un auteur (personne), facultatif.\n" \
            "Une œuvre, conservée, a : un titre (texte), un compositeur (personne).\n" \
            "Pour ajouter un titre et une musique et une parole :\n    La c vaut une nouvelle chanson :\n" \
            "        Le titre vaut titre.\n        Le compositeur vaut musique.\n" \
            "    Si parole est présente, l'auteur de c devient parole.\n    Conserver c.\n" \
            "La brel vaut une nouvelle personne :\n    Le nom vaut « Brel ».\nConserver brel.\n" \
            "Le rauber vaut un nouveau membre :\n    Le nom vaut « Rauber ».\nConserver rauber.\n" \
            "Ajouter « Ne me quitte pas » et brel et brel.\nAjouter « La Valse » et brel et absent.\n" \
            "Ajouter « Madeleine » et rauber et brel.\n" \
            "La b vaut une nouvelle œuvre :\n    Le titre vaut « Sonate ».\n    Le compositeur vaut rauber.\nConserver b.\n"
    PROG(INV "Pour chaque œuvre de rauber, afficher titre de l'œuvre.\nAfficher le nombre d'œuvres de brel.", "Sonate\n0");
    PROG(INV "Pour chaque chanson conservée dont brel est l'auteur, par titre, afficher titre de la chanson.\n"
         "Afficher le nombre de chansons conservées dont brel est le compositeur et rauber n'est pas l'auteur.",
         "Madeleine\nNe me quitte pas\n1");
    PROG(INV "Afficher le nombre de chansons de brel.",
         "~Plusieurs champs d'une chanson peuvent désigner une personne : « compositeur » et « auteur ».");
    PROG(INV "Pour chaque œuvre de 3, afficher 1.",
         "~« de … » désigne un objet : un nombre, un texte ou une date n'a pas de liens.");
    PROG(INV "La x vaut une nouvelle chanson :\n    Le titre vaut « X ».\n    Le compositeur vaut brel.\n"
         "Afficher le nombre d'œuvres de l'auteur de x.", "~Le champ « auteur » est absent");
    PROG(INV "Pour chaque œuvre de b, afficher 1.", "~Aucun champ d'une œuvre ne peut désigner une œuvre");
    PROG(INV "Pour chaque œuvre de rauber dont le titre = « Sonate », par titre décroissant, afficher titre de l'œuvre.",
         "Sonate");

    /* --- Corbeille et cascade (§ 16.12) --- */
#define SD "Une partition, conservée, a : un titre (texte), une cote (texte), unique.\n" \
           "Un pupitre, conservé, a : une partition (partition), et disparaît avec elle, un instrument (texte).\n" \
           "Une note, conservée, a : une partition (partition), un texte (texte).\n" \
           "Pour créer un titre et une cote :\n    La p vaut une nouvelle partition :\n        Le titre vaut titre.\n" \
           "        La cote vaut cote.\n    Conserver p.\n    Le v vaut un nouveau pupitre :\n        La partition vaut p.\n" \
           "        L'instrument vaut « violon ».\n    Conserver v.\n" \
           "Créer « Offrande » et « A-1 ».\nCréer « Sonate » et « A-2 ».\n" \
           "La o vaut la partition conservée dont la cote est « A-1 ».\n" \
           "La s vaut la partition conservée dont la cote est « A-2 ».\n"
    PROG(SD "Supprimer o.\nAfficher le nombre de partitions conservées puis le nombre de partitions supprimées puis "
         "le nombre de pupitres conservés puis le nombre de pupitres supprimés puis titre de o.\n"
         "Pour chaque partition supprimée, afficher titre de la partition.\nRétablir o.\n"
         "Afficher le nombre de partitions conservées puis le nombre de pupitres conservés.",
         "1 1 1 1 Offrande\nOffrande\n2 2");
    PROG(SD "La n vaut une nouvelle note :\n    La partition vaut s.\n    Le texte vaut « x ».\nConserver n.\n"
         "Supprimer s.\nAfficher texte de n puis titre de la partition de n.\nSupprimer n.\nSupprimer s définitivement.",
         "~Cette partition est encore désignée par le champ « partition » d'une note supprimée : supprime-la "
         "définitivement d'abord.");
    PROG(SD "Supprimer s.\nLa m vaut une nouvelle note :\n    La partition vaut s.\n    Le texte vaut « y ».\nConserver m.",
         "~Le champ « partition » désignerait une partition supprimée : rétablis-la d'abord.");
    PROG(SD "Supprimer s.\nLa q vaut une nouvelle partition :\n    Le titre vaut « Z ».\n    La cote vaut « A-2 ».\nConserver q.",
         "~« cote » est unique : « A-2 » appartient à une partition supprimée. Rétablis-la, ou supprime-la définitivement.");
    PROG(SD "Rétablir s.", "~Cette partition n'est pas supprimée.");
    PROG(SD "Supprimer s.\nLe pu vaut le pupitre supprimé dont l'instrument est « violon ».\nRétablir pu.",
         "~Ce pupitre a disparu avec un autre objet : rétablis celui-là, et il reviendra avec lui.");
    PROG(SD "Supprimer s.\nSupprimer o définitivement.\nAfficher le nombre de partitions supprimées puis "
         "le nombre de pupitres conservés puis le nombre de pupitres supprimés.\nConserver o.\n"
         "Afficher le nombre de partitions conservées puis titre de o.",
         "1 0 1\n1 Offrande");
    {   /* une base d'avant la corbeille reçoit ses colonnes, et ses objets restent conservés */
        remove("_essai_corbeille.grymd");
        sqlite3 *db = NULL;
        sqlite3_open("_essai_corbeille.grymd", &db);
        sqlite3_exec(db, "CREATE TABLE grym_objet (id INTEGER PRIMARY KEY AUTOINCREMENT, classe TEXT NOT NULL);"
                         "CREATE TABLE grym_schema (entite TEXT PRIMARY KEY, definition TEXT NOT NULL);"
                         "INSERT INTO grym_objet (classe) VALUES ('livre');"
                         "CREATE TABLE \"e livre\" (id INTEGER PRIMARY KEY REFERENCES grym_objet(id) ON DELETE CASCADE,"
                         " \"c titre\" TEXT NOT NULL);"
                         "INSERT INTO \"e livre\" VALUES (1, 'X');"
                         "INSERT INTO grym_schema VALUES ('livre', 'parent=;titre:texte');", NULL, NULL, NULL);
        sqlite3_close(db);
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        machine_base(m, "_essai_corbeille.grymd");
        char *r = executer_source(p, m, "Un livre, conservé, a : un titre (texte).\n"
                                        "Afficher le nombre de livres conservés puis le nombre de livres supprimés.\n"
                                        "Supprimer le livre conservé dont le titre est « X ».\n"
                                        "Afficher le nombre de livres supprimés.", 0);
        if (strcmp(r, "1 0\n1") != 0) signaler(__LINE__, "base d'avant la corbeille", "1 0\n1", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_corbeille.grymd");
    }

    /* --- Relire, dans une autre exécution, ce qu'une première a conservé --- */
    {
        remove("_essai_relire.grymd");
        const char *decl = "Un client, conservé, a : un nom (texte), un parrain (client), une photo (fichier).\n"
                           "Un membre, conservé, est un client. Un membre a : une cotisation (nombre).\n";
        char *ecrire = grym_formater("%s"
            "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La photo vaut le fichier « _essai_relire.txt ».\n"
            "Le parrain du a devient a.\nConserver a.\n"
            "Le m vaut un nouveau membre :\n    Le nom vaut « Mo ».\n    Le parrain vaut a.\n"
            "    La photo vaut le fichier « _essai_relire.txt ».\n    La cotisation vaut 12,50.\nConserver m.\n", decl);
        char *lire = grym_formater("%s"
            "Pour chaque client conservé, par nom :\n"
            "    Afficher nom du client puis client puis nom du parrain du client puis photo du client.\n"
            "Le m vaut le membre conservé dont le nom est « Mo ».\nAfficher cotisation du m puis parrain du m = "
            "le client conservé dont le nom est « Ana ».\nLa cotisation du m devient 20.\n", decl);
        char *relire = grym_formater("%sAfficher cotisation du membre conservé dont le nom est « Mo ».\n"
            "Supprimer le membre conservé dont le nom est « Mo ».\nAfficher le nombre de clients conservés.\n", decl);
        creer_fichier("_essai_relire.txt", "bonjour", 7);
        const char *sources[] = { ecrire, lire, relire };
        const char *attendus[] = { "", "Ana un client Ana un fichier de 7 octets\nMo un membre Ana un fichier de 7 octets\n12,50 vrai",
                                   "20\n1" };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_relire.grymd");
            char *r = executer_source(p, m, sources[i], 0);
            if (strcmp(r, attendus[i]) != 0) signaler(__LINE__, sources[i], attendus[i], r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        free(ecrire);
        free(lire);
        free(relire);
        remove("_essai_relire.txt");
        remove("_essai_relire.grymd");
    }
    /* --- 2.36 : un champ facultatif retiré dont aucune valeur n'est présente part ; une seule valeur, et il reste --- */
    {
        remove("_essai_mig2.grymd");
        const char *etapes[][2] = {
            { "Une séance, conservée, a : un prix (nombre), un rappel (date), facultatif, une note (texte), facultative.\n"
              "Le s vaut une nouvelle séance :\n    Le prix vaut 10.\n    La note vaut « x ».\nConserver s.\n", "" },
            { "Une séance, conservée, a : un prix (nombre), une note (texte), facultative.\nAfficher le nombre de séances conservées.\n", "1" },
            { "Une séance, conservée, a : un prix (nombre).\n", "~« note » a disparu de « séance » : 1 valeur conservée serait perdue." },
        };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_mig2.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_mig2.grymd");
    }
    /* --- Migrations (§ 16.7) : une même base, des déclarations qui évoluent --- */
    {
        remove("_essai_mig.grymd");
        const char *etapes[][2] = {
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier).\n"
              "Le c vaut un nouveau client :\n    Le nom vaut « Ana ».\n    L'âge vaut 30.\nConserver c.\n", "" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier), un pays (texte).\n",
              "~« pays » est nouveau, et 1 client est déjà conservé : donne-lui une valeur de départ" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier), un pays (texte), « Suisse » au départ, "
              "un actif (vrai ou faux), vrai au départ.\nPour chaque client conservé, afficher pays du client puis actif du client.\n",
              "Suisse vrai" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux).\n"
              "Le a vaut le client conservé dont le nom est « Ana ».\nL'âge du a devient 30,5.\n"
              "Afficher le nombre de clients conservés dont l'âge > 30,25.\n", "1" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte).\n",
              "~« actif » a disparu de « client » : 1 valeur conservée serait perdue." },
            { "Un client, conservé, a : un nom (nombre), un âge (nombre), un pays (texte), un actif (vrai ou faux).\n",
              "~« nom » ne peut pas passer de « texte » à « nombre »" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux), "
              "un parrain (client).\n", "~un lien n'a pas de valeur de départ" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), unique, un actif (vrai ou faux).\n"
              "Le d vaut un nouveau client :\n    Le nom vaut « Bo ».\n    L'âge vaut 1.\n    Le pays vaut « Suisse ».\n"
              "    L'actif vaut faux.\nConserver d.\n", "~« pays » est unique : un autre client conservé a déjà « Suisse »." },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux).\n"
              "Afficher âge du client conservé dont le nom est « Ana » puis le nombre de clients conservés.\n", "30,5 1" },
        };
        for (int i = 0; i < 9; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_mig.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        total++;
        sqlite3 *db = NULL;
        sqlite3_stmt *st = NULL;
        char lu[100] = "";
        if (sqlite3_open("_essai_mig.grymd", &db) == SQLITE_OK
            && sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
            snprintf(lu, sizeof lu, "%s", (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        sqlite3_close(db);
        if (strcmp(lu, "ok") != 0) signaler(__LINE__, "intégrité après migrations", "ok", lu);
        remove("_essai_mig.grymd");
    }
    /* --- Message d'annulation (§ 3.3) --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, "Un client, conservé, a : un nom (texte).\nAfficher 1 ÷ 0.", 0);
        char *a = machine_annulation(m, 0), *b = machine_annulation(m, 1);
        if (!a || strcmp(a, "Exécution annulée : rien n'a été conservé dans la base.") != 0
            || strcmp(b, "Saisie annulée : aucun nom n'a changé, rien n'a été conservé dans la base.") != 0)
            signaler(__LINE__, "annulation", "Exécution annulée : rien n'a été conservé dans la base.", a ? a : "(rien)");
        free(r); free(a); free(b);
        machine_detruire(m);
        portee_detruire(p);
        p = portee_creer();
        m = machine_creer();
        r = executer_source(p, m, "Afficher 1 ÷ 0.", 0);
        a = machine_annulation(m, 0);
        total++;
        if (a) signaler(__LINE__, "annulation sans base", "(rien)", a);
        free(r); free(a);
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- La base dans un fichier : ce qui est validé y reste, le reste n'y entre pas --- */
    {
        remove("_essai.grymd");
        const char *prog = "Un client, conservé, a : un nom (texte), un solde (nombre), une date (date), un actif (vrai ou faux).\n"
                           "Le a vaut un nouveau client :\n    Le nom vaut « Thérèse ».\n    Le solde vaut 0,1.\n"
                           "    La date vaut 21.09.2026.\n    L'actif vaut vrai.\nConserver a.\n";
        const char *rate = "Un client, conservé, a : un nom (texte), un solde (nombre), une date (date), un actif (vrai ou faux).\n"
                           "Le b vaut un nouveau client :\n    Le nom vaut « Bo ».\n    Le solde vaut 1.\n"
                           "    La date vaut 01.01.2026.\n    L'actif vaut faux.\nConserver b.\nAfficher 1 ÷ 0.\n";
        const char *autre = "Un client, conservé, a : un nom (texte).\n";
        const char *sources[] = { prog, rate, autre };
        const char *attendus[] = { "", "~Division par zéro", "~« solde » a disparu de « client » : 1 valeur conservée serait perdue." };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai.grymd");
            char *r = executer_source(p, m, sources[i], 0);
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (!ok) signaler(__LINE__, sources[i], attendus[i], r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        total++;
        sqlite3 *db = NULL;
        sqlite3_stmt *st = NULL;
        char lu[200] = "";
        if (sqlite3_open("_essai.grymd", &db) == SQLITE_OK
            && sqlite3_prepare_v2(db, "SELECT (SELECT count(*) FROM grym_objet) || ' ' || \"c nom\" || ' ' || \"c solde\" "
                                      "|| ' ' || \"c date\" || ' ' || \"c actif\" FROM \"e client\"", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW)
            snprintf(lu, sizeof lu, "%s", (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        sqlite3_close(db);
        if (strcmp(lu, "1 Thérèse 0.1 2026-09-21 1") != 0) signaler(__LINE__, "contenu de la base", "1 Thérèse 0.1 2026-09-21 1", lu);
        remove("_essai.grymd");
    }

    /* --- Ramasse-miettes : cycles et objets abandonnés --- */
    {
        total++;
        const char *src =
            "Un maillon a : un autre.\nLe dernier vaut un nouveau maillon.\n"
            "Répéter 50000 fois :\n    Le a vaut un nouveau maillon.\n    Le b vaut un nouveau maillon.\n"
            "    L'autre du a devient b.\n    L'autre du b devient a.\n    Le dernier devient a.\n";
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, src, 0);
        size_t vivants = machine_objets_vivants(m);
        if (strcmp(r, "") != 0 || vivants != 2) {
            char *o = grym_formater("sortie « %s », %lu objets vivants", r, (unsigned long)vivants);
            signaler(__LINE__, "ramasse-miettes", "2 objets vivants (le dernier maillon et son vis-à-vis)", o);
            free(o);
        }
        free(r);
        machine_detruire(m);
        portee_detruire(p);
    }
    /* un champ modifié par une saisie ratée retrouve sa valeur */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Un client a : un solde.", "Le c vaut un nouveau client :\n    Le solde vaut 100.",
                                  "Le solde du c devient 5. Afficher 1 ÷ 0.", "solde du c" };
        const char *attendus[] = { "", "", "~Division par zéro", "100" };
        for (int i = 0; i < 4; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Une nouvelle version, ajoutée dans une saisie ultérieure --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Une personne a : un nom.\nUn membre est une personne.",
                                  "La sorte d'une personne vaut « personne ».", "Le m vaut un nouveau membre.",
                                  "sorte de m", "La sorte d'un membre vaut « membre ».", "sorte de m" };
        const char *attendus[] = { "", "", "", "personne", "", "membre" };
        for (int i = 0; i < 6; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            if (strncmp(r, "ERREUR", 6) == 0) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (strcmp(r, attendus[i]) != 0) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Formules dans la boucle interactive --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = {
            "Le carré d'un nombre vaut nombre × nombre.",
            "carré de 9",
            "Le cube d'un nombre vaut nombre × carré de nombre. Afficher 1 ÷ 0.",
            "cube de 2",
            "Le total vaut 1.",
            "Pour doubler :\n    Le total devient total × 2.",
            "Doubler.",
            "total",
            "Doubler. Le total devient total ÷ 0.",
            "total"
        };
        const char *attendus[] = { "", "81", "~Division par zéro", "~« cube de » inconnu", "", "", "", "2",
                                   "~Division par zéro", "2" };
        for (int i = 0; i < 10; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                            : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Module à plusieurs blocs : désassemblage et fichier --- */
    {
        total++;
        const char *src = "Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 3.";
        const char *attendu =
            "Programme\n"
            "   2  0000  CONSTANTE         0     ; 3\n"
            "      0003  APPELER           0     ; carré (1 argument, rend une valeur)\n"
            "      0008  AFFICHER          1\n"
            "      0011  RETOUR\n"
            "\n"
            "Calcul « carré » : 1 paramètre, 1 case locale\n"
            "   1  0000  LIRE_LOCAL        0     ; paramètre 1\n"
            "      0003  LIRE_LOCAL        0     ; paramètre 1\n"
            "      0006  MULTIPLICATION\n"
            "      0007  RENDRE\n";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        analyser(src, strlen(src), p, 0, &prog, &d);
        Module *b = compiler(&prog, &d);
        programme_liberer(&prog);
        portee_detruire(p);
        char *r = module_desassembler(b);
        if (strcmp(r, attendu) != 0) signaler(__LINE__, src, attendu, r);
        free(r);
        /* aller-retour et exécution du fichier relu */
        total++;
        size_t taille;
        unsigned char *octets = module_serialiser(b, &taille);
        char *err = NULL;
        Module *relu = module_lire(octets, taille, &err);
        Machine *m = machine_creer();
        Chaine s = {0};
        if (!relu || !machine_executer(m, relu, &s, &d)) signaler(__LINE__, "relecture", "9", err ? err : d.message);
        else if (strcmp(s.d, "9\n") != 0) signaler(__LINE__, "relecture", "9", s.d);
        free(s.d);
        free(err);
        machine_detruire(m);
        module_detruire(relu);
        free(octets);
        module_detruire(b);

        /* Sigles (2.21, § 2.4) : le module les emporte (SIGLES) ; relu dans un registre vide, la fiche les écrit */
        {
            const char *sg = "Un cabinet a : un numéro RCC, un nom.\n"
                             "Le c vaut un nouveau cabinet :\n    Le numéro RCC vaut « A1 ».\n    Le nom vaut « N ».\n"
                             "Afficher la fiche du c.\n";
            total++;
            Portee *ps = portee_creer();
            Programme ps_prog;
            analyser(sg, strlen(sg), ps, 0, &ps_prog, &d);
            Module *ms = compiler(&ps_prog, &d);
            programme_liberer(&ps_prog);
            portee_detruire(ps);
            char *dis = module_desassembler(ms);
            if (!strstr(dis, "SIGLES")) signaler(__LINE__, sg, "SIGLES", dis);
            free(dis);
            size_t ts;
            unsigned char *os = module_serialiser(ms, &ts);
            module_detruire(ms);
            sigles_oublier();
            char *es = NULL;
            Module *rs = module_lire(os, ts, &es);
            Machine *mm = machine_creer();
            Chaine ss = {0};
            if (!rs || !machine_executer(mm, rs, &ss, &d)) signaler(__LINE__, "sigles relus", "fiche", es ? es : d.message);
            else if (!strstr(ss.d, "Numéro RCC  A1")) signaler(__LINE__, "sigles relus", "Numéro RCC  A1", ss.d);
            free(ss.d);
            free(es);
            free(os);
            machine_detruire(mm);
            module_detruire(rs);
        }

        /* un fichier au format 1 (un seul bloc) reste lisible */
        total++;
        const unsigned char v1[] = { 'G','R','Y','M', 1,0, 0,0,0,0, 0,0,0,0, 1,0,0,0, I_RETOUR, 0,0,0,0 };
        Module *ancien = module_lire(v1, sizeof v1, &err);
        if (!ancien) signaler(__LINE__, "format 1", "lisible", err);
        free(err);
        err = NULL;
        module_detruire(ancien);

        /* appel d'une formule absente (fichier fabriqué) */
        total++;
        Bloc *x = bloc_creer();
        long k = bloc_nom(x, "fantôme");
        bloc_emettre_appel(x, (uint16_t)k, 0, 0, 1, 1);
        bloc_emettre(x, I_RETOUR, 0, 1, 1);
        Module *mx = module_creer();
        module_ajouter(mx, x);
        Machine *m2 = machine_creer();
        Chaine s2 = {0};
        if (machine_executer(m2, mx, &s2, &d) || !strstr(d.message, "Formule « fantôme » inconnue"))
            signaler(__LINE__, "formule absente", "Formule « fantôme » inconnue", d.message ? d.message : "(acceptée)");
        else diagnostic_liberer(&d);
        free(s2.d);
        machine_detruire(m2);
        module_detruire(mx);
    }

    /* --- Désassemblage (docs/vm.md, § 12) --- */
    {
        total++;
        const char *src =
            "Le prix unitaire vaut 12,50.\n"
            "La quantité vaut 3.\n"
            "Le total vaut prix unitaire × quantité.\n"
            "Afficher « Total : » puis −total.";
        const char *attendu =
            "   1  0000  CONSTANTE         0     ; 12,50\n"
            "      0003  ÉCRIRE            0     ; prix unitaire\n"
            "   2  0006  CONSTANTE         1     ; 3\n"
            "      0009  ÉCRIRE            1     ; quantité\n"
            "   3  0012  LIRE              0     ; prix unitaire\n"
            "      0015  LIRE              1     ; quantité\n"
            "      0018  MULTIPLICATION\n"
            "      0019  ÉCRIRE            2     ; total\n"
            "   4  0022  CONSTANTE         2     ; « Total : »\n"
            "      0025  LIRE              2     ; total\n"
            "      0028  NÉGATION\n"
            "      0029  AFFICHER          2\n"
            "      0032  RETOUR\n";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        char *r = NULL;
        if (analyser(src, strlen(src), p, 0, &prog, &d)) {
            Module *b = compiler(&prog, &d);
            r = module_desassembler(b);
            module_detruire(b);
            programme_liberer(&prog);
        } else {
            r = grym_dupliquer(d.message);
            diagnostic_liberer(&d);
        }
        if (strcmp(r, attendu) != 0) signaler(__LINE__, src, attendu, r);
        free(r);
        portee_detruire(p);
    }

    /* --- Fichier .grymb : aller-retour et fichiers corrompus (docs/vm.md, § 4 et 9) --- */
    {
        const char *src = "Le x vaut 2 ^ 10.\nAfficher « x = » puis x ÷ 4.";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        analyser(src, strlen(src), p, 0, &prog, &d);
        Module *b = compiler(&prog, &d);
        programme_liberer(&prog);
        portee_detruire(p);
        size_t taille;
        unsigned char *octets = module_serialiser(b, &taille);

        /* aller-retour : même désassemblage, même résultat */
        total++;
        char *err = NULL;
        Module *relu = module_lire(octets, taille, &err);
        char *d1 = module_desassembler(b), *d2 = relu ? module_desassembler(relu) : grym_dupliquer(err);
        if (strcmp(d1, d2) != 0) signaler(__LINE__, "aller-retour .grymb", d1, d2);
        free(d1); free(d2); free(err);
        total++;
        if (relu) {
            Machine *m = machine_creer();
            Chaine s = {0};
            machine_executer(m, relu, &s, &d);
            char *r = chaine_rendre(&s);
            if (strcmp(r, "x = 256\n") != 0) signaler(__LINE__, "exécution du .grymb relu", "x = 256", r);
            free(r);
            machine_detruire(m);
            module_detruire(relu);
        } else {
            signaler(__LINE__, "exécution du .grymb relu", "x = 256", "fichier illisible");
        }

        /* corruptions : chaque cas doit être refusé avec un message, sans planter */
        struct { const char *nom; size_t pos; int octet; size_t coupe; const char *fragment; } cas[] = {
            { "en-tête",           0, 'X', 0, "en-tête" },
            { "version",           4, 99,  0, "version" },
            { "fichier tronqué",   0, -1,  7, "tronqué" },
            { "octet en trop",     0, -1,  (size_t)-1, "en trop" },
        };
        for (size_t k = 0; k < sizeof cas / sizeof *cas; k++) {
            total++;
            size_t t = cas[k].coupe == (size_t)-1 ? taille + 1 : cas[k].coupe ? cas[k].coupe : taille;
            unsigned char *copie = grym_allouer(t);
            memcpy(copie, octets, t < taille ? t : taille);
            if (t > taille) copie[taille] = 0;
            if (cas[k].octet >= 0) copie[cas[k].pos] = (unsigned char)cas[k].octet;
            char *e = NULL;
            Module *x = module_lire(copie, t, &e);
            if (x || !e || !strstr(e, cas[k].fragment))
                signaler(__LINE__, cas[k].nom, cas[k].fragment, e ? e : "(accepté)");
            module_detruire(x);
            free(e);
            free(copie);
        }
        free(octets);

        /* code invalide : vérification avant exécution */
        struct { const char *nom; uint8_t code[16]; size_t n; const char *fragment; } codes[] = {
            { "code inconnu",       { 200 }, 1, "inconnu" },
            { "pile vide",          { I_ADDITION, I_RETOUR }, 2, "pile insuffisante" },
            { "constante absente",  { I_CONSTANTE, 7, 0, I_RETOUR }, 4, "inexistante" },
            { "sans RETOUR",        { I_CONSTANTE, 0, 0, I_AFFICHER, 1, 0 }, 6, "RETOUR" },
            { "pile non vide",      { I_CONSTANTE, 0, 0, I_RETOUR }, 4, "non vide" },
            { "saut hors code",     { I_SAUTER, 9, 0, 0, 0, I_RETOUR }, 6, "ne commence pas une instruction" },
            { "saut au milieu",     { I_SAUTER, 1, 0, 0, 0, I_RETOUR }, 6, "ne commence pas une instruction" },
            { "sortie sans RETOUR", { I_CONSTANTE, 0, 0, I_SAUTER_SI_FAUX, 11, 0, 0, 0 }, 8, "RETOUR" },
            { "pile incohérente",   { I_CONSTANTE, 0, 0, I_SAUTER_SI_FAUX, 11, 0, 0, 0,
                                      I_CONSTANTE, 0, 0, I_AFFICHER, 1, 0, I_RETOUR }, 15, "Bytecode invalide" },
            { "opérande tronqué",   { I_CONSTANTE, 0 }, 2, "tronqué" },
        };
        for (size_t k = 0; k < sizeof codes / sizeof *codes; k++) {
            total++;
            Bloc *x = bloc_creer();
            bloc_constante(x, C_NOMBRE, "1");
            x->code = grym_allouer(codes[k].n);
            memcpy(x->code, codes[k].code, codes[k].n);
            x->taille_code = x->cap_code = codes[k].n;
            Module *mx = module_creer();
            module_ajouter(mx, x);
            Machine *m = machine_creer();
            Chaine s = {0};
            Diagnostic dd;
            int ok = machine_executer(m, mx, &s, &dd);
            if (ok || !dd.message || !strstr(dd.message, codes[k].fragment))
                signaler(__LINE__, codes[k].nom, codes[k].fragment, dd.message ? dd.message : "(accepté)");
            if (!ok) diagnostic_liberer(&dd);
            free(s.d);
            machine_detruire(m);
            module_detruire(mx);
        }
        module_detruire(b);
    }

    /* --- Plusieurs vers plusieurs (§ 16.13) --- */
#define MUL "Une personne, conservée, a : un nom (texte), unique.\n" \
            "Un genre, conservé, a : un nom (texte).\n" \
            "Une œuvre, conservée, a : un titre (texte), un compositeur (personne), des interprètes (personne), " \
            "des genres (genre), des travaux (travail) (genre).\n" \
            "Pour créer un nom :\n    La p vaut une nouvelle personne :\n        Le nom vaut nom.\n    Conserver p.\n" \
            "Pour classer un nom :\n    Le g vaut un nouveau genre :\n        Le nom vaut nom.\n    Conserver g.\n" \
            "Créer « Bach ».\nCréer « Callas ».\nClasser « baroque ».\nClasser « sacré ».\n" \
            "La bach vaut la personne conservée dont le nom est « Bach ».\n" \
            "La callas vaut la personne conservée dont le nom est « Callas ».\n" \
            "Le baroque vaut le genre conservé dont le nom est « baroque ».\n" \
            "Le sacré vaut le genre conservé dont le nom est « sacré ».\n" \
            "L'o vaut une nouvelle œuvre :\n    Le titre vaut « Messe ».\n    Le compositeur vaut bach.\nConserver o.\n" \
            "Les genres de o gagnent baroque.\nLes genres de o gagnent sacré.\nLes genres de o gagnent baroque.\n" \
            "Les interprètes de o gagnent callas.\n"
    PROG(MUL "Pour chaque genre de o, par nom décroissant, afficher nom du genre.\nAfficher le nombre de genres de o.",
         "sacré\nbaroque\n2");
    PROG(MUL "Pour chaque interprète de o, afficher nom de l'interprète.\nAfficher le nombre d'interprètes de o.",
         "Callas\n1");
    PROG(MUL "Pour chaque œuvre de sacré, afficher titre de l'œuvre.",
         "~Plusieurs champs relient une œuvre à un genre : « genres » et « travaux ». "
         "Précise avec « dont … est parmi les genres » ou « dont … est parmi les travaux ».");
    PROG(MUL "Afficher le nombre d'œuvres de callas.",
         "~Plusieurs champs relient une œuvre à une personne : « compositeur » et « interprètes ». "
         "Précise avec « dont … est le compositeur » ou « dont … est parmi les interprètes ».");
    PROG(MUL "Afficher le nombre de travaux de o.\nPour chaque travail de o, afficher 1.", "0");
    PROG(MUL "Pour chaque œuvre conservée dont sacré est parmi les genres, afficher titre de l'œuvre.", "Messe");
    PROG(MUL "Les travaux de o gagnent sacré.\nPour chaque travail de o, afficher nom du travail.\n"
         "Afficher le nombre de genres de o.", "sacré\n2");
    PROG(MUL "Afficher le nombre d'œuvres conservées dont callas est parmi les interprètes puis "
         "le nombre d'œuvres conservées dont bach est parmi les interprètes puis "
         "le nombre d'œuvres conservées dont sacré n'est pas parmi les genres puis "
         "le nombre d'œuvres conservées dont bach est le compositeur et baroque est parmi les genres.", "1 0 0 1");
    PROG(MUL "Les genres de o perdent sacré.\nLes genres de o perdent sacré.\nAfficher le nombre de genres de o.", "1");
    /* corbeille : un genre supprimé disparaît des lectures et ne se gagne pas */
    PROG(MUL "Supprimer sacré.\nAfficher le nombre de genres de o.\nRétablir sacré.\nAfficher le nombre de genres de o.",
         "1\n2");
    PROG(MUL "Supprimer sacré.\nLes genres de o gagnent sacré.",
         "~Le champ « genres » gagnerait un genre supprimé : rétablis-le d'abord.");
    /* effacer : une œuvre emporte ses liaisons ; un genre encore gagné ne s'efface pas */
    PROG(MUL "Supprimer baroque définitivement.",
         "~Ce genre est encore désigné par le champ « genres » d'une œuvre.");
    PROG(MUL "Supprimer o définitivement.\nSupprimer baroque définitivement.\nAfficher le nombre de genres conservés.", "1");
    PROG(MUL "Le g vaut un nouveau genre :\n    Le nom vaut « x ».\nLes genres de o gagnent g.",
         "~Le champ « genres » gagnerait un genre qui n'est pas conservé : conserve-le d'abord.");
    PROG(MUL "La n vaut une nouvelle œuvre :\n    Le titre vaut « N ».\n    Le compositeur vaut bach.\n"
         "Les genres de n gagnent baroque.",
         "~Une œuvre qui n'est pas conservée ne gagne rien : ses « genres » vivent dans la base.");
    PROG(MUL "Les genres de o gagnent bach.", "~Le champ « genres » attend un genre, pas une personne.");
    PROG(MUL "Les genres de o gagnent 3.", "~Le champ « genres » attend un genre, pas un nombre.");
    PROG(MUL "Afficher le genres de o.", "~« genres » est un champ multiple, pas une valeur");
    /* un champ multiple qui porte le nom d'une entité : repli sur la relation inverse quand l'objet ne l'a pas */
    PROG("Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), un genre (genre).\n"
         "Une personne, conservée, a : un nom (texte), des œuvres (œuvre).\n"
         "Le g vaut un nouveau genre :\n    Le nom vaut « b ».\nConserver g.\n"
         "L'o vaut une nouvelle œuvre :\n    Le titre vaut « O ».\n    Le genre vaut g.\nConserver o.\n"
         "La p vaut une nouvelle personne :\n    Le nom vaut « P ».\nConserver p.\n"
         "Pour chaque œuvre de g, afficher titre de l'œuvre.\nAfficher le nombre d'œuvres de p.\n"
         "Les œuvres de p gagnent o.\nPour chaque œuvre de p, afficher titre de l'œuvre.\n"
         "Afficher le nombre de personnes conservées dont o est parmi les œuvres.", "O\n0\nO\n1");
    {   /* migrations : ajout sur une entité peuplée, retrait refusé tant qu'une liaison existe */
        remove("_essai_mul.grymd");
        const char *etapes[][2] = {
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\n"
              "L'o vaut une nouvelle œuvre :\n    Le titre vaut « X ».\nConserver o.\n", "" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), des genres (genre).\n"
              "Le g vaut un nouveau genre :\n    Le nom vaut « b ».\nConserver g.\n"
              "Les genres de l'œuvre conservée dont le titre est « X » gagnent g.\n"
              "Afficher le nombre de genres de l'œuvre conservée dont le titre est « X ».\n", "1" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\n",
              "~« genres » a disparu de « œuvre » : 1 liaison serait perdue." },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), un genres (genre), facultatif.\n",
              "~« genres » ne peut pas devenir un lien simple" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), des genres (genre).\n"
              "Les genres de l'œuvre conservée dont le titre est « X » perdent le genre conservé dont le nom est « b ».\n", "" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\nAfficher 1.\n", "1" },
        };
        for (int i = 0; i < 6; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_mul.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_mul.grymd");
    }

    /* --- Années (§ 14.5) --- */
#define AN "Une œuvre, conservée, a : un titre (texte), une composition (année), une révision (année), facultative.\n" \
           "Pour créer un titre et une année :\n    L'o vaut une nouvelle œuvre :\n        Le titre vaut titre.\n" \
           "        La composition vaut année.\n    Conserver o.\n" \
           "Créer « A » et 1747.\nCréer « B » et l'année de 21.09.1685.\nCréer « C » et 12.\n" \
           "L'a vaut l'œuvre conservée dont le titre est « A ».\n"
    PROG(AN "Afficher composition de a puis composition de a + 3 puis composition de a − 1000 puis "
         "composition de a − composition de l'œuvre conservée dont le titre est « B ».", "1747 1750 747 62");
    PROG(AN "Afficher composition de a > 1700 puis 1747 = composition de a puis composition de a ≠ 1747,0 puis "
         "l'année de 31.12.2026 puis révision de a.", "vrai vrai faux 2026 absent");
    PROG(AN "Pour chaque œuvre conservée dont la composition < 1700, par composition décroissant, afficher titre de l'œuvre.\n"
         "Afficher le nombre d'œuvres conservées dont la composition ≥ 12.", "B\nC\n3");
    PROG(AN "Pour chaque an de composition de a à 1749, afficher an.\nPour chaque an de 1749 à composition de a par pas de −2, "
         "afficher an.", "1747\n1748\n1749\n1749\n1747");
    PROG(AN "Selon composition de a :\n    Cas de 1700 à 1750, afficher « baroque ».\n    Autrement, afficher « autre ».", "baroque");
    PROG("Le premier d'un d vaut l'année de d.\nAfficher le premier de 01.01.2000.", "2000");
    PROG(AN "Afficher composition de a + composition de a.", "~On n'additionne pas deux années.");
    PROG(AN "Afficher composition de a × 2.", "~On ne multiplie pas une année.");
    PROG(AN "Afficher −composition de a.", "~On ne prend pas l'opposé d'une année.");
    PROG(AN "Afficher 3 − composition de a.", "~On ne soustrait pas une année d'un nombre.");
    PROG(AN "Afficher composition de a + 0,5.", "~Une année se décale d'un nombre entier d'années.");
    PROG(AN "Afficher composition de a + 8300.", "~Année hors du calendrier : de 1 à 9999.");
    PROG(AN "Afficher composition de a < 01.01.2000.", "~Une année ne se compare pas à une date");
    PROG(AN "Afficher composition de a = « 1747 ».", "~Comparaison impossible entre une année et un texte.");
    PROG(AN "Le x vaut 2,5.\nLa composition de a devient x.", "~Le champ « composition » attend une année (de 1 à 9999), pas 2,5.");
    PROG(AN "Le x vaut 0.\nLa composition de a devient x.", "~attend une année (de 1 à 9999), pas 0.");
    PROG("Afficher le mois de 01.01.2000.", "1");   /* § 14.8 : le mois d'une date */
    /* § 14.8 et § 14.6 : valeurs confirmées par le module datetime de Python */
    PROG("Le a vaut 16.10.2026.\nAfficher le jour de la semaine de a puis le mois de a puis le jour de a.", "vendredi 10 16");
    PROG("Afficher le jour de la semaine de 29.02.2024 puis le jour de la semaine de 01.01.1900 puis le jour de la semaine de 14.07.1789.",
         "jeudi lundi mardi");
    PROG("Afficher 16.10.2026 en toutes lettres.\nAfficher 01.08.2026 en toutes lettres.", "vendredi 16 octobre 2026\nsamedi 1er août 2026");
    PROG("Afficher le nombre de jours ouvrables entre 12.10.2026 et 16.10.2026.\n"
         "Afficher le nombre de jours ouvrables entre 16.10.2026 et 19.10.2026.\n"
         "Afficher le nombre de jours ouvrables entre 01.01.2026 et 31.12.2026.\n"
         "Afficher le nombre de jours ouvrables entre 19.10.2026 et 16.10.2026.", "4\n1\n260\n−1");
    PROG("Afficher « x » en toutes lettres.", "~« en toutes lettres » s'applique à une date, pas à un texte.");
    PROG("Le d vaut 01.01.2000.\nAfficher taille de d.", "~Une date n'a pas de champ « taille » : « année », « mois », « jour » ou « jour de la semaine ».");
    PROG("Un p a : une taille.\nLa q vaut un nouveau p :\n    La taille vaut 3.\nAfficher 1747 puis taille de q.", "1747 3");
    {   /* migrations : nombre entier ↔ année */
        remove("_essai_an.grymd");
        const char *etapes[][2] = {
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier).\n"
              "Le l vaut un nouveau livre :\n    Le titre vaut « X ».\n    L'an vaut 20000.\nConserver l.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un an (année).\n",
              "~« an » ne peut pas devenir une année : 1 valeur conservée est hors de 1 à 9999." },
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier).\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nL'an du l devient 1990.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un an (année), une sortie (année), 2000 au départ.\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nAfficher an du l + 1 puis sortie du l.\n", "1991 2000" },
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier), une sortie (année).\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nAfficher an du l + 1.\n", "1991" },
        };
        for (int i = 0; i < 5; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_an.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_an.grymd");
    }

    /* --- Questions à l'utilisateur (§ 17) --- */
#define SAISIE_INTER(src, att)  verifier(__LINE__, src, att, 1)
#define SAISIE(src, att, ...) do { static const char *const L[] = { __VA_ARGS__ }; \
    verifier_saisie(__LINE__, src, L, sizeof L / sizeof *L, att); } while (0)
    /* Le formulaire déduit demande aussi le courriel, le téléphone, l'adresse web : des textes vérifiés (§ 22.13) ;
       un mot de passe facultatif n'y passe pas (il ne serait pas haché) */
    SAISIE("Un contact, conservé, a :\n    un nom (texte), unique,\n    un courriel (courriel), facultatif,\n"
           "    un portable (téléphone),\n    un site (adresse web), facultatif,\n    un code (mot de passe), facultatif.\n"
           "Le c vaut un nouveau contact saisi.\nConserver c.\n"
           "Afficher le nom du c puis le courriel du c puis le portable du c.\n",
           "~n'est pas un courriel (nom@domaine.ch). Tape « . » seul pour annuler.\nCourriel ? Portable ? « 12 » n'est pas "
           "un numéro de téléphone (+41 26 912 34 56).\nPortable ? Site ? Anne anne@tilleul.ch "
           "079 123 45 67",
           "Anne", "anne@", "anne@tilleul.ch", "12", "079 123 45 67", "");
    SAISIE("Le nom vaut la réponse à « Nom ? ».\nAfficher « Bonjour » puis nom.", "Nom ? Bonjour Ana", "  Ana  ");
    SAISIE("Une tâche, conservée, a : un titre (texte), un état (« à faire », « en cours » ou « fini »).\n" "Le t vaut une nouvelle tâche saisie.\nAfficher état de t.",
           "~État (« à faire », « en cours » ou « fini ») ? en cours", "Courses", "fermé", "en cours");
    SAISIE("L'âge vaut la réponse en nombre entier à « Âge ? ».\nAfficher âge + 1.", "Âge ? 34", "33");
    SAISIE("Le x vaut la réponse en nombre à « x ? ».\nAfficher x × 2.", "x ? 2'469,00", "1'234,50");
    SAISIE("Le d vaut la réponse en date à « Né le ? ».\nAfficher d + 1.", "Né le ? 02.02.1990", "01.02.1990");
    SAISIE("L'an vaut la réponse en année à « An ? ».\nAfficher an.", "An ? 1747", "1747");
    SAISIE("Si la réponse en vrai ou faux à « Sûr ? », afficher « oui ». Sinon, afficher « non ».",
           "Sûr ? non", "NON");
    SAISIE("La q vaut « Qui ? ».\nAfficher la réponse à (q).", "Qui ? Ana", "Ana");
    /* relance : la réponse fautive est annoncée, la question se repose */
    SAISIE("L'âge vaut la réponse en nombre entier à « Âge ? ».\nAfficher âge.",
           "Âge ? « x » n'est pas un nombre. Tape « . » seul pour annuler.\nÂge ? « 2,5 » n'est pas un nombre entier.\nÂge ? 7", "x", "2,5", "7");
    SAISIE("Le d vaut la réponse en date à « Jour ? ».\nAfficher d.",
           "Jour ? « 21.9.26 » n'est pas une date : écris jour.mois.année (21.09.2026). Tape « . » seul pour annuler.\nJour ? 21.09.2026",
           "21.9.26", "21.09.2026");
    SAISIE("L'an vaut la réponse en année à « An ? ».\nAfficher an.",
           "An ? « 12000 » n'est pas une année : de 1 à 9999. Tape « . » seul pour annuler.\nAn ? 1900", "12000", "1900");
    SAISIE("Le x vaut la réponse en vrai ou faux à « ? ».\nAfficher x.",
           "? Réponds par oui ou non. Tape « . » seul pour annuler.\n? vrai", "peut-être", "vrai");
    SAISIE("Le x vaut la réponse en nombre à « ? ».\nAfficher x.", "? Une réponse est attendue. Tape « . » seul pour annuler.\n? 3", "", "3");
    SAISIE("Le x vaut la réponse à « ? ».\nAfficher « [ » puis x puis « ] ».", "? [  ]", "");
    /* fin de l'entrée, pureté, boucle interactive */
    {   /* fin de l'entrée : aucune réponse à lire */
        static const char *const L[] = { "" };
        verifier_saisie(__LINE__, "Le x vaut la réponse à « Nom ? ».\nAfficher x.", L, 0,
                        "~Plus rien à lire : la réponse à « Nom ? » manque.");
    }
    PROG("Le x vaut la réponse à « Nom ? ».\nAfficher x.", "~Aucune entrée : la question ne peut pas être posée ici.");
    PROG("Le double d'un x vaut la réponse en nombre à « ? ».\nAfficher le double de 1.",
         "~Un calcul ne pose pas de question : demande dans une action.");
    SAISIE_INTER("Le x vaut la réponse à « ? ».", "~La question se pose dans un programme lancé, pas dans la boucle interactive.");
    {   /* une question valide ce qui la précède ; l'erreur suivante n'annule que depuis là */
        remove("_essai_q.grymd");
        static const char *const L[] = { "Ana", "Bob" };
        const char *src = "Un client, conservé, a : un nom (texte).\n"
                          "Le a vaut un nouveau client :\n    Le nom vaut la réponse à « 1 ? ».\nConserver a.\n"
                          "Le b vaut un nouveau client :\n    Le nom vaut la réponse à « 2 ? ».\nConserver b.\n"
                          "Afficher 1 ÷ 0.\n";
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        Script sc = { L, 2, 0 };
        machine_lecteur(m, reponses, &sc);
        machine_base(m, "_essai_q.grymd");
        char *r = executer_source(p, m, src, 0);
        char *annule = machine_annulation(m, 0);
        int ok = strstr(r, "Division par zéro") != NULL && annule
                 && strcmp(annule, "Exécution annulée : rien n'a été conservé depuis la dernière question.") == 0;
        if (!ok) signaler(__LINE__, src, "annulation depuis la dernière question", annule ? annule : r);
        free(annule);
        free(r);
        machine_detruire(m);
        portee_detruire(p);

        total++;
        p = portee_creer();
        m = machine_creer();
        machine_base(m, "_essai_q.grymd");
        r = executer_source(p, m, "Un client, conservé, a : un nom (texte).\n"
                                  "Pour chaque client conservé, par nom, afficher nom du client.", 0);
        if (strcmp(r, "Ana") != 0) signaler(__LINE__, "clients conservés", "Ana", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_q.grymd");
    }

    /* --- Mise en forme de l'affichage (§ 4.1, § 4.2) --- */
    PROG("Afficher « Berthoud » sur 5 puis « |ab ».", "Berth |ab");
    PROG("Afficher « ab » sur 5 puis « | ».", "ab    |");
    PROG("Afficher « ab » sur 5 à droite puis « | ».", "   ab |");
    PROG("Afficher 12,5 sur 6 puis « | ».", "  12,5 |");
    PROG("Afficher 12,5 sur 6 à gauche puis « | ».", "12,5   |");
    PROG("Afficher « été » sur 4 puis « | ».", "été  |");   /* caractères, pas octets */
    PROG("Le l vaut 3.\nAfficher « ab » sur l + 1 puis « | ».", "ab   |");
    PROG("Afficher « a » sur 3, sans passer à la ligne.\nAfficher « b ».", "a  b");
    PROG("Afficher « a ».\nAfficher « ».\nAfficher « b ».", "a\n\nb");
    PROG("Le x vaut « ab » sur 4.\nAfficher x puis « | ».", "ab   |");
    PROG("Afficher 1 sur 0.", "~Largeur invalide : un nombre entier de 1 à 1000 est attendu.");
    PROG("Afficher 1 sur 2,5.", "~Largeur invalide");
    PROG("Afficher 1 sur 1001.", "~Largeur invalide");
    PROG("Afficher 1 sur « a ».", "~Largeur invalide");
    /* style des nombres (§ 4.1) */
    PROG("Afficher 1234567,25.", "1'234'567,25");
    PROG("Les nombres s'affichent sans séparateur.\nAfficher 1234567,25.", "1234567,25");
    PROG("Les nombres s'affichent à la suisse.\nAfficher 1234567,25.", "1'234'567,25");
    PROG("Les nombres s'affichent à la française.\nAfficher 1234567,25 puis −1000.", "1\u202f234\u202f567,25 −1000");
    PROG("Les nombres s'affichent à la française.\nLe d vaut 01.02.1990.\nAfficher l'année de d puis d.",
         "1990 01.02.1990");   /* une année et une date gardent leur forme */
    PROG("Afficher 1.\nLes nombres s'affichent à la suisse.", "~se déclare avant le premier affichage");
    PROG("Les nombres s'affichent à la suisse.\nLes nombres s'affichent à la française.", "~une seule fois");
    PROG("Pour f :\n    Les nombres s'affichent à la suisse.", "~au premier niveau du programme");
    PROG("Les nombres s'affichent à la belge.", "~Style attendu");
    /* La somme (§ 16.4) : décimal exact, dont, lien inverse, pluriel au premier mot, 0 sur rien ; les refus */
#define PUPITRES "Une partition, conservée, a :\n    une cote (texte), unique.\n" \
         "Un pupitre, conservé, a :\n    une partition (partition),\n    un instrument (texte),\n" \
         "    un nombre de parties (nombre entier),\n    un prix (nombre), facultatif.\n" \
         "Pour placer une partition et un instrument et un nombre et un prix :\n    Le n vaut un nouveau pupitre :\n" \
         "        La partition vaut partition.\n        L'instrument vaut instrument.\n        Le nombre de parties vaut nombre.\n" \
         "        Le prix vaut prix.\n    Conserver le n.\n" \
         "La p vaut une nouvelle partition :\n    La cote vaut « A-1 ».\nConserver la p.\n" \
         "Placer la p et « violon » et 12 et 0,10.\nPlacer la p et « alto » et 6 et 0,20.\n" \
         "Placer la p et « violoncelle » et 5 et 1234,70.\n"
    PROG(PUPITRES "Afficher la somme des nombres de parties des pupitres conservés.", "23");
    PROG(PUPITRES "Afficher la somme des prix des pupitres conservés.", "1'235,00");
    PROG(PUPITRES "Afficher la somme des nombres de parties des pupitres de la p.", "23");
    PROG(PUPITRES "Afficher la somme des nombres de parties des pupitres conservés dont l'instrument ≠ « alto ».", "17");
    PROG(PUPITRES "Afficher la somme des prix des pupitres conservés dont l'instrument = « contrebasse ».", "0");
    PROG(PUPITRES "Si la somme des prix des pupitres conservés > 1000, afficher « cher ».", "cher");
    PROG(PUPITRES "Afficher la somme des instruments des pupitres conservés.", "~« instrument » est un champ texte : seul un nombre s'additionne.");
    /* un calcul lit la base (§ 9.4) : recherche, somme, nombre, boucle ; jamais il n'y écrit */
    PROG(PUPITRES "Le total d'un n vaut la somme des prix des pupitres conservés + n.\nAfficher le total de 1.", "1'236,00");
    PROG(PUPITRES "Le prix total d'une partition vaut la somme des prix des pupitres de partition.\nAfficher le prix total de p.", "1'235,00");
    PROG(PUPITRES "Le compte d'un n :\n    Le r vaut 0.\n    Pour chaque pupitre conservé dont le nombre de parties > n, le r devient r + 1.\n    Rendre r.\n"
                  "Afficher le compte de 5.", "2");
    PROG(PUPITRES "Le premier d'un n vaut la partition conservée dont la cote est « A-1 ».\nAfficher cote du premier de 0.", "A-1");
    PROG(PUPITRES "Le f d'un x :\n    Supprimer x.\n    Rendre 1.", "~Un calcul ne");
    PROG(PUPITRES "La somme des prix vaut 3.\nAfficher la somme des prix.", "3");   /* un nom ordinaire reste un nom */
#undef PUPITRES

#define ADHESIONS "Une catégorie, conservée, a :\n    un nom (texte), unique.\n" \
    "Un membre, conservé, a :\n    un nom (texte),\n    une catégorie (catégorie), facultative.\n" \
    "Une cotisation, conservée, a :\n    un membre (membre),\n    un montant (nombre).\n" \
    "La j vaut une nouvelle catégorie :\n    Le nom vaut « Junior ».\nConserver la j.\n" \
    "L'm1 vaut un nouveau membre :\n    Le nom vaut « Élodie ».\n    La catégorie vaut j.\nConserver l'm1.\n" \
    "L'm2 vaut un nouveau membre :\n    Le nom vaut « Sans ».\nConserver l'm2.\n" \
    "Pour chaque membre conservé :\n    La c vaut une nouvelle cotisation :\n        Le membre vaut membre.\n        Le montant vaut 40,00.\n    Conserver la c.\n"
    /* Un champ lu à travers un lien, dans une condition dont (§ 16.4) : un lien absent rend la condition fausse */
    PROG(ADHESIONS "Pour chaque cotisation conservée dont le nom de la catégorie du membre est « Junior », afficher nom du membre de la cotisation.", "Élodie");
    PROG(ADHESIONS "Afficher le nombre de cotisations conservées dont le nom de la catégorie du membre ≠ « Junior ».", "0");
    PROG(ADHESIONS "Afficher le nombre de cotisations conservées dont la catégorie du membre est absente.", "1");
    PROG(ADHESIONS "Afficher la somme des montants des cotisations conservées dont le nom du membre = « Élodie ».", "40,00");
    PROG(ADHESIONS "Afficher le nombre de cotisations conservées dont la couleur du membre est « x ».", "~« couleur » n'est pas un champ du membre.");
    PROG(ADHESIONS "Afficher le nombre de cotisations conservées dont le nom du montant est « x ».", "~« montant » n'est pas un lien vers une entité");
    PROG(ADHESIONS "Afficher le nombre de cotisations conservées dont la nom de la catégorie du membre = « x ».", "~« nom » est un champ masculin.");
#undef ADHESIONS

#define COMPTA "Une écriture, conservée, a :\n    une pièce (texte), unique,\n    une comptabilisation (date), facultative.\n" \
    "Une ligne, conservée, a :\n    une écriture (écriture),\n    un débit (nombre),\n    un crédit (nombre).\n" \
    "Chaque écriture conservée dont la comptabilisation est présente vérifie :\n" \
    "    la somme des débits des lignes de l'écriture = la somme des crédits des lignes de l'écriture,\n" \
    "    sinon « L'écriture » puis la pièce de l'écriture puis « n'est pas équilibrée. ».\n" \
    "Pour noter une pièce et un débit et un crédit et une date :\n" \
    "    L'e vaut une nouvelle écriture :\n        La pièce vaut pièce.\n    Conserver l'e.\n" \
    "    La l vaut une nouvelle ligne :\n        L'écriture vaut e.\n        Le débit vaut débit.\n        Le crédit vaut 0.\n    Conserver la l.\n" \
    "    La m vaut une nouvelle ligne :\n        L'écriture vaut e.\n        Le débit vaut 0.\n        Le crédit vaut crédit.\n    Conserver la m.\n" \
    "    La comptabilisation de l'e devient date.\n"
    /* Contrainte d'entité (§ 16.14) : vérifiée à la fin de la transaction, sur les objets que le filtre retient */
    PROG(COMPTA "Noter « P-1 » et 300 et 300 et 01.01.2026.\nAfficher « ok ».", "ok");
    PROG(COMPTA "Noter « P-1 » et 300 et 30 et 01.01.2026.\nAfficher « ok ».", "~L'écriture P-1 n'est pas équilibrée.");
    PROG(COMPTA "L'e vaut une nouvelle écriture :\n    La pièce vaut « B-1 ».\nConserver l'e.\n"
                "La l vaut une nouvelle ligne :\n    L'écriture vaut e.\n    Le débit vaut 5.\n    Le crédit vaut 0.\nConserver la l.\n"
                "Afficher « brouillon ».", "brouillon");   /* sans comptabilisation, le filtre l'écarte */
    /* un Essayer ne contourne pas la règle : elle attend la fin de la transaction, pas celle de l'essai */
    PROG(COMPTA "Essayer :\n    Noter « P-1 » et 300 et 30 et 01.01.2026.\nEn cas d'échec, afficher « rattrapé ».\nAfficher « suite ».",
         "~n'est pas équilibrée.");
    /* deux règles ; la seconde, sans filtre */
    PROG(COMPTA "Chaque ligne conservée vérifie :\n    le débit de la ligne = 0 ou le crédit de la ligne = 0,\n    sinon « Une ligne est au débit ou au crédit. ».\n"
                "L'e vaut une nouvelle écriture :\n    La pièce vaut « B-1 ».\nConserver l'e.\n"
                "La l vaut une nouvelle ligne :\n    L'écriture vaut e.\n    Le débit vaut 5.\n    Le crédit vaut 5.\nConserver la l.",
         "~Une ligne est au débit ou au crédit.");
    /* une règle est un calcul : elle ne voit pas les variables du programme, n'écrit rien */
    PROG("Le seuil vaut 3.\nUne écriture, conservée, a :\n    une pièce (texte).\n"
         "Chaque écriture conservée vérifie :\n    seuil > 0,\n    sinon « non ».", "~n'est pas visible dans un calcul");
    PROG("Une écriture, conservée, a :\n    une pièce (texte).\nChaque écriture conservée vérifie :\n    vrai.",
         "~« , sinon « message ». » attendu après la condition d'une règle.");
    PROG("Un point a : un x.\nChaque point conservé vérifie :\n    vrai,\n    sinon « non ».", "~une entité conservée");
    PROG("Si vrai :\n    Chaque écriture conservée vérifie :\n        vrai,\n        sinon « non ».", "~premier niveau");
#undef COMPTA

    /* Refuser (§ 18.1) : le message, écrit comme Afficher l'écrirait ; Essayer le rattrape ; tout est annulé */
    PROG("Refuser « Non. ».", "~Non.");
    PROG("Le d vaut 1234,5.\nRefuser « Débits : » puis d puis « , crédits : » puis 0.", "~Débits : 1'234,5 , crédits : 0");
    PROG("Essayer :\n    Refuser « Écriture déséquilibrée. ».\nEn cas d'échec :\n    Afficher « Refusé : » puis le motif de l'échec.",
         "Refusé : Écriture déséquilibrée.");
    PROG("Le x vaut 1.\nEssayer :\n    Le x devient 2.\n    Refuser « Non. ».\nEn cas d'échec, afficher x.", "1");
    PROG("Le carré d'un nombre :\n    Si nombre < 0, refuser « Négatif. ».\n    Rendre nombre × nombre.\nAfficher le carré de −2.",
         "~Négatif.");
    PROG("Une chose absente a : un nom, facultatif.\nRefuser 1.", "~1");
    PROG("Pour refuser :\n    Afficher 1.", "~commence une construction du langage");
    PROG("Refuser.", "~Expression incomplète");

    /* Apparence des écrans (§ 22.5) : sans écran, en console, rien ne se voit ; les règles de la phrase */
    PROG("Les écrans ont la couleur verte.\nAfficher 1.", "1");
    PROG("Les écrans ont la couleur vert.", "~Accord : « verte »");
    PROG("Les écrans ont la couleur rouge.", "~Couleur attendue : bleue, verte, turquoise, violette, grise, ou une couleur libre");
    /* 2.13 : couleur libre, fond, police, logo SVG ; les contrastes vérifiés dès l'analyse */
    PROG("Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police "
         "« vendor/atkinson/AtkinsonHyperlegibleNext-Regular.ttf » et le logo « vendor/lucide/check.svg ».\nAfficher 1.", "1");
    PROG("Les écrans ont la couleur « #E93A69 ».", "~La couleur « #E93A69 » sur le fond « #F5EFE6 » : contraste de 3,4:1, il faut au moins 4,5:1");
    PROG("Les écrans ont la couleur « #46433B » et le fond « #606060 ».", "~Le texte secondaire sur le fond « #606060 »");
    PROG("Les écrans ont la couleur « 46433B ».", "~Couleur « 46433B » mal écrite");
    PROG("Les écrans ont le fond « #E8E2D6 » et la couleur verte.", "~Les réglages s'écrivent dans cet ordre");
    PROG("Les écrans ont la couleur verte, le fond « #E8E2D6 ».", "~Le dernier réglage se relie par « et »");
    PROG("Les écrans ont la police « README.md ».", "~n'est pas une police (TrueType ou OpenType");
    PROG("Les écrans ont le logo « exemples/pixel.png » et la signature « README.md ».", "~n'est pas une image (PNG, JPEG, GIF, WebP ou SVG)");
    PROG("Les écrans ont la signature « exemples/pixel.png » et le logo « exemples/pixel.png ».", "~Les réglages s'écrivent dans cet ordre");
    PROG("Les écrans ont le logo « README.md ».", "~n'est pas une image (PNG, JPEG, GIF, WebP ou SVG)");
    PROG("Les écrans ont le logo « n'existe-pas.png ».", "~introuvable ou illisible");
    PROG("Les écrans ont le logo « exemples/pixel.png » et la couleur verte.", "~Les réglages s'écrivent dans cet ordre : la couleur, le fond, la police, le logo");
    PROG("Les écrans ont la couleur verte.\nLes écrans ont la couleur grise.", "~une seule fois");
    PROG("Pour f :\n    Les écrans ont la couleur grise.", "~au premier niveau du programme");
    PROG("L'écran d'accueil montre :\n    un bouton « B ».\nOuvrir l'écran d'accueil.\nLes écrans ont la couleur grise.",
         "~avant le premier « Ouvrir »");
    PROG("Afficher 1 à droite.", "~« à gauche » et « à droite » suivent une largeur");

    /* --- Effacer l'écran (§ 4.3) : hors d'un terminal, la phrase n'écrit rien --- */
    PROG("Afficher « a ».\nEffacer l'écran.\nAfficher « b ».", "a\nb");
    PROG("Le double d'un x :\n    Effacer l'écran.\n    Rendre x.", "~Un calcul n'affiche rien");
    PROG("Effacer.", "~Écris « Effacer l'écran. ».");
    PROG("Effacer l'écran et le reste.", "~« et » inattendu, attendu : un point final.");

    /* --- Essayer, En cas d'échec (§ 18) --- */
#define ESS(corps, echec) "Essayer :\n" corps "En cas d'échec :\n" echec
    PROG(ESS("    Afficher 1 ÷ 0.\n", "    Afficher « échec : » puis le motif de l'échec.\n") "Afficher « suite ».",
         "échec : Division par zéro.\nsuite");
    PROG("Le n vaut 0.\n" ESS("    Le n devient 1.\n    Afficher 1 ÷ 0.\n", "    Afficher n.\n") "Afficher n.", "0\n0");
    PROG("Le n vaut 0.\n" ESS("    Le n devient 1.\n", "    Afficher « jamais ».\n") "Afficher n.", "1");
    PROG("Essayer :\n    Afficher 1.\nEn cas d'échec, afficher 2.\nAfficher 3.", "1\n3");
    /* un objet d'avant l'essai retrouve ses champs ; les cases locales d'une action aussi */
    PROG("Un point a : un x.\nLe p vaut un nouveau point :\n    Le x vaut 1.\n"
         ESS("    Le x du p devient 9.\n    Afficher 1 ÷ 0.\n", "    Afficher x du p.\n"), "1");
    PROG("Pour tester un nombre :\n    Le l vaut 1.\n    Essayer :\n        Le l devient 2.\n        Afficher 1 ÷ nombre.\n"
         "    En cas d'échec :\n        Afficher « l : » puis l.\n    Afficher « après : » puis l.\nTester 0.\nTester 4.",
         "l : 1\naprès : 1\n0,25\naprès : 2");
    /* l'erreur remonte des appels imbriqués ; leurs cadres disparaissent */
    PROG("Pour plonger un nombre :\n    Si nombre = 0, afficher 1 ÷ nombre.\n    Sinon, plonger nombre − 1.\n"
         ESS("    Plonger 50.\n", "    Afficher le motif de l'échec.\n") "Afficher « suite ».",
         "Division par zéro.\nsuite");
    /* imbrication : l'essai interne rattrape ; l'échec externe annule aussi ce que l'interne a gardé */
    PROG("Le n vaut 0.\nLe m vaut 0.\nEssayer :\n    Essayer :\n        Le n devient 1.\n        Afficher 1 ÷ 0.\n"
         "    En cas d'échec :\n        Le m devient 5.\n    Afficher n puis m.\n    Afficher 1 ÷ 0.\n"
         "En cas d'échec :\n    Afficher n puis m.\n", "0 5\n0 0");
    /* une erreur dans le bloc d'échec n'est pas rattrapée par son propre essai */
    PROG(ESS("    Afficher 1 ÷ 0.\n", "    Afficher 2 ÷ 0.\n"), "ERREUR 4:16 Division par zéro.");
    PROG("Essayer :\n    Essayer :\n        Afficher 1 ÷ 0.\n    En cas d'échec :\n        Afficher 2 ÷ 0.\n"
         "En cas d'échec :\n    Afficher « externe : » puis le motif de l'échec.\n", "externe : Division par zéro.");
    /* Rendre, Sortir de la boucle et Passer au tour suivant quittent l'essai proprement */
    PROG("La valeur sûre d'un nombre :\n    Essayer :\n        Si nombre > 100, rendre 1.\n        Rendre 10 ÷ nombre.\n"
         "    En cas d'échec :\n        Rendre −1.\n    Rendre 0.\n"
         "Afficher la valeur sûre de 4 puis la valeur sûre de 0 puis la valeur sûre de 200.", "2,5 −1 1");
    PROG("Pour chaque i de 1 à 5 :\n    Essayer :\n        Si i = 4, sortir de la boucle.\n        Si i = 2, passer au tour suivant.\n"
         "        Afficher i.\n    En cas d'échec :\n        Afficher « non ».\n"
         ESS("    Afficher 1 ÷ 0.\n", "    Afficher « rattrapé ».\n"), "1\n3\nrattrapé");
    /* le ramasse-miettes garde ce que la photographie des cases locales désigne */
    PROG("Un point a : un x.\nPour tester :\n    Le o vaut un nouveau point :\n        Le x vaut 5.\n"
         "    Essayer :\n        Le o devient 0.\n        Répéter 30000 fois :\n            Le q vaut un nouveau point.\n"
         "        Afficher 1 ÷ 0.\n    En cas d'échec :\n        Afficher x du o.\nTester.", "5");
    /* analyse */
    PROG("Afficher le motif de l'échec.", "~« le motif de l'échec » ne s'emploie que dans un bloc « En cas d'échec ».");
    PROG("Essayer :\n    Afficher 1.\nAfficher 2.", "~« Essayer » attend son « En cas d'échec », aligné sur lui");
    PROG("Essayer :\n    Afficher 1.", "~« Essayer » attend son « En cas d'échec »");
    PROG("Essayer :\n    Afficher 1.\n    En cas d'échec :\n        Afficher 2.", "~« En cas d'échec » sans « Essayer » correspondant");
    PROG("En cas d'échec, afficher 1.", "~« En cas d'échec » sans « Essayer » correspondant");
    PROG("Essayer, afficher 1.", "~« Essayer » ouvre un bloc");
    PROG("Pour essayer :\n    Afficher 1.", "~« essayer » commence une construction du langage");
    PROG("Essayer :\n    Afficher 1.\nEn cas d'échec :\n    Afficher 2.\nAfficher le motif de l'échec.",
         "~ne s'emploie que dans un bloc « En cas d'échec »");
    /* hors d'un bloc d'échec, les mots gardent leur sens ordinaire */
    PROG("Un rapport a : un motif.\nL'échec vaut un nouveau rapport :\n    Le motif vaut « panne ».\n"
         "Afficher le motif de l'échec.", "panne");
    PROG("Le double d'un x :\n    Essayer :\n        Rendre x × 2.\n    En cas d'échec :\n        Rendre 0.\n    Rendre 0.\n"
         "Afficher le double de 4.", "8");
    SAISIE_INTER("Essayer :\n    Afficher 1 ÷ 0.\nEn cas d'échec, afficher le motif de l'échec.", "Division par zéro.");
    /* entités : l'essai ne garde rien de ce qu'il a conservé ; le programme continue */
    {
        remove("_essai_ess.grymd");
        const char *src = "Un contact, conservé, a : un nom (texte), unique.\n"
                          "Pour ajouter un nom :\n    Le c vaut un nouveau contact :\n        Le nom vaut nom.\n    Conserver c.\n"
                          "Essayer :\n    Ajouter « Ana ».\n    Ajouter « Bob ».\n    Ajouter « Ana ».\n"
                          "En cas d'échec :\n    Afficher le motif de l'échec.\n"
                          "Ajouter « Zoé ».\nAfficher le nombre de contacts conservés.\n";
        const char *att = "« nom » est unique : un autre contact conservé a déjà « Ana ».\n1";
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        machine_base(m, "_essai_ess.grymd");
        char *r = executer_source(p, m, src, 0);
        if (strcmp(r, att) != 0) signaler(__LINE__, src, att, r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        total++;
        p = portee_creer();
        m = machine_creer();
        machine_base(m, "_essai_ess.grymd");
        r = executer_source(p, m, "Un contact, conservé, a : un nom (texte), unique.\n"
                                  "Pour chaque contact conservé, afficher nom du contact.", 0);
        if (strcmp(r, "Zoé") != 0) signaler(__LINE__, "base relue", "Zoé", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_ess.grymd");
    }
    /* fichiers : un enregistrement prévu dans un essai raté n'a pas lieu */
    {
        creer_fichier("_essai_src.txt", "abc", 3);
        remove("_essai_rate.txt");
        remove("_essai_garde.txt");
        PROG("Le f vaut le fichier « _essai_src.txt ».\n"
             ESS("    Enregistrer f dans « _essai_rate.txt ».\n    Afficher 1 ÷ 0.\n", "    Afficher « raté ».\n")
             "Enregistrer f dans « _essai_garde.txt ».", "raté");
        total++;
        if (fichier_existe("_essai_rate.txt") || !fichier_existe("_essai_garde.txt"))
            signaler(__LINE__, "écritures d'un essai raté", "rate absent, garde présent", "autre");
        remove("_essai_src.txt");
        remove("_essai_rate.txt");
        remove("_essai_garde.txt");
    }
    /* une question dans un essai : l'échec n'annule que depuis elle */
    SAISIE("Le n vaut 0.\n" ESS("    Le n devient 1.\n    Le x vaut la réponse à « ? ».\n    Le n devient 2.\n"
           "    Afficher 1 ÷ 0.\n", "    Afficher n.\n"), "? 1", "oui");
    SAISIE("Pour tester :\n    Le l vaut 1.\n    Essayer :\n        Le l devient 2.\n        Le x vaut la réponse à « ? ».\n"
           "        Le l devient 3.\n        Afficher 1 ÷ 0.\n    En cas d'échec :\n        Afficher l.\nTester.", "? 2", "oui");
    {   /* la fin de l'entrée n'est jamais rattrapée : sinon un menu tournerait sans fin */
        static const char *const L[] = { "" };
        verifier_saisie(__LINE__, ESS("    Le x vaut la réponse à « ? ».\n", "    Afficher « rattrapé ».\n"), L, 0,
                        "~Plus rien à lire");
    }
    {   /* base : ce qui précède la question reste conservé malgré l'échec */
        remove("_essai_essq.grymd");
        static const char *const L[] = { "ok" };
        const char *src = "Un contact, conservé, a : un nom (texte).\n"
                          "Pour ajouter un nom :\n    Le c vaut un nouveau contact :\n        Le nom vaut nom.\n    Conserver c.\n"
                          "Essayer :\n    Ajouter « Ana ».\n    Le x vaut la réponse à « ? ».\n    Ajouter « Bob ».\n"
                          "    Afficher 1 ÷ 0.\nEn cas d'échec :\n    Afficher le nombre de contacts conservés.\n";
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        Script sc = { L, 1, 0 };
        machine_lecteur(m, reponses, &sc);
        machine_base(m, "_essai_essq.grymd");
        char *r = executer_source(p, m, src, 0);
        if (strcmp(r, "? 1") != 0) signaler(__LINE__, src, "? 1", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_essq.grymd");
    }

    /* --- Formulaire : « un nouveau … saisi » (§ 19) --- */
#define FC "Un compositeur, conservé, a : un nom (texte), unique.\n" \
           "Le b vaut un nouveau compositeur :\n    Le nom vaut « Bach ».\nConserver b.\n"
#define FP "Une partition, conservée, a : un titre (texte), une cote (texte), unique, un compositeur (compositeur), " \
           "un arrangeur (compositeur), facultatif, une édition (date), facultative, un prix (nombre), 20 au départ, " \
           "une création (année), un actif (vrai ou faux).\n"
#define FA "Afficher titre de p puis cote de p puis nom du compositeur de p puis arrangeur de p puis édition de p " \
           "puis prix de p puis création de p puis actif de p."
    SAISIE(FC FP "Le p vaut une nouvelle partition saisie.\nConserver p.\n" FA,
           "Titre ? Cote ? Compositeur ? Arrangeur ? Édition ? Prix [20] ? Création ? Actif ? "
           "Toccata A-1 Bach absent absent 20 1705 vrai",
           "Toccata", "A-1", "Bach", "", "", "", "1705", "oui");
    /* relances : vide obligatoire, lien introuvable, type, puis valeurs */
    SAISIE(FC FP "Le p vaut une nouvelle partition saisie.\n" FA,
           "Titre ? Une réponse est attendue. Tape « . » seul pour annuler.\nTitre ? Cote ? Compositeur ? Aucun compositeur conservé n'a « Brahms » pour nom.\n"
           "Compositeur ? Arrangeur ? Édition ? « 1.2.3 » n'est pas une date : écris jour.mois.année (21.09.2026).\n"
           "Édition ? Prix [20] ? Création ? Actif ? T C Bach un compositeur 01.02.1900 12,5 1720 faux",
           "", "T", "C", "Brahms", "Bach", "Bach", "1.2.3", "01.02.1900", "12,5", "1720", "non");
    /* unique : refusé dès la réponse, même pour un objet de la corbeille */
    SAISIE(FC FP "Le a vaut une nouvelle partition saisie.\nConserver a.\nSupprimer a.\n"
           "Le p vaut une nouvelle partition saisie.\nAfficher cote de p.",
           "~Cote ? « A-1 » est déjà pris. Tape « . » seul pour annuler.\nCote ? Compositeur ",
           "x", "A-1", "Bach", "", "", "", "1", "oui", "y", "A-1", "B-2", "Bach", "", "", "", "1", "oui");
    /* les champs du bloc ne sont pas demandés */
    SAISIE(FC FP "Le p vaut une nouvelle partition saisie :\n    Le titre vaut « Messe ».\n    Le compositeur vaut b.\n"
           "Afficher titre de p puis nom du compositeur de p.",
           "Cote ? Arrangeur ? Édition ? Prix [20] ? Création ? Actif ? Messe Bach", "M", "", "", "", "1", "non");
    /* un lien sans champ texte unique ne se demande pas */
    SAISIE("Une ville, conservée, a : un nom (texte).\nUn club, conservé, a : un nom (texte), une ville (ville).\n"
           "Le c vaut un nouveau club saisi.", "~« ville » n'a aucun champ texte unique", "Club");
    SAISIE("Une ville, conservée, a : un nom (texte).\nUn club, conservé, a : un nom (texte), une ville (ville), facultative.\n"
           "Le c vaut un nouveau club saisi.\nAfficher nom du c puis ville du c.", "Nom ? Club absent", "Club");
    /* fichier : un chemin ; une image refuse ce qui n'en est pas une */
    {
        creer_fichier("_essai_form.txt", "abc", 3);
        SAISIE("Un document, conservé, a : un contenu (fichier), une vignette (image), facultative.\n"
               "Le d vaut un nouveau document saisi.\nAfficher taille du contenu du d.",
               "Contenu ? Fichier « _absent.txt » introuvable ou illisible. Tape « . » seul pour annuler.\nContenu ? Vignette ? "
               "« _essai_form.txt » n'est pas une image (PNG, JPEG, GIF ou WebP).\nVignette ? 3",
               "_absent.txt", "_essai_form.txt", "_essai_form.txt", "");
        remove("_essai_form.txt");
    }
    /* héritage : les champs hérités d'abord ; le formulaire s'arrête avec l'entrée */
    SAISIE("Une personne, conservée, a : un nom (texte).\nUn membre, conservé, est une personne.\nUn membre a : une licence (texte).\n"
           "Le m vaut un nouveau membre saisi.\nAfficher nom du m puis licence du m.", "Nom ? Licence ? Ana L-1", "Ana", "L-1");
    SAISIE(FC FP "Le p vaut une nouvelle partition saisie.", "~Plus rien à lire : la réponse à « Cote ? » manque.", "T");
    /* dans un essai : un échec à la conservation reprend le menu */
    SAISIE(FC ESS("    Le c vaut un nouveau compositeur saisi.\n    Conserver c.\n    Afficher 1 ÷ 0.\n",
                  "    Afficher le motif de l'échec.\n") "Afficher le nombre de compositeurs conservés.",
           "Nom ? Division par zéro.\n1", "Liszt");
    /* analyse */
    PROG("Un point a : un x.\nLe p vaut un nouveau point saisi.", "~« point » n'est pas une entité");
    PROG(FC FP "Le p vaut une nouvelle partition saisi.", "~Accord : « saisie ».");
    PROG(FC "Le double d'un x :\n    Le c vaut un nouveau compositeur saisi.\n    Rendre x.", "~Un calcul ne pose pas de question");
    SAISIE_INTER(FC "Le c vaut un nouveau compositeur saisi.", "~pas dans la boucle interactive");

    /* retours de la Partothèque (exemples/partotheque.grym) */
    SAISIE(FC FP "Le p vaut une nouvelle partition saisie :\n    Le titre vaut « T ».\n    La cote vaut « C ».\n"
           "Une pièce, conservée, a : une cote (texte), unique, une partition (partition).\n"
           "Conserver p.\nLe q vaut une nouvelle pièce saisie.",
           "~Partition ? Aucune partition conservée n'a « Z » pour cote. Tape « . » seul pour annuler.\nPartition ? ",
           "Bach", "", "", "", "1", "oui", "K", "Z", "C");
    PROG("Un genre, conservé, a : un nom (texte), unique.\nPour f :\n    Le nom vaut « x ».\n"
         "    Afficher le nombre de genres conservés dont le nom est nom.\nF.",
         "~« nom » est aussi un champ du genre : dans une condition « dont », il désigne le champ de l'objet examiné");
    {   /* l'application d'exemple, d'un bout à l'autre */
        FILE *f = fopen("exemples/partotheque.grym", "rb");
        char *src = NULL;
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            src = grym_allouer((size_t)n + 1);
            src[fread(src, 1, (size_t)n, f)] = '\0';
            fclose(f);
        }
        static const char *const L[] = { "1", "Johann Sebastian Bach", "", "2", "BWV 1079", "L'Offrande musicale",
                                         "Johann Sebastian Bach", "1747", "canon", "baroque", "", "3", "P-1", "BWV 1079",
                                         "Bärenreiter", "", "violon", "2", "", "7", "P-9", "4", "6", "P-1", "0" };
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        Script sc = { L, sizeof L / sizeof *L, 0 };
        machine_lecteur(m, reponses, &sc);
        char *r = src ? executer_source(p, m, src, 0) : grym_dupliquer("exemples/partotheque.grym introuvable");
        if (!strstr(r, "BWV 1079     L'Offrande musicale            Johann Sebastian Bach (1747)\n· baroque\n· canon")
            || !strstr(r, "Rien n'a changé : Aucune partition conservée ne répond à cette condition.")
            || !strstr(r, "- violon × 2") || !strstr(r, "Au revoir."))
            signaler(__LINE__, "exemples/partotheque.grym", "séance complète", r);
        free(r);
        free(src);
        machine_detruire(m);
        portee_detruire(p);
    }

    /* « . » seul annule une question ou un formulaire ; un essai le rattrape (§ 17) */
    SAISIE("Le x vaut la réponse à « ? ».\nAfficher x.", "~Saisie annulée.", " . ");
    SAISIE(ESS("    Le x vaut la réponse en nombre à « ? ».\n    Afficher x.\n", "    Afficher le motif de l'échec.\n")
           "Afficher « suite ».", "? « a » n'est pas un nombre. Tape « . » seul pour annuler.\n? Saisie annulée.\nsuite",
           "a", ".");
    SAISIE(FC ESS("    Le c vaut un nouveau compositeur saisi.\n    Conserver c.\n", "    Afficher le motif de l'échec.\n")
           "Le d vaut un nouveau compositeur saisi.\nConserver d.\nAfficher le nombre de compositeurs conservés.",
           "Nom ? Saisie annulée.\nNom ? 2", ".", "Liszt");
    SAISIE("Le x vaut la réponse à « ? ».\nAfficher x.", "? ..", "..");
    /* après une annulation rattrapée, la base est de nouveau sous transaction : l'échec suivant l'annule */
    SAISIE(FC "Le l vaut un nouveau compositeur :\n    Le nom vaut « Liszt ».\nLe r vaut un nouveau compositeur :\n"
           "    Le nom vaut « Reger ».\n"
           ESS("    Le x vaut la réponse à « ? ».\n",
               "    Conserver l.\n    Essayer :\n        Conserver r.\n        Afficher 1 ÷ 0.\n    En cas d'échec, afficher « non ».\n")
           "Afficher le nombre de compositeurs conservés.", "? non\n2", ".");

    /* --- Assembler des textes : « suivi de », élision de « de » et « que » (§ 4.4) --- */
    PROG("L'an vaut 1747.\nAfficher « ( » suivi de an suivi de « ) ».", "(1747)");
    PROG("Le v vaut vrai.\nAfficher « a » suivi de 1,50 suivi de 21.09.2026 suivi de v.", "a1,5021.09.2026vrai");
    PROG("Le nom vaut « Anton ».\nAfficher 3 puis « œuvres » puis de nom.", "3 œuvres d'Anton");
    PROG("Afficher de « Bach » puis de « Élodie » puis de « œil » puis de « Yves » puis de « Hélène » puis de « 1 ».",
         "de Bach d'Élodie d'œil d'Yves de Hélène de 1");
    PROG("Afficher « plus » puis que « Ana » puis que « Bob ».", "plus qu'Ana que Bob");
    PROG("Le nom vaut « Ana ».\nLe t vaut « ami » suivi de de nom suivi du nom.\nAfficher t.\nAfficher t = « amid'AnaAna ».",
         "amid'AnaAna\nvrai");
    PROG("Afficher « x » suivi de « y » sur 3 suivi de « z ».", "xy  z");
    PROG("Les nombres s'affichent sans séparateur.\nAfficher « n° » suivi de 1234.", "n°1234");
    PROG("Un point a : un b, facultatif.\nLe x vaut un nouveau point.\nAfficher « ( » suivi de b du x.",
         "~Le champ « b » est absent : vérifie-le d'abord avec « est présent ».");
    PROG("Le carré d'un n vaut n × n.\nAfficher « = » suivi du carré de 3.", "=9");
    PROG("Si « a » suivi de « b », afficher 1.", "~Condition attendue après « Si »");
    PROG("Afficher « a » suivi « b ».", "~« suivi » attend « de »");
    PROG("Le suivi vaut 1.", "~« suivi » est un mot réservé");

    /* --- Modifier par formulaire : « Saisir à nouveau » (§ 19) --- */
#define FM "Un compositeur, conservé, a : un nom (texte), unique.\n" \
           "Une partition, conservée, a : une cote (texte), unique, un titre (texte), un compositeur (compositeur), " \
           "un arrangeur (compositeur), facultatif, une édition (date), facultative.\n" \
           "Pour préparer :\n    Le b vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n    Conserver b.\n" \
           "    Le w vaut un nouveau compositeur :\n        Le nom vaut « Webern ».\n    Conserver w.\n" \
           "    La p vaut une nouvelle partition :\n        La cote vaut « P-1 ».\n        Le titre vaut « Offrande ».\n" \
           "        Le compositeur vaut b.\n        L'arrangeur vaut w.\n    Conserver p.\n" \
           "    La q vaut une nouvelle partition :\n        La cote vaut « P-2 ».\n        Le titre vaut « Fugue ».\n" \
           "        Le compositeur vaut b.\n    Conserver q.\nPréparer.\n" \
           "La p vaut la partition conservée dont la cote est « P-1 ».\n"
#define FMA "Afficher cote de p puis titre de p puis nom du compositeur de p puis arrangeur de p puis édition de p."
    /* ligne vide : la valeur reste ; « - » vide un champ facultatif ; sa propre valeur unique n'est pas un doublon */
    SAISIE(FM "Saisir à nouveau p.\n" FMA,
           "Cote [P-1] ? Titre [Offrande] ? Compositeur [Bach] ? Arrangeur [Webern] (- pour vider) ? Édition ? "
           "P-1 Offrande musicale Webern absent 01.02.1900",
           "P-1", "Offrande musicale", "Webern", "-", "01.02.1900");
    SAISIE(FM "Saisir à nouveau p.\n" FMA,
           "Cote [P-1] ? Titre [Offrande] ? Compositeur [Bach] ? Arrangeur [Webern] (- pour vider) ? Édition ? "
           "P-1 Offrande Bach un compositeur absent", "", "", "", "", "");
    SAISIE(FM "Saisir à nouveau p.", "~Cote [P-1] ? « P-2 » est déjà pris. Tape « . » seul pour annuler.\nCote [P-1] ? ",
           "P-2", "P-3", "", "", "", "");
    /* tout ou rien : une annulation au milieu ne change aucun champ */
    SAISIE(FM ESS("    Saisir à nouveau p.\n", "    Afficher le motif de l'échec.\n") FMA,
           "Cote [P-1] ? Titre [Offrande] ? Compositeur [Bach] ? Saisie annulée.\nP-1 Offrande Bach un compositeur absent",
           "P-9", "Nouveau", ".");
    {   /* la base suit, et relue par une autre exécution */
        remove("_essai_resaisir.grymd");
        static const char *const L[] = { "", "Toccata", "", "", "" };
        const char *src = FM "Saisir à nouveau p.";
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        Script sc = { L, 5, 0 };
        machine_lecteur(m, reponses, &sc);
        machine_base(m, "_essai_resaisir.grymd");
        char *r = executer_source(p, m, src, 0);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        p = portee_creer();
        m = machine_creer();
        machine_base(m, "_essai_resaisir.grymd");
        r = executer_source(p, m, "Un compositeur, conservé, a : un nom (texte), unique.\n"
            "Une partition, conservée, a : une cote (texte), unique, un titre (texte), un compositeur (compositeur), "
            "un arrangeur (compositeur), facultatif, une édition (date), facultative.\n"
            "Pour chaque partition conservée, par cote, afficher titre de la partition.", 0);
        if (strcmp(r, "Toccata\nFugue") != 0) signaler(__LINE__, "base relue après Saisir à nouveau", "Toccata\nFugue", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_resaisir.grymd");
    }
    SAISIE(FM "Supprimer p.\nSaisir à nouveau p.", "~est dans la corbeille : rétablis-le d'abord.", "");
    /* un objet pas encore conservé : les champs sans valeur se demandent comme à la création */
    SAISIE(FM "La n vaut une nouvelle partition :\n    La cote vaut « N ».\nSaisir à nouveau n.\nAfficher titre de n.",
           "Cote [N] ? Titre ? Compositeur ? Arrangeur ? Édition ? T", "", "T", "Bach", "", "");
    PROG("Un point a : un x.\nLe p vaut un nouveau point.\nPour f :\n    Saisir à nouveau p.\nF.", "~Seul un objet d'une entité se saisit.");
    PROG(FC "Le carré d'un n :\n    Saisir à nouveau b.\n    Rendre n.", "~Un calcul ne pose pas de question");
    PROG("Pour saisir un x :\n    Afficher 1.", "~« saisir » commence une construction du langage");

    /* --- Un index par colonne de lien (§ 16.10, § 16.14) : base neuve, et base d'avant les index --- */
    {
        const char *B = "_essai_index.grymd";
        const char *prog = "Une écriture, conservée, a : une pièce (texte).\nUne ligne, conservée, a : une écriture (écriture).\n"
                           "Afficher le nombre de lignes conservées.";
        remove(B);
        char *r = lancer_sur(B, prog);
        total++;
        if (strcmp(r, "0") != 0 || index_existe(B, "l ligne.écriture") != 1) signaler(__LINE__, "index, base neuve", "0, index", r);
        free(r);
        sql_direct(B, "DROP INDEX \"l ligne.écriture\";");   /* une base créée avant les index */
        r = lancer_sur(B, prog);
        total++;
        if (strcmp(r, "0") != 0 || index_existe(B, "l ligne.écriture") != 1) signaler(__LINE__, "index, base ancienne", "0, index", r);
        free(r);
        remove(B);
    }

    /* --- Format des bases (charte, art. 13) --- */
    {
        const char *B = "_essai_format.grymd";
        const char *prog = "Un client, conservé, a : un nom (texte).\nPour f :\n    Le c vaut un nouveau client :\n"
                           "        Le nom vaut « Ana ».\n    Conserver c.\nF.\nAfficher le nombre de clients conservés.";
        char *r;
        /* une base neuve reçoit le format courant */
        remove(B);
        r = lancer_sur(B, prog);
        total++;
        if (strcmp(r, "1") != 0 || format_base(B) != 2) signaler(__LINE__, "base neuve", "1, format 2", r);
        free(r);
        /* une base d'avant le numéro (et d'avant la corbeille) s'ouvre, reçoit ses colonnes et le format */
        remove(B);
        sql_direct(B, "CREATE TABLE grym_objet (id INTEGER PRIMARY KEY AUTOINCREMENT, classe TEXT NOT NULL);"
                      "CREATE TABLE grym_schema (entite TEXT PRIMARY KEY, definition TEXT NOT NULL);");
        r = lancer_sur(B, prog);
        total++;
        if (strcmp(r, "1") != 0 || format_base(B) != 2) signaler(__LINE__, "base ancienne", "1, format 2", r);
        free(r);
        /* une base plus récente se refuse, sans être touchée */
        remove(B);
        sql_direct(B, "PRAGMA user_version = 3;");
        r = lancer_sur(B, prog);
        total++;
        if (!strstr(r, "a le format 3, plus récent que celui de ce grym (2)") || !strstr(r, "Elle n'a pas été modifiée."))
            signaler(__LINE__, "base plus récente", "refus", r);
        free(r);
        {
            sqlite3 *db = NULL;
            sqlite3_stmt *st = NULL;
            int tables = -1;
            if (sqlite3_open_v2(B, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK
                && sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_master;", -1, &st, NULL) == SQLITE_OK
                && sqlite3_step(st) == SQLITE_ROW)
                tables = sqlite3_column_int(st, 0);
            sqlite3_finalize(st);
            sqlite3_close(db);
            total++;
            if (tables != 0 || format_base(B) != 3) signaler(__LINE__, "base plus récente intacte", "0 table, format 3", "modifiée");
        }
        remove(B);
    }


    /* --- Fichiers en base, format 2 (docs/vm.md, § 8) : octets dans grym_fichier, lus au premier besoin --- */
    {
        const char *B = "_essai_fichiers.grymd";
        remove(B);
        static const unsigned char PNG[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3, 4 };
        creer_fichier("_essai_f1.png", PNG, sizeof PNG);
        creer_fichier("_essai_f2.txt", "deuxième", 9);
        creer_fichier("_essai_f3.txt", "deuxième", 9);
#define DOC "Un document, conservé, a : un titre (texte), unique, un contenu (fichier), une vignette (image), facultative.\n"
#define ESSAI_F(src, attendu, quoi) do { char *r_ = lancer_sur(B, DOC src); total++; \
            if (strcmp(r_, attendu) != 0) { signaler(__LINE__, quoi, attendu, r_); } free(r_); } while (0)
        ESSAI_F("Le a vaut un nouveau document :\n    Le titre vaut « A ».\n    Le contenu vaut le fichier « _essai_f2.txt ».\n"
                "    La vignette vaut le fichier « _essai_f1.png ».\nConserver a.\n"
                "Le b vaut un nouveau document :\n    Le titre vaut « B ».\n    Le contenu vaut le fichier « _essai_f3.txt ».\n"
                "Conserver b.\nAfficher 1.", "1", "conserver deux fichiers");
        total++;
        if (sql_nombre(B, "SELECT count(*) FROM grym_fichier") != 3
            || sql_nombre(B, "SELECT count(*) FROM \"e document\" WHERE typeof(\"c contenu\") = 'integer'") != 2
            || format_base(B) != 2)
            signaler(__LINE__, "rangement à part", "3 lignes, 2 numéros, format 2", "autre");
        /* relus : taille, format et nom sans les octets ; égalité par contenu, puis affichage */
        ESSAI_F("Le a vaut le document conservé dont le titre est « A ».\n"
                "Le b vaut le document conservé dont le titre est « B ».\n"
                "Afficher taille du contenu du a puis format de la vignette du a puis nom de fichier du contenu du b.\n"
                "Afficher contenu du a = contenu du b puis contenu du a = vignette du a puis contenu du a = le fichier « _essai_f3.txt ».\n"
                "Afficher vignette du a.", "9 PNG _essai_f3.txt\nvrai faux vrai\nune image PNG de 12 octets", "relire");
        /* copie d'un objet à l'autre, sans passer par la mémoire ; l'ancienne ligne part */
        ESSAI_F("Le a vaut le document conservé dont le titre est « A ».\n"
                "Le b vaut le document conservé dont le titre est « B ».\n"
                "La vignette du b devient la vignette du a.\nAfficher vignette du b = vignette du a.", "vrai", "copier");
        total++;
        if (sql_nombre(B, "SELECT count(*) FROM grym_fichier") != 4) signaler(__LINE__, "copie", "4 lignes", "autre");
        /* remplacé : l'ancien contenu, gardé dans une variable, se lit encore ; sa ligne est partie */
        ESSAI_F("Le a vaut le document conservé dont le titre est « A ».\nLe vieux vaut le contenu du a.\n"
                "Le contenu du a devient le fichier « _essai_f1.png ».\n"
                "Afficher vieux = le fichier « _essai_f2.txt » puis taille du contenu du a.", "vrai 12", "remplacer");
        total++;
        if (sql_nombre(B, "SELECT count(*) FROM grym_fichier") != 4) signaler(__LINE__, "remplacement", "4 lignes", "autre");
        /* une écriture d'un fichier vers le disque lit ses octets en base */
        remove("_essai_sortie.png");
        ESSAI_F("Le a vaut le document conservé dont le titre est « A ».\n"
                "Enregistrer la vignette du a dans « _essai_sortie.png ».\nAfficher 1.", "1", "écrire sur le disque");
        {
            FILE *f = fopen("_essai_sortie.png", "rb");
            unsigned char t[32];
            size_t n = f ? fread(t, 1, sizeof t, f) : 0;
            if (f) fclose(f);
            total++;
            if (n != sizeof PNG || memcmp(t, PNG, n) != 0) signaler(__LINE__, "écrit sur le disque", "12 octets PNG", "autre");
            remove("_essai_sortie.png");
        }
        /* suppression : la corbeille garde tout ; la suppression définitive efface les fichiers */
        ESSAI_F("Supprimer le document conservé dont le titre est « B ».\nAfficher 1.", "1", "corbeille");
        total++;
        if (sql_nombre(B, "SELECT count(*) FROM grym_fichier") != 4) signaler(__LINE__, "corbeille", "4 lignes", "autre");
        ESSAI_F("Le b vaut le document supprimé dont le titre est « B ».\nLe c vaut contenu du b.\n"
                "Supprimer b définitivement.\nAfficher taille du c.", "9", "suppression définitive");
        total++;
        if (sql_nombre(B, "SELECT count(*) FROM grym_fichier") != 2) signaler(__LINE__, "suppression définitive", "2 lignes", "autre");
#undef ESSAI_F
#undef DOC
        remove(B);
        /* unicité : par le contenu, puisque la colonne ne garde que des numéros */
        char *r = lancer_sur(B, "Une pièce, conservée, a : un scan (fichier), unique.\n"
                                "La p vaut une nouvelle pièce :\n    Le scan vaut le fichier « _essai_f2.txt ».\nConserver p.\n"
                                "La q vaut une nouvelle pièce :\n    Le scan vaut le fichier « _essai_f3.txt ».\nConserver q.\n");
        total++;
        if (!strstr(r, "« scan » est unique : une autre pièce conservée a déjà ce fichier."))
            signaler(__LINE__, "fichier unique", "refus", r);
        free(r);
        r = lancer_sur(B, "Une pièce, conservée, a : un scan (fichier), unique.\n"
                          "La p vaut une nouvelle pièce :\n    Le scan vaut le fichier « _essai_f2.txt ».\nConserver p.\n"
                          "La q vaut une nouvelle pièce :\n    Le scan vaut le fichier « _essai_f1.png ».\nConserver q.\n"
                          "Le scan de la q devient le fichier « _essai_f3.txt ».\n");
        total++;
        if (!strstr(r, "« scan » est unique : une autre pièce conservée a déjà ce fichier."))
            signaler(__LINE__, "fichier unique modifié", "refus", r);
        free(r);
        remove(B);
        /* base d'avant le format 2 : fichiers dans la table ; l'essai de migration le dit sans rien changer,
         * la première exécution les déménage */
        const char *DOCS = "Un document, conservé, a : un titre (texte), un contenu (fichier).\n";
        r = lancer_sur(B, "Un document, conservé, a : un titre (texte), un contenu (fichier).\n"
                          "Le a vaut un nouveau document :\n    Le titre vaut « A ».\n    Le contenu vaut le fichier « _essai_f2.txt ».\n"
                          "Conserver a.\n");
        free(r);
        sql_direct(B, "UPDATE \"e document\" SET \"c contenu\" = (SELECT octets FROM grym_fichier WHERE id = \"c contenu\");"
                      "DROP TABLE grym_fichier; PRAGMA user_version = 1;");
        r = essai_migration(B, DOCS);
        total++;
        if (!strstr(r, "Les fichiers conservés (1) quittent les tables de leurs entités pour grym_fichier")
            || format_base(B) != 1 || sql_nombre(B, "SELECT count(*) FROM sqlite_master WHERE name = 'grym_fichier'") != 0)
            signaler(__LINE__, "essai de migration, format 1", "rapport, base intacte", r);
        free(r);
        char *lire = grym_formater("%sLe a vaut le document conservé dont le titre est « A ».\n"
                                   "Afficher contenu du a = le fichier « _essai_f2.txt ».", DOCS);
        r = lancer_sur(B, lire);
        total++;
        if (strcmp(r, "vrai") != 0 || format_base(B) != 2
            || sql_nombre(B, "SELECT count(*) FROM \"e document\" WHERE typeof(\"c contenu\") = 'integer'") != 1)
            signaler(__LINE__, "conversion au format 2", "vrai, format 2", r);
        free(r);
        free(lire);
        remove(B);
        remove("_essai_f1.png");
        remove("_essai_f2.txt");
        remove("_essai_f3.txt");
    }


    /* --- Règles ciblées (docs/vm.md, § 3.3) : une règle ne se revérifie que si une entité qu'elle a lue a changé --- */
    {
        const char *B = "_essai_regles.grymd";
#define REGLES "Une ligne, conservée, a : un montant (nombre).\nUne note, conservée, a : un texte (texte).\n" \
               "Chaque ligne conservée vérifie : le montant de la ligne ≥ 0, sinon « Montant négatif. ».\n"
#define LIGNE(n, x) "La " n " vaut une nouvelle ligne :\n    Le montant vaut " x ".\nConserver " n ".\n"
#define NOTE(n) "La " n " vaut une nouvelle note :\n    Le texte vaut « vu ».\nConserver " n ".\n"
        static const char *const L[] = { "a", "b", "c", "d", "e" };
        const char *sources[] = {
            /* 1, 2, 3 : écrire une ligne, une note, une ligne, modifier la note : la règle ne revient que pour les lignes */
            REGLES LIGNE("l", "5") "Le x vaut la réponse à « 1 ? ».\n" NOTE("n") "Le y vaut la réponse à « 2 ? ».\n"
                   LIGNE("l2", "3") "Le z vaut la réponse à « 3 ? ».\nLe texte de n devient « revu ».\n"
                   "Le w vaut la réponse à « 4 ? ».\nAfficher « fin ».",
            /* une ligne fautive, après des transactions sautées, est toujours refusée */
            REGLES LIGNE("l", "5") "Le x vaut la réponse à « 1 ? ».\n" NOTE("n") "Le y vaut la réponse à « 2 ? ».\n"
                   LIGNE("l2", "-1"),
            /* un autre programme casse la règle pendant la question : la transaction suivante, qui n'écrit qu'une
             * note, la revérifie quand même */
            REGLES LIGNE("l", "5") "Le x vaut la réponse à « 1 ? ».\n" NOTE("n") "Le y vaut la réponse à « casser ? ».\n"
                   NOTE("n2"),
            /* une écriture annulée par un essai compte quand même : la règle se vérifie une fois, puis plus */
            REGLES "Pour ajouter :\n    " "La l vaut une nouvelle ligne :\n        Le montant vaut 5.\n    Conserver l.\n"
                   "    Refuser « non ».\n"
                   "Essayer :\n    Ajouter.\nEn cas d'échec :\n    Afficher le motif de l'échec.\n"
                   NOTE("n") "Le x vaut la réponse à « 1 ? ».\n" NOTE("n2") "Afficher « fin ».",
        };
        const char *attendus[] = {
            "1 ? 2 ? 3 ? 4 ? fin", "~Montant négatif.", "~Montant négatif.", "~non\n1 ? fin" };
        const size_t verifiees[] = { 2, 2, 2, 1 };
        for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
            remove(B);
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            ScriptCasseur sc = { { L, 5, 0 }, B };
            machine_lecteur(m, reponses_casseur, &sc);
            machine_base(m, B);
            char *r = executer_source(p, m, sources[i], 0);
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            size_t n = machine_regles_verifiees(m);
            if (!ok || n != verifiees[i]) {
                char *o = grym_formater("%s (règle vérifiée %lu fois)", r, (unsigned long)n);
                char *a = grym_formater("%s (règle vérifiée %lu fois)", attendus[i], (unsigned long)verifiees[i]);
                signaler(__LINE__, sources[i], a, o);
                free(o);
                free(a);
            }
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove(B);
#undef REGLES
#undef LIGNE
#undef NOTE
    }

    /* --- Interface d'entrée et de sortie (docs/vm.md, § 13) : une page au lieu de la console --- */
    {
        static const char *const R1[] = { "Toccata", "A-1", "Bach", "", "x", "", "1705", "oui", "01.02.1900" };
        char *r = avec_page(FC FP "Le p vaut une nouvelle partition saisie.\nConserver p.\n" FA, R1, 9, NULL);
        const char *att = "Toccata A-1 Bach absent 01.02.1900 20 1705 vrai\n"
                          "page Titre Cote Compositeur Arrangeur Édition "
                          "!« x » n'est pas une date : écris jour.mois.année (21.09.2026). "
                          "Prix=20 Création Actif Édition";
        total++;
        if (strcmp(r, att) != 0) signaler(__LINE__, "formulaire en page", att, r);
        free(r);
        /* modification : valeurs actuelles, « vider » explicite, tout ou rien */
        static const char *const R2[] = { "", "Offrande musicale", "", "VIDER", "" };
        r = avec_page(FM "Saisir à nouveau p.\n" FMA, R2, 5, NULL);
        att = "P-1 Offrande musicale Bach absent absent\n"
              "page Cote=P-1 Titre=Offrande Compositeur=Bach Arrangeur=Webern(videable) Édition";
        total++;
        if (strcmp(r, att) != 0) signaler(__LINE__, "modification en page", att, r);
        free(r);
        static const char *const R3[] = { "P-9", "Nouveau", "ANNULER" };
        r = avec_page(FM ESS("    Saisir à nouveau p.\n", "    Afficher le motif de l'échec.\n") FMA, R3, 3, NULL);
        att = "Saisie annulée.\nP-1 Offrande Bach un compositeur absent\n"
              "page Cote=P-1 Titre=Offrande Compositeur=Bach";
        total++;
        if (strcmp(r, att) != 0) signaler(__LINE__, "annulation en page", att, r);
        free(r);
        /* « la réponse à » est un formulaire d'un champ ; « Effacer l'écran » passe par l'interface */
        static const char *const R4[] = { "douze", "12" };
        int eff = 0;
        r = avec_page("Le x vaut la réponse en nombre à « Âge ? ».\nEffacer l'écran.\nAfficher x + 1.", R4, 2, &eff);
        att = "13\npage Âge ? !« douze » n'est pas un nombre. Âge ?";
        total++;
        if (strcmp(r, att) != 0 || eff != 1) signaler(__LINE__, "question en page", att, r);
        free(r);
        r = avec_page("Le x vaut la réponse à « ? ».", R4, 0, NULL);
        total++;
        if (!strstr(r, "Plus rien à lire : la réponse à « ? » manque.")) signaler(__LINE__, "fin de page", "fin", r);
        free(r);
    }

    /* --- La fiche d'un objet (§ 20) --- */
#define FF "Un genre, conservé, a : un nom (texte), unique.\n" \
           "Un compositeur, conservé, a : un nom (texte), unique, une naissance (date), facultative.\n" \
           "Une œuvre, conservée, a : un catalogue (texte), unique, un titre (texte), un compositeur (compositeur), " \
           "une année d'édition (année), facultative, des genres (genre).\n" \
           "Pour préparer :\n    Le b vaut un nouveau compositeur :\n        Le nom vaut « Bach ».\n    Conserver b.\n" \
           "    Le g vaut un nouveau genre :\n        Le nom vaut « canon ».\n    Conserver g.\n" \
           "    L'o vaut une nouvelle œuvre :\n        Le catalogue vaut « BWV 1079 ».\n        Le titre vaut « Offrande ».\n" \
           "        Le compositeur vaut b.\n    Conserver o.\n    Les genres de l'o gagnent g.\nPréparer.\n" \
           "L'o vaut l'œuvre conservée dont le catalogue est « BWV 1079 ».\n"
    PROG(FF "Afficher la fiche de l'o.",
         "Œuvre BWV 1079\n  Catalogue        BWV 1079\n  Titre            Offrande\n  Compositeur      Bach\n"
         "  Année d'édition  absent\n  Genres           canon");
    PROG(FF "Afficher la fiche du compositeur de l'o.", "Compositeur Bach\n  Nom        Bach\n  Naissance  absent");
    /* un objet pas encore conservé : ses multiples sont vides ; une classe ordinaire a sa fiche aussi */
    PROG(FF "La n vaut une nouvelle œuvre :\n    Le titre vaut « T ».\nAfficher la fiche de n.",
         "Œuvre\n  Catalogue        absent\n  Titre            T\n  Compositeur      absent\n  Année d'édition  absent\n  Genres           aucun");
    PROG("Un point a : un x, un y.\nLe p vaut un nouveau point :\n    Le x vaut 1.\nAfficher la fiche de p.", "Point\n  X  1\n  Y  absent");
    PROG("Afficher la fiche de 3.", "~Une fiche montre un objet : la valeur est un nombre.");
    PROG("Un point a : un x, un y, facultatif.\nLe p vaut un nouveau point.\nAfficher la fiche du y de p.",
         "~Le champ « y » est absent");
    /* « fiche » déclaré garde son sens ordinaire */
    PROG("Un dossier a : une fiche.\nLe d vaut un nouveau dossier :\n    La fiche vaut « F-1 ».\nAfficher la fiche de d.", "F-1");
    {   /* une image dans une fiche : sa description en console */
        creer_fichier("_essai_fiche.png", "\x89PNG\r\n\x1a\nimage", 13);
        PROG("Un membre, conservé, a : un nom (texte), une photo (image).\nLe m vaut un nouveau membre :\n"
             "    Le nom vaut « Ana ».\n    La photo vaut le fichier « _essai_fiche.png ».\nAfficher la fiche de m.",
             "Membre\n  Nom    Ana\n  Photo  une image PNG de 13 octets");
        remove("_essai_fiche.png");
    }

    essais_utiliser();
    /* La section paiement : dans un document seulement (2.28) */
    PROG("La charge vaut « SPC ».\nL'écran x montre :\n    la section paiement de la charge.",
         "~La section paiement d'une QR-facture va dans un document, pas dans un écran.");
    /* Le code QR, le saut de ligne, le format technique (2.27) */
    PROG("Le q vaut le code QR de « https://nicolas-seydoux.ch ».\nAfficher le format du q.", "PNG");
    /* Les lignes d'un Swiss QR Code, vérifiées (2.29) : juste, sans débiteur, clé fausse, QR-IBAN sans QRR, monnaie, adresse */
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH5204835012345671000 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « CHF » suivi du s suivi de « S » suivi du s suivi de « Anne Rochat » suivi du s suivi de « Route » suivi du s suivi de « 12a » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.\nAfficher le format du q.",
         "PNG");
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH5204835012345671000 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « CHF » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.\nAfficher le format du q.",
         "PNG");
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH5304835012345671000 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « CHF » suivi du s suivi de « S » suivi du s suivi de « Anne Rochat » suivi du s suivi de « Route » suivi du s suivi de « 12a » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.",
         "~n'est pas un IBAN suisse ou liechtensteinois valable");
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH4431999123000889012 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « CHF » suivi du s suivi de « S » suivi du s suivi de « Anne Rochat » suivi du s suivi de « Route » suivi du s suivi de « 12a » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.",
         "~est un QR-IBAN ; il exige une référence QR (QRR)");
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH5204835012345671000 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « USD » suivi du s suivi de « S » suivi du s suivi de « Anne Rochat » suivi du s suivi de « Route » suivi du s suivi de « 12a » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.",
         "~la monnaie est CHF ou EUR");
    PROG("Le s vaut un saut de ligne.\nLe d vaut « SPC » suivi du s suivi de « 0200 » suivi du s suivi de « 1 » suivi du s suivi de « CH5204835012345671000 » suivi du s suivi de « S » suivi du s suivi de « Cabinet » suivi du s suivi de « Rue » suivi du s suivi de « 1 » suivi du s suivi de « 1630 » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « » suivi du s suivi de « 130.00 » suivi du s suivi de « CHF » suivi du s suivi de « S » suivi du s suivi de « Anne Rochat » suivi du s suivi de « Route » suivi du s suivi de « 12a » suivi du s suivi de « » suivi du s suivi de « Bulle » suivi du s suivi de « CH » suivi du s suivi de « NON » suivi du s suivi de « » suivi du s suivi de « Facture » suivi du s suivi de « EPD ».\nLe q vaut le code QR suisse du d.",
         "~l'adresse du débiteur n'a pas de numéro postal");
    PROG("Le q vaut le code QR suisse de « SPC ».", "~QR-facture : 1 ligne au lieu de 31 au moins");
    /* « Sauvegarder la base dans … » (2.29) : sans base, refusé ; deux copies, la seconde « (2) » ; pas dans un calcul */
    PROG("Sauvegarder la base dans « _essai_copie.grymd ».", "~Ce programme n'a pas de base : il ne conserve aucune entité.");
    remove("_essai_copie.grymd");
    remove("_essai_copie (2).grymd");
    PROG("Un livre, conservé, a :\n    un titre (texte).\nLe l vaut un nouveau livre :\n    Le titre vaut « Candide ».\n"
         "Conserver le l.\nSauvegarder la base dans « _essai_copie.grymd ».\nSauvegarder la base dans « _essai_copie.grymd ».\n"
         "Afficher le nombre de livres conservés.", "1");
    {
        FILE *a = fopen("_essai_copie.grymd", "rb"), *b2 = fopen("_essai_copie (2).grymd", "rb");
        char tete[16] = "";
        if (a && fread(tete, 1, 16, a) != 16) tete[0] = 0;
        total++;
        if (!(a && b2 && memcmp(tete, "SQLite format 3", 15) == 0))
            signaler(__LINE__, "Sauvegarder la base", "deux sauvegardes, des bases SQLite", a ? "une seule" : "aucune");
        if (a) fclose(a);
        if (b2) fclose(b2);
        remove("_essai_copie.grymd");
        remove("_essai_copie (2).grymd");
    }
    PROG("Le double d'un nombre :\n    Sauvegarder la base dans « x.grymd ».\n    Rendre nombre × 2.\nAfficher le double de 2.",
         "~Un calcul n'écrit pas sur le disque : sauvegarde la base dans une action.");
    /* Le coffre et la restauration (2.30) : une sauvegarde chiffrée dans un dossier créé ; le mauvais mot de passe, pas de
       mot de passe, un fichier qui n'est pas une base : refusés ; la bonne restauration attend la prochaine ouverture */
    remove("_essai_coffre/copie.coffre");
    remove("_essai_coffre");
    PROG("Un livre, conservé, a :\n    un titre (texte).\nLe l vaut un nouveau livre :\n    Le titre vaut « Candide ».\n"
         "Conserver le l.\nSauvegarder la base dans « _essai_coffre/copie.coffre » avec le mot de passe « tilleul ».\nAfficher 1.", "1");
    {
        FILE *a = fopen("_essai_coffre/copie.coffre", "rb");
        char tete[12] = "";
        if (a && fread(tete, 1, 12, a) != 12) tete[0] = 0;
        total++;
        if (!(a && memcmp(tete, "GRYMCOFFRE1\n", 12) == 0)) signaler(__LINE__, "Sauvegarder … avec le mot de passe", "un coffre", "rien");
        if (a) fclose(a);
    }
    PROG("Un livre, conservé, a :\n    un titre (texte).\nRestaurer la base depuis « _essai_coffre/copie.coffre » avec le mot de passe « faux ».",
         "~Mot de passe faux, ou sauvegarde abîmée : elle ne s'ouvre pas.");
    PROG("Un livre, conservé, a :\n    un titre (texte).\nRestaurer la base depuis « _essai_coffre/copie.coffre ».",
         "~Cette sauvegarde est chiffrée : il faut son mot de passe.");
    PROG("Restaurer la base depuis « _essai_coffre/copie.coffre ».", "~Ce programme n'a pas de base : rien à restaurer.");
    PROG("Le pas d'un nombre :\n    Restaurer la base depuis « x ».\n    Rendre nombre.\nAfficher le pas de 2.",
         "~Un calcul ne restaure pas la base : restaure-la dans une action.");
    remove("_essai_coffre/copie.coffre");
    remove("_essai_coffre");
    /* Deux défauts corrigés (2.31) : une parenthèse qui contient un « dont » est un calcul, pas un groupe logique ;
       une action dont le nom finit par « d'un » s'appelle avec « de », « du », « des », « d' » */
    PROG("Une note, conservée, a :\n    une date (date).\nLe n vaut une nouvelle note :\n    La date vaut 01.02.2026.\nConserver le n.\n"
         "Le r vaut (le nombre de notes conservées dont la date ≥ 01.01.2026) + (le nombre de notes supprimées dont la date ≥ 01.01.2026) + 1.\n"
         "Afficher le r.\nSi (le nombre de notes conservées dont la date ≥ 01.01.2026 et la date < 01.01.2027) = 1, afficher « un ».", "2\nun");
    PROG("Pour acquitter le solde d'un relevé :\n    Afficher « solde » puis relevé.\nLe relevé vaut 3.\nL'anne vaut 5.\n"
         "Les notes valent 1 et 2.\nAcquitter le solde du relevé.\nAcquitter le solde de 4.\nAcquitter le solde d'anne.\n"
         "Acquitter le solde des notes.", "solde 3\nsolde 4\nsolde 5\nsolde 1, 2");
    /* Un mot de passe : « est présent », « est absent » ne le lisent pas (2.31) ; le lire reste refusé */
    PROG("Un membre, conservé, a :\n    un nom (texte),\n    un code (mot de passe), facultatif.\nLe m vaut un nouveau membre :\n"
         "    Le nom vaut « A ».\nConserver le m.\nSi le code du m est absent, afficher « sans ».\nLe code du m devient « secret-1 ».\n"
         "Si le code du m est présent, afficher « avec ».\nPour chaque membre conservé :\n    Si le code du membre est présent, afficher « relu ».",
         "sans\navec\nrelu");
    PROG("Un membre, conservé, a :\n    un code (mot de passe).\nLe m vaut un nouveau membre :\n    Le code vaut « secret-1 ».\nAfficher le code du m.",
         "~« code » est un mot de passe : il ne se lit pas, il se vérifie");
    /* Un secret (§ 22.17, 2.40) : dans une base en clair, refusé à l'écriture du champ comme à « Conserver » ; un objet
       qui n'est pas conservé le garde en mémoire */
    PROG("Un cabinet, conservé, a :\n    un nom (texte),\n    une clé (secret), facultative.\nLe c vaut un nouveau cabinet :\n"
         "    Le nom vaut « A ».\n    La clé vaut « k ».\nConserver le c.",
         "~« clé » est un secret : il se range dans une base chiffrée. Écris « La base est chiffrée. » dans le programme.");
    PROG("Un cabinet, conservé, a :\n    un nom (texte),\n    une clé (secret), facultative.\nLe c vaut un nouveau cabinet :\n"
         "    Le nom vaut « A ».\nConserver le c.\nLa clé du c devient « k ».",
         "~« clé » est un secret : il se range dans une base chiffrée.");
    PROG("Un cabinet, conservé, a :\n    une clé (secret), facultative.\nLe c vaut un nouveau cabinet :\n    La clé vaut « k ».\n"
         "Si la clé du c est présente, afficher « en mémoire ».", "en mémoire");
    PROG("L'écran x montre :\n    une clé (secret).", "");   /* une zone (secret) se déclare */
    PROG("L'écran x montre :\n    un n (nombre), avec le choix d'un dossier.", "~Seule une zone (texte) se remplit par le choix d'un dossier.");   /* 2.32 */
    PROG("L'écran x, (\"A\" suivi de « B ») montre :\n    un bouton « OK ».", "~« , » attendu après le titre");   /* 2.33 */
    PROG("L'écran x montre :\n    un code (texte), large de 0 caractères.", "~Une largeur en caractères va de 1 à 200");   /* 2.34 */
    /* « en IBAN » (2.29) : sans espaces, en capitales, vérifié ; trois refus */
    PROG("Afficher « ch52 0483 5012 3456 7100 0 » en IBAN.\nAfficher « GB82 WEST 1234 5698 7654 32 » en IBAN.",
         "CH5204835012345671000\nGB82WEST12345698765432");
    PROG("Afficher « CH53 0483 5012 3456 7100 0 » en IBAN.", "~sa clé de contrôle ne correspond pas");
    PROG("Afficher « 1234 » en IBAN.", "~de 15 à 34 lettres et chiffres");
    PROG("Afficher « 1234 5678 9012 3456 » en IBAN.", "~les deux lettres du pays");
    PROG("Afficher 3 en IBAN.", "~« en IBAN » s'applique à un texte, pas à un nombre.");
    PROG("Le t vaut « a » suivi d'un saut de ligne suivi de « b ».\nAfficher la longueur du t.", "3");
    PROG("Afficher 1590 en format technique.\nAfficher 260,00 en format technique.\nAfficher (0 − 12,30) en format technique.",
         "1590\n260.00\n-12.30");
    PROG("Afficher 20.10.2026 en format technique.\nAfficher « CH52 0483 5012 3456 7100 0 » en format technique.",
         "2026-10-20\nCH5204835012345671000");
    PROG("Le n vaut absent.\nAfficher le code QR de n.", "~Un code QR se fait d'un texte : celui-ci est absent.");
    /* « Ouvrir » un fichier (2.26) : rien d'autre ; un fichier absent ; la console ne montre rien */
    PROG("Ouvrir 3.", "~« Ouvrir » montre un fichier : « Ouvrir le justificatif du frais. »");
    PROG("Le n vaut 3.\nOuvrir le n.", "~« Ouvrir » montre un fichier.");
    PROG("Un dossier, conservé, a :\n    un scan (fichier), facultatif.\nLe d vaut un nouveau dossier.\nOuvrir le scan du d.",
         "~Rien à ouvrir : ce fichier est absent.");
    /* Documents (2.25) : refus de l'analyse */
    PROG("Le document de reçu montre :\n    un bouton « OK ».", "~Un document se lit sur papier : des textes, des images et des listes, ni bouton ni zone.");
    PROG("Ouvrir le document de rien.", "~Document inconnu");
    PROG("Le document de reçu montre :\n    le texte « A », à 3 de la gauche et 4 du haut.", "~« à … de la gauche » ne vaut que dans un bloc");
    /* « tarif de suivi » sujet d'une modification : « suivi » après « de » est un mot de nom (19 octobre 2026) */
    PROG("Un soin a : un tarif de suivi.\nLe s vaut un nouveau soin :\n    Le tarif de suivi vaut 70.\n"
         "Le tarif de suivi du s devient 80.\nAfficher le tarif de suivi du s.", "80");
    /* « anciennement » : les refus de l'analyse (§ 16.7, 2.23) */
    PROG("Un client, conservé, a : un nom (texte), anciennement « nom ».", "~« nom » est déjà le nom de ce champ.");
    PROG("Un client, conservé, a : un nom (texte), un titre (texte), anciennement « nom ».",
         "~« nom » est encore un champ de « client » : un renommage suppose que l'ancien nom a disparu.");
    PROG("Un client, conservé, a : un nom (texte), anciennement « x », un titre (texte), anciennement « x ».",
         "~Deux champs de « client » s'appelaient « x »");
    PROG("Un client a : un nom, anciennement « x ».", "~Seul un champ d'entité se renomme");
    PROG("Un client, conservé, a : un nom (texte), anciennement 3.", "~Le nom d'avant s'écrit entre guillemets");
    PROG("Un client, conservé, a : un nom (texte).\nUn patient, conservé, anciennement « client », a : un nom (texte).",
         "~« client » est encore déclarée : un renommage suppose que l'ancien nom a disparu du programme.");
    PROG("Un patient, anciennement « client », a : un nom.", "~Seule une entité se renomme");
    PROG("Un patient, conservé, anciennement « patient », a : un nom (texte).", "~« patient » est déjà le nom de cette entité.");
    essais_migration();
    essais_base_chiffree();
    essais_secours();
    essais_ecrans();
    essais_etats_ecran();
    essai_case_vide();
    essai_image();

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
