// GrymoiR : l'atelier, le dessin d'un écran (grammaire, § 22). Le même pour l'exécution et pour l'aperçu de
// l'onglet « Écrans » (docs/atelier.md, § 5 bis) : ce que le développeur voit est ce qu'aura l'utilisateur.
#ifndef GRYM_ATELIER_VUE_ECRAN_H
#define GRYM_ATELIER_VUE_ECRAN_H

#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;
class QLabel;
class QTableWidget;
class QPushButton;

// Un élément à dessiner ; `sorte` reprend SorteElement (src/interface.h).
struct ElementVue {
    int sorte = 0;
    QString texte;          // libellé d'un bouton, texte fixe, nom affiché d'une zone ; pour une liste, l'entité
    QStringList colonnes;   // liste : titres des colonnes
    QString type;           // zone : son type
    QStringList choix;      // zone liée à une entité : les clés, pour un menu
};

// Ce que le dessin a produit : pour chaque élément, le contrôle qui le montre (nullptr pour un bloc).
struct VueEcran {
    QWidget *vue = nullptr;
    QLabel *titre = nullptr;
    QLabel *erreur = nullptr;                // caché ; l'exécution y écrit un événement raté
    QVector<QWidget *> controles;            // un par élément : QLabel, QTableWidget, QPushButton, ou la zone
    QVector<QTableWidget *> tables;          // un par élément, nullptr sauf pour une liste
    QVector<QWidget *> zones;                // un par élément, nullptr sauf pour une zone
    QVector<QPushButton *> boutons;          // les boutons, dans l'ordre
};

// Dessine l'écran : blocs emboîtés, boutons qui se suivent sur une ligne en bas à droite, listes qui prennent
// la hauteur libre, zones avec les contrôles des formulaires. Ne branche aucun événement.
VueEcran dessiner_ecran(const QString &titre, const QVector<ElementVue> &elements);

#endif
