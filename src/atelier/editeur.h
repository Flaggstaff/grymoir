// GrymoiR : l'atelier, éditeur de code (docs/atelier.md, § 6.3) : numéros de ligne, coloration par le
// lexeur, erreur de l'analyseur signalée en direct, suites valides proposées à la frappe (grammaire, § 8 ;
// charte, art. 9), sans passer par le protocole LSP.
#ifndef GRYM_ATELIER_EDITEUR_H
#define GRYM_ATELIER_EDITEUR_H

#include <QPlainTextEdit>
#include <QTimer>

class Coloration;
class QCompleter;
class QStringListModel;

struct Diagnostic_atelier {
    QString message;   // vide : aucune erreur
    int ligne = 0;     // à partir de 1 ; 0 : sans position
    int colonne = 0;   // en points de code, à partir de 1
};

// Analyse une source complète par le cœur, sans rien exécuter. `chemin` : le fichier, pour trouver ceux qu'il
// utilise (grammaire, § 21) ; une erreur venue d'un fichier utilisé se place sur la phrase « Utiliser ».
// *declarations reçoit vrai si le fichier ne contient que des déclarations (un fichier qui s'utilise).
Diagnostic_atelier analyser_source(const QString &source, bool compacte, const QString &chemin = QString(),
                                   bool *declarations = nullptr);

class Editeur : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editeur(QWidget *parent = nullptr);
    void charger(const QString &texte, bool compacte, const QString &chemin = QString());
    bool declarations() const { return que_des_declarations; }
    const Diagnostic_atelier &diagnostic() const { return diag; }
    void aller_a(int ligne, int colonne);
    int largeur_marge() const;
    void peindre_marge(QPaintEvent *e);

    // Aide à la saisie : les suites valides au curseur, calculées par le cœur (grammaire, § 8), sans les
    // catégories (« (nombre) ») ni les gabarits ; vide en forme compacte, que le calcul ne couvre pas encore.
    QStringList suites_au_curseur() const;
    // Le début de mot que la suite choisie remplacera : lettres, chiffres, « _ », ou un nom entre crochets ouvert.
    QString debut_de_mot() const;
    // Écrit la suite choisie à la place du début de mot.
    void completer(const QString &suite);
    // Ouvre la liste des suites : `demandee` (Ctrl+Espace) l'ouvre même sans début de mot.
    void proposer(bool demandee);
    QCompleter *completion() const { return completion_; }
    // Un « " » tapé devient « «  » », curseur au milieu ; tapé devant « » » qui ferme un texte, il saute par-dessus.
    // Il reste « " » dans un texte ouvert par « « » ou par « " ». Ctrl+Z rend le « " » tapé.
    void guillemet(int position);

signals:
    void diagnostic_change();

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void analyser();
    void marquer_erreur();
    QWidget *marge;
    Coloration *coloration = nullptr;
    bool compacte = false;
    bool que_des_declarations = false;
    QString chemin;
    QTimer attente;
    Diagnostic_atelier diag;
    QCompleter *completion_;
    QStringListModel *suites_;
    bool chargement = false;   // setPlainText : pas une frappe
    bool retouche = false;     // une modification faite par l'éditeur lui-même
    void sur_changement(int position, int retires, int ajoutes);
};

#endif
