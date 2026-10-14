// GrymoiR : l'atelier, le dessin d'un écran (grammaire, § 22). Le même pour l'exécution et pour l'aperçu de
// l'onglet « Écrans » (docs/atelier.md, § 5 bis) : ce que le développeur voit est ce qu'aura l'utilisateur.
#ifndef GRYM_ATELIER_VUE_ECRAN_H
#define GRYM_ATELIER_VUE_ECRAN_H

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
    QString texte;          // libellé d'un bouton, texte fixe, nom affiché d'une zone ; pour une liste, l'entité
    QStringList colonnes;   // liste : titres des colonnes
    QString type;           // zone : son type
    QStringList choix;      // zone liée à une entité : les clés, pour un menu
    bool plusieurs_lignes = false;   // zone (texte) « sur plusieurs lignes » (§ 22.6)
    bool facultatif = false;         // zone : boutons radio, un choix « — » de plus pour l'absence (§ 22.7)
    QByteArray octets;               // image (§ 22.8) : le fichier ; vide, l'image se montre par son nom
    bool lecture_seule = false;      // zone « en lecture seule »
    bool place = false;              // élément d'un bloc en position libre (§ 22.9)
    int gauche = 0, haut = 0;        // en pixels, depuis le coin du bloc
    int largeur = 0, hauteur = 0;    // en pixels ; 0 : la taille naturelle
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

// La taille d'un élément placé : la naturelle, sauf ce que le programme dit (§ 22.9).
QSize taille_placee(QWidget *w, int largeur, int hauteur);

// Dessine l'écran : blocs emboîtés, boutons qui se suivent sur une ligne en bas à droite, listes qui prennent
// la hauteur libre, zones avec les contrôles des formulaires. Ne branche aucun événement. Un logo non nul
// s'affiche à gauche du titre, à `taille_logo` pixels de haut.
VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements, const QPixmap &logo,
                        int taille_logo);
VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements);

#endif
