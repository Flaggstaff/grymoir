// GrymoiR : l'atelier, le contenu de la base (contenu.h).
#include "contenu.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

extern "C" {
#include "sqlite3.h"
int base_collations(sqlite3 *db);
}

namespace {

// « "e client" », « "c nom" » : les noms SQL de la base (src/base.c, ajouter_nom).
QString sql_nom(const char *prefixe, const QString &nom) {
    return "\"" + QString::fromUtf8(prefixe) + QString(nom).replace("\"", "\"\"") + "\"";
}

QString titre_de(const QString &champ) { return champ.left(1).toUpper() + champ.mid(1); }

// La lignée d'une entité, de la racine à l'entité elle-même ; vide si elle n'est pas dans le schéma.
QVector<const EntiteSchema *> lignee(const QString &entite, const QMap<QString, const EntiteSchema *> &par_nom) {
    QVector<const EntiteSchema *> l;
    for (QString x = entite; par_nom.contains(x) && l.size() < 256; x = par_nom[x]->parent) {
        l.prepend(par_nom[x]);
        if (par_nom[x]->parent.isEmpty()) break;
    }
    return l;
}

// La clé d'une entité : son premier champ texte unique, dans sa lignée (comme les menus des formulaires, § 19).
// *table reçoit l'entité qui le porte.
QString cle_de(const QString &entite, const QMap<QString, const EntiteSchema *> &par_nom, QString *table) {
    for (const EntiteSchema *e : lignee(entite, par_nom))
        for (const auto &c : e->champs)
            if (c.unique && c.type == "texte" && !c.multiple) { *table = e->nom; return c.nom; }
    return QString();
}

// Une valeur lue, écrite comme GrymoiR l'écrit.
QString ecrire(const QString &type, sqlite3_stmt *st, int i) {
    if (sqlite3_column_type(st, i) == SQLITE_NULL) return QString();
    const QString t = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, i)));
    if (type == "nombre") return QString(t).replace('.', ',').replace('-', QChar(0x2212));
    if (type == "nombre entier") return QString(t).replace('-', QChar(0x2212));
    if (type == "vrai ou faux") return t == "1" ? "vrai" : "faux";
    if (type == "date" && t.size() == 10) return t.mid(8, 2) + "." + t.mid(5, 2) + "." + t.left(4);
    return t;
}

}  // namespace

QString base_du_programme(const QString &programme) {
    const QFileInfo f(programme);
    return f.absolutePath() + "/" + f.completeBaseName() + ".grymd";
}

QString lire_contenu(const QString &base, const QString &entite, const QVector<EntiteSchema> &schema, bool corbeille,
                     Contenu *c) {
    *c = Contenu();
    if (!QFileInfo::exists(base))
        return QString("Pas encore de base « %1 » : lance le programme une première fois.").arg(QFileInfo(base).fileName());
    QMap<QString, const EntiteSchema *> par_nom;
    for (const auto &e : schema) par_nom[e.nom] = &e;
    const auto l = lignee(entite, par_nom);
    if (l.isEmpty()) return QString("« %1 » n'est pas une entité du projet.").arg(entite);

    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(QFileInfo(base).absoluteFilePath().toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr)
        != SQLITE_OK) {
        const QString m = QString::fromUtf8(sqlite3_errmsg(db));
        sqlite3_close(db);
        return QString("Base « %1 » illisible : %2.").arg(QFileInfo(base).fileName(), m);
    }
    base_collations(db);
    sqlite3_busy_timeout(db, 2000);

    // Les colonnes réelles de chaque table de la lignée : elles comprennent les champs des aptitudes.
    auto colonnes_de = [db](const QString &table) {
        QStringList r;
        sqlite3_stmt *st = nullptr;
        const QByteArray q = QString("SELECT name FROM pragma_table_info(%1)").arg("'" + QString(table).replace("'", "''") + "'").toUtf8();
        if (sqlite3_prepare_v2(db, q.constData(), -1, &st, nullptr) == SQLITE_OK)
            while (sqlite3_step(st) == SQLITE_ROW) r << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 0)));
        sqlite3_finalize(st);
        return r;
    };
    auto table_existe = [db](const QString &t) {
        sqlite3_stmt *st = nullptr;
        sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?", -1, &st, nullptr);
        const QByteArray b = t.toUtf8();
        sqlite3_bind_text(st, 1, b.constData(), -1, SQLITE_TRANSIENT);
        const bool r = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
        return r;
    };
    if (!table_existe("e " + entite)) {
        sqlite3_close(db);
        return QString("La base ne connaît pas encore « %1 » : lance le programme pour qu'elle l'apprenne.").arg(entite);
    }

    const bool fichiers_a_part = table_existe("grym_fichier");
    QStringList choix = {"o.id", "o.supprime"}, types = {"", ""};
    c->colonnes << "N°";
    QString depuis = sql_nom("e ", entite) + " t0 JOIN grym_objet o ON o.id = t0.id";
    for (int k = 0; k < l.size(); k++) {
        const EntiteSchema *e = l[k];
        const QString alias = QString("t%1").arg(k + 1);
        if (!table_existe("e " + e->nom)) continue;
        depuis += QString(" LEFT JOIN %1 %2 ON %2.id = t0.id").arg(sql_nom("e ", e->nom), alias);
        QMap<QString, ChampSchema> champs;
        for (const auto &x : e->champs) champs[x.nom] = x;
        const QStringList reelles = colonnes_de("e " + e->nom);
        for (const QString &col : reelles) {
            if (!col.startsWith("c ")) continue;
            const QString nom = col.mid(2);
            const ChampSchema ch = champs.value(nom);
            const QString x = alias + "." + sql_nom("c ", nom);
            if (ch.type == "fichier" || ch.type == "image" || ch.type == "son" || reelles.contains("n " + nom)) {
                // format 2 (docs/vm.md, § 8) : la colonne garde le numéro d'une ligne de grym_fichier, qui sait la
                // taille ; une base pas encore convertie garde les octets eux-mêmes
                const QString taille = fichiers_a_part
                    ? QString("CASE WHEN typeof(%1) = 'integer' THEN (SELECT taille FROM grym_fichier WHERE id = %1) "
                              "ELSE length(%1) END").arg(x)
                    : QString("length(%1)").arg(x);
                choix << QString("CASE WHEN %1 IS NULL THEN NULL ELSE %2 || ' (' || %3 || ' octets)' END")
                             .arg(x, alias + "." + sql_nom("n ", nom), taille);
                types << "texte";
            } else if (ch.lien) {
                QString porteur;
                const QString cle = cle_de(ch.type, par_nom, &porteur);
                choix << (cle.isEmpty() ? QString("CASE WHEN %1 IS NULL THEN NULL ELSE 'n° ' || %1 END").arg(x)
                                        : QString("(SELECT %1 FROM %2 WHERE id = %3)")
                                              .arg(sql_nom("c ", cle), sql_nom("e ", porteur), x));
                types << "texte";
            } else {
                choix << x;
                types << ch.type;
            }
            c->colonnes << titre_de(nom);
        }
        for (const auto &x : e->champs) {   // « plusieurs » (§ 16.13) : une table de liaison, montrée par son compte
            if (!x.multiple || !table_existe("m " + e->nom + "." + x.nom)) continue;
            choix << QString("(SELECT count(*) FROM %1 WHERE a = t0.id)").arg(sql_nom("m ", e->nom + "." + x.nom));
            types << "nombre entier";
            c->colonnes << titre_de(x.nom) + " (nombre)";
        }
    }
    if (corbeille) c->colonnes << "Corbeille";
    const QByteArray q = QString("SELECT %1 FROM %2 %3 ORDER BY o.id")
                             .arg(choix.join(", "), depuis, corbeille ? QString() : QString("WHERE o.supprime IS NULL"))
                             .toUtf8();
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, q.constData(), -1, &st, nullptr) != SQLITE_OK) {
        const QString m = QString::fromUtf8(sqlite3_errmsg(db));
        sqlite3_close(db);
        return "Lecture impossible : " + m + ".";
    }
    int r;
    while ((r = sqlite3_step(st)) == SQLITE_ROW) {
        QStringList ligne;
        ligne << QString::number(sqlite3_column_int64(st, 0));
        for (int i = 2; i < choix.size(); i++) ligne << ecrire(types[i], st, i);
        const bool supprime = sqlite3_column_type(st, 1) != SQLITE_NULL;
        if (corbeille) ligne << (supprime ? QString("supprimé") : QString());
        c->lignes << ligne;
    }
    sqlite3_finalize(st);
    QString erreur;
    if (r != SQLITE_DONE) erreur = "Lecture interrompue : " + QString::fromUtf8(sqlite3_errmsg(db)) + ".";
    const QByteArray qc = QString("SELECT count(*) FROM %1 JOIN grym_objet o ON o.id = t0.id WHERE o.supprime IS NOT NULL")
                              .arg(sql_nom("e ", entite) + " t0").toUtf8();
    if (sqlite3_prepare_v2(db, qc.constData(), -1, &st, nullptr) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
        c->dans_la_corbeille = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    sqlite3_close(db);
    return erreur;
}

FenetreContenu::FenetreContenu(const QString &base_, const QString &entite_, const QVector<EntiteSchema> &schema_,
                               QWidget *parent)
    : QDialog(parent), base(base_), entite(entite_), schema(schema_) {
    setWindowTitle(QString("Contenu : %1").arg(entite));
    resize(900, 520);
    modele = new QStandardItemModel(this);
    filtre = new QSortFilterProxyModel(this);
    filtre->setSourceModel(modele);
    filtre->setFilterKeyColumn(-1);
    filtre->setFilterCaseSensitivity(Qt::CaseInsensitive);
    vue = new QTableView;
    vue->setModel(filtre);
    vue->setSortingEnabled(true);
    vue->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vue->setSelectionBehavior(QAbstractItemView::SelectRows);
    vue->verticalHeader()->hide();
    vue->horizontalHeader()->setStretchLastSection(true);
    recherche = new QLineEdit;
    recherche->setPlaceholderText("Filtrer : un mot cherché dans toutes les colonnes");
    recherche->setClearButtonEnabled(true);
    corbeille = new QCheckBox("Montrer la corbeille");
    etat = new QLabel;
    etat->setWordWrap(true);
    auto *note = new QLabel("Lecture seule : un objet se crée, se modifie ou se supprime par le programme, "
                            "pour que les règles et les valeurs permises soient toujours vérifiées.");
    note->setWordWrap(true);
    note->setProperty("role", "discret");
    auto *boutons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *relire_b = boutons->addButton("Relire", QDialogButtonBox::ActionRole);
    auto *haut = new QHBoxLayout;
    haut->addWidget(recherche, 1);
    haut->addWidget(corbeille);
    auto *pile = new QVBoxLayout(this);
    pile->addLayout(haut);
    pile->addWidget(vue, 1);
    pile->addWidget(etat);
    pile->addWidget(note);
    pile->addWidget(boutons);
    connect(recherche, &QLineEdit::textChanged, filtre, &QSortFilterProxyModel::setFilterFixedString);
    connect(recherche, &QLineEdit::textChanged, this, &FenetreContenu::compter);
    connect(corbeille, &QCheckBox::toggled, this, &FenetreContenu::relire);
    connect(relire_b, &QPushButton::clicked, this, &FenetreContenu::relire);
    connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    relire();
}

void FenetreContenu::relire() {
    Contenu c;
    const QString erreur = lire_contenu(base, entite, schema, corbeille->isChecked(), &c);
    if (!erreur.isEmpty() && c.colonnes.isEmpty()) {
        modele->clear();
        etat->setText(erreur);
        erreur_lue = erreur;
        return;
    }
    {
        modele->clear();
        modele->setHorizontalHeaderLabels(c.colonnes);
        for (const auto &l : c.lignes) {
            QList<QStandardItem *> r;
            for (int i = 0; i < l.size(); i++) {
                auto *x = new QStandardItem(l[i]);
                if (i == 0) x->setData(l[i].toLongLong(), Qt::DisplayRole);   // le n° se trie comme un nombre
                r << x;
            }
            modele->appendRow(r);
        }
        vue->resizeColumnsToContents();
    }
    dans_la_corbeille = c.dans_la_corbeille;
    erreur_lue = erreur;
    compter();
}

// Le filtre ne relit pas la base : il trie ce qui est déjà là ; seul le compte change.
void FenetreContenu::compter() {
    const int vus = filtre->rowCount();
    QString t = vus == modele->rowCount() ? QString("%1 objet%2").arg(vus).arg(vus > 1 ? "s" : "")
                                          : QString("%1 sur %2 objets").arg(vus).arg(modele->rowCount());
    if (dans_la_corbeille && !corbeille->isChecked()) t += QString(", et %1 dans la corbeille").arg(dans_la_corbeille);
    etat->setText(erreur_lue.isEmpty() ? t + "." : t + ". " + erreur_lue);
}

namespace {

// Copie `de` dans `vers` par l'API de sauvegarde de SQLite : jamais une copie de fichier à moitié écrite.
QString copier_base(const QString &de, const QString &vers) {
    sqlite3 *source = nullptr, *cible = nullptr;
    QString erreur;
    if (sqlite3_open_v2(QFileInfo(de).absoluteFilePath().toUtf8().constData(), &source, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        erreur = QString("« %1 » ne s'ouvre pas : %2.").arg(QFileInfo(de).fileName(), QString::fromUtf8(sqlite3_errmsg(source)));
    else if (sqlite3_open_v2(QFileInfo(vers).absoluteFilePath().toUtf8().constData(), &cible,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        erreur = QString("« %1 » ne s'écrit pas : %2.").arg(QFileInfo(vers).fileName(), QString::fromUtf8(sqlite3_errmsg(cible)));
    else {
        sqlite3_busy_timeout(source, 2000);
        sqlite3_busy_timeout(cible, 2000);
        sqlite3_backup *b = sqlite3_backup_init(cible, "main", source, "main");
        if (!b) erreur = "Copie impossible : " + QString::fromUtf8(sqlite3_errmsg(cible)) + ".";
        else {
            const int r = sqlite3_backup_step(b, -1);
            sqlite3_backup_finish(b);
            if (r != SQLITE_DONE)
                erreur = r == SQLITE_BUSY || r == SQLITE_LOCKED ? QString("La base est occupée : arrête le programme, puis recommence.")
                                                                : "Copie impossible : " + QString::fromUtf8(sqlite3_errstr(r)) + ".";
        }
    }
    sqlite3_close(source);
    sqlite3_close(cible);
    return erreur;
}

bool est_base_grymoir(const QString &chemin) {
    sqlite3 *db = nullptr;
    bool r = false;
    if (sqlite3_open_v2(QFileInfo(chemin).absoluteFilePath().toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'grym_schema'", -1, &st,
                               nullptr) == SQLITE_OK)
            r = sqlite3_step(st) == SQLITE_ROW;
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return r;
}

}  // namespace

QString sauvegarder_base(const QString &base, const QString &dossier, QString *copie) {
    if (!QFileInfo::exists(base))
        return QString("Pas encore de base « %1 » : rien à sauvegarder.").arg(QFileInfo(base).fileName());
    if (!QDir().mkpath(dossier)) return QString("Le dossier « %1 » ne se crée pas.").arg(dossier);
    const QString racine = QDir(dossier).filePath(QFileInfo(base).completeBaseName() + " "
                                                  + QDateTime::currentDateTime().toString("yyyy-MM-dd HH'h'mm"));
    QString c = racine + ".grymd";
    for (int n = 2; QFileInfo::exists(c); n++) c = QString("%1 (%2).grymd").arg(racine).arg(n);   // jamais d'écrasement
    const QString erreur = copier_base(base, c);
    if (!erreur.isEmpty()) { QFile::remove(c); return erreur; }
    *copie = c;
    return QString();
}

QString restaurer_base(const QString &sauvegarde, const QString &base, const QString &dossier, QString *copie_avant) {
    copie_avant->clear();
    if (QFileInfo(sauvegarde).absoluteFilePath() == QFileInfo(base).absoluteFilePath())
        return "C'est la base en service : choisis une sauvegarde.";
    if (!est_base_grymoir(sauvegarde))
        return QString("« %1 » n'est pas une base GrymoiR.").arg(QFileInfo(sauvegarde).fileName());
    if (QFileInfo::exists(base)) {   // la base actuelle d'abord : une restauration se défait en restaurant celle-ci
        const QString e = sauvegarder_base(base, dossier, copie_avant);
        if (!e.isEmpty()) return "La base actuelle n'a pas pu être sauvegardée, rien n'a changé : " + e;
    }
    return copier_base(sauvegarde, base);
}
