#include "relecture.h"
#include "transcription.h"

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {

class Surligneur : public QSyntaxHighlighter {
public:
    explicit Surligneur(QTextDocument *d) : QSyntaxHighlighter(d) {}

protected:
    void highlightBlock(const QString &t) override {
        if (!document()->characterAt(0).unicode() || document()->characterAt(0) != QChar(MARQUE_TRANSCRIT)) return;
        // tous les nombres d'un texte transcrit : un montant faux passe inaperçu à la lecture
        static const QRegularExpression nombre(R"(\d(?:[\d'’ .,]*\d)?)");
        QTextCharFormat fn;
        fn.setBackground(QColor("#ffe0b2"));
        for (auto m = nombre.globalMatch(t); m.hasNext();) {
            const auto x = m.next();
            setFormat((int)x.capturedStart(), (int)x.capturedLength(), fn);
        }
        // les mots douteux, soulignés par-dessus
        QTextCharFormat fd;
        fd.setBackground(QColor("#fff59d"));
        fd.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        fd.setUnderlineColor(QColor("#c62828"));
        for (qsizetype a = t.indexOf(QChar(DEBUT_DOUTE)); a >= 0; a = t.indexOf(QChar(DEBUT_DOUTE), a + 1)) {
            const qsizetype b = t.indexOf(QChar(FIN_DOUTE), a);
            if (b < 0) break;
            for (int k = (int)a; k <= (int)b; k++) {   // garder le fond orange d'un nombre douteux, ajouter le soulignement
                QTextCharFormat f = format(k);
                if (!f.background().color().isValid() || f.background().style() == Qt::NoBrush) f.setBackground(fd.background());
                f.setUnderlineStyle(fd.underlineStyle());
                f.setUnderlineColor(fd.underlineColor());
                setFormat(k, 1, f);
            }
        }
    }
};

}   // espace sans nom

void installer_relecture(QPlainTextEdit *zone) {
    new Surligneur(zone->document());
    // corriger un mot douteux le relit : ses deux marques s'en vont, et son surlignage avec
    QObject::connect(zone->document(), &QTextDocument::contentsChange, zone, [zone](int pos, int retire, int ajoute) {
        static bool en_cours = false;   // nos propres retraits relancent ce signal : on ne s'y repasse pas
        if (en_cours || (retire == 0 && ajoute == 0)) return;
        QTextDocument *d = zone->document();
        const QString t = d->toPlainText();
        const qsizetype a = t.lastIndexOf(QChar(DEBUT_DOUTE), qMax(0, pos));
        if (a < 0) return;
        const qsizetype b = t.indexOf(QChar(FIN_DOUTE), a);
        if (b < 0 || pos > b || (pos <= a && pos + ajoute <= a)) return;   // la modification n'est pas dans ce mot
        QTextCursor c(d);
        en_cours = true;
        c.setPosition((int)b);
        c.deleteChar();
        c.setPosition((int)a);
        c.deleteChar();
        en_cours = false;
    });
}
