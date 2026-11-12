// Écouter un son, en enregistrer un au micro (grammaire, § 15.7) : voir son.h.
#include "son.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioInput>
#include <QAudioOutput>
#include <QBuffer>
#include <QDateTime>
#include "transcription.h"
#include <thread>
#include <QPointer>
#include <QAudioFormat>
#include <QAudioSource>
#include <QSettings>
#include <QComboBox>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
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
static QString extension_du_format(const QByteArray &o) {
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
    const QString indice = QFileInfo(nom).suffix().isEmpty() ? "son." + extension_du_format(octets) : nom;
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
    // Le 27 octobre 2026 : un bouton étroit « Micro ▾ » et son menu, au lieu d'une liste déroulante à la largeur du nom du
    // micro (« Microphone MacBook Air »), qui débordait sur « Dicter » dans une zone de largeur fixe. La liste reste, cachée :
    // elle garde les micros et le choix ; le menu la reflète.
    micro_ = new QComboBox(this);
    micro_->hide();
    choix_ = new QToolButton;
    choix_->setText(QString::fromUtf8("Micro"));   // la flèche du menu, Qt la dessine
    choix_->setPopupMode(QToolButton::InstantPopup);
    choix_->setMenu(new QMenu(choix_));
    rang->addWidget(choix_);
    remplir_micros();
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
    QMenu *menu = choix_->menu();   // le menu : chaque micro, celui choisi coché
    menu->clear();
    qDeleteAll(menu->findChildren<QActionGroup *>(Qt::FindDirectChildrenOnly));   // relecture : l'ancien groupe ne s'accumule pas
    auto *groupe = new QActionGroup(menu);
    for (int k = 0; k < micro_->count(); k++) {
        QAction *a = menu->addAction(micro_->itemText(k));
        a->setCheckable(true);
        a->setChecked(k == micro_->currentIndex());
        groupe->addAction(a);
        connect(a, &QAction::triggered, this, [this, k] {
            micro_->setCurrentIndex(k);
            choix_->setToolTip(QString::fromUtf8("Le micro de la dictée : ") + micro_->itemText(k));
            QSettings().setValue("micro de dictée", micro_->itemData(k).toString());
        });
    }
    choix_->setToolTip(QString::fromUtf8("Le micro de la dictée : ") + micro_->currentText());
}

EnregistreurSon::~EnregistreurSon() {
    arreter_direct();
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
        figer_direct();          // le direct s'arrête au clic, comme l'enregistrement
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
            duree_ms_ = d;
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
    duree_ms_ = 0;
    // macOS met environ une seconde à ouvrir le micro : « Un instant… » jusqu'à ce que le son arrive, puis le compteur
    // (24 octobre 2026) ; on sait ainsi quand parler, et un clic arrête quand même
    bouton_->setText(QString::fromUtf8("● Un instant…"));
    enregistreur->record();
    demarrer_direct(choisi);
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
    figer_direct();
    const std::shared_ptr<Dictee> dictee = direct;   // la dictée survit à l'arrêt du direct : son texte est attendu
    direct.reset();
    if (message_ && message_->text().startsWith("<i>")) message_->clear();   // le texte final arrive dans sa zone
    auto abandonner = [&] { if (dictee) dictee->abandonner(); };
    if (!dossier) { abandonner(); return; }
    QString chemin = enregistreur->actualLocation().toLocalFile();
    if (chemin.isEmpty()) chemin = dossier->filePath("enregistrement." + extension);
    QFile f(chemin);
    QByteArray octets;
    if (f.open(QIODevice::ReadOnly)) octets = f.readAll();
    f.close();
    delete dossier;   // le fichier provisoire disparaît : les octets suffisent
    dossier = nullptr;
    if (muet_) { abandonner(); return; }   // un enregistrement sans son : le message le dit déjà, rien n'est gardé
    if (octets.isEmpty()) {
        if (message_->text().isEmpty()) message_->setText(QString::fromUtf8("Rien n'a été enregistré."));
        abandonner();
        return;
    }
    // La dictée par segments (12 novembre 2026) : si le direct a reçu le même son que l'enregistreur (à deux secondes ou
    // un dixième près : les deux lisent le micro chacun de son côté), son texte, presque prêt, est promis pour ces octets
    // et la passe complète n'a pas lieu. Sinon, la passe complète, comme avant. L'infobulle du bouton dit laquelle.
    if (dictee) {
        const qint64 recu_ms = (qint64)(dictee->echantillons() / 16);
        const qint64 marge = qMax<qint64>(2000, duree_ms_ / 10);
        if (!dictee_par_segments()) {
            dictee->abandonner();
            bouton_->setToolTip(QString::fromUtf8("Dicter au micro ; un second clic arrête.\nDernière dictée : transcrite d'un bloc (GRYMOIR_DICTEE_ENTIERE)."));
        } else if (duree_ms_ > 0 && recu_ms + marge < duree_ms_) {
            dictee->abandonner();
            bouton_->setToolTip(QString::fromUtf8("Dicter au micro ; un second clic arrête.\nDernière dictée : transcrite d'un bloc (le direct n'a reçu que %1 sur %2).")
                                    .arg(duree_lisible(recu_ms), duree_lisible(duree_ms_)));
        } else {
            dictee->finir();
            promettre_transcription(octets, dictee);
            bouton_->setToolTip(QString::fromUtf8("Dicter au micro ; un second clic arrête.\nDernière dictée : transcrite au fil de la parole, en %1 segments.")
                                    .arg(dictee->bornes().size()));
        }
    }
    const QString nom = QString("enregistrement-%1.%2")
                            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HHmm"), extension);
    message_->clear();   // le nom s'affiche là où l'appelant montre la valeur
    if (enregistre) enregistre(octets, nom);
}

// --- Le direct (24 octobre 2026), par segments (12 novembre 2026) ---
// Pendant l'enregistrement, le même micro est lu en PCM et donné à une dictée (transcription.h) : elle se coupe aux pauses,
// et chaque segment fermé reçoit son texte final dans un fil à part. Toutes les deux secondes et demie, le segment encore
// ouvert passe dans Whisper (recherche gloutonne) pour l'aperçu, sauf si un segment fermé attend son texte ; l'ensemble
// s'affiche à côté du bouton, en italique. À l'arrêt, l'aperçu s'efface et le texte de la dictée arrive dans sa zone.

void EnregistreurSon::demarrer_direct(const QAudioDevice &micro) {
    arreter_direct();
    if (modele_de_transcription().isEmpty() || micro.isNull()) return;   // sans modèle : pas de direct, la dictée marche
    QAudioFormat f = micro.preferredFormat();
    if (f.sampleFormat() == QAudioFormat::Unknown) return;
    direct = std::make_shared<Dictee>();
    frequence_directe = f.sampleRate();
    canaux_directs = qMax(1, f.channelCount());
    format_direct = (int)f.sampleFormat();
    source_directe = new QAudioSource(micro, f, this);
    flux_direct = source_directe->start();
    if (flux_direct) connect(flux_direct, &QIODevice::readyRead, this, &EnregistreurSon::lire_direct);
    if (dictee_par_segments()) {   // le fil des segments : il tient la dictée, et finit avec elle
        const std::shared_ptr<Dictee> d = direct;
        const QString modele = modele_de_transcription();
        std::thread([d, modele] { d->travailler(modele); }).detach();
    }
    minuterie_directe = new QTimer(this);
    connect(minuterie_directe, &QTimer::timeout, this, [this] { montrer_direct(); lancer_passe(); });
    minuterie_directe->start(2500);
}

void EnregistreurSon::lire_direct() {
    if (!flux_direct || !direct) return;
    const QByteArray o = flux_direct->readAll();
    std::vector<float> brut;
    switch ((QAudioFormat::SampleFormat)format_direct) {
    case QAudioFormat::Int16: for (qsizetype i = 0; i + 1 < o.size(); i += 2) brut.push_back(*reinterpret_cast<const qint16 *>(o.constData() + i) / 32768.0f); break;
    case QAudioFormat::Int32: for (qsizetype i = 0; i + 3 < o.size(); i += 4) brut.push_back(*reinterpret_cast<const qint32 *>(o.constData() + i) / 2147483648.0f); break;
    case QAudioFormat::Float: for (qsizetype i = 0; i + 3 < o.size(); i += 4) brut.push_back(*reinterpret_cast<const float *>(o.constData() + i)); break;
    case QAudioFormat::UInt8: for (qsizetype i = 0; i < o.size(); i++) brut.push_back(((unsigned char)o[i] - 128) / 128.0f); break;
    default: return;
    }
    std::vector<float> pcm;
    pcm_en_16k(brut, frequence_directe, canaux_directs, &pcm);
    direct->ajouter(pcm);
}

void EnregistreurSon::nourrir_direct(const std::vector<float> &pcm16k) {
    if (!direct) direct = std::make_shared<Dictee>();
    direct->ajouter(pcm16k);
    lancer_passe();
}

void EnregistreurSon::montrer_direct() {
    if (!direct) return;
    const QString t = direct->texte();
    if (!t.isEmpty()) message_->setText("<i>" + t.toHtmlEscaped() + "</i>");
}

void EnregistreurSon::lancer_passe() {
    if (!direct || passe_en_cours || direct->en_retard()) return;   // un segment fermé d'abord : c'est le texte final
    std::vector<float> morceau;
    size_t debut = 0, fin = 0;
    if (!direct->passe(&morceau, &debut, &fin)) return;
    passe_en_cours = true;
    QPointer<EnregistreurSon> moi(this);
    const std::shared_ptr<Dictee> dictee = direct;
    const QString modele = modele_de_transcription();
    std::thread([moi, dictee, morceau = std::move(morceau), debut, modele]() mutable {
        QString probleme;
        const QString texte = transcrire_pcm(std::move(morceau), modele, &probleme, true);
        QMetaObject::invokeMethod(qApp, [moi, dictee, texte, debut] {
            if (!moi) return;
            moi->passe_en_cours = false;
            if (moi->direct != dictee) return;   // arrêté entre-temps
            dictee->recevoir(texte, debut);
            moi->montrer_direct();
        }, Qt::QueuedConnection);
    }).detach();
}

void EnregistreurSon::figer_direct() {
    if (minuterie_directe) { minuterie_directe->stop(); minuterie_directe->deleteLater(); minuterie_directe = nullptr; }
    if (source_directe) {
        lire_direct();   // les derniers mots, encore dans le tampon du micro
        source_directe->stop();
        source_directe->deleteLater();
        source_directe = nullptr;
    }
    flux_direct = nullptr;
}

void EnregistreurSon::arreter_direct() {
    figer_direct();
    if (direct) {
        direct->abandonner();
        direct.reset();
        if (message_ && message_->text().startsWith("<i>")) message_->clear();
    }
}

QString EnregistreurSon::texte_direct() const { return direct ? direct->texte() : QString(); }
