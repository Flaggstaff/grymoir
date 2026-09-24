// GrymoiR : l'atelier, éditeur de code.
#include "editeur.h"
#include "coloration.h"

#include <QFontDatabase>
#include <QHelpEvent>
#include <QPainter>
#include <QTextBlock>
#include <QToolTip>

extern "C" {
#include "analyseur.h"
}

Diagnostic_atelier analyser_source(const QString &source, bool compacte) {
    Diagnostic_atelier r;
    const QByteArray octets = source.toUtf8();
    Portee *portee = portee_creer();
    Programme programme = {};
    Diagnostic d = {};
    const int ok = compacte ? analyser_compact(octets.constData(), (size_t)octets.size(), portee, &programme, &d)
                            : analyser(octets.constData(), (size_t)octets.size(), portee, 0, &programme, &d);
    if (!ok) {
        r.message = QString::fromUtf8(d.message ? d.message : "Erreur.");
        r.ligne = d.ligne;
        r.colonne = d.colonne;
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

Editeur::Editeur(QWidget *parent) : QPlainTextEdit(parent), marge(new Marge(this)) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(qMax(f.pointSize(), 13));
    setFont(f);
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

void Editeur::charger(const QString &texte, bool c) {
    compacte = c;
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
            p.setPen(fautive ? QColor(Qt::red) : palette().color(QPalette::PlaceholderText));
            p.drawText(0, haut, marge->width() - 8, fontMetrics().height(), Qt::AlignRight, QString::number(numero));
        }
        bloc = bloc.next();
        haut = bas;
        numero++;
    }
}

void Editeur::analyser() {
    diag = analyser_source(toPlainText(), compacte);
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
            s.format.setUnderlineColor(Qt::red);
            s.format.setBackground(QColor(255, 0, 0, 28));
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
