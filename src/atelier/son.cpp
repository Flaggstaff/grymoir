// Écouter un son, en enregistrer un au micro (grammaire, § 15.7) : voir son.h.
#include "son.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioInput>
#include <QAudioOutput>
#include <QBuffer>
#include <QDateTime>
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
    rang->addWidget(message_, 1);
    connect(bouton_, &QPushButton::clicked, this, &EnregistreurSon::basculer);
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
            bouton_->setText(QString::fromUtf8("■ Arrêter (%1)").arg(duree_lisible(d)));
        });
        connect(enregistreur, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState e) {
            if (e == QMediaRecorder::StoppedState) fini();
        });
        connect(enregistreur, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &m) {
            message_->setText(QString::fromUtf8("L'enregistrement a échoué : ") + m);
        });
    }
    entree->setDevice(QMediaDevices::defaultAudioInput());   // le micro par défaut, au moment de commencer
    enregistreur->setMediaFormat(f.format);
    enregistreur->setQuality(QMediaRecorder::HighQuality);
    enregistreur->setOutputLocation(QUrl::fromLocalFile(dossier->filePath("enregistrement." + extension)));
    message_->clear();
    bouton_->setText(QString::fromUtf8("■ Arrêter (0:00)"));
    enregistreur->record();
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
    if (octets.isEmpty()) {
        if (message_->text().isEmpty()) message_->setText(QString::fromUtf8("Rien n'a été enregistré."));
        return;
    }
    const QString nom = QString("enregistrement-%1.%2")
                            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm"), extension);
    message_->clear();   // le nom s'affiche là où l'appelant montre la valeur
    if (enregistre) enregistre(octets, nom);
}
