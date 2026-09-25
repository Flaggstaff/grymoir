// GrymoiR : l'atelier, fenêtre principale (docs/atelier.md, jalon A1).
#ifndef GRYM_ATELIER_FENETRE_H
#define GRYM_ATELIER_FENETRE_H

#include <QMainWindow>

class Editeur;
class QFileSystemModel;
class ModeleProjet;
class QTreeView;
class QListWidget;
class QProcess;
class QAction;
class Aide;

class Fenetre : public QMainWindow {
    Q_OBJECT
public:
    Fenetre();
    // Un dossier ouvre le projet ; un fichier ouvre son dossier, puis le fichier.
    void ouvrir(const QString &chemin);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void choisir_projet();
    void ouvrir_projet(const QString &dossier);
    void ouvrir_fichier(const QString &chemin);
    bool enregistrer();
    bool quitter_fichier();   // faux : l'utilisateur renonce
    void mettre_a_jour_titre();
    void montrer_diagnostic();
    void lancer();
    void arreter();
    void execution_finie();

    QString projet, fichier;
    Editeur *editeur;
    ModeleProjet *modele;
    QTreeView *arbre;
    QListWidget *erreurs;
    QProcess *execution = nullptr;         // le programme lancé, dans son propre processus
    QAction *action_lancer, *action_arreter;
    Aide *aide = nullptr;                  // la grammaire, ouverte au premier F1 (§ 6.5)
    QString erreur_execution;              // la dernière erreur d'exécution, en clair
    QString erreur_fichier;                // le fichier de cette erreur (le programme, ou un fichier utilisé)
    int erreur_ligne = 0, erreur_colonne = 0;
    QString programme_a_lancer();          // le fichier courant s'il est un programme, sinon le programme principal
    void choisir_principal();
};

#endif
