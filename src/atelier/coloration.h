// GrymoiR : l'atelier, coloration d'un fichier source (docs/atelier.md, § 6.3).
// Chaque ligne passe par le lexeur de GrymoiR : la couleur d'un mot est celle que le langage lui donne,
// jamais celle d'une expression régulière.
#ifndef GRYM_ATELIER_COLORATION_H
#define GRYM_ATELIER_COLORATION_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class Coloration : public QSyntaxHighlighter {
public:
    enum Sorte { Controle, MotCle, Nombre, Texte, Remarque, Operateur, Nom, Erreur, Date, NbSortes };

    Coloration(QTextDocument *document, bool compacte);
    // La sorte du caractère à cette position d'une ligne, ou -1 (espace, nom ordinaire). Sert aux tests.
    static QVector<int> sortes(const QString &ligne, bool compacte);

protected:
    void highlightBlock(const QString &ligne) override;

private:
    void preparer();   // les formats, selon le thème courant
    bool compacte;
    QTextCharFormat formats[NbSortes];
};

#endif
