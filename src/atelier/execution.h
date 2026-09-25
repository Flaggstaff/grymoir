// GrymoiR : l'atelier, exécution d'un programme dans sa propre fenêtre (docs/atelier.md, § 5, A1).
// grym-atelier --lancer fichier.grym : un processus à part, pour que l'atelier survive à tout ce qui s'y passe.
// La machine tourne dans un fil à elle ; la fenêtre reste vivante pendant un long calcul, et « Arrêter »
// lève l'interruption que la machine consulte à chaque saut arrière et à chaque appel (docs/vm.md, § 6).
#ifndef GRYM_ATELIER_EXECUTION_H
#define GRYM_ATELIER_EXECUTION_H

#include <QMainWindow>
#include <QSemaphore>
#include <QThread>
#include <QVector>

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
    explicit Travail(const QString &chemin);
    void run() override;

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
    void texte(const QString &t);
    void effacer();
    void image(const QByteArray &octets, const QString &description);
    void fiche(const QString &html, const QList<QByteArray> &images);
    void question();
    // Écrans (grammaire, § 22) : chaque élément en (sorte, texte, colonnes)
    void ecran_ouvert(const QString &titre, const QVector<int> &sortes, const QStringList &textes,
                      const QVector<QStringList> &colonnes);
    void ecran_lignes(int element, const QStringList &cellules, int nb_lignes);
    void ecran_attente();
    void ecran_erreur(const QString &message);
    void ecran_valeurs(const QStringList &valeurs);   // une par élément ; vide pour un élément qui n'est pas une zone
    void ecran_ferme();
    void reponses(const QString &echo);   // les réponses acceptées, recopiées dans le fil comme en console
    void fin(bool ok, const QString &erreur, const QString &annulation);

public:
    // Appelées par la machine, dans ce fil.
    void vider_sortie(Chaine *sortie);
    Issue formulaire(Chaine *sortie, Champ *champs, size_t n, size_t *arret, Validation valider, void *vcontexte);
    Issue attendre_evenement(Chaine *sortie, Evenement *e);
    Evenement evenement = {EVENEMENT_FERMETURE, 0, 0, nullptr, nullptr, 0};   // rempli par la fenêtre avant de libérer `reponse`
    QVector<int> nb_colonnes;                             // de chaque élément de l'écran ouvert
    QStringList types_zones;                              // de chaque élément : le type d'une zone, sinon vide
    QVector<int> facultatives;
    QVector<QStringList> choix_zones;                     // zone liée : les clés, pour un menu
    size_t colonnes_de(size_t element) const { return element < (size_t)nb_colonnes.size() ? (size_t)nb_colonnes[(int)element] : 0; }

private:
    QString chemin;
};

class Execution : public QMainWindow {
    Q_OBJECT
public:
    explicit Execution(const QString &chemin);
    ~Execution() override;
    void demarrer();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void poser();
    void envoyer(int issue);
    void arreter();
    void terminer(bool ok, const QString &erreur, const QString &annulation);
    void ecrire(const QString &t, bool erreur = false);

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
    QString texte_zone(int k) const;
    QVector<long> lignes_choisies;         // envoyées avec chaque événement (§ 22.2)
    QByteArray texte_envoye;
    void envoyer_evenement(SorteEvenement sorte, int element, int ligne);
    QWidget *vue_ecran = nullptr;
    QLabel *erreur_ecran = nullptr;
    QVector<class QTableWidget *> tables;
    QVector<QPushButton *> boutons_ecran;
    bool attente_ecran = false;
    bool attente = false, fini = false, fermer_a_la_fin = false;
    int images = 0;
};

#endif
