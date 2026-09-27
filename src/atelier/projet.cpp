// GrymoiR : l'atelier, le fichier projet.grymatelier.
#include "projet.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

void FichierProjet::lire(const QString &d) {
    dossier = d;
    programme_principal.clear();
    positions.clear();
    QFile f(chemin(d));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QStringList lignes = QString::fromUtf8(f.readAll()).split('\n');
    for (QString l : lignes) {
        l = l.trimmed();
        if (l.isEmpty() || l.startsWith("Remarque", Qt::CaseInsensitive)) continue;
        const int deux_points = l.lastIndexOf(" : ");
        if (deux_points < 0) continue;   // une ligne mal formée s'ignore : l'atelier la réécrira
        const QString cle = l.left(deux_points).trimmed(), valeur = l.mid(deux_points + 3).trimmed();
        if (cle == "programme principal") {
            programme_principal = valeur;
            continue;
        }
        const QStringList xy = valeur.split(',');
        bool ok1 = false, ok2 = false;
        if (xy.size() == 2) {
            const double x = xy[0].trimmed().toDouble(&ok1), y = xy[1].trimmed().toDouble(&ok2);
            if (ok1 && ok2) positions.insert(cle, QPointF(x, y));
        }
    }
}

QString FichierProjet::texte() const {
    QString t = "Remarque : disposition de l'atelier ; sans effet sur le programme.\n";
    if (!programme_principal.isEmpty()) t += "programme principal : " + programme_principal + "\n";
    for (auto i = positions.constBegin(); i != positions.constEnd(); ++i)   // QMap : trié par nom
        t += QString("%1 : %2, %3\n").arg(i.key()).arg(qRound(i.value().x())).arg(qRound(i.value().y()));
    return t;
}

bool FichierProjet::ecrire() const {
    if (dossier.isEmpty()) return false;
    QSaveFile f(chemin(dossier));
    return f.open(QIODevice::WriteOnly) && f.write(texte().toUtf8()) >= 0 && f.commit();
}

QString creer_fichier(const QString &dossier, const QString &nom, QString *erreur) {
    QString n = nom.trimmed();
    if (n.isEmpty()) { *erreur = "Donnez un nom au fichier."; return QString(); }
    if (n.contains('/') || n.contains('\\')) { *erreur = "Un nom de fichier, sans dossier : « association »."; return QString(); }
    if (!n.endsWith(".grym", Qt::CaseInsensitive) && !n.endsWith(".grymc", Qt::CaseInsensitive)) n += ".grym";
    const QString chemin = QDir(dossier).filePath(n);
    if (QFileInfo::exists(chemin)) { *erreur = QString("« %1 » existe déjà : il n'est jamais écrasé.").arg(n); return QString(); }
    QFile f(chemin);
    if (!f.open(QIODevice::NewOnly | QIODevice::WriteOnly)) { *erreur = QString("« %1 » ne peut pas être créé.").arg(n); return QString(); }
    f.close();
    return chemin;
}
