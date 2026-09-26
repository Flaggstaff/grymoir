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
}

void Editeur::charger(const QString &texte, bool c, const QString &ch) {
    compacte = c;
    chemin = ch;
    completion_->popup()->hide();
    delete coloration;
    coloration = nullptr;
    setPlainText(texte);
    coloration = new Coloration(document(), compacte);
    document()->setModified(false);
    attente.stop();
    analyser();
}

int Editeur::largeur_marge() const {
    int chiffres = QString::number(qMax(1, blockCount())).size();
    return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, chiffres);
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
            p.drawText(0, haut, marge->width() - 8, fontMetrics().height(), Qt::AlignRight, QString::number(numero));
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
    const QString t = e->text();
    const bool lettre = !t.isEmpty() && (t.at(0).isLetterOrNumber() || t.at(0) == '_' || t.at(0) == '[');
    if (lettre || (e->key() == Qt::Key_Backspace && completion_->popup()->isVisible())) proposer(false);
    else completion_->popup()->hide();
}
