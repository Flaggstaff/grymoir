#include "transcription.h"
#include <cmath>
#include <algorithm>

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>
#include <QtEndian>
#include <cstring>

#include "whisper.h"

static const char *const NOM_MODELE = "ggml-large-v3-turbo-q5_0.bin";

QString modele_de_transcription() {
    // GRYMOIR_MODELE_TRANSCRIPTION : un autre modèle, pour un essai, sans rien reconstruire
    const QString impose = qEnvironmentVariable("GRYMOIR_MODELE_TRANSCRIPTION");
    if (!impose.isEmpty()) return QFileInfo(impose).isFile() ? QFileInfo(impose).absoluteFilePath() : QString();
    const QString app = QCoreApplication::applicationDirPath();
    QStringList candidats = {app + "/../Resources/modeles/" + NOM_MODELE, app + "/modeles/" + NOM_MODELE,
                             app + "/../share/grymoir/modeles/" + NOM_MODELE};
#ifdef Q_OS_MACOS
    candidats << QDir::homePath() + "/Library/Application Support/GrymoiR/modeles/" + NOM_MODELE;
#endif
    const QString donnees = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    candidats << donnees + "/GrymoiR/modeles/" + NOM_MODELE;
    for (const QString &c : candidats)
        if (QFileInfo(c).isFile()) return QFileInfo(c).absoluteFilePath();
    return QString();
}

// Ramène des échantillons à 16 kHz par interpolation linéaire (la parole n'en souffre pas pour Whisper).
static void reechantillonner(const std::vector<float> &e, int frequence, std::vector<float> *pcm) {
    pcm->clear();
    if (frequence == 16000) { *pcm = e; return; }
    const double pas = frequence / 16000.0;
    const size_t n = (size_t)(e.size() / pas);
    pcm->reserve(n);
    for (size_t i = 0; i < n; i++) {
        const double x = i * pas;
        const size_t a = (size_t)x;
        const double f = x - a;
        const float u = e[a], v = a + 1 < e.size() ? e[a + 1] : e[a];
        pcm->push_back((float)(u + (v - u) * f));
    }
}

bool wav_en_16k(const QByteArray &o, std::vector<float> *pcm) {
    if (o.size() < 44 || std::memcmp(o.constData(), "RIFF", 4) != 0 || std::memcmp(o.constData() + 8, "WAVE", 4) != 0) return false;
    const auto *d = reinterpret_cast<const uchar *>(o.constData());
    int format = 0, canaux = 0, frequence = 0, bits = 0;
    const uchar *donnees = nullptr;
    qsizetype taille = 0;
    for (qsizetype p = 12; p + 8 <= o.size();) {   // les morceaux : « fmt », puis « data »
        const quint32 t = qFromLittleEndian<quint32>(d + p + 4);
        if (std::memcmp(d + p, "fmt ", 4) == 0 && t >= 16 && p + 8 + 16 <= o.size()) {
            format = qFromLittleEndian<quint16>(d + p + 8);
            canaux = qFromLittleEndian<quint16>(d + p + 10);
            frequence = (int)qFromLittleEndian<quint32>(d + p + 12);
            bits = qFromLittleEndian<quint16>(d + p + 22);
            if (format == 0xFFFE && t >= 40) format = qFromLittleEndian<quint16>(d + p + 32);   // WAVE_FORMAT_EXTENSIBLE
        } else if (std::memcmp(d + p, "data", 4) == 0) {
            donnees = d + p + 8;
            taille = qMin<qsizetype>(t, o.size() - p - 8);
            break;
        }
        p += 8 + t + (t & 1);
    }
    if (!donnees || canaux < 1 || frequence < 8000 || !((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
                                                       || (format == 3 && bits == 32)))
        return false;
    const int octets = bits / 8;
    const qsizetype trames = taille / (octets * canaux);
    std::vector<float> mono((size_t)trames);
    for (qsizetype i = 0; i < trames; i++) {
        float somme = 0;
        for (int c = 0; c < canaux; c++) {
            const uchar *s = donnees + (i * canaux + c) * octets;
            float x;
            if (format == 3) { float f; std::memcpy(&f, s, 4); x = f; }
            else if (bits == 8) x = (s[0] - 128) / 128.0f;
            else if (bits == 16) x = qFromLittleEndian<qint16>(s) / 32768.0f;
            else if (bits == 24) x = ((qint32)((s[0] << 8) | (s[1] << 16) | (s[2] << 24)) >> 8) / 8388608.0f;
            else x = qFromLittleEndian<qint32>(s) / 2147483648.0f;
            somme += x;
        }
        mono[(size_t)i] = somme / canaux;
    }
    reechantillonner(mono, frequence, pcm);
    return true;
}

// L'extension d'un son, d'après ses premiers octets : le décodeur de macOS (AVFoundation) ne lit un fichier que s'il porte
// la bonne ; sans elle, « Could not load media source's tracks » (24 octobre 2026)
QString extension_de_son(const QByteArray &o) {
    if (o.size() >= 12 && o.mid(4, 4) == "ftyp") return "m4a";
    if (o.startsWith("RIFF") && o.mid(8, 4) == "WAVE") return "wav";
    if (o.startsWith("OggS")) return "ogg";
    if (o.startsWith("fLaC")) return "flac";
    if (o.startsWith("\x1A\x45\xDF\xA3")) return "webm";
    if (o.startsWith("ID3") || (o.size() >= 2 && (uchar)o[0] == 0xFF && ((uchar)o[1] & 0xE0) == 0xE0)) return "mp3";
    return "son";
}

bool decoder_en_16k(const QByteArray &octets, std::vector<float> *pcm, QString *erreur) {
    // le décodeur lit un fichier, qui porte l'extension de son format
    QTemporaryFile f(QDir::tempPath() + "/grymoir-son-XXXXXX." + extension_de_son(octets));
    if (!f.open() || f.write(octets) != octets.size()) { *erreur = "Le son ne peut pas être préparé."; return false; }
    f.flush();
    QAudioDecoder d;
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);
    d.setAudioFormat(format);
    d.setSource(QUrl::fromLocalFile(QFileInfo(f.fileName()).absoluteFilePath()));
    pcm->clear();
    QEventLoop boucle;
    bool rate = false;
    QObject::connect(&d, &QAudioDecoder::bufferReady, &boucle, [&] {
        const QAudioBuffer b = d.read();
        if (b.format().sampleFormat() != QAudioFormat::Float || b.format().channelCount() != 1 || b.format().sampleRate() != 16000) {
            rate = true;   // le décodeur n'a pas converti : on ne devine pas
            boucle.quit();
            return;
        }
        const float *p = b.constData<float>();
        pcm->insert(pcm->end(), p, p + b.frameCount());
    });
    QObject::connect(&d, &QAudioDecoder::finished, &boucle, &QEventLoop::quit);
    QObject::connect(&d, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &boucle, [&](QAudioDecoder::Error) {
        *erreur = "Ce son ne se décode pas : " + d.errorString();
        rate = true;
        boucle.quit();
    });
    d.start();
    boucle.exec();
    if (rate && erreur->isEmpty()) *erreur = "Ce son ne se convertit pas en 16 kHz mono.";
    return !rate && !pcm->empty();
}

QString nettoyer_transcription(const QString &texte) {
    static const QRegularExpression regles[] = {
        QRegularExpression(R"(\[\s*Propos inaudibles\s*\])", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"(\[\s*(Musique|Applaudissements|Rires|Silence|Bruit[^\]]*)\s*\])", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"(\[?\s*Sous-titr(?:es|age)[^\]\n]*?(?:Amara\.org|Radio-Canada|ST.? ?501)[^\]\n]*\]?)",
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"(Merci d.avoir regard[ée] (cette vidéo|la vidéo)[^.\n]*\.?)", QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(R"((N.oubliez pas de )?(vous )?abonnez?-vous[^.\n]*\.?)", QRegularExpression::CaseInsensitiveOption)};
    QString t = texte;
    for (const auto &r : regles) t.remove(r);
    QStringList lignes;
    for (const QString &l : t.split('\n'))
        if (!l.trimmed().isEmpty()) lignes << l.trimmed();
    return lignes.join('\n');
}

// Le modèle gardé entre deux transcriptions. Il doit être libéré avant la fin du programme : sur Mac, Metal (la puce
// graphique, dont whisper.cpp se sert) vérifie en se fermant qu'aucune ressource n'est plus réservée, et arrête le
// programme sinon. qAddPostRoutine le libère à la fermeture de l'application, avant cet inventaire.
static QMutex verrou;
static whisper_context *ctx = nullptr;
static QString charge;

void liberer_transcription() {
    QMutexLocker garde(&verrou);
    if (ctx) whisper_free(ctx);
    ctx = nullptr;
    charge.clear();
}

bool modele_charge() {
    QMutexLocker garde(&verrou);
    return ctx != nullptr;
}

QString transcrire_son(const QByteArray &octets, const QString &modele, QString *erreur) {
    if (modele.isEmpty()) {
        *erreur = "Le modèle de transcription est introuvable (ggml-large-v3-turbo-q5_0.bin).";
        return QString();
    }
    std::vector<float> pcm;
    if (!wav_en_16k(octets, &pcm) && !decoder_en_16k(octets, &pcm, erreur)) return QString();
    return transcrire_pcm(std::move(pcm), modele, erreur, false);
}

QString transcrire_pcm(std::vector<float> pcm, const QString &modele, QString *erreur, bool rapide) {
    if (modele.isEmpty()) {
        *erreur = "Le modèle de transcription est introuvable (ggml-large-v3-turbo-q5_0.bin).";
        return QString();
    }
    // un enregistrement faible (micro peu sensible, casque) : ramené à un niveau que Whisper entend, au plus vingt fois plus
    // fort ; le son gardé dans la fiche ne change pas (24 octobre 2026)
    float crete = 0;
    for (const float x : pcm) crete = std::max(crete, std::fabs(x));
    if (crete > 0 && crete < 0.5f) {
        const float gain = std::min(0.9f / crete, 20.0f);
        for (float &x : pcm) x *= gain;
    }
    // le modèle, chargé une fois (environ un gigaoctet en mémoire), gardé pour les transcriptions suivantes
    QMutexLocker garde(&verrou);
    if (!ctx || charge != modele) {
        whisper_log_set([](enum ggml_log_level, const char *, void *) {}, nullptr);   // pas de journal dans une application
        if (ctx) whisper_free(ctx);
        whisper_context_params cp = whisper_context_default_params();
        ctx = whisper_init_from_file_with_params(modele.toUtf8().constData(), cp);
        charge = ctx ? modele : QString();
        static bool inscrit = false;   // une seule fois : libérer à la fermeture de l'application
        if (ctx && !inscrit && QCoreApplication::instance()) {
            qAddPostRoutine(liberer_transcription);
            inscrit = true;
        }
        if (!ctx) { *erreur = "Le modèle de transcription ne se charge pas : " + modele; return QString(); }
    }
    // en direct (24 octobre 2026) : la recherche gloutonne, plus rapide ; le texte final reprend la recherche en faisceau
    whisper_full_params p = whisper_full_default_params(rapide ? WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH);
    p.language = "fr";
    p.translate = false;
    p.n_max_text_ctx = 0;   // « -mc 0 » : sans s'appuyer sur son propre texte, il ne s'y enferme pas
    p.n_threads = qMin(4, qMax(1, QThread::idealThreadCount()));
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.print_special = false;
    if (whisper_full(ctx, p, pcm.data(), (int)pcm.size()) != 0) {
        *erreur = "La transcription a échoué.";
        return QString();
    }
    // le texte, mot par mot, avec ses doutes : un mot commence par un morceau précédé d'une espace ; sa probabilité est
    // celle de son morceau le moins sûr ; un passage entièrement inventé (« [Propos inaudibles] »…) est écarté
    const whisper_token fin = whisper_token_eot(ctx);
    QStringList passages;
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; i++) {
        if (nettoyer_transcription(QString::fromUtf8(whisper_full_get_segment_text(ctx, i))).trimmed().isEmpty()) continue;
        QString passage;
        QByteArray mot;   // en octets : un caractère accentué peut tenir sur deux morceaux
        float sur = 1;
        auto finir = [&] {
            if (mot.isEmpty()) return;
            QString m = QString::fromUtf8(mot);
            const qsizetype debut = m.startsWith(' ') ? 1 : 0;   // l'espace avant le mot reste hors du doute
            if (sur < 0.5f && !m.trimmed().isEmpty())
                m = m.left(debut) + QChar(DEBUT_DOUTE) + m.mid(debut) + QChar(FIN_DOUTE);
            passage += m;
            mot.clear();
            sur = 1;
        };
        for (int j = 0; j < whisper_full_n_tokens(ctx, i); j++) {
            const whisper_token_data d = whisper_full_get_token_data(ctx, i, j);
            if (d.id >= fin) continue;   // les jetons spéciaux (fin, horodatage…)
            const QByteArray t = whisper_full_get_token_text(ctx, i, j);
            if (t.startsWith(' ')) finir();
            mot += t;
            sur = qMin(sur, d.p);
        }
        finir();
        if (!passage.trimmed().isEmpty()) passages << passage.trimmed();
    }
    return passages.isEmpty() ? QString() : QString(QChar(MARQUE_TRANSCRIT)) + passages.join('\n');
}

// --- La transcription en direct (24 octobre 2026) ---

void pcm_en_16k(const std::vector<float> &entree, int frequence, int canaux, std::vector<float> *pcm) {
    std::vector<float> mono;
    if (canaux <= 1) mono = entree;
    else {
        mono.reserve(entree.size() / (size_t)canaux);
        for (size_t i = 0; i + (size_t)canaux <= entree.size(); i += (size_t)canaux) {
            float x = 0;
            for (int c = 0; c < canaux; c++) x += entree[i + (size_t)c];
            mono.push_back(x / (float)canaux);
        }
    }
    if (frequence == 16000) { pcm->insert(pcm->end(), mono.begin(), mono.end()); return; }
    std::vector<float> r;
    reechantillonner(mono, frequence, &r);
    pcm->insert(pcm->end(), r.begin(), r.end());
}

void TranscriptionDirecte::ajouter(const std::vector<float> &pcm16k) { son.insert(son.end(), pcm16k.begin(), pcm16k.end()); }

bool TranscriptionDirecte::passe(std::vector<float> *morceau, size_t *fin) const {
    if (son.size() < debut + (size_t)(16000 * 1.5)) return false;   // moins d'une seconde et demie : rien à dire encore
    *fin = son.size();
    morceau->assign(son.begin() + (long)debut, son.begin() + (long)*fin);
    return true;
}

void TranscriptionDirecte::recevoir(const QString &texte, size_t fin) {
    QString t = texte;
    t.remove(QChar(MARQUE_TRANSCRIT)).remove(QChar(DEBUT_DOUTE)).remove(QChar(FIN_DOUTE));
    t = t.simplified();
    if (fin - debut >= (size_t)(16000 * fenetre_secondes)) {
        // une fenêtre pleine : son texte est acquis, la suite repart de là (Whisper ne voit que trente secondes à la fois)
        if (!t.isEmpty()) acquis = acquis.isEmpty() ? t : acquis + " " + t;
        debut = fin;
        courant.clear();
    } else {
        courant = t;
    }
}

QString TranscriptionDirecte::texte() const {
    if (acquis.isEmpty()) return courant;
    return courant.isEmpty() ? acquis : acquis + " " + courant;
}
