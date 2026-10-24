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
    nom_application.clear();
    version.clear();
    editeur.clear();
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
        if (cle == "application, nom") { nom_application = valeur; continue; }
        if (cle == "application, version") { version = valeur; continue; }
        if (cle == QString::fromUtf8("application, éditeur")) { editeur = valeur; continue; }
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
    if (!nom_application.isEmpty()) t += "application, nom : " + nom_application + "\n";
    if (!version.isEmpty()) t += "application, version : " + version + "\n";
    if (!editeur.isEmpty()) t += QString::fromUtf8("application, éditeur : ") + editeur + "\n";
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
    if (n.isEmpty()) { *erreur = "Donne un nom au fichier."; return QString(); }
    if (n.contains('/') || n.contains('\\')) { *erreur = "Un nom de fichier, sans dossier : « association »."; return QString(); }
    if (!n.endsWith(".grym", Qt::CaseInsensitive) && !n.endsWith(".grymc", Qt::CaseInsensitive)) n += ".grym";
    const QString chemin = QDir(dossier).filePath(n);
    if (QFileInfo::exists(chemin)) { *erreur = QString("« %1 » existe déjà : il n'est jamais écrasé.").arg(n); return QString(); }
    QFile f(chemin);
    if (!f.open(QIODevice::NewOnly | QIODevice::WriteOnly)) { *erreur = QString("« %1 » ne peut pas être créé.").arg(n); return QString(); }
    f.close();
    return chemin;
}

static bool copier_dossier(const QString &de, const QString &vers) {
    if (!QDir().mkpath(vers)) return false;
    const QDir d(de);
    for (const QFileInfo &i : d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString cible = QDir(vers).filePath(i.fileName());
        if (i.isDir() ? !copier_dossier(i.filePath(), cible) : !QFile::copy(i.filePath(), cible)) return false;
    }
    return true;
}

QString installer_exemple(const QString &source, const QString &parent, QString *erreur) {
    const QFileInfo s(source);
    if (!s.isDir()) { *erreur = QString("« %1 » introuvable.").arg(source); return QString(); }
    const QString copie = QDir(parent).filePath(s.fileName());
    if (QFileInfo(copie).exists()) return copie;   // déjà installé : les données du lecteur restent
    const QString provisoire = copie + ".installation";
    QDir(provisoire).removeRecursively();
    if (!copier_dossier(source, provisoire) || !QDir().rename(provisoire, copie)) {
        QDir(provisoire).removeRecursively();
        *erreur = QString("« %1 » ne peut pas être recopié dans « %2 ».").arg(s.fileName(), parent);
        return QString();
    }
    return copie;
}

QString creer_projet(const QString &parent, const QString &nom, QString *erreur) {
    const QString n = nom.trimmed();
    if (n.isEmpty()) { *erreur = "Donne un nom au projet."; return QString(); }
    if (n.contains('/') || n.contains('\\')) { *erreur = "Un nom de projet, sans dossier : « Gestion de projet »."; return QString(); }
    if (n.startsWith('.')) { *erreur = "Un nom qui commence par un point serait caché."; return QString(); }
    if (n.contains(QChar(0x00AB)) || n.contains(QChar(0x00BB)) || n.contains('"')) {
        *erreur = "Pas de guillemets dans un nom de projet.";
        return QString();
    }
    const QString d = QDir(parent).filePath(n);
    if (QFileInfo::exists(d)) { *erreur = QString("« %1 » existe déjà : un projet ne se crée jamais par-dessus un autre.").arg(n); return QString(); }
    if (!QDir().mkpath(parent) || !QDir(parent).mkdir(n)) { *erreur = QString("« %1 » ne peut pas être créé dans « %2 ».").arg(n, parent); return QString(); }
    const QString programme = QDir(d).filePath(n + ".grym");
    QFile f(programme);
    FichierProjet fp;
    fp.dossier = d;
    fp.programme_principal = n + ".grym";
    if (!f.open(QIODevice::NewOnly | QIODevice::WriteOnly)
        || f.write(QString("Remarque : %1, le programme principal.\n").arg(n).toUtf8()) < 0 || (f.close(), !fp.ecrire())) {
        QDir(d).removeRecursively();
        *erreur = QString("« %1 » ne peut pas être écrit.").arg(programme);
        return QString();
    }
    return d;
}

QStringList recents_avec(QStringList recents, const QString &dossier, int max) {
    const QString d = QFileInfo(dossier).absoluteFilePath();
    recents.removeAll(d);
    recents.prepend(d);
    while (recents.size() > max) recents.removeLast();
    return recents;
}

QStringList projets_du_depot(const QString &depuis, bool *depot_trouve) {
    QDir d(depuis);
    *depot_trouve = false;
    for (int n = 0; n < 6 && !*depot_trouve; n++) {
        *depot_trouve = d.exists("projets") && d.exists("src/atelier");
        if (!*depot_trouve && !d.cdUp()) break;
    }
    QStringList r;
    if (!*depot_trouve) return r;
    const QDir projets(d.filePath("projets"));
    // un projet : un projet.grymatelier et au moins un programme ; un projet.grymatelier seul (l'atelier en écrit un quand on
    // ouvre un groupe) ne fait pas un projet (24 octobre 2026)
    auto est_projet = [](const QDir &x) {
        return QFileInfo::exists(x.filePath("projet.grymatelier")) && !x.entryList({"*.grym"}, QDir::Files).isEmpty();
    };
    for (const QString &nom : projets.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir sous(projets.filePath(nom));
        if (est_projet(sous)) { r << projets.filePath(nom); continue; }
        // un groupe de projets (« projets/Praxis » : Nicolas, Lorianne, Praxis, Test de charge), un niveau plus bas
        for (const QString &n2 : sous.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (est_projet(QDir(sous.filePath(n2)))) r << sous.filePath(n2);
    }
    return r;
}
