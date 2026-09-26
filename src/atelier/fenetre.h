// GrymoiR : l'atelier, fenêtre principale (docs/atelier.md, jalon A1).
#ifndef GRYM_ATELIER_FENETRE_H
#define GRYM_ATELIER_FENETRE_H

#include <QMainWindow>

#include "projet.h"
#include "reecriture.h"
#include "schema.h"

#include <QVector>
#include <functional>

class Editeur;
class QFileSystemModel;
class ModeleProjet;
class QTreeView;
class QListWidget;
class QProcess;
class QAction;
class Aide;
class VueSchema;
class PanneauEntite;
class OngletEcrans;
class QTabWidget;
class QLabel;

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
    void rafraichir_schema();              // relit les entités du projet et redessine le schéma (A2-a)
    FichierProjet projet_fichier;          // projet.grymatelier (docs/atelier.md, § 6.1)
    VueSchema *schema;
    QTabWidget *onglets;
    QLabel *schema_etat;
    PanneauEntite *panneau;
    OngletEcrans *onglet_ecrans;           // A4 : l'onglet « Écrans »
    QAction *action_annuler_geste;
    QVector<Geste> gestes;                 // les gestes de l'éditeur de données, du plus ancien au plus récent
    QString entite_choisie;
    QVector<EntiteSchema> entites_lues;   // le schéma à sa dernière lecture
    QStringList types_lus;
    void montrer_choisie();
    // Un geste de l'éditeur de données : enregistre d'abord le code en cours, applique, puis relit tout.
    void appliquer(const std::function<QString(Geste *)> &geste, const QString &choisir_ensuite);
    void nouvelle_entite();
    void annuler_dernier_geste();
    void apres_geste(const QStringList &touches, const QString &choisir_ensuite);
};

#endif
