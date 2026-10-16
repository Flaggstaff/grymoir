// Un rapport en PDF (grammaire, § 15.6) : la page HTML d'un export, mise en page sur A4, avec ses numéros de page.
#pragma once

#include <QByteArray>
#include <QString>

// Les octets du PDF, ou vide avec *erreur. À appeler dans le fil de l'interface (Qt y dessine).
QByteArray pdf_depuis_html(const QString &html, QString *erreur);
