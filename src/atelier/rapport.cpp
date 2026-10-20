#include "rapport.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>
#include <QPainterPath>
#include <QTextObjectInterface>
#include <QTextFormat>
#include <QUrl>
#include <QImage>
#include <QRegularExpression>

// Les images d'un document, dessinées telles quelles : le dessin par défaut passe par un QPixmap en couleurs, que le
// PDF range en JPEG ; un code QR y perdrait la netteté de ses bords. Une image d'un bit (Format_Mono) reste un bitmap
// sans perte dans le PDF (2.27).
class ImagesNettes : public QObject, public QTextObjectInterface {
    Q_OBJECT   // registerHandler retrouve l'interface par qobject_cast : il faut le moc
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit ImagesNettes(QTextDocument *d) : QObject(d), doc(d) {}
    QSizeF intrinsicSize(QTextDocument *, int, const QTextFormat &f) override {
        const QTextImageFormat im = f.toImageFormat();
        const QImage i = image(im);
        QSizeF t(im.width(), im.height());
        if (t.width() <= 0 && t.height() <= 0) t = i.size();
        else if (t.width() <= 0) t.setWidth(i.height() ? t.height() * i.width() / i.height() : 0);
        else if (t.height() <= 0) t.setHeight(i.width() ? t.width() * i.height() / i.width() : 0);
        // les tailles HTML sont en pixels à 96 par pouce ; la mise en page est à la résolution du PDF
        const qreal echelle = doc->documentLayout()->paintDevice() ? doc->documentLayout()->paintDevice()->logicalDpiX() / 96.0 : 1.0;
        return t * echelle;
    }
    void drawObject(QPainter *p, const QRectF &r, QTextDocument *, int, const QTextFormat &f) override {
        const QImage i = image(f.toImageFormat());
        if (i.depth() != 1 || i.isNull()) { p->drawImage(r, i); return; }
        // Une image d'un bit (un code QR) devient un dessin vectoriel : le PDF de Qt rangerait le bitmap en JPEG, au
        // bord flou ; un tracé reste net à toute taille, comme SIX le demande pour le code des QR-factures (IG 2.4,
        // § 6.4). Les segments noirs de chaque ligne, les lignes identiques fusionnées, un seul chemin rempli.
        const qreal sx = r.width() / i.width(), sy = r.height() / i.height();
        QPainterPath chemin;
        chemin.setFillRule(Qt::WindingFill);
        auto noir = [&](int x, int y) { return qGray(i.pixel(x, y)) < 128; };
        auto meme_ligne = [&](int a, int b) {
            for (int x = 0; x < i.width(); x++) if (noir(x, a) != noir(x, b)) return false;
            return true;
        };
        for (int y = 0; y < i.height(); ) {
            int h = 1;
            while (y + h < i.height() && meme_ligne(y, y + h)) h++;
            for (int x = 0; x < i.width(); ) {
                if (!noir(x, y)) { x++; continue; }
                int x1 = x;
                while (x1 < i.width() && noir(x1, y)) x1++;
                chemin.addRect(QRectF(r.x() + x * sx, r.y() + y * sy, (x1 - x) * sx, h * sy));
                x = x1;
            }
            y += h;
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        p->fillRect(r, Qt::white);
        p->fillPath(chemin, Qt::black);
        p->restore();
    }
private:
    QImage image(const QTextImageFormat &f) const {
        return doc->resource(QTextDocument::ImageResource, QUrl(f.name())).value<QImage>();
    }
    QTextDocument *doc;
};

QByteArray pdf_depuis_html(const QString &html, QString *erreur) {
    QByteArray octets;
    QBuffer tampon(&octets);
    tampon.open(QIODevice::WriteOnly);
    QPdfWriter pdf(&tampon);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(15, 18, 15, 18), QPageLayout::Millimeter);   // comme la page HTML (@page)
    pdf.setResolution(300);
    pdf.setCreator("GrymoiR");
    QTextDocument doc;
    doc.documentLayout()->setPaintDevice(&pdf);   // les tailles en points, à la résolution du PDF
    // Les images de la page (data:) passent par des ressources QImage : une image en noir et blanc (un code QR) y
    // reste d'un bit, et le PDF la garde nette, sans compression JPEG qui flouterait ses bords (2.27)
    QString page_html = html;
    static const QRegularExpression donnee("src=\"data:image/[a-z]+;base64,([A-Za-z0-9+/=]+)\"");
    int nb_images = 0;
    for (QRegularExpressionMatch m = donnee.match(page_html); m.hasMatch(); m = donnee.match(page_html, m.capturedStart() + 1)) {
        QImage im = QImage::fromData(QByteArray::fromBase64(m.captured(1).toLatin1()));
        if (im.isNull()) continue;
        if (im.depth() == 1 || (im.isGrayscale() && im.colorCount() == 2)) im = im.convertToFormat(QImage::Format_Mono);
        const QString nom = QString("grym-image-%1").arg(++nb_images);
        doc.addResource(QTextDocument::ImageResource, QUrl(nom), im);
        page_html.replace(m.capturedStart(), m.capturedLength(), QString("src=\"%1\"").arg(nom));
    }
    doc.setHtml(page_html);
    doc.documentLayout()->registerHandler(QTextFormat::ImageObject, new ImagesNettes(&doc));   // après setHtml, qui le remet
    const QRect zone = pdf.pageLayout().paintRectPixels(pdf.resolution());
    const int pied = pdf.resolution() * 8 / 25;   // environ 8 mm pour le numéro de page
    const QSizeF page(zone.width(), zone.height() - pied);
    doc.setPageSize(page);   // la mise en page découpe le tableau et répète son en-tête (<thead>) à chaque page
    const int pages = doc.pageCount();
    QPainter p;
    if (!p.begin(&pdf)) {
        *erreur = "Le PDF ne peut pas être commencé.";
        return QByteArray();
    }
    QFont petite = p.font();
    petite.setPointSizeF(8);
    for (int i = 0; i < pages; i++) {
        if (i) pdf.newPage();
        p.save();
        p.translate(0, -i * page.height());
        doc.drawContents(&p, QRectF(0, i * page.height(), page.width(), page.height()));
        p.restore();
        p.setFont(petite);
        p.setPen(QColor("#555555"));
        p.drawText(QRectF(0, page.height(), page.width(), pied), Qt::AlignRight | Qt::AlignBottom,
                   QString("page %1 / %2").arg(i + 1).arg(pages));
    }
    p.end();
    if (octets.isEmpty()) *erreur = "Le PDF est vide.";
    return octets;
}

#include "rapport.moc"
