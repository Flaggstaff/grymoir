// Les courriels (grammaire, § 22.27) : SMTP sur TLS implicite, avec Qt Network seul, pour l'atelier, les applications
// fabriquées et leur essai (tests/test_courriel.cpp).
#pragma once

#include <QString>

// Envoie un courriel au format texte, en UTF-8. serveur : « mail.infomaniak.com » ou « mail.infomaniak.com:465 ».
// Rend true ; sinon false et *erreur, en français, avec la réponse du serveur quand il en a donné une.
bool grym_courriel(const QString &serveur, const QString &compte, const QString &cle, const QString &de, const QString &a,
                   const QString &objet, const QString &texte, QString *erreur);
