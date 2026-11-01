// GrymoiR : l'atelier, la liste en agenda (grammaire 2.74). Voir agenda.h.
#include "agenda.h"
#include "theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

static const char *const JOURS[] = {"", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
static const char *const MOIS[] = {"", "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."};

Agenda::Agenda(QWidget *parent) : QWidget(parent) {
    setObjectName("agenda");
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QString::fromUtf8("Agenda"));
    premier_ = dernier_ = QDate::currentDate();
    recalculer();
}

void Agenda::donner_grille(const QString &grille) {
    const QStringList parts = grille.split('|');
    const QDate a = QDate::fromString(parts.value(0).trimmed(), "dd.MM.yyyy");
    const QDate b = QDate::fromString(parts.value(1).trimmed(), "dd.MM.yyyy");
    if (a.isValid()) premier_ = a;
    dernier_ = b.isValid() && b >= premier_ ? b : premier_;
    if (premier_.daysTo(dernier_) > 41) dernier_ = premier_.addDays(41);   // six semaines au plus
    plages_.clear();
    for (int k = 2; k < parts.size(); k++) {   // « 1 08:00-12:00 13:30-18:00 »
        const QStringList mots = parts[k].split(' ', Qt::SkipEmptyParts);
        bool ok = false;
        const int j = mots.value(0).toInt(&ok);
        if (!ok || j < 1 || j > 7) continue;
        auto &l = plages_[j];
        for (int q = 1; q < mots.size(); q++) {
            const QStringList de_a = mots[q].split('-');
            const QTime d = QTime::fromString(de_a.value(0), "HH:mm"), f = QTime::fromString(de_a.value(1), "HH:mm");
            if (d.isValid() && f.isValid() && d < f) l.append({d, f});
        }
    }
    recalculer();
}

void Agenda::donner_lignes(const QStringList &cellules, int nb_lignes, int nb_colonnes) {
    blocs_.clear();
    for (int r = 0; r < nb_lignes && nb_colonnes >= 2 && (r + 1) * nb_colonnes <= cellules.size(); r++) {
        const auto c = [&](int k) { QString x = cellules[r * nb_colonnes + k]; return x.startsWith(QChar(0x1d)) ? x.mid(2) : x; };
        Bloc b;
        b.jour = QDate::fromString(c(0).trimmed(), "dd.MM.yyyy");
        b.debut = QTime::fromString(c(1).trimmed(), "HH:mm");
        if (!b.jour.isValid() || !b.debut.isValid()) continue;   // une séance sans heure ne se place pas
        bool ok = false;
        const int d = nb_colonnes > 2 ? QString(c(2)).remove('\'').trimmed().toInt(&ok) : 0;
        b.duree = ok && d > 0 ? qMin(d, 12 * 60) : 60;
        for (int k = 3; k < nb_colonnes; k++) if (!c(k).isEmpty()) b.textes << c(k);
        b.rang = r;
        blocs_.append(b);
    }
    recalculer();
}

void Agenda::recalculer() {
    jours_.clear();
    for (QDate d = premier_; d <= dernier_; d = d.addDays(1))
        if (plages_.isEmpty() || plages_.contains(d.dayOfWeek())) jours_.append(d);
    heure_debut_ = 7;
    heure_fin_ = 20;
    for (const auto &l : plages_)   // les horaires et les séances restent toujours visibles
        for (const auto &p : l) {
            heure_debut_ = qMin(heure_debut_, p.first.hour());
            heure_fin_ = qMax(heure_fin_, p.second.hour() + (p.second.minute() ? 1 : 0));
        }
    for (const Bloc &b : blocs_) {
        heure_debut_ = qMin(heure_debut_, b.debut.hour());
        const int fin = b.debut.hour() * 60 + b.debut.minute() + b.duree;
        heure_fin_ = qMin(24, qMax(heure_fin_, (fin + 59) / 60));
    }
    updateGeometry();
    update();
}

QSize Agenda::sizeHint() const {
    return QSize(largeur_heures() + 160 * qMax(1, (int)jours_.size()),
                 haut_tete() + (int)((heure_fin_ - heure_debut_) * 60 * pixels_par_minute()) + 8);
}
QSize Agenda::minimumSizeHint() const { return QSize(largeur_heures() + 90 * qMax(1, (int)jours_.size()), sizeHint().height()); }

int Agenda::largeur_colonne() const { return qMax(40, (width() - largeur_heures()) / qMax(1, (int)jours_.size())); }
int Agenda::colonne_x(int c) const { return largeur_heures() + c * largeur_colonne(); }

QRect Agenda::place(int rang) const {
    for (const Bloc &b : blocs_) {
        if (b.rang != rang) continue;
        const int c = jours_.indexOf(b.jour);
        if (c < 0) return QRect();
        const int y = haut_tete() + (int)(((b.debut.hour() - heure_debut_) * 60 + b.debut.minute()) * pixels_par_minute());
        return QRect(colonne_x(c) + 3, y + 1, largeur_colonne() - 6, qMax(18, (int)(b.duree * pixels_par_minute()) - 3));
    }
    return QRect();
}

QPoint Agenda::point_de(const QDate &jour, const QTime &heure) const {
    const int c = jours_.indexOf(jour);
    return QPoint(colonne_x(qMax(0, c)) + largeur_colonne() / 2,
                  haut_tete() + (int)(((heure.hour() - heure_debut_) * 60 + heure.minute()) * pixels_par_minute()) + 1);
}

// Là où tomberait le haut d'un bloc posé en p : le jour de la colonne, l'heure au quart d'heure.
bool Agenda::arrivee(const QPoint &p, QDate *jour, QTime *heure) const {
    if (p.x() < largeur_heures() || jours_.isEmpty()) return false;
    const int c = (p.x() - largeur_heures()) / largeur_colonne();
    if (c < 0 || c >= jours_.size()) return false;
    int minutes = (int)((p.y() - haut_tete()) / pixels_par_minute()) + heure_debut_ * 60;
    minutes = qBound(heure_debut_ * 60, (minutes + 7) / 15 * 15, 23 * 60 + 45);
    *jour = jours_[c];
    *heure = QTime(minutes / 60, minutes % 60);
    return true;
}

void Agenda::paintEvent(QPaintEvent *) {
    const Theme &t = Theme::courant();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), t.couleur("surface"));
    const int haut = haut_tete(), bas = height() - 4;
    QFont f = font();
    // l'en-tête : un jour par colonne, aujourd'hui en gras
    for (int c = 0; c < jours_.size(); c++) {
        const QDate d = jours_[c];
        const QRect r(colonne_x(c), 0, largeur_colonne(), haut);
        f.setBold(d == QDate::currentDate());
        p.setFont(f);
        p.setPen(t.couleur("texte"));
        p.drawText(r, Qt::AlignCenter, QString::fromUtf8("%1 %2 %3").arg(QString::fromUtf8(JOURS[d.dayOfWeek()])).arg(d.day())
                                           .arg(QString::fromUtf8(MOIS[d.month()])));
    }
    f.setBold(false);
    // hors des plages : hachuré ; les jours sans plage connue ne le sont pas
    for (int c = 0; c < jours_.size(); c++) {
        const auto plages = plages_.value(jours_[c].dayOfWeek());
        if (plages.isEmpty()) continue;
        QPainterPath hors;
        hors.addRect(colonne_x(c), haut, largeur_colonne(), bas - haut);
        for (const auto &pl : plages) {
            const int y1 = haut + (int)(((pl.first.hour() - heure_debut_) * 60 + pl.first.minute()) * pixels_par_minute());
            const int y2 = haut + (int)(((pl.second.hour() - heure_debut_) * 60 + pl.second.minute()) * pixels_par_minute());
            QPainterPath dedans;
            dedans.addRect(colonne_x(c), y1, largeur_colonne(), y2 - y1);
            hors = hors.subtracted(dedans);
        }
        p.fillPath(hors, QBrush(t.couleur("separateur"), Qt::BDiagPattern));
    }
    // les heures, et une ligne par heure
    p.setFont(f);
    for (int h = heure_debut_; h <= heure_fin_; h++) {
        const int y = haut + (int)((h - heure_debut_) * 60 * pixels_par_minute());
        p.setPen(t.couleur("separateur"));
        p.drawLine(largeur_heures(), y, width(), y);
        p.setPen(t.couleur("texte-2"));
        p.drawText(QRect(0, y - 9, largeur_heures() - 8, 18), Qt::AlignRight | Qt::AlignVCenter, QString("%1:00").arg(h, 2, 10, QChar('0')));
    }
    p.setPen(t.couleur("separateur"));
    for (int c = 0; c <= jours_.size(); c++) p.drawLine(colonne_x(c), 0, colonne_x(c), bas);
    // les blocs
    for (const Bloc &b : blocs_) {
        QRect r = place(b.rang);
        if (r.isNull()) continue;
        const bool deplace = glisse_ && b.rang == saisi_;
        if (deplace) p.setOpacity(0.35);
        p.setPen(QPen(t.couleur("accent"), 1));
        p.setBrush(t.couleur("fond"));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(t.couleur("texte"));
        QStringList lignes;
        lignes << b.debut.toString("HH:mm") + QString::fromUtf8(" – ") + b.debut.addSecs(b.duree * 60).toString("HH:mm");
        lignes << b.textes;
        p.drawText(r.adjusted(6, 3, -6, -3), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, lignes.join('\n'));
        p.setOpacity(1.0);
    }
    // le bloc qu'on glisse, à son arrivée
    if (glisse_) {
        QDate j;
        QTime h;
        for (const Bloc &b : blocs_) {
            if (b.rang != saisi_ || !arrivee(courant_ - (depart_ - place(b.rang).topLeft()) + QPoint(0, 0), &j, &h)) continue;
            Bloc fantome = b;
            const QPoint coin = point_de(j, h);
            const QRect r(colonne_x(jours_.indexOf(j)) + 3, coin.y(), largeur_colonne() - 6, qMax(18, (int)(b.duree * pixels_par_minute()) - 3));
            p.setPen(QPen(t.couleur("accent"), 2, Qt::DashLine));
            p.setBrush(t.couleur("accent-doux"));
            p.drawRoundedRect(r, 6, 6);
            p.setPen(t.couleur("texte"));
            p.drawText(r.adjusted(6, 3, -6, -3), Qt::AlignLeft | Qt::AlignTop,
                       h.toString("HH:mm") + QString::fromUtf8(" – ") + h.addSecs(b.duree * 60).toString("HH:mm"));
        }
    }
}

void Agenda::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    saisi_ = -1;
    for (const Bloc &b : blocs_) if (place(b.rang).contains(e->position().toPoint())) saisi_ = b.rang;
    depart_ = courant_ = e->position().toPoint();
    glisse_ = false;
}

void Agenda::mouseMoveEvent(QMouseEvent *e) {
    if (saisi_ < 0) {
        bool sur = false;
        for (const Bloc &b : blocs_) sur |= place(b.rang).contains(e->position().toPoint());
        setCursor(sur ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    courant_ = e->position().toPoint();
    if (!glisse_ && (courant_ - depart_).manhattanLength() >= 6) glisse_ = true;
    if (glisse_) { setCursor(Qt::ClosedHandCursor); update(); }
}

void Agenda::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || saisi_ < 0) return;
    const int rang = saisi_;
    const bool glisse = glisse_;
    saisi_ = -1;
    glisse_ = false;
    setCursor(Qt::ArrowCursor);
    update();
    if (!glisse) { emit choisir(rang); return; }
    QDate j;
    QTime h;
    const QRect r = place(rang);
    if (!arrivee(e->position().toPoint() - (depart_ - r.topLeft()), &j, &h)) return;
    for (const Bloc &b : blocs_)
        if (b.rang == rang && b.jour == j && b.debut == h) return;   // reposé à sa place : rien
    emit deplacer(rang, j.toString("dd.MM.yyyy") + "|" + h.toString("HH:mm"));
}
