// La clé de secours : voir secours.h.
#include "secours.h"

extern "C" {
#include "base.h"
#include "coffre.h"
#include "texte.h"
}

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <cstdlib>
#include <cstring>

static QString motif(char *e) {
    const QString m = QString::fromUtf8(e ? e : "Échec.");
    std::free(e);
    return m;
}

QString secours_creer_fichier(const QString &chemin, const QString &mdp) {
    if (QFileInfo::exists(chemin)) return QString("« %1 » existe déjà : une clé de secours n'est jamais écrasée.").arg(chemin);
    unsigned char pub[32];
    size_t n = 0;
    char *e = nullptr;
    const QByteArray m = mdp.toUtf8();
    unsigned char *f = secours_creer(m.constData(), pub, &n, &e);
    if (!f) return motif(e);
    QSaveFile s(chemin);
    const bool ok = s.open(QIODevice::WriteOnly) && s.write(reinterpret_cast<const char *>(f), (qint64)n) == (qint64)n && s.commit();
    std::free(f);
    return ok ? QString() : QString("« %1 » ne s'écrit pas.").arg(chemin);
}

QString secours_poser(const QString &fichier_secours, const QString &projet) {
    QFile f(fichier_secours);
    if (!f.open(QIODevice::ReadOnly)) return QString("« %1 » ne se lit pas.").arg(fichier_secours);
    const QByteArray o = f.readAll();
    unsigned char pub[32];
    if (!secours_publique(reinterpret_cast<const unsigned char *>(o.constData()), (size_t)o.size(), pub))
        return "Ce fichier n'est pas une clé de secours de GrymoiR.";
    char h[65];
    secours_hex_ecrire(pub, h);
    QSaveFile s(QDir(projet).filePath(QString::fromUtf8("clé-de-secours.pub")));
    if (!s.open(QIODevice::WriteOnly)) return "Le projet ne s'écrit pas.";
    s.write(QByteArray(h) + "\n");
    return s.commit() ? QString() : QString("Le projet ne s'écrit pas.");
}

QString secours_rouvrir(const QString &fichier_secours, const QString &mdp_secours, const QString &base_ou_coffre,
                        const QString &nouveau, const QString &destination) {
    QFile f(fichier_secours);
    if (!f.open(QIODevice::ReadOnly)) return QString("« %1 » ne se lit pas.").arg(fichier_secours);
    const QByteArray o = f.readAll();
    unsigned char sec[32];
    char *e = nullptr;
    const QByteArray ms = mdp_secours.toUtf8();
    if (!secours_ouvrir(reinterpret_cast<const unsigned char *>(o.constData()), (size_t)o.size(), ms.constData(), sec, &e))
        return motif(e);
    const QByteArray n = nouveau.toUtf8(), cible = QFile::encodeName(base_ou_coffre), dest = QFile::encodeName(destination);
    const int ok = base_ou_coffre.endsWith(".coffre") ? base_secourir_coffre(cible.constData(), sec, n.constData(), dest.constData(), &e)
                                                      : base_secourir(cible.constData(), sec, n.constData(), &e);
    std::memset(sec, 0, sizeof sec);
    return ok ? QString() : motif(e);
}

// ---- Les fenêtres ----

// Un ou deux champs masqués, avec un texte ; vide si l'on renonce.
static QString demander(QWidget *parent, const QString &titre, const QString &texte, bool confirmer, int minimum) {
    QDialog d(parent);
    d.setWindowTitle(titre);
    d.setMinimumWidth(520);
    auto *l = new QVBoxLayout(&d);
    auto *t = new QLabel(texte);
    t->setWordWrap(true);
    l->addWidget(t);
    auto *a = new QLineEdit, *b = new QLineEdit;
    a->setEchoMode(QLineEdit::Password);
    b->setEchoMode(QLineEdit::Password);
    auto *form = new QFormLayout;
    form->addRow("Mot de passe", a);
    if (confirmer) form->addRow("Confirmation", b);
    else b->hide();
    l->addLayout(form);
    auto *m = new QLabel;
    m->setStyleSheet("color: #B3261E;");
    l->addWidget(m);
    auto *bt = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bt->button(QDialogButtonBox::Ok)->setText("Continuer");
    bt->button(QDialogButtonBox::Ok)->setDefault(true);
    bt->button(QDialogButtonBox::Ok)->setProperty("role", "principal");
    bt->button(QDialogButtonBox::Cancel)->setText("Renoncer");
    l->addWidget(bt);
    QObject::connect(bt, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(bt, &QDialogButtonBox::accepted, &d, [&] {
        if (a->text().size() < minimum) { m->setText(QString("Au moins %1 caractères.").arg(minimum)); return; }
        if (confirmer && a->text() != b->text()) { m->setText("Les deux saisies diffèrent."); return; }
        d.accept();
    });
    return d.exec() == QDialog::Accepted ? a->text() : QString();
}

static QString chemin_retenu() { return QSettings().value("secours/chemin").toString(); }

void secours_creer_dialogue(QWidget *parent) {
    const QString defaut = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                               .filePath(QString::fromUtf8("GrymoiR - clé de secours.grymsecours"));
    const QString chemin = QFileDialog::getSaveFileName(parent, "Où ranger ta clé de secours ?", defaut,
                                                        "Clé de secours (*.grymsecours)");
    if (chemin.isEmpty()) return;
    const QString mdp = demander(parent, "Mot de passe de secours",
                                 QString::fromUtf8("Ce mot de passe protège ta clé de secours : sans lui, elle n'ouvre rien. "
                                                   "Au moins douze caractères, noté en lieu sûr, loin de la clé."), true, 12);
    if (mdp.isEmpty()) return;
    const QString r = secours_creer_fichier(chemin, mdp);
    if (!r.isEmpty()) { QMessageBox::warning(parent, "Clé de secours", r); return; }
    QSettings().setValue("secours/chemin", chemin);
    QMessageBox::information(parent, "Clé de secours",
        QString::fromUtf8("Ta clé de secours est créée :\n%1\n\nGarde-en une seconde copie ailleurs (une clé USB, un "
                          "gestionnaire de mots de passe). Sans ce fichier et son mot de passe, aucune base oubliée ne "
                          "se rouvre. Pose-la ensuite dans chaque projet (Programme, Poser la clé de secours).").arg(chemin));
}

void secours_poser_dialogue(QWidget *parent, const QString &projet) {
    if (projet.isEmpty()) { QMessageBox::information(parent, "Atelier", "Ouvre d'abord un projet."); return; }
    QString chemin = chemin_retenu();
    if (chemin.isEmpty() || !QFileInfo::exists(chemin))
        chemin = QFileDialog::getOpenFileName(parent, "Ta clé de secours", QString(), "Clé de secours (*.grymsecours)");
    if (chemin.isEmpty()) return;
    const QString r = secours_poser(chemin, projet);
    if (!r.isEmpty()) { QMessageBox::warning(parent, "Clé de secours", r); return; }
    QMessageBox::information(parent, "Clé de secours",
        QString::fromUtf8("La partie publique de ta clé de secours est posée dans le projet (clé-de-secours.pub). "
                          "L'application fabriquée l'emporte : chaque base chiffrée recevra, à sa prochaine ouverture, "
                          "une enveloppe que seule ta clé de secours ouvre. Dis-le au client, par écrit."));
}

void secours_rouvrir_dialogue(QWidget *parent) {
    const QString cible = QFileDialog::getOpenFileName(parent, "La base à rouvrir, ou sa sauvegarde", QString(),
                                                       "Base ou sauvegarde (*.grymd *.coffre)");
    if (cible.isEmpty()) return;
    QString chemin = chemin_retenu();
    if (chemin.isEmpty() || !QFileInfo::exists(chemin))
        chemin = QFileDialog::getOpenFileName(parent, "Ta clé de secours", QString(), "Clé de secours (*.grymsecours)");
    if (chemin.isEmpty()) return;
    const QString mdp_secours = demander(parent, "Mot de passe de secours", "Le mot de passe de ta clé de secours.", false, 1);
    if (mdp_secours.isEmpty()) return;
    const QString nouveau = demander(parent, "Nouveau mot de passe de la base",
                                     QString::fromUtf8("Le nouveau mot de passe de cette base, à donner au client."), true, 8);
    if (nouveau.isEmpty()) return;
    QString destination;
    if (cible.endsWith(".coffre")) {
        const QFileInfo i(cible);
        destination = QFileDialog::getSaveFileName(parent, "Où écrire la base retrouvée ?",
                                                   i.dir().filePath(i.completeBaseName() + QString::fromUtf8(" retrouvée.grymd")),
                                                   "Base (*.grymd)");
        if (destination.isEmpty()) return;
    }
    const QString r = secours_rouvrir(chemin, mdp_secours, cible, nouveau, destination);
    if (!r.isEmpty()) { QMessageBox::warning(parent, "Clé de secours", r); return; }
    QMessageBox::information(parent, "Clé de secours",
        destination.isEmpty() ? QString::fromUtf8("La base a un nouveau mot de passe. Les anciennes clés sont gardées à côté "
                                                  "(« .clés avant secours »).")
                              : QString::fromUtf8("La base retrouvée est écrite, avec ses clés :\n%1\n\nRemets-la à la place "
                                                  "de la base de l'application, avec son fichier .clés.").arg(destination));
}
