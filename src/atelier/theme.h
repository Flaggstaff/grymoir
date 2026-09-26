// GrymoiR : le thème de l'atelier et des écrans (docs/ecrans.md, § 5 ; docs/atelier.md, § 5 ter).
// Un seul thème, fixé par GrymoiR : les jetons (src/atelier/theme/grymoir-jetons.json) donnent les couleurs,
// en clair et en sombre, pour chacun des cinq accents ; le modèle (grymoir.qss.modele) en fait une feuille
// de style Qt. Le mode suit le système ; l'accent de l'atelier est « bleue ».
#ifndef GRYM_ATELIER_THEME_H
#define GRYM_ATELIER_THEME_H

#include <QColor>
#include <QFont>
#include <QObject>
#include <QStringList>

class QApplication;

class Theme : public QObject {
    Q_OBJECT
public:
    enum Mode { Clair, Sombre };

    // Le thème de l'application : créé au premier appel.
    static Theme &courant();

    // Polices, style Fusion, feuille, palette ; suit ensuite le mode du système (Qt 6.5 et plus).
    // GRYMOIR_MODE=clair ou sombre impose un mode, pour essayer l'un et l'autre sur tout système.
    void installer(QApplication &app);
    // Change de mode ou d'accent et réapplique tout ; émet change().
    void appliquer(Mode mode, const QString &accent);

    Mode mode() const { return mode_; }
    QString accent() const { return accent_; }
    // Les accents permis, dans l'ordre des jetons ; le premier est celui par défaut.
    QStringList accents() const;

    // La couleur d'un jeton, pour le mode et l'accent courants : « fond », « accent », « danger »…,
    // ou un jeton de coloration préfixé : « syntaxe-nombre ». Un jeton inconnu est une faute de programmation :
    // magenta, pour se voir tout de suite.
    QColor couleur(const QString &jeton) const;
    // La même, en #rrggbb, pour le texte enrichi (HTML) des panneaux.
    QString hex(const QString &jeton) const { return couleur(jeton).name(); }

    // La feuille de style d'un mode et d'un accent, sans jeton restant (sinon : vide et message sur stderr).
    QString feuille(Mode mode, const QString &accent) const;
    // Polices du thème : l'interface et le code (taille en pixels, comme les jetons).
    QFont police_interface(int pixels = 14) const;
    QFont police_code(int pixels = 14) const;

    // Contrôle des jetons, pour les essais : chaque rapport de contraste déclaré est recalculé (WCAG 2.1,
    // luminance relative) ; renvoie les écarts, un par ligne, vide si tout concorde et tient son seuil.
    QString verifier_contrastes() const;
    // Rapport de contraste WCAG 2.1 entre deux couleurs, de 1 à 21.
    static double contraste(const QColor &a, const QColor &b);

signals:
    void change();

private:
    Theme();
    QString valeur(Mode mode, const QString &accent, const QString &jeton) const;
    void preparer_icones();

    Mode mode_ = Clair;
    QString accent_ = "bleue";
    QByteArray jetons_;
    QString modele_;
    QString dossier_icones_;
    bool installe_ = false;
};

#endif
