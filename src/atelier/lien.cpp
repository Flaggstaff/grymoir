// GrymoiR : l'atelier, dialogue d'un lien.
#include "lien.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace {
enum Sorte { PLUSIEURS_POUR_UN, PLUSIEURS_POUR_UN_FACULTATIF, UN_POUR_UN, PLUSIEURS_POUR_PLUSIEURS };

Sorte sorte(const ChampVoulu &c) {
    if (c.plusieurs) return PLUSIEURS_POUR_PLUSIEURS;
    if (c.unique) return UN_POUR_UN;
    return c.facultatif ? PLUSIEURS_POUR_UN_FACULTATIF : PLUSIEURS_POUR_UN;
}

QString pluriel(const QString &m) { return m.endsWith('s') || m.endsWith('x') ? m : m + "s"; }

// « un pupitre », « une partition » ; « son pupitre », « sa partition », « son œuvre » (devant une voyelle)
QString un(const QString &m, bool f) { return (f ? "une " : "un ") + m; }
QString son(const QString &m, bool f) {
    const QChar c = m.isEmpty() ? QChar() : m.at(0).toLower();
    const bool voyelle = QString("aeiouyàâéèêëîïôöùûüœh").contains(c);
    return (f && !voyelle ? "sa " : "son ") + m;
}
}  // namespace

QString sorte_de_lien(const QString &de, bool fd, const QString &vers, bool fv, const ChampVoulu &c) {
    switch (sorte(c)) {
    case PLUSIEURS_POUR_UN: return QString("chaque %1 a %2 ; %2 a plusieurs %3").arg(de, un(vers, fv), pluriel(de));
    case PLUSIEURS_POUR_UN_FACULTATIF:
        return QString("chaque %1 a %2 ou aucun ; %2 a plusieurs %3").arg(de, un(vers, fv), pluriel(de));
    case UN_POUR_UN: return QString("%1 pour %2, et inversement").arg(un(de, fd), un(vers, fv));
    case PLUSIEURS_POUR_PLUSIEURS: return QString("plusieurs %1 pour plusieurs %2").arg(pluriel(de), pluriel(vers));
    }
    return QString();
}

bool demander_lien(QWidget *parent, const QString &de, bool de_feminin, const QString &vers, bool vers_feminin,
                   ChampVoulu *champ, bool modification) {
    QDialog d(parent);
    d.setWindowTitle(modification ? "Modifier le lien" : QString("Lier « %1 » à « %2 »").arg(de, vers));
    auto *nom = new QLineEdit(modification ? champ->nom : vers);
    auto *feminin = new QCheckBox("féminin (une …)");
    feminin->setChecked(modification ? champ->feminin : vers_feminin);
    auto *liste = new QComboBox;
    liste->addItems({QString("Plusieurs %1 pour %2").arg(pluriel(de), un(vers, vers_feminin)),
                     QString("Plusieurs %1 pour %2, ou aucun (facultatif)").arg(pluriel(de), un(vers, vers_feminin)),
                     QString("%1 pour %2 (un pour un)").arg(un(de, de_feminin), un(vers, vers_feminin)),
                     QString("Plusieurs %1 pour plusieurs %2").arg(pluriel(de), pluriel(vers))});
    liste->setItemText(UN_POUR_UN, liste->itemText(UN_POUR_UN).left(1).toUpper() + liste->itemText(UN_POUR_UN).mid(1));
    if (modification) liste->setCurrentIndex(sorte(*champ));
    auto *cascade = new QCheckBox(QString("%1 disparaît avec %2").arg(un(de, de_feminin), son(vers, vers_feminin)));
    cascade->setChecked(modification && champ->cascade);
    auto *phrase = new QLabel;   // ce que l'atelier écrira, en GrymoiR
    phrase->setWordWrap(true);
    phrase->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto mettre_a_jour = [&] {
        const bool plusieurs = liste->currentIndex() == PLUSIEURS_POUR_PLUSIEURS;
        cascade->setEnabled(!plusieurs);   // « disparaît avec » ne vaut que pour un lien simple (§ 16.12)
        if (plusieurs && !modification && nom->text() == vers) nom->setText(pluriel(vers));
        if (!plusieurs && !modification && nom->text() == pluriel(vers)) nom->setText(vers);
        const QString n = nom->text().trimmed();
        QString t = plusieurs ? QString("des %1 (%2)").arg(n, vers)
                              : QString("%1 %2 (%3)").arg(feminin->isChecked() ? "une" : "un", n, vers);
        if (liste->currentIndex() == PLUSIEURS_POUR_UN_FACULTATIF) t += ", facultatif";
        if (liste->currentIndex() == UN_POUR_UN) t += ", unique";
        if (!plusieurs && cascade->isChecked()) t += ", et disparaît avec " + QString(vers_feminin ? "elle" : "lui");
        phrase->setText("Dans l'entité « " + de + " », l'atelier écrira :<br><tt>" + t.toHtmlEscaped() + "</tt>");
    };
    QObject::connect(nom, &QLineEdit::textChanged, &d, mettre_a_jour);
    QObject::connect(feminin, &QCheckBox::toggled, &d, mettre_a_jour);
    QObject::connect(cascade, &QCheckBox::toggled, &d, mettre_a_jour);
    QObject::connect(liste, &QComboBox::currentIndexChanged, &d, mettre_a_jour);
    mettre_a_jour();
    auto *boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto *forme = new QFormLayout(&d);
    forme->addRow("Nom du champ :", nom);
    forme->addRow("", feminin);
    forme->addRow("Sorte :", liste);
    forme->addRow("", cascade);
    forme->addRow(phrase);
    forme->addRow(boutons);
    if (d.exec() != QDialog::Accepted) return false;
    champ->nom = nom->text().trimmed();
    champ->type = vers;
    champ->feminin = feminin->isChecked();
    champ->plusieurs = liste->currentIndex() == PLUSIEURS_POUR_PLUSIEURS;
    champ->unique = liste->currentIndex() == UN_POUR_UN;
    champ->facultatif = liste->currentIndex() == PLUSIEURS_POUR_UN_FACULTATIF;
    champ->cascade = !champ->plusieurs && cascade->isChecked();
    if (champ->plusieurs) champ->depart.clear();
    return true;
}
