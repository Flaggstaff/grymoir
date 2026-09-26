// GrymoiR : le thème (voir theme.h).
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QWidget>
#include <QtMath>
#include <cstdio>

// Les polices du thème (vendor/atkinson, SIL Open Font License 1.1).
static const char *const POLICES[] = {
    ":/polices/AtkinsonHyperlegibleNext-Regular.ttf", ":/polices/AtkinsonHyperlegibleNext-Italic.ttf",
    ":/polices/AtkinsonHyperlegibleNext-SemiBold.ttf", ":/polices/AtkinsonHyperlegibleNext-Bold.ttf",
    ":/polices/AtkinsonHyperlegibleMono-Regular.ttf", ":/polices/AtkinsonHyperlegibleMono-Italic.ttf",
    ":/polices/AtkinsonHyperlegibleMono-SemiBold.ttf"};

// Les icônes de la feuille : nom dans le modèle, fichier Lucide (vendor/lucide, licence ISC et MIT), jeton de sa couleur.
struct Icone {
    const char *nom, *fichier, *jeton;
};
static const Icone ICONES[] = {{"chevron-down", "chevron-down", "texte-2"}, {"check-sur-accent", "check", "sur-accent"}};

Theme &Theme::courant() {
    static Theme t;
    return t;
}

Theme::Theme() {
    QFile j(":/theme/grymoir-jetons.json"), m(":/theme/grymoir.qss.modele");
    if (j.open(QIODevice::ReadOnly)) jetons_ = j.readAll();
    if (m.open(QIODevice::ReadOnly)) modele_ = QString::fromUtf8(m.readAll());
    const QString defaut = QJsonDocument::fromJson(jetons_).object().value("accents").toObject().value("_defaut").toString();
    if (!defaut.isEmpty()) accent_ = defaut;
}

QStringList Theme::accents() const {
    // L'ordre vient de « _ordre » : un objet JSON ne garde pas l'ordre de ses clés.
    const QJsonObject a = QJsonDocument::fromJson(jetons_).object().value("accents").toObject();
    QStringList r;
    for (const auto &v : a.value("_ordre").toArray())
        if (a.contains(v.toString())) r << v.toString();
    return r;
}

QString Theme::valeur(Mode mode, const QString &accent, const QString &jeton) const {
    if (jeton == "icones") return dossier_icones_ + "/" + (mode == Sombre ? "sombre-" : "clair-") + accent;
    const QJsonObject j = QJsonDocument::fromJson(jetons_).object();
    const QString m = mode == Sombre ? "sombre" : "clair";
    if (jeton.startsWith("syntaxe-")) return j.value("syntaxe").toObject().value(m).toObject().value(jeton.mid(8)).toString();
    const QJsonObject acc = j.value("accents").toObject().value(accent).toObject().value(m).toObject();
    if (acc.contains(jeton) && acc.value(jeton).isString()) return acc.value(jeton).toString();
    return j.value("couleurs").toObject().value(m).toObject().value(jeton).toString();
}

QColor Theme::couleur(const QString &jeton) const {
    const QString v = valeur(mode_, accent_, jeton);
    return v.isEmpty() ? QColor(Qt::magenta) : QColor(v);
}

QString Theme::feuille(Mode mode, const QString &accent) const {
    static const QRegularExpression re("@([a-z0-9-]+)@");
    QString r;
    qsizetype fin = 0;
    auto it = re.globalMatch(modele_);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString v = valeur(mode, accent, m.captured(1));
        if (v.isEmpty()) {
            std::fprintf(stderr, "grym-atelier : jeton inconnu dans la feuille : %s\n", qPrintable(m.captured(1)));
            return QString();
        }
        r += modele_.mid(fin, m.capturedStart() - fin) + v;
        fin = m.capturedEnd();
    }
    return r + modele_.mid(fin);
}

QFont Theme::police_interface(int pixels) const {
    QFont f("Atkinson Hyperlegible Next");
    f.setPixelSize(pixels);
    return f;
}

QFont Theme::police_code(int pixels) const {
    QFont f("Atkinson Hyperlegible Mono");
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(pixels);
    return f;
}

// Les icônes, recolorées pour un mode et un accent, en PNG trois fois plus grand que leur place :
// net sur un écran à haute densité, sans dépendre d'un greffon de lecture SVG.
void Theme::preparer_icones() {
    static QTemporaryDir dossier;
    if (!dossier.isValid()) return;
    dossier_icones_ = dossier.path();
    for (Mode mode : {Clair, Sombre})
        for (const QString &accent : accents()) {
            const QString d = valeur(mode, accent, "icones");
            QDir().mkpath(d);
            for (const Icone &i : ICONES) {
                QFile f(QString(":/icones/%1.svg").arg(i.fichier));
                if (!f.open(QIODevice::ReadOnly)) continue;
                QByteArray svg = f.readAll();
                svg.replace("currentColor", valeur(mode, accent, i.jeton).toUtf8());
                QSvgRenderer rendu(svg);
                QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter p(&image);
                rendu.render(&p);
                p.end();
                image.save(d + "/" + i.nom + ".png");
            }
        }
}

void Theme::installer(QApplication &app) {
    if (!installe_) {
        for (const char *p : POLICES) QFontDatabase::addApplicationFont(p);
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        preparer_icones();
        installe_ = true;
    }
    app.setFont(police_interface());
    const QByteArray impose = qgetenv("GRYMOIR_MODE");
    Mode m = impose == "sombre" ? Sombre : Clair;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (impose.isEmpty()) {
        m = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? Sombre : Clair;
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme s) { appliquer(s == Qt::ColorScheme::Dark ? Sombre : Clair, accent_); });
    }
#endif
    appliquer(m, accent_);
}

void Theme::appliquer(Mode mode, const QString &accent) {
    mode_ = mode;
    if (accents().contains(accent)) accent_ = accent;
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        QPalette p;
        auto mettre = [&p](QPalette::ColorRole r, const QColor &c) { p.setColor(QPalette::All, r, c); };
        mettre(QPalette::Window, couleur("fond"));
        mettre(QPalette::WindowText, couleur("texte"));
        mettre(QPalette::Base, couleur("surface"));
        mettre(QPalette::AlternateBase, couleur("surface-alt"));
        mettre(QPalette::Text, couleur("texte"));
        mettre(QPalette::PlaceholderText, couleur("texte-2"));
        mettre(QPalette::Button, couleur("surface"));
        mettre(QPalette::ButtonText, couleur("texte"));
        mettre(QPalette::Highlight, couleur("accent"));
        mettre(QPalette::HighlightedText, couleur("sur-accent"));
        mettre(QPalette::ToolTipBase, couleur("texte"));
        mettre(QPalette::ToolTipText, couleur("fond"));
        mettre(QPalette::Link, couleur("accent"));
        mettre(QPalette::Mid, couleur("bordure"));
        mettre(QPalette::BrightText, couleur("danger"));
        for (QPalette::ColorRole r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, r, couleur("texte-desactive"));
        app->setPalette(p);
        app->setStyleSheet(feuille(mode_, accent_));
        for (QWidget *w : QApplication::allWidgets()) w->update();
    }
    emit change();
}

double Theme::contraste(const QColor &a, const QColor &b) {
    auto lum = [](const QColor &c) {
        auto canal = [](double x) { return x <= 0.04045 ? x / 12.92 : qPow((x + 0.055) / 1.055, 2.4); };
        return 0.2126 * canal(c.redF()) + 0.7152 * canal(c.greenF()) + 0.0722 * canal(c.blueF());
    };
    const double la = lum(a), lb = lum(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

QString Theme::verifier_contrastes() const {
    const QJsonObject j = QJsonDocument::fromJson(jetons_).object();
    QString ecarts;
    // Les noms courts des paires générales désignent des jetons de coloration ou le texte.
    const QHash<QString, QString> alias = {{"syntaxe kw", "syntaxe-construction"}, {"syntaxe num", "syntaxe-nombre"},
                                           {"syntaxe str", "syntaxe-texte-litteral"}, {"syntaxe date", "syntaxe-date"},
                                           {"anneau focus principal (texte)", "texte"}};
    auto controler = [&](Mode mode, const QString &accent, const QJsonObject &paires) {
        for (const QString &k : paires.keys()) {
            QString nom = k;
            nom.remove(" (icônes)");
            const QStringList ab = nom.split(" / ");
            if (ab.size() != 2) {
                ecarts += "paire illisible : " + k + "\n";
                continue;
            }
            const QString a = valeur(mode, accent, alias.value(ab[0], ab[0])), b = valeur(mode, accent, alias.value(ab[1], ab[1]));
            if (a.isEmpty() || b.isEmpty()) {
                ecarts += "jeton inconnu : " + k + "\n";
                continue;
            }
            const double r = contraste(QColor(a), QColor(b));
            const QJsonObject d = paires.value(k).toObject();
            const QString ou = QString("%1, %2, %3 : ").arg(mode == Sombre ? "sombre" : "clair", accent.isEmpty() ? "neutres" : accent, k);
            if (qAbs(r - d.value("rapport").toDouble()) > 0.01)
                ecarts += ou + QString("déclaré %1, calculé %2\n").arg(d.value("rapport").toDouble()).arg(r, 0, 'f', 2);
            if (r < d.value("seuil").toDouble()) ecarts += ou + QString("%1 sous le seuil %2\n").arg(r, 0, 'f', 2).arg(d.value("seuil").toDouble());
        }
    };
    for (Mode mode : {Clair, Sombre}) {
        const QString m = mode == Sombre ? "sombre" : "clair";
        controler(mode, accents().value(0), j.value("contrastes").toObject().value(m).toObject());
        for (const QString &a : accents())
            controler(mode, a, j.value("accents").toObject().value(a).toObject().value(m).toObject().value("contrastes").toObject());
    }
    return ecarts;
}
