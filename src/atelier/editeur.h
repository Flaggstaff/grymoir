// GrymoiR : l'atelier, éditeur de code (docs/atelier.md, § 6.3) : numéros de ligne, coloration par le
// lexeur, erreur de l'analyseur signalée en direct, sans passer par le protocole LSP.
#ifndef GRYM_ATELIER_EDITEUR_H
#define GRYM_ATELIER_EDITEUR_H

#include <QPlainTextEdit>
#include <QTimer>

class Coloration;

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

signals:
    void diagnostic_change();

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool event(QEvent *e) override;

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
};

#endif
