// La clé de secours du développeur (base chiffrée, étape 3 ; grammaire, § 16.18) : la créer, la poser dans un projet,
// et rouvrir avec elle la base d'un client qui a oublié son mot de passe.
#ifndef GRYM_SECOURS_H
#define GRYM_SECOURS_H

#include <QString>
#include <QWidget>

// Les opérations, sans fenêtre (les essais les appellent) ; une chaîne vide si tout va bien, sinon le motif.
QString secours_creer_fichier(const QString &chemin, const QString &mdp);
QString secours_poser(const QString &fichier_secours, const QString &projet);
// Une base (.grymd, avec son fichier .clés à côté) ou un coffre (.coffre) ; pour un coffre, la base neuve s'écrit à
// `destination`. Le mot de passe de la base devient `nouveau`.
QString secours_rouvrir(const QString &fichier_secours, const QString &mdp_secours, const QString &base_ou_coffre,
                        const QString &nouveau, const QString &destination);

// Les mêmes, avec leurs fenêtres (menu Programme de l'atelier).
void secours_creer_dialogue(QWidget *parent);
void secours_poser_dialogue(QWidget *parent, const QString &projet);
void secours_rouvrir_dialogue(QWidget *parent);

#endif
