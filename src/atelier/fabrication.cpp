// GrymoiR : l'atelier fabrique une application autonome (voir fabrication.h).
#include "fabrication.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <utility>

QString programme_d_application(QString *nom, QString *principal) {
    const QString a = QCoreApplication::applicationDirPath();
    for (const QString &d : {a + "/../Resources/Programme", a + "/Programme"}) {
        QFile f(d + "/application.txt");
        if (!f.open(QIODevice::ReadOnly)) continue;
        for (const QString &l : QString::fromUtf8(f.readAll()).split('\n')) {
            const int i = l.indexOf(" : ");
            if (i < 0 || l.startsWith("Remarque")) continue;
            const QString cle = l.left(i).trimmed(), valeur = l.mid(i + 3).trimmed();
            if (cle == "nom") *nom = valeur;
            else if (cle == "programme") *principal = valeur;
        }
        if (!nom->isEmpty() && !principal->isEmpty()) return QDir(d).absolutePath();
    }
    return QString();
}

static bool a_ignorer(const QFileInfo &i) {
    const QString n = i.fileName();
    return n.startsWith('.') || n.endsWith(".grymd") || n.endsWith(".grymb") || n == "projet.grymatelier"
           || n.endsWith(".installation");
}

static bool copier(const QString &de, const QString &vers, QString *erreur) {
    if (!QDir().mkpath(vers)) { *erreur = QString("« %1 » ne peut pas être créé.").arg(vers); return false; }
    for (const QFileInfo &i : QDir(de).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (a_ignorer(i)) continue;
        const QString cible = QDir(vers).filePath(i.fileName());
        if (i.isDir() ? !copier(i.filePath(), cible, erreur) : !QFile::copy(i.filePath(), cible)) {
            if (erreur->isEmpty()) *erreur = QString("« %1 » ne peut pas être recopié.").arg(i.fileName());
            return false;
        }
    }
    return true;
}

bool preparer_programme(const QString &projet, const QString &principal, const QString &nom, const QString &vers,
                        QString *erreur) {
    if (!QFileInfo(QDir(projet).filePath(principal)).isFile()) {
        *erreur = QString("Programme principal « %1 » introuvable dans le projet.").arg(principal);
        return false;
    }
    if (!copier(projet, vers, erreur)) return false;
    QFile f(QDir(vers).filePath("application.txt"));
    if (!f.open(QIODevice::WriteOnly)) { *erreur = "application.txt ne peut pas être écrit."; return false; }
    f.write(QString("Remarque : une application fabriquée par l'atelier de GrymoiR.\nnom : %1\nprogramme : %2\n")
                .arg(nom, principal).toUtf8());
    return true;
}

// Un outil du système, attendu jusqu'au bout ; faux, avec son message, s'il échoue.
static bool outil(const QString &programme, const QStringList &arguments, QString *erreur) {
    QProcess p;
    p.start(programme, arguments);
    if (!p.waitForStarted(10000)) { *erreur = QString("« %1 » ne se lance pas.").arg(programme); return false; }
    p.waitForFinished(600000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *erreur = QString("« %1 » a échoué : %2").arg(programme, QString::fromUtf8(p.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

QString fabriquer_application(const QString &projet, const QString &principal, const QString &destination,
                              QString *erreur, QString *remarque) {
    const QString nom = QFileInfo(projet).fileName();
    QTemporaryDir travail;
    if (!travail.isValid()) { *erreur = "Dossier de travail impossible à créer."; return QString(); }
#ifdef Q_OS_MACOS
    const QString paquet = QDir(destination).filePath(nom + ".dmg");
    if (QFileInfo::exists(paquet)) { *erreur = QString("« %1 » existe déjà : il n'est jamais écrasé.").arg(paquet); return QString(); }
    const QString source = QDir(QCoreApplication::applicationDirPath() + "/../..").absolutePath();
    if (!source.endsWith(".app")) { *erreur = "L'atelier ne tourne pas depuis une application (.app)."; return QString(); }
    const QString disque = travail.filePath("disque"), app = disque + "/" + nom + ".app";
    QDir().mkpath(disque);
    if (!outil("ditto", {source, app}, erreur)) return QString();   // ditto garde liens et attributs du paquet
    QDir(app + "/Contents/Resources/Exemples").removeRecursively();
    if (!preparer_programme(projet, principal, nom, app + "/Contents/Resources/Programme", erreur)) return QString();
    const QString plist = app + "/Contents/Info.plist";
    QString ident = nom.toLower();
    ident.replace(QRegularExpression("[^a-z0-9]+"), "-");
    for (const auto &[cle, valeur] : {std::pair<QString, QString>{"CFBundleName", nom}, {"CFBundleDisplayName", nom},
                                      {"CFBundleIdentifier", "ch.grymoir.application." + ident}})
        if (!outil("plutil", {"-replace", cle, "-string", valeur, plist}, erreur)) return QString();
    if (!QFileInfo(app + "/Contents/Frameworks").isDir())
        *remarque = "L'atelier n'a pas été installé depuis GrymoiR.dmg : l'application dépend du Qt de ce Mac, "
                    "et ne tournera pas sur un autre. Fabrique-la depuis /Applications/GrymoiR.app.";
    if (!outil("codesign", {"--force", "--deep", "--sign", "-", app}, erreur)) return QString();
    QFile::link("/Applications", disque + "/Applications");
    if (!outil("hdiutil", {"create", "-volname", nom, "-srcfolder", disque, "-format", "UDZO", paquet}, erreur))
        return QString();
    return paquet;
#elif defined(Q_OS_LINUX)
    const QString paquet = QDir(destination).filePath(nom + "-linux.tar.gz");
    if (QFileInfo::exists(paquet)) { *erreur = QString("« %1 » existe déjà : il n'est jamais écrasé.").arg(paquet); return QString(); }
    const QString dossier = travail.filePath(nom);
    QDir().mkpath(dossier);
    const QString exe = QCoreApplication::applicationFilePath();
    if (!QFile::copy(exe, dossier + "/grym-atelier")) { *erreur = "Le lanceur ne peut pas être recopié."; return QString(); }
    QFile(dossier + "/grym-atelier").setPermissions(QFile(exe).permissions());
    if (!preparer_programme(projet, principal, nom, dossier + "/Programme", erreur)) return QString();
    QFile lanceur(dossier + "/" + nom);   // on double-clique ce script, qui démarre l'application
    lanceur.open(QIODevice::WriteOnly);
    lanceur.write("#!/bin/sh\nexec \"$(dirname \"$0\")/grym-atelier\" \"$@\"\n");
    lanceur.close();
    lanceur.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup
                           | QFile::ReadOther | QFile::ExeOther);
    *remarque = "L'application utilise le Qt 6 installé sur la machine Linux qui la reçoit.";
    if (!outil("tar", {"-czf", paquet, "-C", travail.path(), nom}, erreur)) return QString();
    return paquet;
#else
    (void)nom;
    (void)principal;
    (void)destination;
    (void)remarque;
    *erreur = "Fabriquer une application n'est pas encore possible sur ce système (prévu : macOS, Linux).";
    return QString();
#endif
}
