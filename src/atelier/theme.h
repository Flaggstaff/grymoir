// GrymoiR : le thème de l'atelier et des écrans (docs/ecrans.md, § 5 ; docs/atelier.md, § 5 ter).
// Un seul thème, fixé par GrymoiR : les jetons (src/atelier/theme/grymoir-jetons.json) donnent les couleurs,
// en clair et en sombre, pour chacun des cinq accents ; le modèle (grymoir.qss.modele) en fait une feuille
// de style Qt. Le mode suit le système ; l'accent de l'atelier est « bleue ».
#ifndef GRYM_ATELIER_THEME_H
#define GRYM_ATELIER_THEME_H

#include <QColor>
#include <QFont>
#include <QMap>
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
    // Une mesure des jetons, en pixels : « marge-ecran », « cote-a-cote », « hauteur-ligne »… (espaces,
    // dimensions, rayons, traits). Une mesure inconnue est une faute de programmation : 0, et un message.
    int mesure(const QString &jeton) const;
    // La même, en #rrggbb, pour le texte enrichi (HTML) des panneaux.
    QString hex(const QString &jeton) const { return couleur(jeton).name(); }

    // La feuille de style d'un mode et d'un accent, sans jeton restant (sinon : vide et message sur stderr).
    QString feuille(Mode mode, const QString &accent) const;
    // La même, avec des jetons qui recouvrent ceux du thème et une famille de police (vide : celle du thème) :
    // l'aperçu d'un programme au thème libre (§ 22.5, 2.13), sans toucher à l'atelier.
    QString feuille(Mode mode, const QString &accent, const QMap<QString, QString> &jetons, const QString &famille) const;

    // Le thème libre d'un programme (§ 22.5, 2.13), pour ce processus seul (la fenêtre d'exécution) : les jetons
    // dérivés (src/teinte.c), en clair et en sombre (une valeur vide garde celle du thème) ; clair_impose : un fond
    // est donné, le mode reste clair quel que soit le système ; la police, ses octets (TrueType ou OpenType) ou vide.
    // Rend la famille de la police installée, ou vide.
    QString personnaliser(const QMap<QString, QString> &clair, const QMap<QString, QString> &sombre, bool clair_impose,
                          const QByteArray &police);
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
    void preparer_icones(Mode mode, const QString &accent);

    QMap<QString, QString> perso_clair_, perso_sombre_;   // le thème libre (2.13) ; vides : celui de GrymoiR
    bool clair_impose_ = false;
    QString famille_;                                     // la police du programme, ou vide

    Mode mode_ = Clair;
    QString accent_ = "bleue";
    QByteArray jetons_;
    QString modele_;
    QString dossier_icones_;
    bool installe_ = false;
};

#endif
