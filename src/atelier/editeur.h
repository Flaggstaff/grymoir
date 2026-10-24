// GrymoiR : l'atelier, éditeur de code (docs/atelier.md, § 6.3) : numéros de ligne, coloration par le
// lexeur, erreur de l'analyseur signalée en direct, suites valides proposées à la frappe (grammaire, § 8 ;
// charte, art. 9), sans passer par le protocole LSP.
#ifndef GRYM_ATELIER_EDITEUR_H
#define GRYM_ATELIER_EDITEUR_H

#include <QPlainTextEdit>
#include <QSet>
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
    void bloc_change(int premiere_ligne);   // le bloc du curseur a changé : sa première ligne, à partir de 1 (0 : aucun)

protected:
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void analyser();
    void marquer_erreur();
public:
    // Le bloc du curseur (24 octobre 2026) : la déclaration de premier niveau qui le contient, avec ses remarques de tête
    // (une entité, un écran, une action, un événement) ; premier et dernier numéros de ligne, à partir de 0.
    QPair<int, int> bloc_du_curseur() const;
    // Le repli (24 octobre 2026) : une ligne qui finit par « : » et dont la suite est en retrait se replie sur elle-même ;
    // un clic sur le triangle de la marge la replie ou la déplie ; aller dans un bloc replié le déplie.
    bool repliable(const QTextBlock &b) const;
    bool replie(const QTextBlock &b) const;
    void basculer_repli(const QTextBlock &b);
    void deplier_tout();
    void clic_marge(int y);
    // Le plan : les déclarations de premier niveau qui ont un corps (« … : »), avec leur ligne (à partir de 1)
    QList<QPair<QString, int>> plan() const;
    // Ne montrer que ces déclarations de premier niveau (leurs premières lignes, à partir de 1), cachées toutes les autres ;
    // une liste vide montre tout. Une ligne ajoutée ou retirée montre tout de nouveau.
    void montrer_seulement(const QList<int> &debuts);
    static bool debut_de_bloc(const QTextBlock &b);   // une ligne non vide, sans retrait
private:
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
    QSet<int> replies_;   // les en-têtes repliés, par numéro de ligne
    int dernier_bloc_ = -2;
    void appliquer_repli(const QTextBlock &entete, bool cacher);
    QTextBlock fin_du_corps(const QTextBlock &entete) const;
};

// Le plan d'un fichier (24 octobre 2026) : ses déclarations, un filtre, un clic y mène.
// Par écran (24 octobre 2026) : chaque écran, avec sa déclaration et tous ses événements, quelle que soit leur place dans le
// fichier ; puis « Hors des écrans » (entités, actions, calculs, documents…). Ou l'ordre du fichier.
QString ecran_de_ligne(const QString &entete);   // « du patient » pour « L'écran du patient … » et « … dans l'écran du patient : »

class PlanDuCode : public QWidget {
    Q_OBJECT
public:
    explicit PlanDuCode(Editeur *e, QWidget *parent = nullptr);
    void rafraichir();
    class QTreeWidget *arbre() const { return arbre_; }
    class QLineEdit *filtre() const { return filtre_; }
    class QComboBox *ordre() const { return ordre_; }
    class QPushButton *seul() const { return seul_; }
    void montrer_seulement(const QString &ecran);   // l'éditeur ne montre que cet écran et ses événements ; vide : tout
private:
    Editeur *editeur_;
    class QLineEdit *filtre_;
    class QComboBox *ordre_;
    class QTreeWidget *arbre_;
    class QPushButton *seul_;
    QList<QPair<QString, int>> entrees_;
    QString seul_ecran_;
    void remplir();
    void suivre(int ligne);
};

#endif
