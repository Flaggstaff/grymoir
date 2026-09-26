// GrymoiR : l'atelier, l'onglet « Écrans » (docs/atelier.md, § 5 bis, A4-a) : les écrans du projet, leur
// aperçu (la structure, pas les données), et les propriétés de l'élément choisi. En lecture seule.
#ifndef GRYM_ATELIER_ONGLET_ECRANS_H
#define GRYM_ATELIER_ONGLET_ECRANS_H

#include "vue_ecran.h"

#include <QWidget>

// Un élément d'écran, tel que l'arbre le décrit.
struct ElementLu {
    int sorte = 0;           // SorteElement (src/interface.h)
    QString texte;           // libellé, texte fixe, nom de la zone, entité de la liste
    QStringList colonnes;    // liste : titres des colonnes, par défaut ou choisies (« avec »)
    QString type;            // zone : type
    bool facultatif = false;
    QString depart;          // zone : valeur de départ, telle qu'écrite
    QString tri;             // liste : champ du tri, « » sinon
    bool decroissant = false;
    QString ecrit;           // le texte GrymoiR de l'élément, tel qu'il est dans le fichier
    QString evenement_fichier;   // l'événement de l'élément (clic, choix, changement), s'il existe
    int evenement_ligne = 0;
};

struct EcranLu {
    QString nom, titre, fichier;   // nom : « des compositeurs » ; fichier : chemin absolu de la déclaration
    int ligne = 0;
    QVector<ElementLu> elements;
};

// Les écrans déclarés dans les fichiers .grym et .grymc du dossier, fichiers utilisés compris, chacun une fois.
// *problemes reçoit les fichiers qui ne s'analysent pas.
QVector<EcranLu> lire_ecrans(const QString &dossier, QStringList *problemes = nullptr);

class QListWidget;
class QTextBrowser;
class QPushButton;
class QScrollArea;
class QLabel;

class OngletEcrans : public QWidget {
    Q_OBJECT
public:
    explicit OngletEcrans(QWidget *parent = nullptr);
    void montrer(const QString &dossier);   // relit le projet ; garde l'écran et l'élément choisis
    void choisir_ecran(const QString &nom);
    void choisir_element(int k);            // −1 : l'écran lui-même
    const QVector<EcranLu> &ecrans() const { return lus; }
    QString proprietes() const;             // le texte du panneau des propriétés (pour les essais)
    QWidget *controle(int k) const { return k >= 0 && k < vue.controles.size() ? vue.controles[k] : nullptr; }

signals:
    void ouvrir(const QString &fichier, int ligne);   // « Voir l'événement », « Voir la déclaration »

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void dessiner();
    QVector<EcranLu> lus;
    int courant = -1, element = -1;
    VueEcran vue;
    QListWidget *liste;
    QScrollArea *centre;
    QTextBrowser *panneau;
    QPushButton *voir_evenement, *voir_declaration;
    QLabel *etat;
};

#endif
