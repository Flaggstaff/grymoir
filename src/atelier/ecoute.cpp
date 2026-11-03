// L'écoute (grammaire, 2.69) : voir ecoute.h.
#include "ecoute.h"
#include "micro.h"
#include "transcription.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QMediaDevices>
#include <QProgressBar>
#include <QSettings>
#include <QHash>
#include <QVariant>
#include <QSlider>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <QtEndian>
#include <cmath>

static constexpr size_t FENETRE = 320;              // 20 ms à 16 kHz
static constexpr size_t MARGE = 4800;               // 300 ms : avant la première parole, après la dernière
static constexpr size_t PAROLE_MIN = 4800;          // moins de 300 ms de parole : un craquement
static constexpr size_t PASSAGE_MAX = 16000 * 300;  // 5 minutes

void Decoupeur::nourrir(const float *pcm, size_t n, std::vector<std::vector<float>> *finis) {
    for (size_t i = 0; i < n; i++) {
        fenetre.push_back(pcm[i]);
        if (fenetre.size() == FENETRE) fenetre_pleine(finis);
    }
}

void Decoupeur::fenetre_pleine(std::vector<std::vector<float>> *finis) {
    double somme = 0;
    float c = 0;
    for (float x : fenetre) {
        somme += (double)x * x;
        c = std::max(c, std::fabs(x));
    }
    niveau = (float)std::sqrt(somme / (double)fenetre.size());
    crete = c;
    const bool parle = niveau >= seuil;
    if (!en_passage) {
        if (parle) {   // un passage commence, avec les 300 ms d'avant
            en_passage = true;
            passage = avant;
            passage.insert(passage.end(), fenetre.begin(), fenetre.end());
            parole = FENETRE;
            silence = 0;
            avant.clear();
        } else {
            avant.insert(avant.end(), fenetre.begin(), fenetre.end());
            if (avant.size() > MARGE) avant.erase(avant.begin(), avant.begin() + (long)(avant.size() - MARGE));
        }
    } else {
        passage.insert(passage.end(), fenetre.begin(), fenetre.end());
        if (parle) { parole += FENETRE; silence = 0; }
        else silence += FENETRE;
        const size_t limite = (size_t)(silence_ms > 0 ? silence_ms : 1500) * 16;   // ms → échantillons à 16 kHz
        if (silence >= limite || passage.size() >= PASSAGE_MAX) clore(finis);
    }
    fenetre.clear();
}

void Decoupeur::clore(std::vector<std::vector<float>> *finis) {
    // le silence de la fin, sauf 300 ms
    if (silence > MARGE) passage.resize(passage.size() - (silence - MARGE));
    if (parole >= PAROLE_MIN) finis->push_back(std::move(passage));
    passage.clear();
    en_passage = false;
    silence = parole = 0;
}

void Decoupeur::finir(std::vector<std::vector<float>> *finis) {
    if (en_passage) clore(finis);
    fenetre.clear();
    avant.clear();
}

QByteArray wav_16k(const std::vector<float> &pcm) {
    const quint32 donnees = (quint32)(pcm.size() * 2);
    QByteArray o;
    o.reserve(44 + (int)donnees);
    auto u32 = [&o](quint32 v) { char b[4]; qToLittleEndian(v, b); o.append(b, 4); };
    auto u16 = [&o](quint16 v) { char b[2]; qToLittleEndian(v, b); o.append(b, 2); };
    o.append("RIFF", 4); u32(36 + donnees); o.append("WAVE", 4);
    o.append("fmt ", 4); u32(16); u16(1); u16(1); u32(16000); u32(32000); u16(2); u16(16);
    o.append("data", 4); u32(donnees);
    for (float x : pcm) {
        const float y = std::max(-1.0f, std::min(1.0f, x));
        u16((quint16)(qint16)std::lround(y * 32767.0f));
    }
    return o;
}

// ---------------------------------------------------------------------------------------------------------------------

void Biquad::regler(bool passe_haut, double f, double fe) {
    const double w = 2 * 3.14159265358979323846 * f / fe, cw = std::cos(w), al = std::sin(w) / (2 * 0.7071067811865476);
    const double a0 = 1 + al;
    if (passe_haut) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; }
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 = -2 * cw / a0;
    a2 = (1 - al) / a0;
}

float Biquad::passer(float x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return (float)y;
}

void Affinage::appliquer(std::vector<float> *pcm) {
    const float g = (float)std::pow(10.0, gain_db / 20.0);
    for (float &x : *pcm) {
        float y = x * g;
        if (filtre) y = bas.passer(haut.passer(y));
        x = std::max(-1.0f, std::min(1.0f, y));
    }
}

double squelch_en_db(int p) { return -35.0 + std::max(-50, std::min(50, p)) / 2.0; }
int position_du_squelch(double db) { return std::max(-50, std::min(50, (int)std::lround((db + 35.0) * 2.0))); }

// Le niveau sur l'échelle du squelch : la barre et le curseur, l'un sous l'autre, parlent la même langue.
static int position_du_niveau(float x) { return x <= 1e-6f ? -50 : position_du_squelch(20.0 * std::log10(x)); }
static float seuil_de(double db) { return (float)std::pow(10.0, db / 20.0); }

// Une réglette (2 novembre 2026) : un curseur large, ses chiffres de 10 en 10 dessous (0 au milieu), et un mot à chaque
// bout. Les chiffres sont posés sous la poignée, à la place exacte que le style lui donne.
class Reglette : public QWidget {
public:
    QSlider *curseur;
    QString gauche, droite;
    Reglette(int min, int max, const QString &g, const QString &d, QWidget *parent = nullptr)
        : QWidget(parent), curseur(new QSlider(Qt::Horizontal, this)), gauche(g), droite(d) {
        curseur->setRange(min, max);
        curseur->setSingleStep(1);
        curseur->setPageStep(10);
        curseur->setTickPosition(QSlider::NoTicks);
        curseur->setMinimumHeight(30);
        curseur->setStyleSheet(QString::fromUtf8(
            "QSlider::groove:horizontal { height: 10px; border-radius: 5px; background: palette(mid); }"
            "QSlider::handle:horizontal { width: 26px; height: 26px; margin: -8px 0; border-radius: 13px;"
            " background: palette(highlight); border: 2px solid palette(base); }"
            "QSlider::handle:horizontal { width: 24px; height: 24px; margin: -7px 0; border-radius: 12px; }"));
        setMinimumHeight(30 + 2 * fontMetrics().height() + 6);
    }
    // les chiffres des bouts (« −50 », « +50 ») débordent la poignée : le curseur laisse cette marge de chaque côté
    static constexpr int MARGE = 20;
    int demi_poignee() const { return MARGE + 12; }

protected:
    void resizeEvent(QResizeEvent *) override { curseur->setGeometry(MARGE, 0, width() - 2 * MARGE, 30); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        QStyleOptionSlider o;
        o.initFrom(curseur);
        o.orientation = Qt::Horizontal;
        o.minimum = curseur->minimum();
        o.maximum = curseur->maximum();
        o.rect = curseur->rect();
        const int h = fontMetrics().height();
        const int premier = (int)std::ceil(curseur->minimum() / 10.0) * 10;
        for (int v = premier; v <= curseur->maximum(); v += 10) {
            o.sliderPosition = o.sliderValue = v;
            const QRect poignee = curseur->style()->subControlRect(QStyle::CC_Slider, &o, QStyle::SC_SliderHandle, curseur);
            const int x = MARGE + poignee.center().x();
            p.drawLine(x, 31, x, 35);
            const QString t = v > 0 ? QString("+%1").arg(v) : QString::number(v);
            QFont f = font();
            f.setBold(v == 0);
            p.setFont(f);
            p.drawText(QRect(x - 30, 36, 60, h), Qt::AlignHCenter | Qt::AlignTop, t);
        }
        QFont f = font();
        f.setBold(false);
        p.setFont(f);
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRect(MARGE, 36 + h, width() / 2 - MARGE, h), Qt::AlignLeft | Qt::AlignTop, gauche);
        p.drawText(QRect(width() / 2, 36 + h, width() / 2 - MARGE, h), Qt::AlignRight | Qt::AlignTop, droite);
    }
};

QSlider *Ecoute::squelch() const { return seuil_->curseur; }

void Ecoute::calibrer(int duree_ms) {
    if (calibrage) return;
    calibrage = true;
    bruit.clear();
    calibrer_->setEnabled(false);
    etat_->setText(QString::fromUtf8("Calibrage : silence sur le poste, %1 secondes…").arg(duree_ms / 1000));
    if (!minuterie) {
        minuterie = new QTimer(this);
        minuterie->setSingleShot(true);
        connect(minuterie, &QTimer::timeout, this, [this] { finir_calibrage(); });
    }
    minuterie->start(duree_ms);
}

bool Ecoute::finir_calibrage() {
    if (minuterie) minuterie->stop();
    calibrage = false;
    calibrer_->setEnabled(true);
    if (bruit.size() < 10) {   // moins de 200 ms reçues : l'entrée est muette, ou hors veille
        etat_->setText(QString::fromUtf8("Calibrage impossible : aucun son reçu. Vérifie l'entrée et la veille."));
        return false;
    }
    std::vector<float> tri = bruit;
    std::sort(tri.begin(), tri.end());
    const float haut = tri[(tri.size() * 9) / 10];
    const double db = haut <= 1e-6f ? -60.0 : 20.0 * std::log10(haut);
    seuil_->curseur->setValue(position_du_squelch(db + 6.0));
    etat_->setText(QString::fromUtf8("Squelch calibré à %1 : 6 dB au-dessus du bruit.").arg(seuil_->curseur->value()));
    return true;
}
QSlider *Ecoute::gain() const { return gain_->curseur; }

Ecoute::Ecoute(const QString &n, int silence_ms, QWidget *parent, bool materiel_, bool veille_libre) : QWidget(parent), nom(n), materiel(materiel_) {
    d.silence_ms = silence_ms > 0 ? silence_ms : 1500;
    setObjectName("bandeau-ecoute");
    auto *pile = new QVBoxLayout(this);
    pile->setContentsMargins(0, 0, 0, 0);
    auto *haut = new QHBoxLayout;
    auto *titre = new QLabel(QString::fromUtf8("<b>%1</b>").arg(nom.toHtmlEscaped()));
    veille_ = new QCheckBox(QString::fromUtf8("En veille"));
    veille_->setToolTip(QString::fromUtf8("Écouter ce poste : ses passages sont gardés et transcrits"));
    entree_ = new QComboBox;
    entree_->setToolTip(QString::fromUtf8("L'entrée son de ce poste, et son canal"));
    haut_parleur_ = new QToolButton;
    haut_parleur_->setText(QString::fromUtf8("Haut-parleur"));
    haut_parleur_->setCheckable(true);
    haut_parleur_->setToolTip(QString::fromUtf8("Écouter ce poste sur la sortie son de l'ordinateur"));
    haut->addWidget(titre);
    haut->addWidget(veille_);
    haut->addWidget(entree_, 1);
    haut->addWidget(haut_parleur_);
    pile->addLayout(haut);
    // Le niveau, puis le squelch juste dessous, à la même échelle et à la même largeur : quand la barre dépasse la
    // poignée, un passage commence (2 novembre 2026 ; avant, un petit curseur « Seuil » à droite, de −60 à −10 dB).
    niveau_ = new QProgressBar;
    niveau_->setRange(-50, 50);
    niveau_->setValue(-50);
    niveau_->setTextVisible(false);
    niveau_->setMaximumHeight(12);
    niveau_->setToolTip(QString::fromUtf8("Le niveau du son reçu, sur l'échelle du squelch : quand il dépasse la poignée, c'est un passage"));
    seuil_ = new Reglette(-50, 50, QString::fromUtf8("◀ prend plus de son, même faible"), QString::fromUtf8("seulement les sons forts ▶"));
    seuil_->curseur->setToolTip(QString::fromUtf8("Le squelch : plus il est à gauche, plus il prend le son. 0 = −35 dB ; un cran = ½ dB"));
    auto *ligne_niveau = new QHBoxLayout;
    ligne_niveau->setContentsMargins(seuil_->demi_poignee(), 0, seuil_->demi_poignee(), 0);
    ligne_niveau->addWidget(niveau_);
    auto *titre_squelch = new QLabel(QString::fromUtf8("<b>Squelch</b>"));
    calibrer_ = new QToolButton;
    calibrer_->setText(QString::fromUtf8("Calibrer"));
    calibrer_->setToolTip(QString::fromUtf8("Poste allumé, sans émission : Écoutille écoute le bruit 5 secondes et place le squelch juste au-dessus"));
    connect(calibrer_, &QToolButton::clicked, this, [this] { calibrer(); });
    auto *ligne_squelch = new QHBoxLayout;
    ligne_squelch->addWidget(titre_squelch);
    ligne_squelch->addStretch(1);
    ligne_squelch->addWidget(calibrer_);
    pile->addLayout(ligne_squelch);
    pile->addLayout(ligne_niveau);
    pile->addWidget(seuil_);
    // Le gain et le filtre : affiner le son avant tout le reste (découpage, haut-parleur, transcription)
    auto *ligne_gain = new QHBoxLayout;
    gain_ = new Reglette(-30, 30, QString::fromUtf8("◀ plus faible"), QString::fromUtf8("plus fort ▶"));
    gain_->curseur->setToolTip(QString::fromUtf8("Le gain, en dB : pour un poste trop faible ou trop fort ; 0 ne change rien"));
    filtre_ = new QCheckBox(QString::fromUtf8("Filtre radio (150 à 3000 Hz)"));
    filtre_->setToolTip(QString::fromUtf8("Ôte le ronflement du secteur et le souffle aigu ; la voix et le morse passent"));
    ligne_gain->addWidget(new QLabel(QString::fromUtf8("<b>Gain</b>")));
    ligne_gain->addStretch(1);
    ligne_gain->addWidget(filtre_);
    pile->addLayout(ligne_gain);
    pile->addWidget(gain_);
    etat_ = new QLabel;
    etat_->setObjectName("ecoute-etat");
    pile->addWidget(etat_);
    QSettings s;
    // le seuil reste retenu en dB (« seuil »), comme avant : les réglages déjà faits gardent leur sens
    seuil_->curseur->setValue(position_du_squelch(s.value(cle("seuil"), -35.0).toDouble()));
    d.seuil = seuil_de(squelch_en_db(seuil_->curseur->value()));
    gain_->curseur->setValue(s.value(cle("gain"), 0).toInt());
    affinage.gain_db = gain_->curseur->value();
    filtre_->setChecked(s.value(cle("filtre"), false).toBool());
    affinage.filtre = filtre_->isChecked();
    haut_parleur_->setChecked(s.value(cle("haut-parleur"), false).toBool());
    veille_->setChecked(veille_libre ? s.value(cle("veille"), false).toBool() : true);   // la première fois : rien n'écoute sans qu'on le veuille
    veille_->setVisible(veille_libre);
    if (veille_libre) connect(veille_, &QCheckBox::toggled, this, [this](bool oui) {
        QSettings().setValue(cle("veille"), oui);
        if (!this->materiel) return;
        if (oui) demarrer();
        else arreter();
    });
    connect(seuil_->curseur, &QSlider::valueChanged, this, [this](int v) {
        d.seuil = seuil_de(squelch_en_db(v));
        QSettings().setValue(cle("seuil"), squelch_en_db(v));
    });
    connect(gain_->curseur, &QSlider::valueChanged, this, [this](int v) {
        affinage.gain_db = v;
        QSettings().setValue(cle("gain"), v);
    });
    connect(filtre_, &QCheckBox::toggled, this, [this](bool oui) {
        affinage.filtre = oui;
        affinage.vider();
        QSettings().setValue(cle("filtre"), oui);
    });
    connect(haut_parleur_, &QToolButton::toggled, this, [this](bool oui) {
        QSettings().setValue(cle("haut-parleur"), oui);
        if (oui) ouvrir_sortie();
        else fermer_sortie();
    });
    if (!veille_->isChecked()) etat_->setText(QString::fromUtf8("Hors veille."));
    if (!materiel) return;
    remplir();
    connect(entree_, qOverload<int>(&QComboBox::activated), this, [this](int) {
        QSettings().setValue(cle("entrée"), entree_->currentData().toString());
        demarrer();
    });
    auto *appareils = new QMediaDevices(this);
    connect(appareils, &QMediaDevices::audioInputsChanged, this, [this] { remplir(); demarrer(); });
    switch (etat_micro()) {
    case EtatMicro::Accorde: demarrer(); break;
    case EtatMicro::Refuse:
        etat_->setText(QString::fromUtf8("L'accès au micro est refusé : Réglages Système, Confidentialité, Micro."));
        break;
    case EtatMicro::Indecis:
        demander_micro(this, [this](EtatMicro e) {
            if (e == EtatMicro::Accorde) demarrer();
            else etat_->setText(QString::fromUtf8("L'accès au micro est refusé : Réglages Système, Confidentialité, Micro."));
        });
        break;
    }
    if (haut_parleur_->isChecked()) ouvrir_sortie();
}

// Détruit avec son écran : rien ne touche plus aux barres ni aux libellés, dont l'écran parent est en pleine démolition
// (sous macOS, la remise à zéro de la barre de niveau à cet instant faisait planter, 1er novembre 2026).
Ecoute::~Ecoute() {
    capte = nullptr;
    couper();
    fermer_sortie();
}

bool Ecoute::reporter_reglages(const QString &ancien, const QString &nouveau) {
    QSettings s;
    const QString de = QString::fromUtf8("écoute/%1").arg(ancien), vers = QString::fromUtf8("écoute/%1").arg(nouveau);
    s.beginGroup(vers);
    const bool deja = !s.childKeys().isEmpty();
    s.endGroup();
    if (deja) return false;
    s.beginGroup(de);
    const QStringList cles = s.childKeys();
    QHash<QString, QVariant> valeurs;
    for (const QString &k : cles) valeurs.insert(k, s.value(k));
    s.endGroup();
    if (valeurs.isEmpty()) return false;
    for (auto it = valeurs.constBegin(); it != valeurs.constEnd(); ++it) s.setValue(vers + "/" + it.key(), it.value());
    s.remove(de);
    return true;
}

QString Ecoute::cle(const QString &reglage) const { return QString::fromUtf8("écoute/%1/%2").arg(nom, reglage); }

// Chaque appareil, chaque canal : « U-Phoria UM2, entrée 1 ». Deux appareils au même nom (deux Yaesu, « USB Audio
// CODEC ») sont numérotés : « USB Audio CODEC (2) ». La donnée : l'identifiant de l'appareil, « ␟ », le canal.
void Ecoute::remplir() {
    const QString retenu = QSettings().value(cle("entrée")).toString();
    entree_->clear();
    const QList<QAudioDevice> liste = QMediaDevices::audioInputs();
    QHash<QString, int> vus, total;
    for (const QAudioDevice &a : liste) total[a.description()]++;
    for (const QAudioDevice &a : liste) {
        QString n = a.description();
        if (total[n] > 1) n += QString(" (%1)").arg(++vus[n]);
        const int nc = std::max(1, a.preferredFormat().channelCount());
        const QString id = QString::fromUtf8(a.id().toHex());
        if (nc == 1) entree_->addItem(n, id + QChar(0x1f) + "0");
        else
            for (int c = 0; c < nc; c++)
                entree_->addItem(QString::fromUtf8("%1, entrée %2").arg(n).arg(c + 1), id + QChar(0x1f) + QString::number(c));
    }
    if (entree_->count() == 0) { etat_->setText(QString::fromUtf8("Aucune entrée son sur cet ordinateur.")); return; }
    const int i = entree_->findData(retenu);
    entree_->setCurrentIndex(i >= 0 ? i : 0);   // une entrée débranchée : la première, sans oublier le choix
}

void Ecoute::demarrer() {
    arreter();
    affinage.vider();   // une autre entrée : le filtre ne garde rien de l'ancienne
    if (!veille_->isChecked()) return;
    const QString choix = entree_->currentData().toString();
    if (choix.isEmpty()) return;
    const QByteArray id = QByteArray::fromHex(choix.section(QChar(0x1f), 0, 0).toUtf8());
    canal = choix.section(QChar(0x1f), 1, 1).toInt();
    QAudioDevice appareil;
    for (const QAudioDevice &a : QMediaDevices::audioInputs())
        if (a.id() == id) appareil = a;
    if (appareil.isNull()) { etat_->setText(QString::fromUtf8("Entrée débranchée.")); return; }
    QAudioFormat f = appareil.preferredFormat();
    if (f.sampleFormat() == QAudioFormat::Unknown) { etat_->setText(QString::fromUtf8("Format de l'entrée illisible.")); return; }
    frequence = f.sampleRate();
    canaux = std::max(1, f.channelCount());
    format = (int)f.sampleFormat();
    if (canal >= canaux) canal = 0;
    source = new QAudioSource(appareil, f, this);
    flux = source->start();
    if (!flux) { etat_->setText(QString::fromUtf8("L'entrée ne s'ouvre pas.")); return; }
    connect(flux, &QIODevice::readyRead, this, &Ecoute::lire);
    etat_->setText(QString::fromUtf8("En écoute."));
}

void Ecoute::couper() {
    if (source) {
        source->disconnect(this);
        source->stop();
        delete source;   // son flux avec elle ; rien ne doit plus arriver dans lire()
        source = nullptr;
    }
    flux = nullptr;
    std::vector<std::vector<float>> finis;
    d.finir(&finis);   // un passage en cours quand on change d'entrée : perdu, il serait coupé en son milieu
    d.niveau = d.crete = 0;
}

void Ecoute::arreter() {
    couper();
    if (niveau_) niveau_->setValue(-50);
    if (etat_) etat_->setText(veille_ && veille_->isChecked() ? QString() : QString::fromUtf8("Hors veille."));
}

// Les octets reçus : le canal choisi, en flottants, puis en 16 kHz.
void Ecoute::lire() {
    if (!flux) return;
    const QByteArray o = flux->readAll();
    std::vector<float> mono;
    const int taille = format == QAudioFormat::Int16 ? 2 : format == QAudioFormat::UInt8 ? 1 : 4;
    const qsizetype pas = (qsizetype)taille * canaux;
    for (qsizetype i = (qsizetype)taille * canal; i + taille <= o.size(); i += pas) {
        const char *p = o.constData() + i;
        switch ((QAudioFormat::SampleFormat)format) {
        case QAudioFormat::Int16: mono.push_back(qFromLittleEndian<qint16>(p) / 32768.0f); break;
        case QAudioFormat::Int32: mono.push_back(qFromLittleEndian<qint32>(p) / 2147483648.0f); break;
        case QAudioFormat::Float: { float x; memcpy(&x, p, 4); mono.push_back(x); break; }
        case QAudioFormat::UInt8: mono.push_back(((unsigned char)*p - 128) / 128.0f); break;
        default: return;
        }
    }
    std::vector<float> pcm;
    pcm_en_16k(mono, frequence, 1, &pcm);
    nourrir(pcm);
}

void Ecoute::nourrir(const std::vector<float> &brut) {
    if (!veille_->isChecked()) return;
    std::vector<float> pcm16k = brut;
    affinage.appliquer(&pcm16k);   // le gain et le filtre d'abord : ce qu'on découpe, entend et transcrit est le son affiné
    std::vector<std::vector<float>> finis;
    if (calibrage) {   // le bruit seul, fenêtre par fenêtre ; rien ne devient un passage pendant ce temps
        Decoupeur mesure;
        mesure.seuil = 2.0f;   // jamais atteint
        for (size_t i = 0; i + 320 <= pcm16k.size(); i += 320) {
            mesure.nourrir(pcm16k.data() + i, 320, &finis);
            bruit.push_back(mesure.niveau);
        }
        finis.clear();
        jouer(pcm16k);
        niveau_->setValue(position_du_niveau(mesure.niveau));
        return;
    }
    d.nourrir(pcm16k.data(), pcm16k.size(), &finis);
    jouer(pcm16k);
    montrer();
    for (const auto &p : finis) {
        const QString fichier = QString::fromUtf8("%1-%2.wav").arg(nom, QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmmss"));
        if (capte) capte(wav_16k(p), fichier);
    }
}

void Ecoute::montrer() {
    niveau_->setValue(position_du_niveau(d.niveau));
    niveau_->setProperty("sature", d.crete >= 0.98f);   // la feuille du thème peut le colorer
    if (d.crete >= 0.98f) etat_->setText(QString::fromUtf8("Le son sature : baisse le gain, ou le volume du poste."));
    else if (d.en_passage) etat_->setText(QString::fromUtf8("Passage en cours (%1 s)").arg((int)d.secondes_en_cours()));
    else if (source) etat_->setText(QString::fromUtf8("En écoute."));
}

// Le haut-parleur : la sortie son par défaut, dans son format préféré ; le son de 16 kHz y est porté à sa fréquence.
void Ecoute::ouvrir_sortie() {
    fermer_sortie();
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return;
    QAudioFormat f = dev.preferredFormat();
    if (f.sampleFormat() == QAudioFormat::Unknown) return;
    frequence_sortie = f.sampleRate();
    canaux_sortie = std::max(1, f.channelCount());
    format_sortie = (int)f.sampleFormat();
    sortie = new QAudioSink(dev, f, this);
    flux_sortie = sortie->start();
}

void Ecoute::fermer_sortie() {
    if (sortie) { sortie->stop(); delete sortie; sortie = nullptr; }
    flux_sortie = nullptr;
}

void Ecoute::jouer(const std::vector<float> &pcm16k) {
    if (!flux_sortie || pcm16k.empty() || frequence_sortie <= 0) return;
    const double rapport = 16000.0 / frequence_sortie;
    const size_t n = (size_t)((double)pcm16k.size() / rapport);
    QByteArray o;
    const int taille = format_sortie == QAudioFormat::Int16 ? 2 : format_sortie == QAudioFormat::UInt8 ? 1 : 4;
    o.reserve((qsizetype)(n * (size_t)canaux_sortie * (size_t)taille));
    for (size_t i = 0; i < n; i++) {   // interpolation linéaire, assez bonne pour l'oreille
        const double x = (double)i * rapport;
        const size_t a = std::min((size_t)x, pcm16k.size() - 1), b = std::min(a + 1, pcm16k.size() - 1);
        const float v = (float)(pcm16k[a] + (pcm16k[b] - pcm16k[a]) * (x - (double)a));
        for (int c = 0; c < canaux_sortie; c++) {
            char t[4];
            switch ((QAudioFormat::SampleFormat)format_sortie) {
            case QAudioFormat::Int16: qToLittleEndian<qint16>((qint16)std::lround(std::max(-1.0f, std::min(1.0f, v)) * 32767), t); break;
            case QAudioFormat::Int32: qToLittleEndian<qint32>((qint32)std::llround(std::max(-1.0f, std::min(1.0f, v)) * 2147483647.0), t); break;
            case QAudioFormat::Float: memcpy(t, &v, 4); break;
            case QAudioFormat::UInt8: t[0] = (char)(unsigned char)std::lround((std::max(-1.0f, std::min(1.0f, v)) + 1) * 127.5); break;
            default: return;
            }
            o.append(t, taille);
        }
    }
    flux_sortie->write(o);
}
