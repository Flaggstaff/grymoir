#include "transcription.h"
#include <cmath>
#include <algorithm>

#include <QAudioBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QPair>
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
#include <cstdio>
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

// À la fermeture de l'application, plus aucune transcription ne commence : un fil encore au travail (une dictée par
// segments, une zone en attente) rechargerait le modèle après sa libération, et Metal arrêterait le programme.
static bool fermeture = false;   // lu et écrit sous le verrou

void liberer_transcription() {
    QMutexLocker garde(&verrou);
    if (ctx) whisper_free(ctx);
    ctx = nullptr;
    charge.clear();
}

static void fermer_transcription() {
    { QMutexLocker garde(&verrou); fermeture = true; }
    liberer_transcription();
}

bool modele_charge() {
    QMutexLocker garde(&verrou);
    return ctx != nullptr;
}

static std::shared_ptr<Dictee> dictee_promise(const QByteArray &octets);

QString transcrire_son(const QByteArray &octets, const QString &modele, QString *erreur, const QString &amorce) {
    if (modele.isEmpty()) {
        *erreur = "Le modèle de transcription est introuvable (ggml-large-v3-turbo-q5_0.bin).";
        return QString();
    }
    // une dictée déjà transcrite par segments, pendant qu'on parlait (12 novembre 2026) : son texte, sans refaire la passe ;
    // avec une amorce, la passe complète, qui seule en tient compte
    if (amorce.isEmpty()) {
        if (const std::shared_ptr<Dictee> d = dictee_promise(octets)) {
            QString t;
            if (d->attendre(&t) && !QString(t).remove(QChar(MARQUE_TRANSCRIT)).trimmed().isEmpty()) return t;
        }
    }
    std::vector<float> pcm;
    if (!wav_en_16k(octets, &pcm) && !decoder_en_16k(octets, &pcm, erreur)) return QString();
    return transcrire_pcm(std::move(pcm), modele, erreur, false, amorce);
}

QString transcrire_pcm(std::vector<float> pcm, const QString &modele, QString *erreur, bool rapide, const QString &amorce) {
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
    if (fermeture) { *erreur = "L'application se ferme."; return QString(); }
    if (!ctx || charge != modele) {
        whisper_log_set([](enum ggml_log_level, const char *, void *) {}, nullptr);   // pas de journal dans une application
        if (ctx) whisper_free(ctx);
        whisper_context_params cp = whisper_context_default_params();
        ctx = whisper_init_from_file_with_params(modele.toUtf8().constData(), cp);
        charge = ctx ? modele : QString();
        static bool inscrit = false;   // une seule fois : libérer à la fermeture de l'application
        if (ctx && !inscrit && QCoreApplication::instance()) {
            qAddPostRoutine(fermer_transcription);
            inscrit = true;
        }
        if (!ctx) { *erreur = "Le modèle de transcription ne se charge pas : " + modele; return QString(); }
    }
    // en direct (24 octobre 2026) : la recherche gloutonne, plus rapide ; le texte final reprend la recherche en faisceau
    whisper_full_params p = whisper_full_default_params(rapide ? WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH);
    // « en toute langue » (2.86) : l'amorce commence par U+001D ; Whisper reconnaît la langue parlée et l'écrit telle
    // quelle (« auto ») ; sinon, le français imposé, comme avant
    const bool toute_langue = amorce.startsWith(QChar(0x1d));
    p.language = toute_langue ? "auto" : "fr";
    p.translate = false;
    p.n_max_text_ctx = 0;   // « -mc 0 » : sans s'appuyer sur son propre texte, il ne s'y enferme pas
    p.n_threads = qMin(4, qMax(1, QThread::idealThreadCount()));
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.print_special = false;
    // L'amorce (2.80) : le vocabulaire, donné comme texte précédent à chaque fenêtre de 30 secondes (carry_initial_prompt).
    // whisper.cpp (v1.9.5) ne met aucun texte précédent si n_max_text_ctx vaut 0 : il vaut ici la taille de l'amorce,
    // plus la marque qui la précède, si bien que le texte déjà transcrit n'y entre toujours pas (« -mc 0 » tenu).
    const QByteArray amorce8 = (toute_langue ? amorce.mid(1) : amorce).trimmed().toUtf8();
    if (!amorce8.isEmpty()) {
        std::vector<whisper_token> jetons(1024);
        int n = whisper_tokenize(ctx, amorce8.constData(), jetons.data(), (int)jetons.size());
        if (n < 0) n = -n;   // trop long : coupé par whisper.cpp à la moitié du contexte de texte
        if (n > 0) {
            p.initial_prompt = amorce8.constData();
            p.carry_initial_prompt = true;
            p.n_max_text_ctx = n + 1;
        }
    }
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

// --- Le son du micro, en 16 kHz mono (24 octobre 2026) ---

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

// --- La dictée par segments (12 novembre 2026) ---

static const size_t TRAME = 320;   // 20 ms à 16 kHz

// Le seuil sous lequel une trame est une pause, en dB : huit décibels au-dessus du bruit de fond (le 2e centile des
// niveaux reçus, que le « Un instant… » du début donne presque toujours), et dix au moins sous la voix (le 90e centile).
// Sans contraste (quinze décibels entre les deux), ou avant une seconde de son, aucune pause ne se reconnaît : seule la
// coupe forcée joue, et rien n'est coupé au milieu d'une voix faible.
double Dictee::seuil() const {
    if (trames_comptees_ < 50) return -1000;
    auto centile = [this](double part) {
        const int rang = (int)(trames_comptees_ * part);
        int vus = 0;
        for (int i = 0; i <= 100; i++) {
            vus += histogramme_[i];
            if (vus > rang) return (double)(i - 100);
        }
        return 0.0;
    };
    const double fond = centile(0.02), voix = centile(0.90);
    if (voix - fond < 15) return -1000;
    return std::min(fond + 8, voix - 10);
}

void Dictee::ajouter(const std::vector<float> &pcm16k) {
    std::lock_guard<std::mutex> garde(verrou_);
    if (terminee_ || abandonnee_) return;
    son_.insert(son_.end(), pcm16k.begin(), pcm16k.end());
    total_ = son_.size();
    while ((niveaux_.size() + 1) * TRAME <= son_.size()) {
        const float *t = son_.data() + niveaux_.size() * TRAME;
        double somme = 0;
        for (size_t i = 0; i < TRAME; i++) somme += (double)t[i] * t[i];
        const double efficace = std::sqrt(somme / TRAME);
        const double db = efficace > 1e-5 ? std::max(-100.0, 20 * std::log10(efficace)) : -100.0;
        niveaux_.push_back((float)db);
        if (db > -90) {   // le silence numérique (un micro pas encore ouvert) ne dit rien du bruit de fond
            histogramme_[std::min(100, std::max(0, (int)std::lround(db + 100)))]++;
            trames_comptees_++;
        }
    }
    const size_t avant = segments_.size();
    couper();
    if (segments_.size() != avant) signal_.notify_all();
}

// Ferme les segments finis : à la première pause assez longue qui suit de la voix, en son milieu, à deux secondes au
// moins du début ; sinon, à la durée maximale, au creux le plus calme de ses dix dernières secondes.
void Dictee::couper() {
    const size_t pause = std::max<size_t>(2, (size_t)(pause_secondes * 50)), voix_requise = 15;
    const size_t minimum = (size_t)(minimum_secondes * 16000), maximum = (size_t)(maximum_secondes * 16000);
    for (;;) {
        const double s = seuil();
        const size_t premiere = (debut_ + TRAME - 1) / TRAME;
        size_t coupe = 0, voisees = 0, suite = 0;
        for (size_t f = premiere; f < niveaux_.size() && !coupe; f++) {
            const bool calme = niveaux_[f] < s || niveaux_[f] <= -90;
            if (!calme) { voisees++; suite = 0; continue; }
            suite++;
            if (suite < pause || voisees < voix_requise) continue;
            // au milieu de la pause exigée, jamais avant la durée minimale, et toujours avec une demi-pause de calme après
            const size_t voulue = std::max(f + 1 - suite + pause / 2, (debut_ + minimum + TRAME - 1) / TRAME);
            if (voulue + pause / 2 <= f + 1) coupe = voulue * TRAME;
        }
        if (!coupe && son_.size() - debut_ >= maximum) {
            const size_t de = (debut_ + std::max(minimum, maximum > 160000 ? maximum - 160000 : minimum)) / TRAME,
                         a = (debut_ + maximum) / TRAME;
            size_t meilleure = a;
            double plus_calme = 1e9;
            for (size_t f = de; f + 10 <= a && f + 10 <= niveaux_.size(); f++) {
                double somme = 0;
                for (size_t k = 0; k < 10; k++) somme += niveaux_[f + k];
                if (somme <= plus_calme) { plus_calme = somme; meilleure = f + 5; }   // à égalité, le plus tard
            }
            coupe = meilleure * TRAME;
        }
        if (!coupe || coupe <= debut_ || coupe > son_.size()) return;
        Segment g;
        g.debut = debut_;
        g.fin = coupe;
        g.apercu = courant_;
        segments_.push_back(g);
        courant_.clear();
        debut_ = coupe;
    }
}

void Dictee::finir() {
    std::lock_guard<std::mutex> garde(verrou_);
    if (terminee_ || abandonnee_) return;
    terminee_ = true;
    if (son_.size() > debut_) {
        const size_t minimum = (size_t)(minimum_secondes * 16000);
        // une fin trop courte pour être transcrite seule rejoint le segment d'avant, refait (trente secondes au plus :
        // ce que Whisper voit d'un coup)
        if (son_.size() - debut_ < minimum && !segments_.empty() && son_.size() - segments_.back().debut <= 29 * 16000) {
            Segment &g = segments_.back();
            g.fin = son_.size();
            g.fait = false;
            g.texte.clear();
            g.version++;
            if (!courant_.isEmpty()) g.apercu = g.apercu.isEmpty() ? courant_ : g.apercu + " " + courant_;
        } else {
            Segment g;
            g.debut = debut_;
            g.fin = son_.size();
            g.apercu = courant_;
            segments_.push_back(g);
        }
        courant_.clear();
        debut_ = son_.size();
    }
    signal_.notify_all();
}

void Dictee::abandonner() {
    std::lock_guard<std::mutex> garde(verrou_);
    abandonnee_ = true;
    signal_.notify_all();
}

void Dictee::echouer() {
    std::lock_guard<std::mutex> garde(verrou_);
    echec_ = true;
    signal_.notify_all();
}

bool Dictee::passe(std::vector<float> *morceau, size_t *debut, size_t *fin) const {
    std::lock_guard<std::mutex> garde(verrou_);
    if (terminee_ || abandonnee_ || son_.size() < debut_ + (size_t)(16000 * 1.5)) return false;
    *debut = debut_;
    *fin = son_.size();
    morceau->assign(son_.begin() + (long)debut_, son_.end());
    return true;
}

static QString sans_marques(const QString &texte) {
    QString t = texte;
    t.remove(QChar(MARQUE_TRANSCRIT)).remove(QChar(DEBUT_DOUTE)).remove(QChar(FIN_DOUTE));
    return t.simplified();
}

static QString sans_marques_par_ligne(const QString &texte) {
    QString t = texte;
    t.remove(QChar(MARQUE_TRANSCRIT)).remove(QChar(DEBUT_DOUTE)).remove(QChar(FIN_DOUTE));
    return t + "\n";
}

void Dictee::recevoir(const QString &texte, size_t debut) {
    std::lock_guard<std::mutex> garde(verrou_);
    const QString t = sans_marques(texte);
    if (debut == debut_ && !terminee_) { courant_ = t; return; }
    for (Segment &g : segments_)   // le segment s'est fermé pendant la passe : son aperçu, en attendant son texte
        if (g.debut == debut && !g.fait) g.apercu = t;
}

bool Dictee::segment_a_faire(int *rang, int *version, std::vector<float> *morceau) const {
    std::lock_guard<std::mutex> garde(verrou_);
    if (abandonnee_ || echec_) return false;
    for (size_t k = 0; k < segments_.size(); k++) {
        const Segment &g = segments_[k];
        if (g.fait) continue;
        *rang = (int)k;
        *version = g.version;
        morceau->assign(son_.begin() + (long)g.debut, son_.begin() + (long)g.fin);
        return true;
    }
    return false;
}

void Dictee::recevoir_segment(int rang, int version, const QString &texte) {
    std::lock_guard<std::mutex> garde(verrou_);
    if (rang < 0 || rang >= (int)segments_.size() || segments_[(size_t)rang].version != version) return;   // refait depuis
    Segment &g = segments_[(size_t)rang];
    g.texte = texte;
    g.fait = true;
    if (terminee_ && tout_fait()) { son_.clear(); son_.shrink_to_fit(); niveaux_.clear(); niveaux_.shrink_to_fit(); }
    signal_.notify_all();
}

bool Dictee::tout_fait() const {
    for (const Segment &g : segments_)
        if (!g.fait) return false;
    return true;
}

bool Dictee::en_retard() const {
    std::lock_guard<std::mutex> garde(verrou_);
    return !tout_fait();
}

bool Dictee::finie() const {
    std::lock_guard<std::mutex> garde(verrou_);
    return terminee_ && tout_fait();
}

QString Dictee::texte() const {
    std::lock_guard<std::mutex> garde(verrou_);
    QStringList parts;
    for (const Segment &g : segments_) {
        const QString t = g.fait ? sans_marques(g.texte) : g.apercu;
        if (!t.isEmpty()) parts << t;
    }
    if (!courant_.isEmpty()) parts << courant_;
    return parts.join(' ');
}

QString Dictee::texte_final() const {
    std::lock_guard<std::mutex> garde(verrou_);
    QStringList passages;
    for (const Segment &g : segments_) {
        QString t = g.texte;
        t.remove(QChar(MARQUE_TRANSCRIT));   // une seule marque, en tête du tout ; les doutes restent
        if (!t.trimmed().isEmpty()) passages << t.trimmed();
    }
    return passages.isEmpty() ? QString() : QString(QChar(MARQUE_TRANSCRIT)) + passages.join('\n');
}

bool Dictee::attendre(QString *texte) {
    {
        std::unique_lock<std::mutex> garde(verrou_);
        signal_.wait(garde, [this] { return abandonnee_ || echec_ || (terminee_ && tout_fait()); });
        if (abandonnee_ || echec_) return false;
    }
    *texte = texte_final();
    return true;
}

void Dictee::travailler(const QString &modele) {
    for (;;) {
        {
            std::unique_lock<std::mutex> garde(verrou_);
            signal_.wait(garde, [this] { return abandonnee_ || echec_ || !tout_fait() || terminee_; });
            if (abandonnee_ || echec_ || (terminee_ && tout_fait())) return;
        }
        int rang = 0, version = 0;
        std::vector<float> morceau;
        if (!segment_a_faire(&rang, &version, &morceau)) return;
        QString probleme;
        const QString texte = transcrire_pcm(std::move(morceau), modele, &probleme, false);
        if (!probleme.isEmpty()) { echouer(); return; }
        recevoir_segment(rang, version, texte);
    }
}

std::vector<std::pair<size_t, size_t>> Dictee::bornes() const {
    std::lock_guard<std::mutex> garde(verrou_);
    std::vector<std::pair<size_t, size_t>> b;
    for (const Segment &g : segments_) b.push_back({g.debut, g.fin});
    return b;
}

size_t Dictee::echantillons() const {
    std::lock_guard<std::mutex> garde(verrou_);
    return total_;
}

// Les dictées promises, par l'empreinte de leur enregistrement ; les huit dernières.
static QMutex verrou_promesses;
static QList<QPair<QByteArray, std::shared_ptr<Dictee>>> promesses;

void promettre_transcription(const QByteArray &octets, std::shared_ptr<Dictee> dictee) {
    if (!dictee || octets.isEmpty()) return;
    const QByteArray cle = QCryptographicHash::hash(octets, QCryptographicHash::Sha256);
    QMutexLocker garde(&verrou_promesses);
    promesses.append({cle, std::move(dictee)});
    while (promesses.size() > 8) promesses.removeFirst();
}

static std::shared_ptr<Dictee> dictee_promise(const QByteArray &octets) {
    {
        QMutexLocker garde(&verrou_promesses);
        if (promesses.isEmpty()) return nullptr;
    }
    const QByteArray cle = QCryptographicHash::hash(octets, QCryptographicHash::Sha256);
    QMutexLocker garde(&verrou_promesses);
    for (qsizetype k = promesses.size() - 1; k >= 0; k--)
        if (promesses[k].first == cle) return promesses[k].second;
    return nullptr;
}

bool dictee_par_segments() { return !qEnvironmentVariableIsSet("GRYMOIR_DICTEE_ENTIERE"); }

// Les mots d'un texte, pour les comparer : sans marques, sans ponctuation, en minuscules.
static QStringList mots_nus(const QString &texte) {
    QString t = sans_marques(texte).toLower();
    for (QChar &c : t)
        if (!c.isLetterOrNumber() && c != QChar('\'') && c != QChar(0x2019)) c = QChar(' ');
    return t.split(' ', Qt::SkipEmptyParts);
}

int comparer_dictee(const QString &chemin) {
    QFile f(chemin);
    if (!f.open(QIODevice::ReadOnly)) { std::fprintf(stderr, "« %s » ne s'ouvre pas.\n", qPrintable(chemin)); return 1; }
    const QByteArray octets = f.readAll();
    const QString modele = modele_de_transcription();
    if (modele.isEmpty()) { std::fprintf(stderr, "Le modèle de transcription est introuvable.\n"); return 1; }
    std::vector<float> pcm;
    QString probleme;
    if (!wav_en_16k(octets, &pcm) && !decoder_en_16k(octets, &pcm, &probleme)) {
        std::fprintf(stderr, "%s\n", qPrintable(probleme.isEmpty() ? QString::fromUtf8("Ce son ne se lit pas.") : probleme));
        return 1;
    }
    const double duree = pcm.size() / 16000.0;
    std::printf("Son : %.0f secondes.\n", duree);
    std::fflush(stdout);
    auto ecrire = [&](const QString &suffixe, const QString &texte) {
        QFile s(chemin + suffixe);
        if (s.open(QIODevice::WriteOnly)) s.write(sans_marques_par_ligne(texte).toUtf8());
        return s.fileName();
    };
    QElapsedTimer chrono;
    chrono.start();
    const QString bloc = transcrire_pcm(pcm, modele, &probleme, false);
    if (!probleme.isEmpty()) { std::fprintf(stderr, "%s\n", qPrintable(probleme)); return 1; }
    const double temps_bloc = chrono.elapsed() / 1000.0;
    std::printf("D'un bloc : %.1f s de calcul, écrit dans %s\n", temps_bloc, qPrintable(ecrire("-bloc.txt", bloc)));
    std::fflush(stdout);
    // par segments : le son arrive par dixièmes de seconde, comme du micro ; chaque segment est transcrit à sa fermeture
    Dictee d;
    chrono.restart();
    double dernier = 0, plus_long = 0;
    QStringList textes;   // le texte de chaque segment, pour la liste des coupes
    auto vider = [&](bool fin) {
        int rang = 0, version = 0;
        std::vector<float> morceau;
        while (d.segment_a_faire(&rang, &version, &morceau)) {
            QElapsedTimer un;
            un.start();
            plus_long = std::max(plus_long, morceau.size() / 16000.0);
            const QString t = transcrire_pcm(std::move(morceau), modele, &probleme, false);
            if (!probleme.isEmpty()) return false;
            d.recevoir_segment(rang, version, t);
            while (textes.size() <= rang) textes << QString();
            textes[rang] = sans_marques(t);
            if (fin) dernier = un.elapsed() / 1000.0;
            morceau.clear();
        }
        return true;
    };
    for (size_t i = 0; i < pcm.size(); i += 1600) {
        d.ajouter(std::vector<float>(pcm.begin() + (long)i, pcm.begin() + (long)std::min(pcm.size(), i + 1600)));
        if (!vider(false)) { std::fprintf(stderr, "%s\n", qPrintable(probleme)); return 1; }
    }
    d.finir();
    if (!vider(true)) { std::fprintf(stderr, "%s\n", qPrintable(probleme)); return 1; }
    const QString segments = d.texte_final();
    std::printf("Par segments : %d segments (le plus long : %.1f s), %.1f s de calcul en tout, %.1f s pour le dernier "
                "(l'attente à l'arrêt), écrit dans %s\n",
                (int)d.bornes().size(), plus_long, chrono.elapsed() / 1000.0, dernier, qPrintable(ecrire("-segments.txt", segments)));
    // les coupes : où chaque segment commence et finit, et ce qu'il a donné (pour voir une coupe mal tombée)
    {
        QString coupes;
        const auto bornes = d.bornes();
        for (size_t k = 0; k < bornes.size(); k++)
            coupes += QString("%1 à %2 s : %3\n").arg(bornes[k].first / 16000.0, 0, 'f', 2).arg(bornes[k].second / 16000.0, 0, 'f', 2)
                          .arg((qsizetype)k < textes.size() ? textes[(qsizetype)k] : QString());
        QFile c(chemin + "-coupes.txt");
        if (c.open(QIODevice::WriteOnly)) c.write(coupes.toUtf8());
        std::printf("Les coupes : %s\n", qPrintable(c.fileName()));
    }
    // l'écart : les mots communs aux deux textes, dans l'ordre (plus longue sous-suite commune)
    const QStringList a = mots_nus(bloc), b = mots_nus(segments);
    std::vector<int> avant((size_t)b.size() + 1, 0), ligne((size_t)b.size() + 1, 0);
    for (qsizetype i = 0; i < a.size(); i++) {
        for (qsizetype j = 0; j < b.size(); j++)
            ligne[(size_t)j + 1] = a[i] == b[j] ? avant[(size_t)j] + 1 : std::max(avant[(size_t)j + 1], ligne[(size_t)j]);
        std::swap(avant, ligne);
    }
    const int communs = avant[(size_t)b.size()];
    const qsizetype plus = std::max(a.size(), b.size());
    std::printf("Mots : %d d'un bloc, %d par segments, %d communs dans l'ordre ; écart : %.1f %%\n", (int)a.size(), (int)b.size(),
                communs, plus ? 100.0 * (double)(plus - communs) / (double)plus : 0.0);
    return 0;
}
