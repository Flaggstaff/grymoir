#include "rapport.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>

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
    doc.setHtml(html);
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
