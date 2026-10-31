// GrymoiR : l'atelier, exécution d'un programme dans sa propre fenêtre (docs/atelier.md, § 5, A1).
// grym-atelier --lancer fichier.grym : un processus à part, pour que l'atelier survive à tout ce qui s'y passe.
// La machine tourne dans un fil à elle ; la fenêtre reste vivante pendant un long calcul, et « Arrêter »
// lève l'interruption que la machine consulte à chaque saut arrière et à chaque appel (docs/vm.md, § 6).
#ifndef GRYM_ATELIER_EXECUTION_H
#define GRYM_ATELIER_EXECUTION_H

#include <QTemporaryDir>
#include <QMainWindow>
#include <QPixmap>
#include <QSemaphore>
#include <QThread>
#include <QVector>
#include <QMap>
#include <atomic>

extern "C" {
#include "interface.h"
}

class QTextBrowser;
class QWidget;
class QPushButton;
class QLabel;

// Une réponse saisie dans la fenêtre, avant sa validation par la machine.
struct Saisie {
    QString ligne;
    bool vider = false;
    bool fichier = false;
    QByteArray octets;
    QString nom_fichier;
};

// Le fil de la machine. Il ne touche jamais aux contrôles : il émet des signaux, et attend la fenêtre
// sur un sémaphore quand une question se pose.
class Travail : public QThread {
    Q_OBJECT
public:
    std::atomic<int> transcriptions_en_cours{0};   // des zones transcrites en arrière-plan (24 octobre 2026)
    explicit Travail(const QString &chemin);
    void run() override;
    // Une application fabriquée par l'atelier : sa base dans le dossier de données du système, pas à côté du
    // programme (docs/atelier.md, § 8). Vide : à côté du programme, comme grym lancer.
    QString base_imposee;
    QString dossier_ecriture;   // une application fabriquée : ses fichiers s'écrivent dans Documents/<Nom> (§ 15.6)

    // Rempli par la fenêtre avant de libérer `reponse` : 0 envoyé, 1 annulé, 2 interrompu.
    int issue = 0;
    bool nouvelle = true;   // première pose de cette question : la fenêtre repart de zéro
    QVector<Saisie> saisies;
    QSemaphore reponse;

    // Accès de la fenêtre aux champs, pendant que la machine attend.
    Champ *champs = nullptr;
    size_t n = 0;
    QVector<QString> refus;
    QVector<bool> acceptes;

signals:
    void transcrire_zone(int cible, const QByteArray &octets);   // la transcription en arrière-plan (24 octobre 2026)
    void texte(const QString &t);
    void effacer();
    void image(const QByteArray &octets, const QString &description);
    void son(const QByteArray &octets, const QString &nom);   // « Afficher le mémo de v. » (§ 15.7)
    // les images de la fiche, désignées « image:K » ; ses sons, « son:K » (§ 15.7) : nom, puis octets, deux par son
    void fiche(const QString &html, const QList<QByteArray> &images, const QList<QByteArray> &sons);
    void question();
    // Écrans (grammaire, § 22) : chaque élément en (sorte, texte, colonnes)
    void ecran_ouvert(const QString &titre, const QVector<int> &sortes, const QStringList &textes,
                      const QVector<QStringList> &colonnes);
    void ecran_lignes(int element, const QStringList &cellules, int nb_lignes);
    void ecran_attente();
    void ecran_erreur(const QString &message);
    void ecran_valeurs(const QStringList &valeurs);   // une par élément ; vide pour un élément qui n'est pas une zone
    void ecran_etats(const QVector<int> &etats);      // un par élément : bit 1 inactif, bit 2 caché (§ 22.6)
    void ecran_image(int element, const QByteArray &octets);   // une image calculée, vide si absente (§ 22.8)
    void ecran_texte(int element, const QString &texte);       // un texte calculé, relu avant chaque attente (2.24)
    void ecran_erreurs_zones(const QVector<int> &elements, const QStringList &messages);   // les zones en faute (§ 22.6)
    void ecran_ferme();
    void apparence(int couleur, const QByteArray &logo);   // « Les écrans ont … » (grammaire, § 22.5)
    // le thème libre (2.13) : noms, valeurs claires, sombres (vides : celles du thème), le clair imposé, la police
    void theme(const QStringList &noms, const QStringList &clair, const QStringList &sombre, bool clair_impose,
               const QByteArray &police);
    void signature(const QByteArray &octets);   // l'image du bandeau (2.16)
    void reponses(const QString &echo);   // les réponses acceptées, recopiées dans le fil comme en console
    void utilise_micro();   // le programme a un champ ou une zone (son) : le micro se demande dès l'ouverture (§ 15.7)
    void document(const QByteArray &octets, const QString &nom);   // « Ouvrir le document … », « Ouvrir x. » (2.25, 2.26)
    void adresse(const QString &url);   // « Ouvrir le lien OneDoc du cabinet. » : une adresse web (2.41)
    void fin(bool ok, const QString &erreur, const QString &annulation);

public:
    // Appelées par la machine, dans ce fil.
    void vider_sortie(Chaine *sortie);
    Issue formulaire(Chaine *sortie, Champ *champs, size_t n, size_t *arret, Validation valider, void *vcontexte);
    Issue attendre_evenement(Chaine *sortie, Evenement *e);
    Evenement evenement = {EVENEMENT_FERMETURE, 0, 0, nullptr, nullptr, 0, nullptr, 0};   // rempli par la fenêtre avant de libérer `reponse`
    QVector<int> nb_colonnes;                             // de chaque élément de l'écran ouvert
    QStringList types_zones;                              // de chaque élément : le type d'une zone, sinon vide
    QVector<int> facultatives;
    QVector<QStringList> choix_zones;                     // zone liée : les clés, pour un menu
    QVector<int> options_zones;                           // zone : bit 1 sur plusieurs lignes, bit 2 en lecture seule
    QVector<QByteArray> images;                           // image (§ 22.8) : ses octets, sinon vide
    QVector<QVector<int>> places;                         // position libre (§ 22.9) : gauche, haut, largeur, hauteur ; vide sinon
    QVector<int> graphiques;                              // graphique (§ 22.11) : 1, + 2 en secteurs, + 4 une somme ; 0 sinon
    int taille_titre = 0;                                 // la taille du titre de l'écran qui s'ouvre (§ 22.5)
    QStringList indications;                              // zone : son indication, vide sinon (§ 22.6)
    QStringList confirmations;                            // bouton à confirmer : « question ␟ mot », vide sinon (2.65)
    QVector<int> longueurs;                               // zone de texte : « au plus N caractères », 0 sinon (§ 22.6)
    QStringList unites;                                   // zone : « <CHF » devant, « >% » derrière, vide sinon (§ 22.6)
    QVector<QVector<int>> valeurs_fiche;                  // texte de fiche (§ 22.3) : rang de la valeur, étiquette, lien ; vide sinon
    // les descriptions des écrans du dessous (écrans empilés, § 22.3), rendues à la fermeture de celui du dessus
    struct Description { QVector<int> nb_colonnes; QStringList types_zones; QVector<int> facultatives; QVector<QStringList> choix_zones;
                         QVector<int> options_zones; QVector<QByteArray> images; QVector<QVector<int>> places;
                         QVector<int> graphiques; };
    QVector<Description> dessous;
    size_t colonnes_de(size_t element) const { return element < (size_t)nb_colonnes.size() ? (size_t)nb_colonnes[(int)element] : 0; }

private:
    QString chemin;
};

class Execution : public QMainWindow {
    Q_OBJECT
public:
    void transcrire_dans(int cible, const QByteArray &octets);   // la transcription en arrière-plan (24 octobre 2026)
    explicit Execution(const QString &chemin);
    void placer_base(const QString &chemin) { travail.base_imposee = chemin; }   // avant demarrer()
    void placer_ecriture(const QString &dossier) { travail.dossier_ecriture = dossier; }   // avant demarrer()
    // Une application fabriquée : la fenêtre prend la taille de chaque écran qui n'en recouvre aucun (la connexion, puis
    // l'accueil), sans dépasser l'écran de l'ordinateur, et se centre ; plus grand, il défile. Avant demarrer().
    void ajuster_au_premier_ecran() { ajuster_fenetre = true; }
    ~Execution() override;
    void demarrer();
    void demander_micro();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    bool micro_signale = false;   // le refus du micro dit une fois par lancement
    QTemporaryDir documents;      // les PDF montrés (2.25), le temps de la fenêtre
    int nb_documents = 0;
    void poser();
    void envoyer(int issue);
    void arreter();
    void terminer(bool ok, const QString &erreur, const QString &annulation);
    void ecrire(const QString &t, bool erreur = false);
    // Ce qui se voit (docs/atelier.md, § 8) : un écran ouvert occupe toute la fenêtre ; le fil et la zone des questions
    // n'apparaissent que pour une question, à la fin du programme, ou sur demande (Ctrl+J, « le fil d'exécution »).
    void ranger();
    bool console = false;
    QWidget *bas = nullptr;

    Travail travail;
    QTextBrowser *fil;
    class QSplitter *partage;
    QWidget *zone;
    QPushButton *bouton_envoyer, *bouton_annuler, *bouton_arreter;
    QLabel *etat;
    QVector<QWidget *> controles;   // un par champ
    QVector<QWidget *> videurs;     // case « vider », ou nullptr
    QVector<Saisie> fichiers_choisis;
    // L'écran ouvert (§ 22) : au-dessus du fil ; ses listes et ses boutons, par élément
    void montrer_ecran(const QString &titre, const QVector<int> &sortes, const QStringList &textes,
                       const QVector<QStringList> &colonnes);
    QVector<QWidget *> zones;              // zone de saisie de chaque élément, ou nullptr
    QVector<QWidget *> controles_ecran;    // ce qui montre chaque élément (libellé compris pour une zone), ou nullptr
    QVector<int> etats_recus;              // les derniers états de chaque élément (§ 22.6), bit ETAT_MODIFIE compris (2.67)
    bool saisie_en_suspens() const;        // une zone tapée que la machine n'a pas encore reçue (2.67)
    void fermer_par_echap();               // Échap : le bouton « Fermer » de l'écran, comme un clic (2.67)
    QString texte_zone(int k) const;
    QVector<long> lignes_choisies;         // envoyées avec chaque événement (§ 22.2)
    QByteArray texte_envoye;
    void envoyer_evenement(SorteEvenement sorte, int element, int ligne);
    void effacer_erreurs_zones();
    QByteArray octets_envoyes;   // le fichier choisi dans une zone de fichier, gardé jusqu'à l'événement suivant
    bool fichier_envoye = false;
    QWidget *vue_ecran = nullptr;   // l'écran montré : sa zone de défilement, qui contient le cadre centré
    bool ajuster_fenetre = false;
    // les écrans du dessous, masqués tant qu'un écran les recouvre (§ 22.3)
    struct EcranMontre { QWidget *vue; QLabel *erreur; QVector<class QTableWidget *> tables; QVector<QPushButton *> boutons;
                         QVector<QWidget *> zones; QString titre; QVector<QWidget *> controles; };
    QVector<EcranMontre> recouverts;
    QPixmap logo;   // « … et le logo « … ». » (§ 22.5) : grand sur l'écran du dessous, petit sur les autres
    QPixmap signature;   // « … et la signature « … ». » (2.16) : en bandeau sur chaque écran ; le logo devient l'icône seule
    QLabel *erreur_ecran = nullptr;
    QVector<class QTableWidget *> tables;
    QVector<QPushButton *> boutons_ecran;
    bool attente_ecran = false;
    // Un clic arrivé pendant qu'une zone envoyait sa valeur (le bouton prend le focus, la zone le perd et part la
    // première) : gardé, et envoyé dès que l'écran attend de nouveau, s'il est toujours le même (21 octobre 2026)
    int clic_en_attente = -1;
    // Un clic : d'abord la valeur d'une zone encore modifiée (sous macOS, un bouton ne prend pas le focus : la zone
    // n'envoie rien d'elle-même), puis le clic, quand l'écran attend de nouveau (21 octobre 2026)
    void cliquer(int k);
    // un bouton « à confirmer » (2.65) : la question dans une fenêtre, Annuler d'abord ; le clic part si on confirme,
    // et, avec un mot à taper, le bouton ne s'active qu'une fois le mot écrit
    void confirmer(int k, QWidget *bouton, const QString &question, const QString &mot, bool par_la_croix = false);
    QWidget *ecran_du_clic = nullptr;
    bool attente = false, fini = false, fermer_a_la_fin = false;
    bool ecran_vu = false;
    int clic_apres_transcription = -1;       // un clic arrivé pendant une transcription : il part après elle
    QMap<int, QString> transcrits_a_envoyer; // les textes prêts pendant que la machine travaillait
    int reduction = 0;   // la fenêtre, réduite de ces pixels de chaque côté sous un écran qui recouvre l'accueil   // un écran s'est montré : à la fin, la fenêtre se ferme (24 octobre 2026)
    int images = 0;
    // Les sons du fil (§ 15.7) : « son:K » dans le texte, joués par un lecteur caché ; nom et octets
    QVector<QPair<QString, QByteArray>> sons_du_fil;
    class LecteurSon *lecteur_fil = nullptr;
    int son_joue = -1;
    void jouer_son_du_fil(int k);
    int lien_son(const QString &nom, const QByteArray &octets);   // range le son, rend son rang
};

// « La base est chiffrée. » (2.37) : demande le mot de passe dans une fenêtre, depuis n'importe quel fil ; une copie à
// libérer, ou NULL si l'utilisateur renonce. Pour machine_demande_mot_de_passe.
char *atelier_demander_mot_de_passe(void *contexte, int nouveau);

#endif
