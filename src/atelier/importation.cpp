// GrymoiR : l'atelier, importer un tableau CSV dans une entité (importation.h).
#include "importation.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

bool voyelle(const QString &m) {   // « l'œuvre », « d'œuvres » : devant une voyelle ou un h (muet par défaut)
    return !m.isEmpty() && QString("aeiouyhàâäéèêëîïôöùûüœAEIOUYHÀÂÉÈÊÎÔÙÛŒ").contains(m.at(0));
}
QString le(const QString &m, bool f) { return voyelle(m) ? "l'" + m : (f ? "la " : "le ") + m; }
QString capitale(const QString &s) { return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1); }
QString nouveau(const QString &m, bool f) { return f ? "une nouvelle " + m : voyelle(m) ? "un nouvel " + m : "un nouveau " + m; }

// « Écrit », « ecrit » et « ÉCRIT » se valent, comme pour GrymoiR (§ 15.5).
QString simple(const QString &s) {
    QString r;
    for (QChar c : s.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing) r += c.toLower();
    return r.simplified();
}

QMap<QString, const EntiteSchema *> index(const QVector<EntiteSchema> &schema) {
    QMap<QString, const EntiteSchema *> m;
    for (const auto &e : schema) m[e.nom] = &e;
    return m;
}

QVector<const EntiteSchema *> lignee(const QString &entite, const QMap<QString, const EntiteSchema *> &par_nom) {
    QVector<const EntiteSchema *> l;
    for (QString x = entite; par_nom.contains(x) && l.size() < 256;) {
        l.prepend(par_nom[x]);
        if (par_nom[x]->parent.isEmpty() || par_nom[x]->parent == x) break;
        x = par_nom[x]->parent;
    }
    return l;
}

// La clé d'une entité, dans sa lignée : son premier champ texte unique (comme les menus des formulaires, § 19).
const ChampSchema *cle_de(const QString &entite, const QMap<QString, const EntiteSchema *> &par_nom) {
    for (const EntiteSchema *e : lignee(entite, par_nom))
        for (const auto &c : e->champs)
            if (c.unique && c.type == "texte" && !c.multiple) return &c;
    return nullptr;
}

}  // namespace

QStringList titres_csv(const QString &chemin, QString *erreur) {
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly)) { *erreur = QString("« %1 » ne s'ouvre pas.").arg(QFileInfo(chemin).fileName()); return {}; }
    QString t = QString::fromUtf8(f.read(1 << 20));
    if (t.startsWith(QChar(0xFEFF))) t.remove(0, 1);
    // la ligne des titres, guillemets compris (un titre peut contenir un saut de ligne)
    QString ligne;
    bool dedans = false;
    for (QChar c : t) {
        if (c == '"') dedans = !dedans;
        if (!dedans && (c == '\n' || c == '\r')) { if (ligne.isEmpty()) continue; break; }
        ligne += c;
    }
    if (ligne.trimmed().isEmpty()) { *erreur = "Le tableau est vide : la première ligne doit porter les titres."; return {}; }
    // le séparateur le plus fréquent hors guillemets : « ; », puis « , », puis la tabulation (§ 15.5)
    const QChar seps[] = {';', ',', '\t'};
    QChar sep = ';';
    int meilleur = -1;
    for (QChar s : seps) {
        int n = 0;
        bool q = false;
        for (QChar c : ligne) { if (c == '"') q = !q; else if (!q && c == s) n++; }
        if (n > meilleur) { meilleur = n; sep = s; }
    }
    QStringList r;
    QString cellule;
    bool q = false;
    for (int i = 0; i < ligne.size(); i++) {
        const QChar c = ligne.at(i);
        if (c == '"') {
            if (q && i + 1 < ligne.size() && ligne.at(i + 1) == '"') { cellule += '"'; i++; }
            else q = !q;
        } else if (c == sep && !q) { r << cellule.trimmed(); cellule.clear(); }
        else cellule += c;
    }
    r << cellule.trimmed();
    return r;
}

QVector<ChampSchema> champs_importables(const QString &entite, const QVector<EntiteSchema> &schema) {
    const auto par_nom = index(schema);
    QVector<ChampSchema> r;
    for (const EntiteSchema *e : lignee(entite, par_nom))
        for (const auto &c : e->champs) {
            if (c.multiple || c.type == "fichier" || c.type == "image" || c.type == "son") continue;
            if (c.lien && !cle_de(c.type, par_nom)) continue;
            r << c;
        }
    return r;
}

QString colonne_pour(const QString &champ, const QStringList &titres) {
    for (const QString &t : titres) if (simple(t) == simple(champ)) return t;
    return QString();
}

QString code_importation(const QString &entite, const QVector<EntiteSchema> &schema, const QString &csv,
                         const QMap<QString, QString> &colonnes, QString *erreur) {
    const auto par_nom = index(schema);
    if (!par_nom.contains(entite)) { *erreur = QString("« %1 » n'est pas une entité du projet.").arg(entite); return {}; }
    const EntiteSchema &e = *par_nom[entite];
    auto guillemets = [](const QString &s) { return s.contains(QChar(0x00AB)) || s.contains(QChar(0x00BB)); };
    if (guillemets(csv)) { *erreur = "Le nom du fichier ne doit pas contenir de guillemets « »."; return {}; }
    QStringList dans_le_bloc, apres;
    for (const ChampSchema &c : champs_importables(entite, schema)) {
        const QString col = colonnes.value(c.nom);
        if (col.isEmpty()) {
            if (!c.facultatif) {
                *erreur = QString("« %1 » est obligatoire : choisis sa colonne.").arg(c.nom);
                return {};
            }
            continue;
        }
        if (guillemets(col)) { *erreur = QString("La colonne « %1 » a des guillemets dans son titre : renomme-la dans le tableau.").arg(col); return {}; }
        const QString cellule = QString("la colonne « %1 » de la rangée").arg(col);
        QString valeur = cellule;
        if (c.lien) {
            const ChampSchema *cle = cle_de(c.type, par_nom);
            const bool f = par_nom[c.type]->feminin;
            valeur = QString("%1 conservé%2 dont %3 est %4").arg(le(c.type, f), f ? "e" : "", le(cle->nom, cle->feminin), cellule);
        } else if (c.type != "texte") {
            valeur += " en " + c.type;   // nombre, nombre entier, date, année, heure, vrai ou faux (§ 4.5)
        }
        if (!c.facultatif) dans_le_bloc << QString("            %1 vaut %2.").arg(capitale(le(c.nom, c.feminin)), valeur);
        else apres << QString("        Si %1 est présente :\n            %2 de l'importé devient %3.")
                          .arg(cellule, capitale(le(c.nom, c.feminin)), valeur);
    }
    if (dans_le_bloc.isEmpty() && apres.isEmpty()) { *erreur = "Choisis au moins une colonne."; return {}; }
    const QString conserves = QString("conservé%1s").arg(e.feminin ? "e" : "");
    QStringList l;
    l << QString("Remarque : importé de « %1 » par l'atelier, tant que la base n'a aucun%2 %3.")
             .arg(csv, e.feminin ? "e" : "", e.nom);
    l << QString("Si le nombre %1%2 %3 = 0 :").arg(voyelle(e.pluriel) ? "d'" : "de ", e.pluriel, conserves);
    l << QString("    Pour chaque rangée du tableau « %1 » :").arg(csv);
    l << QString("        L'importé vaut %1%2").arg(nouveau(e.nom, e.feminin), dans_le_bloc.isEmpty() ? "." : " :");
    l << dans_le_bloc << apres;
    l << "        Conserver l'importé.";
    return l.join('\n') + '\n';
}

DialogueImportation::DialogueImportation(const QString &csv_, const QString &csv_ecrit_, const QString &entite,
                                         const QVector<EntiteSchema> &schema_, QWidget *parent)
    : QDialog(parent), csv(csv_), csv_ecrit(csv_ecrit_), schema(schema_) {
    setWindowTitle(QString("Importer « %1 »").arg(QFileInfo(csv).fileName()));
    resize(760, 620);
    QString erreur;
    titres = titres_csv(csv, &erreur);
    entites = new QComboBox;
    for (const auto &e : schema) entites->addItem(e.nom);
    entites->setCurrentIndex(qMax(0, entites->findText(entite)));
    table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({"Champ", "Type", "Colonne du tableau"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    texte = new QPlainTextEdit;
    texte->setReadOnly(true);
    texte->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    etat = new QLabel;
    etat->setWordWrap(true);
    auto *boutons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    ecrire = boutons->addButton("Écrire dans le programme", QDialogButtonBox::AcceptRole);
    ecrire->setProperty("role", "principal");
    auto *forme = new QFormLayout;
    forme->addRow("Entité :", entites);
    forme->addRow("Colonnes :", new QLabel(titres.isEmpty() ? erreur : titres.join(", ")));
    auto *pile = new QVBoxLayout(this);
    pile->addLayout(forme);
    pile->addWidget(table, 1);
    pile->addWidget(new QLabel("Ce que l'atelier écrira dans le programme principal, avant ce qu'il montre (« Ouvrir », « Afficher ») :"));
    pile->addWidget(texte, 1);
    pile->addWidget(etat);
    pile->addWidget(boutons);
    connect(entites, &QComboBox::currentIndexChanged, this, &DialogueImportation::remplir);
    connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    remplir();
}

void DialogueImportation::remplir() {
    table->setRowCount(0);
    for (const ChampSchema &c : champs_importables(entites->currentText(), schema)) {
        const int k = table->rowCount();
        table->insertRow(k);
        auto *n = new QTableWidgetItem(c.nom + (c.facultatif ? QString() : QString(" *")));
        n->setData(Qt::UserRole, c.nom);
        n->setFlags(Qt::ItemIsEnabled);
        n->setToolTip(c.facultatif ? "Facultatif : une cellule vide le laisse absent."
                                   : "Obligatoire : une cellule vide arrêtera l'importation, sans rien conserver.");
        table->setItem(k, 0, n);
        auto *t = new QTableWidgetItem(c.valeurs.isEmpty() ? c.type : "texte, valeurs permises");
        t->setFlags(Qt::ItemIsEnabled);
        table->setItem(k, 1, t);
        auto *m = new QComboBox;
        m->addItem("(aucune)");
        m->addItems(titres);
        m->setCurrentIndex(qMax(0, m->findText(colonne_pour(c.nom, titres))));
        connect(m, &QComboBox::currentIndexChanged, this, &DialogueImportation::apercu);
        table->setCellWidget(k, 2, m);
    }
    apercu();
}

void DialogueImportation::apercu() {
    QMap<QString, QString> colonnes;
    for (int k = 0; k < table->rowCount(); k++)
        if (auto *m = qobject_cast<QComboBox *>(table->cellWidget(k, 2)); m && m->currentIndex() > 0)
            colonnes[table->item(k, 0)->data(Qt::UserRole).toString()] = m->currentText();
    QString erreur;
    code_ = titres.isEmpty() ? QString() : code_importation(entites->currentText(), schema, csv_ecrit, colonnes, &erreur);
    texte->setPlainText(code_);
    ecrire->setEnabled(!code_.isEmpty());
    etat->setText(!erreur.isEmpty() ? erreur
                                    : "Les objets seront conservés au prochain lancement, tout ou rien : une ligne "
                                      "refusée (cellule illisible, doublon d'une clé, règle) n'en laisse conserver aucun.");
}
