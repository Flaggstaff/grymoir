// GrymoiR : l'atelier, aide (docs/atelier.md, § 6.5) : la grammaire du langage, embarquée dans l'exécutable,
// avec sa table des matières et une recherche. Toujours accordée à la version de l'atelier, même hors ligne.
#ifndef GRYM_ATELIER_AIDE_H
#define GRYM_ATELIER_AIDE_H

#include <QMainWindow>

class QTextBrowser;
class QTreeWidget;
class QLineEdit;
class QLabel;

class Aide : public QMainWindow {
    Q_OBJECT
public:
    explicit Aide(QWidget *parent = nullptr);
    // Chercher `texte` à partir du curseur, en reprenant au début ; faux s'il n'apparaît nulle part.
    bool chercher(const QString &texte, bool arriere = false);
    // Aller au titre qui commence par `debut` (« 21. », « 16.4 ») ; faux s'il n'existe pas.
    bool aller_au_titre(const QString &debut);
    QTreeWidget *table() const { return sommaire; }
    QTextBrowser *texte() const { return lecteur; }

private:
    void aller_au_bloc(int position);
    QTextBrowser *lecteur;
    QTreeWidget *sommaire;
    QLineEdit *recherche;
    QLabel *etat;
};

#endif
