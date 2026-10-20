#include "rapport.h"
#include <cstring>
#include <QFontMetricsF>
#include "../../vendor/qrcodegen/qrcodegen.h"

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

// ---------------------------------------------------------------------------------------------------------------
// La section paiement d'une QR-facture (2.28), dessinée en bas de la dernière page d'un document, d'après les lignes de
// son Swiss QR Code et les règles de mise en page des Swiss Implementation Guidelines for the QR-bill de SIX
// (version 2.4 du 24.02.2026, chapitres 3.4 à 3.7, intitulés français de l'annexe C). Les cotes fines viennent de ces
// chapitres, pas du Style Guide de SIX : à vérifier sur papier avant d'en dépendre au guichet.
// ---------------------------------------------------------------------------------------------------------------

static QString par_blocs(const QString &t, int premier, int taille) {   // « CH52 0483 5012 … », « 21 00000 00003 … »
    QString r;
    int k = 0;
    if (premier > 0) { r = t.left(premier); k = premier; }
    for (; k < t.size(); k += taille) { if (!r.isEmpty()) r += ' '; r += t.mid(k, taille); }
    return r;
}

static QString montant_suisse(const QString &m) {   // « 1590.00 » → « 1 590.00 » (§ 3.5.3)
    const int point = m.indexOf('.');
    QString entier = point < 0 ? m : m.left(point);
    const QString reste = point < 0 ? QString() : m.mid(point);
    for (int k = entier.size() - 3; k > 0; k -= 3) entier.insert(k, ' ');
    return entier + reste;
}

static void dessiner_paiement(QPainter &p, double ppm, double y0, const QString &donnees) {
    const QStringList l = donnees.split('\n');
    auto ligne = [&](int k) { return k < l.size() ? l[k].trimmed() : QString(); };
    if (ligne(0) != "SPC" || l.size() < 31) return;
    auto mm = [&](double x, double y) { return QPointF(x * ppm, y0 + y * ppm); };
    auto police = [&](double pt, bool gras) {
        QFont f("Helvetica");
        f.setStyleHint(QFont::SansSerif);
        f.setFamilies({"Helvetica", "Arial", "Liberation Sans", "Frutiger"});
        f.setPointSizeF(pt);
        f.setBold(gras);
        return f;
    };
    p.save();
    p.setPen(Qt::black);
    // les lignes de découpe et leur consigne (§ 3.7)
    QPen tirets(Qt::black, 0.2 * ppm, Qt::DashLine);
    p.setPen(tirets);
    p.drawLine(mm(0, 0), mm(210, 0));
    p.drawLine(mm(62, 0), mm(62, 105));
    p.setPen(Qt::black);
    p.setFont(police(7, false));
    p.drawText(QRectF(mm(0, -4), mm(210, -0.5)), Qt::AlignHCenter | Qt::AlignBottom, QString::fromUtf8("A détacher avant le versement"));
    p.save();
    p.translate(mm(62, 52.5));
    p.rotate(-90);
    p.drawText(QRectF(-40 * ppm, -4 * ppm, 80 * ppm, 3.5 * ppm), Qt::AlignCenter, QString::fromUtf8("A détacher avant le versement"));
    p.restore();

    const QString iban = par_blocs(ligne(3), 0, 4);
    QStringList creancier = {ligne(5)};
    const QString rue_c = (ligne(6) + " " + ligne(7)).trimmed(), ville_c = (ligne(8) + " " + ligne(9)).trimmed();
    if (!rue_c.isEmpty()) creancier << rue_c;
    if (!ville_c.isEmpty()) creancier << (ligne(10) != "CH" && !ligne(10).isEmpty() ? ligne(10) + "-" + ville_c : ville_c);
    const QString montant = ligne(18).isEmpty() ? QString() : montant_suisse(ligne(18)), monnaie = ligne(19);
    QStringList debiteur;
    if (!ligne(21).isEmpty()) {
        debiteur << ligne(21);
        const QString rue_d = (ligne(22) + " " + ligne(23)).trimmed(), ville_d = (ligne(24) + " " + ligne(25)).trimmed();
        if (!rue_d.isEmpty()) debiteur << rue_d;
        if (!ville_d.isEmpty()) debiteur << (ligne(26) != "CH" && !ligne(26).isEmpty() ? ligne(26) + "-" + ville_d : ville_d);
    }
    QString reference;
    if (ligne(27) == "QRR") reference = par_blocs(ligne(28), 2, 5);
    else if (ligne(27) == "SCOR") reference = par_blocs(ligne(28), 0, 4);
    const QString message = ligne(29);

    // un bloc « intitulé, puis ses valeurs » ; rend le bas du bloc
    auto bloc = [&](double x, double y, double largeur, double pt_titre, double pt_valeur, const QString &titre,
                    const QStringList &valeurs) {
        p.setFont(police(pt_titre, true));
        const double h_titre = QFontMetricsF(p.font()).height() / ppm;
        p.drawText(QRectF(mm(x, y), QSizeF(largeur * ppm, h_titre * ppm)), Qt::AlignLeft | Qt::AlignTop, titre);
        y += h_titre;
        p.setFont(police(pt_valeur, false));
        const double h = QFontMetricsF(p.font()).height() / ppm;
        for (const QString &v : valeurs) {
            p.drawText(QRectF(mm(x, y), QSizeF(largeur * ppm, h * ppm)), Qt::AlignLeft | Qt::AlignTop,
                       QFontMetricsF(p.font()).elidedText(v, Qt::ElideRight, largeur * ppm));
            y += h;
        }
        return y + h * 0.6;
    };
    auto cadre = [&](double x, double y, double l, double h) {   // un champ à remplir à la main, coins seuls (§ 3.5.4)
        QPen trait(Qt::black, 0.75 / 72.0 * 25.4 * ppm);
        p.setPen(trait);
        const double c = 3;
        const double X[2] = {x, x + l}, Y[2] = {y, y + h};
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                p.drawLine(mm(X[i], Y[j]), mm(X[i] + (i ? -c : c), Y[j]));
                p.drawLine(mm(X[i], Y[j]), mm(X[i], Y[j] + (j ? -c : c)));
            }
        p.setPen(Qt::black);
    };

    // le récépissé (§ 3.6) : 62 × 105 mm
    p.setFont(police(11, true));
    p.drawText(QRectF(mm(5, 5), QSizeF(52 * ppm, 7 * ppm)), Qt::AlignLeft | Qt::AlignTop, QString::fromUtf8("Récépissé"));
    double y = 12;
    y = bloc(5, y, 52, 6, 8, QString::fromUtf8("Compte / Payable à"), QStringList{iban} + creancier);
    if (!reference.isEmpty()) y = bloc(5, y, 52, 6, 8, QString::fromUtf8("Référence"), {reference});
    if (!debiteur.isEmpty()) y = bloc(5, y, 52, 6, 8, QString::fromUtf8("Payable par"), debiteur);
    else { y = bloc(5, y, 52, 6, 8, QString::fromUtf8("Payable par (nom/adresse)"), {}); cadre(5, y - 1.5, 52, 20); }
    bloc(5, 68, 15, 6, 8, QString::fromUtf8("Monnaie"), {monnaie});
    if (!montant.isEmpty()) bloc(20, 68, 37, 6, 8, QString::fromUtf8("Montant"), {montant});
    else { bloc(20, 68, 37, 6, 8, QString::fromUtf8("Montant"), {}); cadre(27, 71, 30, 10); }
    p.setFont(police(6, true));
    p.drawText(QRectF(mm(5, 82), QSizeF(52 * ppm, 4 * ppm)), Qt::AlignRight | Qt::AlignTop, QString::fromUtf8("Point de dépôt"));

    // la section paiement (§ 3.5) : 148 × 105 mm, le code QR de 46 mm avec sa croix (§ 6.4)
    const double x0 = 62;
    p.setFont(police(11, true));
    p.drawText(QRectF(mm(x0 + 5, 5), QSizeF(51 * ppm, 7 * ppm)), Qt::AlignLeft | Qt::AlignTop, QString::fromUtf8("Section paiement"));
    {
        const QByteArray octets = donnees.toUtf8();
        uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
        const size_t n = (size_t)octets.size() < sizeof tmp ? (size_t)octets.size() : 0;
        memcpy(tmp, octets.constData(), n);
        if (n && qrcodegen_encodeBinary(tmp, n, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 25, qrcodegen_Mask_AUTO, false)) {
            const int m = qrcodegen_getSize(qr);
            const double cote = 46, module = cote / m, qx = x0 + 5, qy = 17;
            QPainterPath chemin;
            for (int yy = 0; yy < m; yy++)
                for (int xx = 0; xx < m; ) {
                    if (!qrcodegen_getModule(qr, xx, yy)) { xx++; continue; }
                    int x1 = xx;
                    while (x1 < m && qrcodegen_getModule(qr, x1, yy)) x1++;
                    chemin.addRect(QRectF(mm(qx + xx * module, qy + yy * module), QSizeF((x1 - xx) * module * ppm, module * ppm)));
                    xx = x1;
                }
            chemin.setFillRule(Qt::WindingFill);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillPath(chemin, Qt::black);
            // la croix (§ 6.4.2) : 7 mm, un carré blanc, un carré noir, la croix blanche (proportions du drapeau)
            const double cx = qx + cote / 2, cy = qy + cote / 2, t = 7, noir = t * 0.86;
            const double bras = noir * 20.0 / 32.0, larg = noir * 6.0 / 32.0;
            p.fillRect(QRectF(mm(cx - t / 2, cy - t / 2), QSizeF(t * ppm, t * ppm)), Qt::white);
            p.fillRect(QRectF(mm(cx - noir / 2, cy - noir / 2), QSizeF(noir * ppm, noir * ppm)), Qt::black);
            p.fillRect(QRectF(mm(cx - larg / 2, cy - bras / 2), QSizeF(larg * ppm, bras * ppm)), Qt::white);
            p.fillRect(QRectF(mm(cx - bras / 2, cy - larg / 2), QSizeF(bras * ppm, larg * ppm)), Qt::white);
        }
    }
    bloc(x0 + 5, 68, 15, 8, 10, QString::fromUtf8("Monnaie"), {monnaie});
    if (!montant.isEmpty()) bloc(x0 + 20, 68, 31, 8, 10, QString::fromUtf8("Montant"), {montant});
    else { bloc(x0 + 20, 68, 31, 8, 10, QString::fromUtf8("Montant"), {}); cadre(x0 + 11, 78, 40, 15); }
    y = 5;
    const double xi = x0 + 56, li = 148 - 56 - 5;
    y = bloc(xi, y, li, 8, 10, QString::fromUtf8("Compte / Payable à"), QStringList{iban} + creancier);
    if (!reference.isEmpty()) y = bloc(xi, y, li, 8, 10, QString::fromUtf8("Référence"), {reference});
    if (!message.isEmpty()) y = bloc(xi, y, li, 8, 10, QString::fromUtf8("Informations supplémentaires"), {message});
    if (!debiteur.isEmpty()) bloc(xi, y, li, 8, 10, QString::fromUtf8("Payable par"), debiteur);
    else { y = bloc(xi, y, li, 8, 10, QString::fromUtf8("Payable par (nom/adresse)"), {}); cadre(xi, y - 2, 65, 25); }
    p.restore();
}

QByteArray pdf_depuis_html(const QString &html, QString *erreur) {
    QByteArray octets;
    QBuffer tampon(&octets);
    tampon.open(QIODevice::WriteOnly);
    QPdfWriter pdf(&tampon);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);   // les marges, ci-dessous : la section paiement va au bord
    pdf.setResolution(300);
    pdf.setCreator("GrymoiR");
    const double ppm = pdf.resolution() / 25.4;   // points du PDF par millimètre
    const double marge_g = 15, marge_h = 18, marge_b = 18, hauteur_page = 297, largeur_page = 210;
    QTextDocument doc;
    doc.documentLayout()->setPaintDevice(&pdf);   // les tailles en points, à la résolution du PDF
    QString page_html = html;
    // la section paiement d'une QR-facture (2.28) : retirée de la page, dessinée en bas de la dernière
    QString paiement;
    static const QRegularExpression marque("<p class=\"grym-paiement\" title=\"([A-Za-z0-9+/=]*)\"></p>\\n?");
    if (const QRegularExpressionMatch m = marque.match(page_html); m.hasMatch()) {
        paiement = QString::fromUtf8(QByteArray::fromBase64(m.captured(1).toLatin1()));
        page_html.remove(m.capturedStart(), m.capturedLength());
    }
    // Les images de la page (data:) passent par des ressources QImage : une image en noir et blanc (un code QR) y
    // reste d'un bit, et ImagesNettes la dessine en traits (2.27)
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
    const double pied = 8;   // le numéro de page, en millimètres
    const QSizeF page((largeur_page - 2 * marge_g) * ppm, (hauteur_page - marge_h - marge_b - pied) * ppm);
    doc.setPageSize(page);   // la mise en page découpe le tableau et répète son en-tête (<thead>) à chaque page
    int pages = doc.pageCount();
    // la section paiement prend les 105 mm du bas de la dernière page, plus 10 mm d'air : sinon, une page de plus
    bool page_seule = false;
    if (!paiement.isEmpty()) {
        const double utilise = doc.size().height() - (pages - 1) * page.height();
        if (marge_h * ppm + utilise > (hauteur_page - 105 - 10) * ppm) { page_seule = true; pages++; }
    }
    QPainter p;
    if (!p.begin(&pdf)) {
        *erreur = "Le PDF ne peut pas être commencé.";
        return QByteArray();
    }
    QFont petite = p.font();
    petite.setPointSizeF(8);
    const int pages_texte = page_seule ? pages - 1 : pages;
    for (int i = 0; i < pages; i++) {
        if (i) pdf.newPage();
        if (i < pages_texte) {
            p.save();
            p.translate(marge_g * ppm, marge_h * ppm - i * page.height());
            doc.drawContents(&p, QRectF(0, i * page.height(), page.width(), page.height()));
            p.restore();
        }
        const bool ici = !paiement.isEmpty() && i == pages - 1;
        // le numéro de page : en bas, ou au-dessus de la section paiement
        const double bas = ici ? hauteur_page - 105 - 5 : hauteur_page - marge_b;
        p.setFont(petite);
        p.setPen(QColor("#555555"));
        p.drawText(QRectF(marge_g * ppm, (bas - pied) * ppm, page.width(), pied * ppm), Qt::AlignRight | Qt::AlignBottom,
                   QString("page %1 / %2").arg(i + 1).arg(pages));
        if (ici) dessiner_paiement(p, ppm, (hauteur_page - 105) * ppm, paiement);
    }
    p.end();
    if (octets.isEmpty()) *erreur = "Le PDF est vide.";
    return octets;
}

#include "rapport.moc"
