// GrymoiR : l'atelier, éditeur de code.
#include "editeur.h"
#include "theme.h"
#include "coloration.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QToolTip>

extern "C" {
#include "analyseur.h"
}

Diagnostic_atelier analyser_source(const QString &source, bool compacte, const QString &chemin, bool *declarations) {
    Diagnostic_atelier r;
    const QByteArray octets = source.toUtf8();
    Portee *portee = portee_creer();
    const QByteArray c = chemin.toUtf8();
    if (!chemin.isEmpty()) portee_fichier(portee, c.constData());
    Programme programme = {};
    Diagnostic d = {};
    const int ok = compacte ? analyser_compact(octets.constData(), (size_t)octets.size(), portee, &programme, &d)
                            : analyser(octets.constData(), (size_t)octets.size(), portee, 0, &programme, &d);
    if (declarations) *declarations = ok && programme_declarations_seules(&programme);
    if (!ok) {
        r.message = QString::fromUtf8(d.message ? d.message : "Erreur.");
        r.ligne = d.ligne;
        r.colonne = d.colonne;
        if (d.fichier && d.origine_ligne > 0) {   // l'erreur est dans un fichier utilisé : sur sa phrase « Utiliser »
            r.message = QString("« %1 », ligne %2 : %3").arg(QString::fromUtf8(d.fichier)).arg(d.ligne).arg(r.message);
            r.ligne = d.origine_ligne;
            r.colonne = d.origine_colonne;
        }
    }
    diagnostic_liberer(&d);
    programme_liberer(&programme);
    portee_detruire(portee);
    return r;
}

namespace {
class Marge : public QWidget {
public:
    explicit Marge(Editeur *e) : QWidget(e), editeur(e) {}
    QSize sizeHint() const override { return QSize(editeur->largeur_marge(), 0); }

protected:
    void paintEvent(QPaintEvent *e) override { editeur->peindre_marge(e); }
    void mousePressEvent(QMouseEvent *e) override { editeur->clic_marge(qRound(e->position().y())); }

private:
    Editeur *editeur;
};
}  // namespace

Editeur::Editeur(QWidget *parent)
    : QPlainTextEdit(parent), marge(new Marge(this)), completion_(new QCompleter(this)), suites_(new QStringListModel(this)) {
    // Le cœur a déjà filtré les suites par le début de mot : la liste les montre telles quelles, dans son ordre.
    completion_->setModel(suites_);
    completion_->setWidget(this);
    completion_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    completion_->setMaxVisibleItems(10);
    connect(completion_, qOverload<const QString &>(&QCompleter::activated), this, &Editeur::completer);
    // La frappe se lit sur le document, pas sur les touches : sous macOS, un caractère peut arriver par la méthode
    // de saisie du système (QInputMethodEvent) sans passer par keyPressEvent.
    connect(document(), &QTextDocument::contentsChange, this, &Editeur::sur_changement);
    setFont(Theme::courant().police_code(14));
    setProperty("role", "editeur");
    connect(&Theme::courant(), &Theme::change, this, [this] {
        marquer_erreur();
        marge->update();
    });
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    attente.setSingleShot(true);
    attente.setInterval(300);   // on analyse quand la frappe s'arrête, pas à chaque touche
    connect(&attente, &QTimer::timeout, this, &Editeur::analyser);
    connect(this, &QPlainTextEdit::textChanged, &attente, qOverload<>(&QTimer::start));
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(largeur_marge(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
        if (dy) marge->scroll(0, dy);
        else marge->update(0, r.y(), marge->width(), r.height());
    });
    setViewportMargins(largeur_marge(), 0, 0, 0);
    // le bloc du curseur se surligne, et suit le curseur (24 octobre 2026)
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &Editeur::marquer_erreur);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        QTextBlock b = textCursor().block();
        while (!b.isVisible()) {   // le curseur dans un corps replié : les en-têtes qui le cachent se déplient
            QTextBlock h = b.previous();
            while (h.isValid() && !(replies_.contains(h.blockNumber()) && fin_du_corps(h).blockNumber() >= b.blockNumber())) h = h.previous();
            if (!h.isValid()) { b.setVisible(true); break; }
            basculer_repli(h);
        }
        const QPair<int, int> z = bloc_du_curseur();
        const int premiere = z.first >= 0 ? z.first + 1 : 0;
        if (premiere != dernier_bloc_) { dernier_bloc_ = premiere; emit bloc_change(premiere); }
    });
    // une ligne ajoutée ou retirée : le repli se défait, plutôt que de cacher un texte qui a bougé
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { if (!replies_.isEmpty()) deplier_tout(); });
}

bool Editeur::debut_de_bloc(const QTextBlock &b) {
    const QString t = b.text();
    return !t.isEmpty() && !t.at(0).isSpace();
}

static bool est_remarque(const QTextBlock &b) {
    const QString t = b.text();
    return t.startsWith("Remarque") || t.startsWith("#");
}

QPair<int, int> Editeur::bloc_du_curseur() const {
    QTextBlock b = textCursor().block();
    if (b.text().trimmed().isEmpty()) return {-1, -1};   // sur une ligne vide, entre deux blocs : rien
    while (b.isValid() && !debut_de_bloc(b)) b = b.previous();   // la tête de la déclaration
    if (!b.isValid()) return {-1, -1};
    QTextBlock fin = b;
    if (est_remarque(b)) {   // dans les remarques de tête : la déclaration qu'elles annoncent suit
        while (fin.next().isValid() && debut_de_bloc(fin.next()) && est_remarque(fin.next())) fin = fin.next();
        if (fin.next().isValid() && debut_de_bloc(fin.next())) fin = fin.next();
    }
    QTextBlock debut = b;   // les remarques collées au-dessus font partie du bloc
    while (debut.previous().isValid() && debut_de_bloc(debut.previous()) && est_remarque(debut.previous())) debut = debut.previous();
    // le corps : les lignes en retrait qui suivent, jusqu'à la prochaine ligne sans retrait (les lignes vides finales exclues)
    QTextBlock k = fin.next(), dernier = fin;
    while (k.isValid() && !debut_de_bloc(k)) {
        if (!k.text().trimmed().isEmpty()) dernier = k;
        k = k.next();
    }
    return {debut.blockNumber(), dernier.blockNumber()};
}

// Des repères pour lire le code (24 octobre 2026) : un filet au-dessus de chaque déclaration de premier niveau, et un
// fil vertical léger par niveau de retrait.
void Editeur::paintEvent(QPaintEvent *e) {
    QPlainTextEdit::paintEvent(e);
    QPainter p(viewport());
    QColor filet = palette().color(QPalette::PlaceholderText);
    filet.setAlpha(90);
    QColor fil = filet;
    fil.setAlpha(45);
    const int pas = fontMetrics().horizontalAdvance(QString(4, ' '));
    const qreal x0 = contentOffset().x() + document()->documentMargin();
    QTextBlock bloc = firstVisibleBlock();
    while (bloc.isValid()) {
        const QRectF r = blockBoundingGeometry(bloc).translated(contentOffset());
        if (r.top() > e->rect().bottom()) break;
        const QString t = bloc.text();
        if (debut_de_bloc(bloc) && bloc.blockNumber() > 0) {
            QTextBlock avant = bloc.previous();   // un filet entre deux blocs, pas entre une remarque et sa déclaration
            const bool suite = avant.isValid() && debut_de_bloc(avant) && est_remarque(avant);
            if (!suite) {
                p.setPen(QPen(filet, 1));
                p.drawLine(QPointF(x0, r.top() + 0.5), QPointF(viewport()->width(), r.top() + 0.5));
            }
        }
        if (bloc.isVisible() && replies_.contains(bloc.blockNumber())) {   // un en-tête replié : « ⋯ » au bout de la ligne
            const qreal x = x0 + fontMetrics().horizontalAdvance(t) + 8;
            const QRectF boite(x, r.top() + 2, fontMetrics().horizontalAdvance(QString::fromUtf8(" ⋯ ")), r.height() - 4);
            p.setPen(QPen(filet, 1));
            p.drawRoundedRect(boite, 3, 3);
            p.drawText(boite, Qt::AlignCenter, QString::fromUtf8("⋯"));
        }
        if (!bloc.isVisible()) { bloc = bloc.next(); continue; }
        int retrait = 0;
        while (retrait < t.size() && t.at(retrait) == ' ') retrait++;
        p.setPen(QPen(fil, 1));
        for (int n = 1; n * 4 <= retrait; n++) {
            const qreal x = x0 + (n - 1) * pas + 2;
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        bloc = bloc.next();
    }
}

void Editeur::charger(const QString &texte, bool c, const QString &ch) {
    replies_.clear();
    compacte = c;
    chemin = ch;
    completion_->popup()->hide();
    delete coloration;
    coloration = nullptr;
    chargement = true;
    setPlainText(texte);
    chargement = false;
    coloration = new Coloration(document(), compacte);
    document()->setModified(false);
    attente.stop();
    analyser();
}

int Editeur::largeur_marge() const {
    int chiffres = QString::number(qMax(1, blockCount())).size();
    return 16 + 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, chiffres);   // + le triangle du repli
}

static int retrait_de(const QString &t) {
    int r = 0;
    while (r < t.size() && t.at(r) == ' ') r++;
    return r;
}

bool Editeur::repliable(const QTextBlock &b) const {
    const QString t = b.text();
    if (!t.trimmed().endsWith(':')) return false;
    const int r = retrait_de(t);
    for (QTextBlock k = b.next(); k.isValid(); k = k.next())
        if (!k.text().trimmed().isEmpty()) return retrait_de(k.text()) > r;
    return false;
}

bool Editeur::replie(const QTextBlock &b) const { return replies_.contains(b.blockNumber()); }

QTextBlock Editeur::fin_du_corps(const QTextBlock &entete) const {
    const int r = retrait_de(entete.text());
    QTextBlock dernier = entete;
    for (QTextBlock k = entete.next(); k.isValid(); k = k.next()) {
        if (k.text().trimmed().isEmpty()) continue;
        if (retrait_de(k.text()) <= r) break;
        dernier = k;
    }
    return dernier;
}

void Editeur::appliquer_repli(const QTextBlock &entete, bool cacher) {
    const QTextBlock fin = fin_du_corps(entete);
    for (QTextBlock k = entete.next(); k.isValid() && k.blockNumber() <= fin.blockNumber(); k = k.next()) {
        bool visible = !cacher;
        if (!cacher) {   // en dépliant, un bloc intérieur resté replié garde son corps caché
            for (QTextBlock h = k.previous(); h.isValid() && h.blockNumber() > entete.blockNumber(); h = h.previous())
                if (replies_.contains(h.blockNumber()) && fin_du_corps(h).blockNumber() >= k.blockNumber()) { visible = false; break; }
        }
        k.setVisible(visible);
    }
    document()->markContentsDirty(entete.position(), fin.position() + fin.length() - entete.position());
    viewport()->update();
    marge->update();
}

void Editeur::basculer_repli(const QTextBlock &b) {
    if (!repliable(b)) return;
    const bool cacher = !replies_.contains(b.blockNumber());
    if (cacher) replies_.insert(b.blockNumber());
    else replies_.remove(b.blockNumber());
    appliquer_repli(b, cacher);
}

void Editeur::deplier_tout() {
    for (QTextBlock k = document()->begin(); k.isValid(); k = k.next()) k.setVisible(true);
    replies_.clear();
    document()->markContentsDirty(0, document()->characterCount());
    viewport()->update();
    marge->update();
}

void Editeur::clic_marge(int y) {
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next()) {
        if (!b.isVisible()) continue;
        const QRectF r = blockBoundingGeometry(b).translated(contentOffset());
        if (r.top() > y) break;
        if (y < r.bottom()) { basculer_repli(b); return; }
    }
}

QList<QPair<QString, int>> Editeur::plan() const {
    QList<QPair<QString, int>> r;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const QString t = b.text();
        if (!debut_de_bloc(b) || t.startsWith("Remarque") || t.startsWith("#") || !t.trimmed().endsWith(':')) continue;
        QString l = t.trimmed();
        l.chop(1);
        l = l.trimmed();
        if (l.size() > 70) l = l.left(68) + QString::fromUtf8("…");
        r.append({l, b.blockNumber() + 1});
    }
    return r;
}

void Editeur::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    marge->setGeometry(QRect(cr.left(), cr.top(), largeur_marge(), cr.height()));
}

void Editeur::peindre_marge(QPaintEvent *e) {
    QPainter p(marge);
    p.fillRect(e->rect(), palette().alternateBase());
    QTextBlock bloc = firstVisibleBlock();
    int numero = bloc.blockNumber() + 1;
    int haut = qRound(blockBoundingGeometry(bloc).translated(contentOffset()).top());
    while (bloc.isValid() && haut <= e->rect().bottom()) {
        const int bas = haut + qRound(blockBoundingRect(bloc).height());
        if (bloc.isVisible() && bas >= e->rect().top()) {
            const bool fautive = !diag.message.isEmpty() && diag.ligne == numero;
            p.setPen(fautive ? Theme::courant().couleur("danger") : palette().color(QPalette::PlaceholderText));
            p.drawText(0, haut, marge->width() - 8 - 16, fontMetrics().height(), Qt::AlignRight, QString::number(numero));
            if (repliable(bloc)) {   // le triangle : ▸ replié, ▾ dépliable
                p.setPen(palette().color(QPalette::PlaceholderText));
                p.drawText(marge->width() - 18, haut, 14, fontMetrics().height(), Qt::AlignCenter,
                           replie(bloc) ? QString::fromUtf8("▸") : QString::fromUtf8("▾"));
            }
        }
        bloc = bloc.next();
        haut = bas;
        numero++;
    }
}

void Editeur::analyser() {
    diag = analyser_source(toPlainText(), compacte, chemin, &que_des_declarations);
    marquer_erreur();
    marge->update();
    emit diagnostic_change();
}

// Position UTF-16 dans le bloc d'un numéro de colonne en points de code (à partir de 1).
static int position_bloc(const QTextBlock &bloc, int colonne) {
    const QString t = bloc.text();
    int i = 0;
    for (int c = 1; c < colonne && i < t.size(); c++) i += t.at(i).isHighSurrogate() ? 2 : 1;
    return qMin(i, (int)t.size());
}

void Editeur::marquer_erreur() {
    QList<QTextEdit::ExtraSelection> marques;
    const QPair<int, int> zone = bloc_du_curseur();   // le bloc du curseur, sur un fond jaune léger
    if (zone.first >= 0) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(QColor(255, 210, 0, 40));
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        QTextCursor c(document()->findBlockByNumber(zone.first));
        const QTextBlock dernier = document()->findBlockByNumber(zone.second);
        // jusqu'au début de la ligne suivante : la dernière ligne du bloc se surligne aussi sur toute la largeur
        if (dernier.next().isValid()) c.setPosition(dernier.next().position(), QTextCursor::KeepAnchor);
        else { c.setPosition(dernier.position(), QTextCursor::KeepAnchor); c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); }
        s.cursor = c;
        marques.append(s);
    }
    if (!diag.message.isEmpty() && diag.ligne > 0) {
        const QTextBlock bloc = document()->findBlockByNumber(diag.ligne - 1);
        if (bloc.isValid()) {
            QTextEdit::ExtraSelection s;
            s.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            s.format.setUnderlineColor(Theme::courant().couleur("danger"));
            s.format.setBackground(Theme::courant().couleur("danger-doux"));
            QTextCursor c(bloc);
            int debut = position_bloc(bloc, diag.colonne);
            const QString t = bloc.text();
            int fin = debut;
            while (fin < t.size() && !t.at(fin).isSpace()) fin++;
            if (fin == debut) {   // en fin de ligne : on marque le dernier caractère
                debut = qMax(0, debut - 1);
                fin = qMax(debut + 1, (int)t.size());
            }
            c.setPosition(bloc.position() + debut);
            c.setPosition(bloc.position() + qMin(fin, (int)t.size()), QTextCursor::KeepAnchor);
            s.cursor = c;
            marques.append(s);
        }
    }
    setExtraSelections(marques);
}

void Editeur::aller_a(int ligne, int colonne) {
    const QTextBlock bloc = document()->findBlockByNumber(qMax(0, ligne - 1));
    if (!bloc.isValid()) return;
    QTextCursor c(bloc);
    c.setPosition(bloc.position() + position_bloc(bloc, colonne));
    setTextCursor(c);
    centerCursor();
    setFocus();
}

bool Editeur::event(QEvent *e) {
    // Le survol de la ligne fautive montre le message, comme dans l'extension VS Code.
    if (e->type() == QEvent::ToolTip && !diag.message.isEmpty() && diag.ligne > 0) {
        auto *h = static_cast<QHelpEvent *>(e);
        const QTextCursor c = cursorForPosition(viewport()->mapFrom(this, h->pos()));
        if (c.blockNumber() + 1 == diag.ligne) {
            QToolTip::showText(h->globalPos(), diag.message, this);
            return true;
        }
        QToolTip::hideText();
    }
    return QPlainTextEdit::event(e);
}

// ------------------------------------------------------------------------------------------------
// Aide à la saisie (grammaire, § 8)
// ------------------------------------------------------------------------------------------------

QString Editeur::debut_de_mot() const {
    const QTextCursor c = textCursor();
    const QString avant = c.block().text().left(c.positionInBlock());
    const int crochet = avant.lastIndexOf('[');
    if (crochet >= 0 && avant.indexOf(']', crochet) < 0) return avant.mid(crochet);   // [frais et po…
    int i = avant.size();
    while (i > 0 && (avant.at(i - 1).isLetterOrNumber() || avant.at(i - 1) == '_' || avant.at(i - 1).isMark())) i--;
    return avant.mid(i);
}

QStringList Editeur::suites_au_curseur() const {
    if (compacte) return {};
    const QByteArray avant = toPlainText().left(textCursor().position()).toUtf8();
    const QByteArray c = chemin.toUtf8();
    Suggestions g = suites_valides_fichier(avant.constData(), (size_t)avant.size(), chemin.isEmpty() ? nullptr : c.constData());
    QStringList r;
    for (size_t k = 0; k < g.nb; k++) {
        const QString x = QString::fromUtf8(g.items[k]);
        if (x.size() > 1 && x.startsWith('(')) continue;   // « (nombre) » : une catégorie, pas un mot
        if (x.contains(QChar(0x2026))) continue;           // « « … » » : un gabarit
        if (!r.contains(x)) r << x;
    }
    suggestions_liberer(&g);
    return r;
}

void Editeur::completer(const QString &suite) {
    QTextCursor c = textCursor();
    const QString debut = debut_de_mot();
    if (!debut.isEmpty() && suite.startsWith(debut, Qt::CaseInsensitive))
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, (int)debut.size());
    c.insertText(suite);
    setTextCursor(c);
    completion_->popup()->hide();
}

void Editeur::proposer(bool demandee) {
    if (compacte || (!demandee && debut_de_mot().size() < 2)) {
        completion_->popup()->hide();
        return;
    }
    const QStringList s = suites_au_curseur();
    if (s.isEmpty() || (s.size() == 1 && s.first() == debut_de_mot())) {   // rien à proposer, ou déjà écrit
        completion_->popup()->hide();
        return;
    }
    suites_->setStringList(s);
    QRect r = cursorRect();
    r.setWidth(completion_->popup()->sizeHintForColumn(0) + completion_->popup()->verticalScrollBar()->sizeHint().width() + 24);
    completion_->complete(r);
    completion_->popup()->setCurrentIndex(suites_->index(0, 0));
}

void Editeur::keyPressEvent(QKeyEvent *e) {
    if (completion_->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab: {
            const QModelIndex i = completion_->popup()->currentIndex();
            if (i.isValid()) {
                completer(i.data().toString());
                return;
            }
            completion_->popup()->hide();
            break;
        }
        case Qt::Key_Escape:
            completion_->popup()->hide();
            return;
        default:
            break;
        }
    }
    // Ctrl+Espace ouvre la liste à tout moment. Sous macOS, la touche Contrôle (Qt::MetaModifier) :
    // Cmd+Espace appartient à Spotlight.
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifier commande = Qt::MetaModifier;
#else
    const Qt::KeyboardModifier commande = Qt::ControlModifier;
#endif
    if (e->key() == Qt::Key_Space && (e->modifiers() & commande)) {
        proposer(true);
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

// Un caractère tapé (ou effacé) : la suite se décide une fois le curseur à sa place, au prochain tour de boucle.
// On lit la différence nette et le caractère avant le curseur : une frappe par la méthode de saisie du système
// se signale comme le remplacement d'un passage entier, avec un caractère de plus.
void Editeur::sur_changement(int /*position*/, int retires, int ajoutes) {
    // Autant retiré qu'ajouté : une recoloration (QSyntaxHighlighter), pas une frappe.
    if (chargement || retouche || retires == ajoutes) return;
    if (ajoutes - retires == 1) {
        QTimer::singleShot(0, this, [this] {
            const int p = textCursor().position() - 1;
            const QChar ch = p >= 0 ? document()->characterAt(p) : QChar();
            if (ch == '"') guillemet(p);
            else if (ch.isLetterOrNumber() || ch == '_' || ch == '[') proposer(false);
            else completion_->popup()->hide();
        });
    } else if (ajoutes - retires == -1 && completion_->popup()->isVisible()) {
        QTimer::singleShot(0, this, [this] { proposer(false); });
    } else {
        completion_->popup()->hide();
    }
}

void Editeur::guillemet(int position) {
    if (document()->characterAt(position) != '"' || textCursor().position() != position + 1 || textCursor().hasSelection()) return;
    const QTextBlock bloc = document()->findBlock(position);
    const QString ligne = bloc.text();
    const int k = position - bloc.position();
    const QString avant = ligne.left(k), apres = ligne.mid(k + 1);
    QTextCursor c(document());
    c.setPosition(position);
    c.setPosition(position + 1, QTextCursor::KeepAnchor);
    retouche = true;
    if (apres.startsWith(QString::fromUtf8(" »")) || apres.startsWith(QString::fromUtf8("»"))) {   // fermer : sauter « » »
        c.removeSelectedText();
        c.setPosition(position + (apres.startsWith(' ') ? 2 : 1));
        setTextCursor(c);
    } else if (avant.count(QString::fromUtf8("«")) > avant.count(QString::fromUtf8("»")) || avant.count('"') % 2 == 1) {
        // dans un texte déjà ouvert : un « " » ordinaire, ou la fin d'un texte entre « " »
    } else {
        c.insertText(QString::fromUtf8("«  »"));
        c.setPosition(position + 2);
        setTextCursor(c);
    }
    retouche = false;
}

// --- Le plan (24 octobre 2026) ---

PlanDuCode::PlanDuCode(Editeur *e, QWidget *parent) : QWidget(parent), editeur_(e) {
    auto *pile = new QVBoxLayout(this);
    pile->setContentsMargins(0, 0, 0, 0);
    pile->setSpacing(4);
    filtre_ = new QLineEdit;
    filtre_->setPlaceholderText(QString::fromUtf8("Chercher dans le plan"));
    filtre_->setClearButtonEnabled(true);
    liste_ = new QListWidget;
    liste_->setObjectName("plan");
    liste_->setAccessibleName(QString::fromUtf8("Plan du fichier"));
    liste_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // une ligne longue s'abrège ; la bulle la donne entière
    liste_->setTextElideMode(Qt::ElideRight);
    pile->addWidget(filtre_);
    pile->addWidget(liste_, 1);
    connect(filtre_, &QLineEdit::textChanged, this, &PlanDuCode::remplir);
    connect(liste_, &QListWidget::itemClicked, this, [this](QListWidgetItem *i) {
        editeur_->aller_a(i->data(Qt::UserRole).toInt(), 1);
    });
    connect(editeur_, &Editeur::diagnostic_change, this, &PlanDuCode::rafraichir);
    connect(editeur_, &Editeur::bloc_change, this, &PlanDuCode::suivre);
    rafraichir();
}

void PlanDuCode::rafraichir() {
    entrees_ = editeur_->plan();
    remplir();
}

void PlanDuCode::remplir() {
    // le filtre ne tient compte ni des majuscules ni des accents, comme la recherche des zones (2.40)
    auto plat = [](const QString &t) {
        QString r;
        for (const QChar c : t.normalized(QString::NormalizationForm_D))
            if (c.category() != QChar::Mark_NonSpacing) r += c.toLower();
        return r;
    };
    const QString f = plat(filtre_->text().trimmed());
    liste_->clear();
    for (const auto &[texte, ligne] : entrees_) {
        if (!f.isEmpty() && !plat(texte).contains(f)) continue;
        auto *i = new QListWidgetItem(texte);
        i->setData(Qt::UserRole, ligne);
        i->setToolTip(QString::fromUtf8("%1\nLigne %2").arg(texte).arg(ligne));
        liste_->addItem(i);
    }
}

void PlanDuCode::suivre(int ligne) {   // le plan montre la déclaration où est le curseur
    for (int k = 0; k < liste_->count(); k++) {
        const int l = liste_->item(k)->data(Qt::UserRole).toInt();
        if (l == ligne || (k + 1 < liste_->count() && l < ligne && liste_->item(k + 1)->data(Qt::UserRole).toInt() > ligne)) {
            liste_->setCurrentRow(k);
            liste_->scrollToItem(liste_->item(k));
            return;
        }
    }
}
