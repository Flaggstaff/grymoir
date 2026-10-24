// Écouter un son, en enregistrer un au micro (grammaire, § 15.7) : voir son.h.
#include "son.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioInput>
#include <QAudioOutput>
#include <QBuffer>
#include <QDateTime>
#include <QSettings>
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

// L'autorisation du micro (Qt 6.5 et suivants) ; Qt 6.4, celui des distributions Linux, n'en a pas besoin.
#include "micro.h"

static QString duree_lisible(qint64 ms) {
    const qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// L'extension d'un son à sa signature (« m4a »), vide si ce n'en est pas un : elle guide le décodeur.
static QString extension_de_son(const QByteArray &o) {
    if (!octets_de_son(o)) return QString();
    if (o.startsWith("RIFF")) return "wav";
    if (o.startsWith("ID3")) return "mp3";
    if (o.mid(4, 4) == "ftyp") return "m4a";
    if (o.startsWith("OggS")) return "ogg";
    if (o.startsWith("fLaC")) return "flac";
    if ((unsigned char)o[0] == 0x1A) return "webm";
    return ((unsigned char)o[1] & 0xF6) == 0xF0 ? "aac" : "mp3";
}

bool octets_de_son(const QByteArray &o) {
    const auto u = [&](int i) { return (unsigned char)o[i]; };
    const qsizetype n = o.size();
    if (n >= 12 && o.startsWith("RIFF") && o.mid(8, 4) == "WAVE") return true;
    if (n >= 3 && o.startsWith("ID3")) return true;
    if (n >= 2 && u(0) == 0xFF && ((u(1) & 0xF6) == 0xF2 || (u(1) & 0xF6) == 0xF0)) return true;   // MP3, AAC (ADTS)
    if (n >= 12 && o.mid(4, 4) == "ftyp") return true;
    if (n >= 4 && (o.startsWith("OggS") || o.startsWith("fLaC"))) return true;
    if (n >= 4 && u(0) == 0x1A && u(1) == 0x45 && u(2) == 0xDF && u(3) == 0xA3) return true;
    return false;
}

// --- Écouter ---

LecteurSon::LecteurSon(QWidget *parent) : QWidget(parent) {
    auto *rang = new QHBoxLayout(this);
    rang->setContentsMargins(0, 0, 0, 0);
    bouton_ = new QPushButton(QString::fromUtf8("▶ Écouter"));
    bouton_->setEnabled(false);
    message_ = new QLabel;
    message_->setProperty("niveau", "etiquette");
    rang->addWidget(bouton_);
    rang->addWidget(message_, 1);
    connect(bouton_, &QPushButton::clicked, this, &LecteurSon::basculer);
}

LecteurSon::~LecteurSon() {
    if (lecteur) lecteur->stop();
}

void LecteurSon::donner(const QByteArray &o, const QString &n) {
    arreter();
    octets = o;
    nom = n;
    bouton_->setEnabled(!octets.isEmpty());
    message_->clear();
}

void LecteurSon::arreter() {
    if (lecteur) {
        lecteur->stop();
        lecteur->setSourceDevice(nullptr);
    }
    delete tampon;
    tampon = nullptr;
    bouton_->setText(QString::fromUtf8("▶ Écouter"));
}

void LecteurSon::basculer() {
    if (lecteur && lecteur->playbackState() == QMediaPlayer::PlayingState) { arreter(); return; }
    if (octets.isEmpty()) return;
    if (!lecteur) {
        lecteur = new QMediaPlayer(this);
        sortie = new QAudioOutput(this);
        lecteur->setAudioOutput(sortie);
        connect(lecteur, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState e) {
            bouton_->setText(e == QMediaPlayer::PlayingState ? QString::fromUtf8("■ Arrêter") : QString::fromUtf8("▶ Écouter"));
        });
        connect(lecteur, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
            if (d > 0) message_->setText(duree_lisible(d));
        });
        connect(lecteur, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &) {
            message_->setText(QString::fromUtf8("Ce son ne se lit pas sur cet ordinateur."));
            if (signaler) signaler(message_->text());
            arreter();
        });
    }
    delete tampon;
    tampon = new QBuffer(this);
    tampon->setData(octets);
    tampon->open(QIODevice::ReadOnly);
    // l'extension guide le décodeur : celle du nom, sinon celle de la signature (« un fichier de 16 octets »)
    const QString indice = QFileInfo(nom).suffix().isEmpty() ? "son." + extension_de_son(octets) : nom;
    lecteur->setSourceDevice(tampon, QUrl(indice));
    lecteur->play();
}

// --- Enregistrer ---

namespace {
struct FormatEnregistrement {
    QMediaFormat format;
    QString extension;
};
}

// Le premier format que ce système sait écrire, parmi ceux que la machine reconnaît (§ 15.7).
static FormatEnregistrement format_d_enregistrement() {
    const struct { QMediaFormat::FileFormat fichier; QMediaFormat::AudioCodec codec; const char *extension; } essais[] = {
        {QMediaFormat::Mpeg4Audio, QMediaFormat::AudioCodec::AAC, "m4a"},
        {QMediaFormat::MPEG4, QMediaFormat::AudioCodec::AAC, "m4a"},
        {QMediaFormat::Ogg, QMediaFormat::AudioCodec::Opus, "ogg"},
        {QMediaFormat::Ogg, QMediaFormat::AudioCodec::Vorbis, "ogg"},
        {QMediaFormat::FLAC, QMediaFormat::AudioCodec::FLAC, "flac"},
        {QMediaFormat::Wave, QMediaFormat::AudioCodec::Wave, "wav"},
    };
    for (const auto &e : essais) {
        QMediaFormat f;
        f.setFileFormat(e.fichier);
        if (!f.supportedAudioCodecs(QMediaFormat::Encode).contains(e.codec)) continue;
        f.setAudioCodec(e.codec);
        return {f, QString::fromLatin1(e.extension)};
    }
    return {QMediaFormat(), QString()};
}

QString EnregistreurSon::extension_choisie() { return format_d_enregistrement().extension; }

EnregistreurSon::EnregistreurSon(QWidget *parent) : QWidget(parent) {
    auto *rang = new QHBoxLayout(this);
    rang->setContentsMargins(0, 0, 0, 0);
    bouton_ = new QPushButton(QString::fromUtf8("● Dicter"));
    bouton_->setToolTip(QString::fromUtf8("Dicter au micro ; un second clic arrête"));
    message_ = new QLabel;
    message_->setProperty("niveau", "etiquette");
    rang->addWidget(bouton_);
    // le micro : celui du système par défaut, ou celui qu'on a choisi ici (un casque Bluetooth dont on prend le micro
    // passe en mode appel : son faible, volume de la musique qui saute ; on choisit alors celui de l'ordinateur)
    micro_ = new QComboBox;
    micro_->setToolTip(QString::fromUtf8("Le micro de la dictée"));
    micro_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    rang->addWidget(micro_);
    remplir_micros();
    connect(micro_, &QComboBox::activated, this, [this](int i) {
        QSettings().setValue("micro de dictée", micro_->itemData(i).toString());
    });
    auto *appareils = new QMediaDevices(this);
    connect(appareils, &QMediaDevices::audioInputsChanged, this, &EnregistreurSon::remplir_micros);
    rang->addWidget(message_, 1);
    // le texte entier, sur plusieurs lignes : « L'enregistrement a échoué : … » se coupait au bord du formulaire, et
    // « ■ Arrêter (0:00) » perdait sa parenthèse (24 octobre 2026)
    message_->setWordWrap(true);
    message_->setMinimumWidth(0);
    bouton_->setMinimumWidth(bouton_->fontMetrics().horizontalAdvance(QString::fromUtf8("■ Arrêter (00:00)")) + 32);
    connect(bouton_, &QPushButton::clicked, this, &EnregistreurSon::basculer);
}

void EnregistreurSon::remplir_micros() {
    const QString retenu = QSettings().value("micro de dictée").toString();
    micro_->clear();
    micro_->addItem(QString::fromUtf8("Micro du système"), QString());
    for (const QAudioDevice &a : QMediaDevices::audioInputs())
        micro_->addItem(a.description(), QString::fromUtf8(a.id()));
    const int i = micro_->findData(retenu);
    micro_->setCurrentIndex(i >= 0 ? i : 0);   // un micro débranché : celui du système, sans oublier le choix
}

EnregistreurSon::~EnregistreurSon() {
    if (enregistreur) {
        enregistreur->disconnect(this);   // fermer la fenêtre en enregistrant : rien n'est rendu
        enregistreur->stop();
    }
    delete dossier;
}

bool EnregistreurSon::enregistre_en_cours() const {
    return enregistreur && enregistreur->recorderState() != QMediaRecorder::StoppedState;
}

void EnregistreurSon::basculer() {
    if (enregistre_en_cours()) {
        enregistreur->stop();   // la fin arrive par recorderStateChanged : le fichier est alors complet
        return;
    }
    // macOS demande l'accord de l'utilisateur, une fois (Info.plist : NSMicrophoneUsageDescription ; micro.h)
    switch (etat_micro()) {
    case EtatMicro::Indecis:
        demander_micro(this, [this](EtatMicro) { basculer(); });
        return;
    case EtatMicro::Refuse:
        message_->setText(QString::fromUtf8("Le micro est refusé à cette application : Réglages Système, "
                                            "Confidentialité et sécurité, Microphone."));
        return;
    case EtatMicro::Accorde:
        break;
    }
    commencer();
}

void EnregistreurSon::commencer() {
    if (QMediaDevices::defaultAudioInput().isNull()) {
        message_->setText(QString::fromUtf8("Aucun micro sur cet ordinateur."));
        return;
    }
    const FormatEnregistrement f = format_d_enregistrement();
    if (f.extension.isEmpty()) {
        message_->setText(QString::fromUtf8("Ce système ne sait enregistrer aucun format de son reconnu."));
        return;
    }
    extension = f.extension;
    delete dossier;
    dossier = new QTemporaryDir;
    if (!dossier->isValid()) {
        message_->setText(QString::fromUtf8("Le dossier provisoire de l'enregistrement ne se crée pas."));
        return;
    }
    if (!session) {
        session = new QMediaCaptureSession(this);
        entree = new QAudioInput(this);
        enregistreur = new QMediaRecorder(this);
        session->setAudioInput(entree);
        session->setRecorder(enregistreur);
        connect(enregistreur, &QMediaRecorder::durationChanged, this, [this](qint64 d) {
            if (d > 0) bouton_->setText(QString::fromUtf8("■ Arrêter (%1)").arg(duree_lisible(d)));   // le son arrive : parler
        });
        connect(enregistreur, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState e) {
            if (e == QMediaRecorder::StoppedState) fini();
        });
        connect(enregistreur, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &m) {
            message_->setText(QString::fromUtf8("L'enregistrement a échoué : ") + m);
        });
    }
    QAudioDevice choisi = QMediaDevices::defaultAudioInput();   // le micro choisi, s'il est branché ; sinon celui du système
    const QString id = micro_->currentData().toString();
    for (const QAudioDevice &a : QMediaDevices::audioInputs())
        if (!id.isEmpty() && QString::fromUtf8(a.id()) == id) choisi = a;
    entree->setDevice(choisi);
    enregistreur->setMediaFormat(f.format);
    enregistreur->setQuality(QMediaRecorder::HighQuality);
    enregistreur->setOutputLocation(QUrl::fromLocalFile(dossier->filePath("enregistrement." + extension)));
    message_->clear();
    muet_ = false;
    // macOS met environ une seconde à ouvrir le micro : « Un instant… » jusqu'à ce que le son arrive, puis le compteur
    // (24 octobre 2026) ; on sait ainsi quand parler, et un clic arrête quand même
    bouton_->setText(QString::fromUtf8("● Un instant…"));
    enregistreur->record();
    // un micro qui ne donne rien (le compteur figé à 0:00) : on le dit et on arrête, plutôt que d'attendre en silence
    QTimer::singleShot(3000, this, [this] {
        if (enregistre_en_cours() && enregistreur->duration() == 0) {
            muet_ = true;
            enregistreur->stop();
            message_->setText(QString::fromUtf8("Aucun son n'arrive du micro : l'enregistrement est arrêté. Vérifie le micro "
                                                "choisi dans Réglages Système, Son, Entrée."));
        }
    });
}

void EnregistreurSon::fini() {
    bouton_->setText(QString::fromUtf8("● Dicter"));
    if (!dossier) return;
    QString chemin = enregistreur->actualLocation().toLocalFile();
    if (chemin.isEmpty()) chemin = dossier->filePath("enregistrement." + extension);
    QFile f(chemin);
    QByteArray octets;
    if (f.open(QIODevice::ReadOnly)) octets = f.readAll();
    f.close();
    delete dossier;   // le fichier provisoire disparaît : les octets suffisent
    dossier = nullptr;
    if (muet_) return;   // un enregistrement sans son : le message le dit déjà, rien n'est gardé
    if (octets.isEmpty()) {
        if (message_->text().isEmpty()) message_->setText(QString::fromUtf8("Rien n'a été enregistré."));
        return;
    }
    const QString nom = QString("enregistrement-%1.%2")
                            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm"), extension);
    message_->clear();   // le nom s'affiche là où l'appelant montre la valeur
    if (enregistre) enregistre(octets, nom);
}
