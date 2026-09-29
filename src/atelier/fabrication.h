// GrymoiR : l'atelier fabrique une application autonome à partir d'un projet (docs/atelier.md, § 8).
// Une application, c'est le lanceur (l'atelier lui-même, sans éditeur) et un dossier « Programme » : les fichiers
// du projet, sans ses données, et application.txt (nom, programme principal). Au démarrage, l'exécutable qui trouve
// ce dossier ouvre le programme directement, sa base dans le dossier de données du système.
#ifndef GRYM_ATELIER_FABRICATION_H
#define GRYM_ATELIER_FABRICATION_H

#include <QImage>
#include <QString>

// Le dossier « Programme » d'une application fabriquée, s'il y en a un à côté de l'exécutable
// (Contents/Resources/Programme sous macOS, Programme ailleurs) ; *nom et *principal lus dans application.txt.
// Vide pour l'atelier ordinaire.
QString programme_d_application(QString *nom, QString *principal);

// Recopie les fichiers du projet (sans données .grymd, bytecode .grymb, fichier d'atelier ni fichiers cachés)
// dans `vers`, et y écrit application.txt. Faux, avec *erreur, si une copie échoue.
bool preparer_programme(const QString &projet, const QString &principal, const QString &nom, const QString &vers,
                        QString *erreur);

// L'icône d'une application fabriquée : la première image PNG du projet, par ordre alphabétique (son logo),
// sinon celle de GrymoiR (outils/icone-application.png). La même règle que pour Linux (outils/fabriquer-ci).
QImage icone_du_projet(const QString &projet);

// Écrit `image` au format .icns de macOS : centrée dans la grille des icônes (824 pixels sur 1024, marges
// transparentes), en dix tailles de 16 à 1024 pixels, chacune en PNG. Les types et leur ordre sont ceux
// d'outils/icone/fabriquer. Faux si le fichier ne peut pas être écrit.
bool ecrire_icns(const QImage &image, const QString &chemin);

// Fabrique l'application du projet pour le système où tourne l'atelier, dans le dossier `destination` :
// « Nom.dmg » sous macOS, « Nom-linux.tar.gz » sous Linux. N'écrase jamais un fichier existant.
// Rend le chemin du paquet, ou vide avec *erreur ; *remarque reçoit un avertissement éventuel.
QString fabriquer_application(const QString &projet, const QString &principal, const QString &destination,
                              QString *erreur, QString *remarque);

#endif
