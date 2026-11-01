// GrymoiR : l'atelier, chercher et remplacer (docs/atelier.md, § 5 quindecies, 1er novembre 2026).
// Dans le fichier ouvert : une barre sous l'éditeur (Cmd+F, Cmd+G, Maj+Cmd+G, Alt+Cmd+F). Dans tout le projet : une
// fenêtre qui liste chaque occurrence, fichier et ligne ; un double-clic y mène ; « Tout remplacer » est un geste, qui
// s'annule comme ceux de l'éditeur de données.
#ifndef GRYM_ATELIER_RECHERCHE_H
#define GRYM_ATELIER_RECHERCHE_H

#include <QDialog>
#include <QRegularExpression>
#include <QVector>
#include <QWidget>

class Editeur;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

// Le motif cherché : le texte tel quel, au mot entier ou non, en respectant la casse ou non. Mot entier : ni lettre, ni
// chiffre, ni « _ » juste avant ni juste après (accents compris : « séance » n'est pas dans « séances »).
QRegularExpression motif_recherche(const QString &texte, bool mot_entier, bool casse);

// Chaque occurrence remplacée par `par`, mot pour mot (un « \1 » reste « \1 ») ; *n : leur nombre.
QString remplacer_litteralement(const QString &texte, const QRegularExpression &m, const QString &par, int *n = nullptr);

struct Occurrence {
    QString fichier;   // chemin absolu
    int ligne = 0;     // à partir de 1
    int colonne = 0;   // à partir de 1, en unités UTF-16 (celles de l'éditeur)
    QString extrait;   // la ligne, sans son retrait
};

// Les fichiers du projet (.grym et .grymc), dossiers compris, sauf les sauvegardes et la corbeille ; triés.
QStringList fichiers_du_projet(const QString &projet);
// Chaque occurrence, fichier par fichier, ligne par ligne. `ouverts` : le texte des fichiers modifiés dans l'éditeur, lu à
// la place du disque.
QVector<Occurrence> chercher_dans_projet(const QString &projet, const QRegularExpression &m,
                                         const QMap<QString, QString> &ouverts = {});

class BarreRecherche : public QWidget {
    Q_OBJECT
public:
    explicit BarreRecherche(Editeur *e, QWidget *parent = nullptr);
    void montrer(bool remplacer);        // le texte choisi devient le motif ; le champ prend le focus
    bool suivant(bool arriere = false);  // va à l'occurrence suivante, en repartant du début ; faux : aucune
    int remplacer_tout();                // en un seul pas d'annulation de l'éditeur ; rend le nombre
    bool remplacer_un();                 // remplace l'occurrence choisie, puis va à la suivante
    QLineEdit *cherche() const { return cherche_; }
    QLineEdit *remplace() const { return remplace_; }
    QCheckBox *mot_entier() const { return mot_entier_; }
    QLabel *compte() const { return compte_; }
protected:
    void keyPressEvent(QKeyEvent *e) override;   // Échap ferme, Entrée cherche la suivante (Maj : la précédente)
private:
    Editeur *editeur_;
    QLineEdit *cherche_, *remplace_;
    QCheckBox *mot_entier_, *casse_;
    QLabel *compte_;
    QWidget *rangee_remplacer_;
    QRegularExpression motif() const;
    void compter();
};

class RechercheProjet : public QDialog {
    Q_OBJECT
public:
    // `ouverts` : rendu par la fenêtre, le texte des fichiers ouverts et modifiés
    RechercheProjet(const QString &projet, std::function<QMap<QString, QString>()> ouverts, QWidget *parent = nullptr);
    void chercher_maintenant();
    QLineEdit *cherche() const { return cherche_; }
    QLineEdit *remplace() const { return remplace_; }
    QCheckBox *mot_entier() const { return mot_entier_; }
    QTreeWidget *resultats() const { return resultats_; }
    QPushButton *bouton_remplacer() const { return remplacer_; }
    QRegularExpression motif() const;
signals:
    void aller(const QString &fichier, int ligne, int colonne);
    // « Tout remplacer » : la fenêtre demande confirmation, écrit, et en fait un geste annulable
    void remplacer_tout(const QRegularExpression &motif, const QString &par);
private:
    QString projet_;
    std::function<QMap<QString, QString>()> ouverts_;
    QLineEdit *cherche_, *remplace_;
    QCheckBox *mot_entier_, *casse_;
    QTreeWidget *resultats_;
    QLabel *compte_;
    QPushButton *remplacer_;
};

#endif
