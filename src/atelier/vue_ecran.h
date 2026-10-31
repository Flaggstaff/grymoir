// GrymoiR : l'atelier, le dessin d'un écran (grammaire, § 22). Le même pour l'exécution et pour l'aperçu de
// l'onglet « Écrans » (docs/atelier.md, § 5 bis) : ce que le développeur voit est ce qu'aura l'utilisateur.
#ifndef GRYM_ATELIER_VUE_ECRAN_H
#define GRYM_ATELIER_VUE_ECRAN_H

#include <QWidget>
#include <QPaintEvent>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;
class QLabel;
class QPixmap;
class QTableWidget;
class QPushButton;
class QSize;

// Un élément à dessiner ; `sorte` reprend SorteElement (src/interface.h).
struct ElementVue {
    int sorte = 0;
    int graphique = 0;      // graphique (§ 22.11) : 2 en secteurs, 4 une somme
    bool principal = false; // un bouton « , principal » : à la couleur d'accent (2.14)
    bool tuile = false;     // un bouton « , en tuile » (2.15) : titre, description (indication), image (octets)
    bool a_cote_du_titre = false;   // un bouton « , à côté du titre » (2.17)
    bool inverse = false;           // un groupe « , en couleur inversée » (2.18)
    bool cartes = false;            // une liste « , en cartes » (2.19)
    bool capitales = false;         // un texte « , en petites capitales » (2.20)
    QString confirmation;           // un bouton « , à confirmer par « … » » : la question, vide sinon (2.65)
    QString confirmation_mot;       // « … en tapant « effacer » » : le mot, vide sinon (2.65)
    bool confirmation_si_modifie = false;   // « … si l'écran est modifié » (2.67)
    QString texte;          // libellé d'un bouton, texte fixe, nom affiché d'une zone ; pour une liste, l'entité
    QStringList colonnes;   // liste : titres des colonnes
    QString type;           // zone : son type
    QStringList choix;      // zone liée à une entité : les clés, pour un menu
    bool plusieurs_lignes = false;   // zone (texte) « sur plusieurs lignes » (§ 22.6)
    bool facultatif = false;         // zone : boutons radio, un choix « — » de plus pour l'absence (§ 22.7)
    QByteArray octets;               // image (§ 22.8) : le fichier ; vide, l'image se montre par son nom
    bool lecture_seule = false;      // zone « en lecture seule »
    bool interrupteur = false;       // zone (vrai ou faux) « en interrupteur » (§ 22.6)
    bool multiple = false;           // « des genres (genre) » : une case par choix (§ 22.1)
    QString indication;              // « , avec l'indication « … » », sous le libellé (§ 22.6)
    bool recherche = false;          // zone d'un objet « avec recherche » (§ 22.6)
    int longueur_max = 0;            // zone de texte « au plus N caractères » : la frappe s'arrête là (§ 22.6)
    bool masque = false;             // zone de texte « masqué » : un mot de passe (§ 22.6)
    bool dossier = false;            // zone de texte « avec le choix d'un dossier » (2.32)
    int largeur_caracteres = 0;      // « , large de 12 caractères » (2.34) ; 0 : la largeur de la colonne
    QString unite;                   // « , avec l'unité « CHF » » ; unite_devant : à gauche du champ (§ 22.6)
    bool unite_devant = false;
    bool place = false;              // élément d'un bloc en position libre (§ 22.9)
    int gauche = 0, haut = 0;        // en pixels, depuis le coin du bloc
    int largeur = 0, hauteur = 0;    // en pixels ; 0 : la taille naturelle
    // texte d'une fiche déduite (§ 22.3) : la valeur commence à ce rang (0 : texte ordinaire), en étiquette de cette
    // couleur (b v t p g r o, § 16.15), ou en lien : LIEN_COURRIEL, LIEN_WEB (§ 22.13, src/interface.h)
    int valeur_depuis = 0;
    QChar etiquette;
    int lien = 0;
};

// Ce que le dessin a produit : pour chaque élément, le contrôle qui le montre (nullptr pour un bloc).
struct VueEcran {
    QWidget *vue = nullptr;
    QLabel *titre = nullptr;
    QLabel *logo = nullptr;                  // à gauche du titre, s'il y a un logo (grammaire, § 22.5)
    QLabel *erreur = nullptr;                // caché ; l'exécution y écrit un événement raté
    QVector<QWidget *> controles;            // un par élément : QLabel, QTableWidget, QPushButton, ou la zone
    QVector<QTableWidget *> tables;          // un par élément, nullptr sauf pour une liste
    QVector<QWidget *> zones;                // un par élément, nullptr sauf pour une zone
    QVector<QPushButton *> boutons;          // les boutons, dans l'ordre
};

// Une zone (texte) à valeurs permises, de deux à quatre : des boutons radio plutôt qu'un menu (§ 22.7).
inline bool en_boutons_radio(const ElementVue &e) { return e.type == "texte" && e.choix.size() >= 2 && e.choix.size() <= 4; }

// Montre une image dans un label : sa taille, réduite pour tenir dans 480 × 320 (§ 22.8), ou sa taille placée ;
// sans octets lisibles, le texte de repli (le nom du fichier, ou rien pour une image calculée absente).
void montrer_image(QLabel *l, const QByteArray &octets, const QString &repli, int largeur = 0, int hauteur = 0);

// Les couleurs d'une étiquette (§ 16.15), celles du navigateur : le fond, le texte ; vides pour une lettre inconnue.
QPair<QString, QString> teinte_etiquette(QChar lettre);

// Une valeur de fiche (§ 22.3, § 22.13) en HTML pour un QLabel ou un QTextBrowser : un lien d'écriture pour un
// courriel, un lien pour une adresse web en http:// ou https:// ; sinon le texte, échappé. Vide si ni l'un ni l'autre.
QString lien_html(const QString &valeur, int lien);

// Un graphique (§ 22.11) : des barres, ou des secteurs avec leur légende, à partir de libellés et de valeurs. Sans
// données (l'aperçu de l'atelier), il montre un exemple grisé.
class Graphique : public QWidget {
public:
    explicit Graphique(bool secteurs, QWidget *parent = nullptr);
    void donner(const QStringList &libelles, const QVector<double> &valeurs);
    QStringList libelles() const { return noms; }
    QVector<double> valeurs() const { return vals; }
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *) override;
private:
    bool secteurs;
    bool exemple = true;
    QStringList noms;
    QVector<double> vals;
};
double nombre_affiche(const QString &t);
// La taille du titre d'un écran (§ 22.5) : −1 en petit, 1 en grand, 0 celle du thème ; une règle propre au titre,
// qui l'emporte sur la feuille du thème.
void taille_du_titre(QLabel *titre, int taille);   // « 1'234.5 », « 12,5 » → 1234.5, 12.5

// La taille d'un élément placé : la naturelle, sauf ce que le programme dit (§ 22.9).
QSize taille_placee(QWidget *w, int largeur, int hauteur);

// Dessine l'écran : blocs emboîtés, boutons qui se suivent sur une ligne en bas à droite, listes qui prennent
// la hauteur libre, zones avec les contrôles des formulaires. Ne branche aucun événement. Un logo non nul
// s'affiche à gauche du titre, à `taille_logo` pixels de haut.
VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements, const QPixmap &logo,
                        int taille_logo);
VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements);
void serrer_ecran(QWidget *vue);   // à l'exécution : la place en trop sous le dernier élément (24 octobre 2026)

// Une ligne de total (§ 22.3, src/interface.h) : sa première cellule porte ce drapeau. Elle reste la dernière ligne quel
// que soit le tri à l'écran ; ni choisie ni sélectionnée.
constexpr int ROLE_TOTAL = 0x0101;   // Qt::UserRole + 1
void epingler_total(QTableWidget *t);
// Une zone (date) : une icône de calendrier au bout du champ ; le jour choisi s'écrit « 19.10.2026 », comme si on
// l'avait tapé, et la zone change (§ 22.1). Le champ reste un champ de texte : on peut toujours taper, ou le vider.
class QLineEdit;
void ajouter_calendrier(QLineEdit *champ);
// Une zone (dossier) (2.32) : une icône au bout du champ ouvre le sélecteur de dossiers du système.
void ajouter_choix_dossier(QLineEdit *champ);

// Une cellule de liste qui se trie comme on la lit (§ 22.1) : les nombres par leur valeur (« 70 » avant « 130 »,
// « 1'234,50 », « −3 »), les dates par le calendrier (jj.mm.aaaa), les heures par l'horloge, le reste selon la langue.
#include <QTableWidgetItem>
class CelluleTriable : public QTableWidgetItem {
public:
    explicit CelluleTriable(const QString &texte) : QTableWidgetItem(texte) {}
    bool operator<(const QTableWidgetItem &autre) const override;
};

#endif
