// GrymoiR : l'atelier, chercher et remplacer (1er novembre 2026). Voir recherche.h.
#include "recherche.h"
#include "editeur.h"

#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

QRegularExpression motif_recherche(const QString &texte, bool mot_entier, bool casse) {
    QString m = QRegularExpression::escape(texte);
    if (mot_entier) m = QStringLiteral("(?<![\\p{L}\\p{N}_])") + m + QStringLiteral("(?![\\p{L}\\p{N}_])");
    QRegularExpression::PatternOptions o = QRegularExpression::UseUnicodePropertiesOption;
    if (!casse) o |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(m, o);
}

QString remplacer_litteralement(const QString &texte, const QRegularExpression &m, const QString &par, int *n) {
    QString r;
    qsizetype depuis = 0;
    int compte = 0;
    auto it = m.globalMatch(texte);
    while (it.hasNext()) {
        const auto x = it.next();
        r += texte.mid(depuis, x.capturedStart() - depuis);
        r += par;
        depuis = x.capturedEnd();
        compte++;
    }
    r += texte.mid(depuis);
    if (n) *n = compte;
    return r;
}

QStringList fichiers_du_projet(const QString &projet) {
    QStringList l;
    QDirIterator it(projet, {"*.grym", "*.grymc"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = QFileInfo(it.next()).absoluteFilePath();
        const QString rel = QDir(projet).relativeFilePath(f);
        // les copies datées et la corbeille ne sont pas le programme : on n'y cherche ni n'y remplace rien
        if (rel.startsWith("Sauvegardes/") || rel.startsWith(".corbeille/") || rel.contains("/Sauvegardes/")) continue;
        l << f;
    }
    l.sort();
    return l;
}

QVector<Occurrence> chercher_dans_projet(const QString &projet, const QRegularExpression &m,
                                         const QMap<QString, QString> &ouverts) {
    QVector<Occurrence> r;
    if (!m.isValid() || m.pattern().isEmpty()) return r;
    for (const QString &f : fichiers_du_projet(projet)) {
        QString texte;
        if (ouverts.contains(f)) texte = ouverts.value(f);
        else {
            QFile d(f);
            if (!d.open(QIODevice::ReadOnly)) continue;
            texte = QString::fromUtf8(d.readAll());
        }
        const QStringList lignes = texte.split('\n');
        for (int k = 0; k < lignes.size(); k++) {
            auto it = m.globalMatch(lignes[k]);
            while (it.hasNext()) {
                const auto x = it.next();
                r.append({f, k + 1, (int)x.capturedStart() + 1, lignes[k].trimmed()});
            }
        }
    }
    return r;
}


// --- renommer partout (1er novembre 2026) ---

static QString avec_majuscule(const QString &modele, const QString &mot) {
    if (modele.isEmpty() || mot.isEmpty() || !modele.at(0).isUpper()) return mot;
    return mot.at(0).toUpper() + mot.mid(1);
}

// Une ligne de code : les mots remplacés hors des textes « … » et "…".
static QString renommer_ligne(const QString &ligne, const QRegularExpression &m, const QString &nouveau,
                              const QString &nouveau_pluriel, int *n) {
    QString r;
    int i = 0;
    while (i < ligne.size()) {
        const QChar c = ligne.at(i);
        if (c == QChar(0x00AB) || c == '"') {   // un texte : recopié tel quel jusqu'à sa fin
            const QChar fin = c == '"' ? QChar('"') : QChar(0x00BB);
            const int j = ligne.indexOf(fin, i + 1);
            const int k = j < 0 ? ligne.size() : j + 1;
            r += ligne.mid(i, k - i);
            i = k;
            continue;
        }
        int j = i;   // un morceau de code, jusqu'au prochain texte
        while (j < ligne.size() && ligne.at(j) != QChar(0x00AB) && ligne.at(j) != '"') j++;
        const QString code = ligne.mid(i, j - i);
        int depuis = 0;
        auto it = m.globalMatch(code);
        while (it.hasNext()) {
            const auto x = it.next();
            r += code.mid(depuis, x.capturedStart() - depuis);
            const bool pluriel = !x.captured(1).isEmpty();
            r += avec_majuscule(x.captured(0), pluriel ? nouveau_pluriel : nouveau);
            depuis = x.capturedEnd();
            if (n) (*n)++;
        }
        r += code.mid(depuis);
        i = j;
    }
    return r;
}

QString renommer_dans_code(const QString &texte, const QString &ancien, const QString &nouveau, int *n) {
    if (n) *n = 0;
    const auto pluriel_regulier = [](const QString &x) {
        return x.endsWith('s') || x.endsWith('x') || x.endsWith('z') ? x : x + "s";
    };
    // le nom, ou son pluriel régulier (capturé à part) ; au mot entier, sans égard aux majuscules
    const QString a = QRegularExpression::escape(ancien);
    const QString plur = pluriel_regulier(ancien) != ancien ? QStringLiteral("(s)?") : QStringLiteral("()");
    const QRegularExpression m(QStringLiteral("(?<![\\p{L}\\p{N}_])") + a + plur + QStringLiteral("(?![\\p{L}\\p{N}_])"),
                               QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QStringList lignes = texte.split('\n');
    bool dans_entite = false;   // le corps d'une entité conservée : ses champs
    for (int k = 0; k < lignes.size(); k++) {
        QString &l = lignes[k];
        const QString net = l.trimmed();
        const bool tete = !l.isEmpty() && !l.at(0).isSpace();
        if (tete) dans_entite = false;
        if (net.startsWith(QStringLiteral("Remarque"), Qt::CaseInsensitive)) continue;
        int avant = n ? *n : 0;
        const QString neuve = renommer_ligne(l, m, nouveau, pluriel_regulier(nouveau), n);
        const bool change = neuve != l;
        // l'entité conservée renommée : « Un patient, conservé, anciennement « client », a : »
        static const QRegularExpression entite(QStringLiteral("^(Une?) (.+?), conservée?,(.*) a :\\s*$"));
        const auto e = entite.match(neuve);
        if (tete && e.hasMatch()) {
            dans_entite = true;
            if (change && e.captured(2).compare(nouveau, Qt::CaseInsensitive) == 0 && !neuve.contains(QStringLiteral("anciennement"))) {
                const int virgule = neuve.indexOf(QStringLiteral(", conservé")) + 1;
                const int apres = neuve.indexOf(',', virgule) + 1;
                l = neuve.left(apres) + QStringLiteral(" anciennement \u00AB ") + ancien + QStringLiteral(" \u00BB,") + neuve.mid(apres);
                continue;
            }
        }
        // un champ renommé dans une entité conservée : « un portrait (image), anciennement « photo », »
        static const QRegularExpression champ(QStringLiteral("^\\s+(?:une?|des) (.+?) \\("));
        const auto c = champ.match(neuve);
        if (!tete && dans_entite && change && c.hasMatch() && c.captured(1).compare(nouveau, Qt::CaseInsensitive) == 0
            && !neuve.contains(QStringLiteral("anciennement"))) {
            QString corps = neuve;
            QString fin;
            while (!corps.isEmpty() && (corps.endsWith(',') || corps.endsWith('.') || corps.endsWith(' '))) {
                fin.prepend(corps.back());
                corps.chop(1);
            }
            l = corps + QStringLiteral(", anciennement \u00AB ") + ancien + QStringLiteral(" \u00BB") + fin;
            continue;
        }
        Q_UNUSED(avant);
        l = neuve;
    }
    return lignes.join('\n');
}

static bool a_copier(const QString &rel) {
    return !(rel.endsWith(".grymd") || rel.contains(".grymd.") || rel.endsWith(".grymb") || rel.startsWith("Sauvegardes/")
             || rel.contains("/Sauvegardes/"));
}

QString verifier_dans_une_copie(const QString &projet, const QMap<QString, QString> &changes) {
    QTemporaryDir copie;
    if (!copie.isValid()) return QString::fromUtf8("Pas de dossier temporaire pour vérifier.");
    QDirIterator it(projet, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString rel = QDir(projet).relativeFilePath(f);
        if (!a_copier(rel)) continue;
        QDir().mkpath(QFileInfo(copie.filePath(rel)).absolutePath());
        QFile::copy(f, copie.filePath(rel));
    }
    for (auto c = changes.constBegin(); c != changes.constEnd(); ++c) {
        QFile d(copie.filePath(QDir(projet).relativeFilePath(c.key())));
        if (!d.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString::fromUtf8("La copie ne s'écrit pas.");
        d.write(c.value().toUtf8());
    }
    for (const QString &f : fichiers_du_projet(copie.path())) {
        QFile d(f);
        if (!d.open(QIODevice::ReadOnly)) continue;
        const Diagnostic_atelier r = analyser_source(QString::fromUtf8(d.readAll()), f.endsWith(".grymc"), f);
        if (!r.message.isEmpty())
            return QString::fromUtf8("%1, ligne %2 : %3").arg(QDir(copie.path()).relativeFilePath(f)).arg(r.ligne).arg(r.message);
    }
    return QString();
}

QString premiere_erreur_du_projet(const QString &projet) {
    for (const QString &f : fichiers_du_projet(projet)) {
        QFile d(f);
        if (!d.open(QIODevice::ReadOnly)) continue;
        const Diagnostic_atelier r = analyser_source(QString::fromUtf8(d.readAll()), f.endsWith(".grymc"), f);
        if (!r.message.isEmpty())
            return QString::fromUtf8("%1, ligne %2 : %3").arg(QDir(projet).relativeFilePath(f)).arg(r.ligne).arg(r.message);
    }
    return QString();
}

// --- la barre, dans le fichier ouvert ---

BarreRecherche::BarreRecherche(Editeur *e, QWidget *parent) : QWidget(parent), editeur_(e) {
    setObjectName("barre-recherche");
    cherche_ = new QLineEdit;
    cherche_->setPlaceholderText(QString::fromUtf8("Chercher"));
    cherche_->setClearButtonEnabled(true);
    remplace_ = new QLineEdit;
    remplace_->setPlaceholderText(QString::fromUtf8("Remplacer par"));
    mot_entier_ = new QCheckBox(QString::fromUtf8("Mot entier"));
    casse_ = new QCheckBox(QString::fromUtf8("Majuscules"));
    casse_->setToolTip(QString::fromUtf8("Respecter les majuscules"));
    compte_ = new QLabel;
    compte_->setProperty("niveau", "legende");
    auto *precedent = new QPushButton(QString::fromUtf8("‹"));
    precedent->setToolTip(QString::fromUtf8("Précédente (Maj+Entrée)"));
    auto *suivante = new QPushButton(QString::fromUtf8("›"));
    suivante->setToolTip(QString::fromUtf8("Suivante (Entrée)"));
    auto *fermer = new QPushButton(QString::fromUtf8("×"));
    fermer->setToolTip(QString::fromUtf8("Fermer (Échap)"));
    for (QPushButton *b : {precedent, suivante, fermer}) b->setFixedWidth(32);
    auto *un = new QPushButton(QString::fromUtf8("Remplacer"));
    auto *tous = new QPushButton(QString::fromUtf8("Tout remplacer"));

    auto *haut = new QHBoxLayout;
    haut->addWidget(cherche_, 1);
    haut->addWidget(compte_);
    haut->addWidget(precedent);
    haut->addWidget(suivante);
    haut->addWidget(mot_entier_);
    haut->addWidget(casse_);
    haut->addWidget(fermer);
    rangee_remplacer_ = new QWidget;
    auto *bas = new QHBoxLayout(rangee_remplacer_);
    bas->setContentsMargins(0, 0, 0, 0);
    bas->addWidget(remplace_, 1);
    bas->addWidget(un);
    bas->addWidget(tous);
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(6, 4, 6, 4);
    col->setSpacing(4);
    col->addLayout(haut);
    col->addWidget(rangee_remplacer_);

    connect(cherche_, &QLineEdit::textChanged, this, [this] {
        compter();
        // pendant la frappe : la première occurrence depuis le début de la sélection, sans sauter plus loin
        QTextCursor c = editeur_->textCursor();
        c.setPosition(c.selectionStart());
        editeur_->setTextCursor(c);
        if (!cherche_->text().isEmpty()) suivant();
    });
    connect(mot_entier_, &QCheckBox::toggled, this, &BarreRecherche::compter);
    connect(casse_, &QCheckBox::toggled, this, &BarreRecherche::compter);
    connect(suivante, &QPushButton::clicked, this, [this] { suivant(); });
    connect(precedent, &QPushButton::clicked, this, [this] { suivant(true); });
    connect(fermer, &QPushButton::clicked, this, [this] { hide(); editeur_->setFocus(); });
    connect(un, &QPushButton::clicked, this, &BarreRecherche::remplacer_un);
    connect(tous, &QPushButton::clicked, this, &BarreRecherche::remplacer_tout);
    connect(editeur_->document(), &QTextDocument::contentsChanged, this, [this] { if (isVisible()) compter(); });
    hide();
}

QRegularExpression BarreRecherche::motif() const {
    return motif_recherche(cherche_->text(), mot_entier_->isChecked(), casse_->isChecked());
}

void BarreRecherche::compter() {
    if (cherche_->text().isEmpty()) { compte_->clear(); return; }
    const int n = (int)editeur_->toPlainText().count(motif());
    compte_->setText(n == 0 ? QString::fromUtf8("aucune") : n == 1 ? QString::fromUtf8("1 trouvée") : QString::fromUtf8("%1 trouvées").arg(n));
}

void BarreRecherche::montrer(bool remplacer) {
    const QString choisi = editeur_->textCursor().selectedText();
    if (!choisi.isEmpty() && !choisi.contains(QChar(0x2029))) cherche_->setText(choisi);   // pas une sélection de plusieurs lignes
    rangee_remplacer_->setVisible(remplacer);
    show();
    cherche_->setFocus();
    cherche_->selectAll();
    compter();
}

bool BarreRecherche::suivant(bool arriere) {
    if (cherche_->text().isEmpty()) return false;
    const QRegularExpression m = motif();
    QTextDocument::FindFlags f;
    if (arriere) f |= QTextDocument::FindBackward;
    QTextCursor c = editeur_->document()->find(m, editeur_->textCursor(), f);
    if (c.isNull()) {   // au bout : on repart de l'autre bout
        QTextCursor depart(editeur_->document());
        if (arriere) depart.movePosition(QTextCursor::End);
        c = editeur_->document()->find(m, depart, f);
    }
    if (c.isNull()) return false;
    if (!c.block().isVisible()) editeur_->deplier_tout();   // dans un bloc replié ou caché par le plan
    editeur_->setTextCursor(c);
    editeur_->centerCursor();
    return true;
}

bool BarreRecherche::remplacer_un() {
    QTextCursor c = editeur_->textCursor();
    const QRegularExpression m = motif();
    if (c.hasSelection()) {
        const auto x = m.match(c.selectedText());
        if (x.hasMatch() && x.capturedStart() == 0 && x.capturedLength() == c.selectedText().size()) {
            c.insertText(remplace_->text());
            editeur_->setTextCursor(c);
        }
    }
    return suivant();
}

int BarreRecherche::remplacer_tout() {
    if (cherche_->text().isEmpty()) return 0;
    const QRegularExpression m = motif();
    const QString avant = editeur_->toPlainText();
    const int n = (int)avant.count(m);
    if (n == 0) return 0;
    const QString apres = remplacer_litteralement(avant, m, remplace_->text());
    QTextCursor c(editeur_->document());
    c.beginEditBlock();   // un seul Cmd+Z défait tout
    c.select(QTextCursor::Document);
    c.insertText(apres);
    c.endEditBlock();
    compter();
    compte_->setText(n == 1 ? QString::fromUtf8("1 remplacée") : QString::fromUtf8("%1 remplacées").arg(n));
    return n;
}

void BarreRecherche::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { hide(); editeur_->setFocus(); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (remplace_->hasFocus()) remplacer_un();
        else suivant(e->modifiers() & Qt::ShiftModifier);
        return;
    }
    QWidget::keyPressEvent(e);
}

// --- la fenêtre, dans tout le projet ---

RechercheProjet::RechercheProjet(const QString &projet, std::function<QMap<QString, QString>()> ouverts, QWidget *parent)
    : QDialog(parent), projet_(projet), ouverts_(std::move(ouverts)) {
    setWindowTitle(QString::fromUtf8("Chercher dans le projet"));
    setObjectName("recherche-projet");
    cherche_ = new QLineEdit;
    cherche_->setPlaceholderText(QString::fromUtf8("Chercher"));
    remplace_ = new QLineEdit;
    remplace_->setPlaceholderText(QString::fromUtf8("Remplacer par"));
    mot_entier_ = new QCheckBox(QString::fromUtf8("Mot entier"));
    mot_entier_->setChecked(true);   // le cas courant : un nom (un écran, un bouton, un champ)
    casse_ = new QCheckBox(QString::fromUtf8("Majuscules"));
    compte_ = new QLabel;
    compte_->setProperty("niveau", "legende");
    auto *chercher = new QPushButton(QString::fromUtf8("Chercher"));
    chercher->setDefault(true);
    remplacer_ = new QPushButton(QString::fromUtf8("Tout remplacer…"));
    resultats_ = new QTreeWidget;
    resultats_->setHeaderLabels({QString::fromUtf8("Fichier"), QString::fromUtf8("Ligne"), QString::fromUtf8("Texte")});
    resultats_->setRootIsDecorated(false);
    resultats_->setUniformRowHeights(true);
    resultats_->header()->setStretchLastSection(true);

    auto *l1 = new QHBoxLayout;
    l1->addWidget(cherche_, 1);
    l1->addWidget(mot_entier_);
    l1->addWidget(casse_);
    l1->addWidget(chercher);
    auto *l2 = new QHBoxLayout;
    l2->addWidget(remplace_, 1);
    l2->addWidget(remplacer_);
    auto *col = new QVBoxLayout(this);
    col->addLayout(l1);
    col->addLayout(l2);
    col->addWidget(compte_);
    col->addWidget(resultats_, 1);
    resize(820, 520);

    connect(chercher, &QPushButton::clicked, this, &RechercheProjet::chercher_maintenant);
    connect(cherche_, &QLineEdit::returnPressed, this, &RechercheProjet::chercher_maintenant);
    connect(mot_entier_, &QCheckBox::toggled, this, &RechercheProjet::chercher_maintenant);
    connect(casse_, &QCheckBox::toggled, this, &RechercheProjet::chercher_maintenant);
    connect(resultats_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) {
        emit aller(i->data(0, Qt::UserRole).toString(), i->text(1).toInt(), i->data(1, Qt::UserRole).toInt());
    });
    connect(remplacer_, &QPushButton::clicked, this, [this] {
        if (cherche_->text().isEmpty()) return;
        emit remplacer_tout(motif(), remplace_->text());
        chercher_maintenant();
    });
}

QRegularExpression RechercheProjet::motif() const {
    return motif_recherche(cherche_->text(), mot_entier_->isChecked(), casse_->isChecked());
}

void RechercheProjet::chercher_maintenant() {
    resultats_->clear();
    if (cherche_->text().isEmpty()) { compte_->clear(); return; }
    const auto occ = chercher_dans_projet(projet_, motif(), ouverts_ ? ouverts_() : QMap<QString, QString>());
    QSet<QString> fichiers;
    for (const Occurrence &o : occ) {
        auto *i = new QTreeWidgetItem({QDir(projet_).relativeFilePath(o.fichier), QString::number(o.ligne), o.extrait});
        i->setData(0, Qt::UserRole, o.fichier);
        i->setData(1, Qt::UserRole, o.colonne);
        resultats_->addTopLevelItem(i);
        fichiers.insert(o.fichier);
    }
    resultats_->resizeColumnToContents(0);
    resultats_->resizeColumnToContents(1);
    compte_->setText(occ.isEmpty() ? QString::fromUtf8("Aucune occurrence.")
                                   : QString::fromUtf8("%1 occurrence%2 dans %3 fichier%4. Double-clic : y aller.")
                                         .arg(occ.size()).arg(occ.size() > 1 ? "s" : "")
                                         .arg(fichiers.size()).arg(fichiers.size() > 1 ? "s" : ""));
    remplacer_->setEnabled(!occ.isEmpty());
}
