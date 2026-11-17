// La séparation des voix d'un son (grammaire 3.40, « les voix de x ») : qui parle quand. sherpa-onnx (k2-fsa,
// Apache 2.0), par son API C, chargée à l'exécution (QLibrary) : l'atelier se construit et tourne sans elle ; sans elle,
// « les voix de x » échoue en le disant. Les modèles : la segmentation de pyannote 3.0 (Hervé Bredin, MIT), exportée en
// ONNX par sherpa-onnx, et l'empreinte vocale CAM++ de 3D-Speaker, entraînée sur VoxCeleb (des voix de nombreuses
// langues). Le tout dans un dossier « voix » : la bibliothèque, onnxruntime à côté d'elle, et les deux modèles.
#include "voix.h"
#include "transcription.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QStandardPaths>
#include <QThread>
#include <cmath>
#include <cstring>
#include <map>

extern "C" {
#include "json.h"
#include "texte.h"
}

// Les structures de l'API C de sherpa-onnx v2.0.1 (c-api.h), recopiées pour ne pas dépendre de ses en-têtes : la
// bibliothèque est épinglée à cette version (CMakeLists.txt).
namespace {
struct ConfigEmpreinte {
    const char *model;
    int32_t num_threads;
    int32_t debug;
    const char *provider;
};
struct ConfigPyannote {
    const char *model;
    float window_shift_ratio;
};
struct ConfigSegmentation {
    ConfigPyannote pyannote;
    int32_t num_threads;
    int32_t debug;
    const char *provider;
};
struct ConfigRegroupement {
    int32_t num_clusters;
    float threshold;
    int32_t compute_confidence;
};
struct ConfigSeparation {
    ConfigSegmentation segmentation;
    ConfigEmpreinte embedding;
    ConfigRegroupement clustering;
    float min_duration_on;
    float min_duration_off;
};
struct Passage {
    float start, end;
    int32_t speaker;
    float confidence;
};
using FCreer = const void *(*)(const ConfigSeparation *);
using FDetruire = void (*)(const void *);
using FFrequence = int32_t (*)(const void *);
using FTraiter = const void *(*)(const void *, const float *, int32_t);
using FNombre = int32_t (*)(const void *);
using FTrier = const Passage *(*)(const void *);
using FDetruirePassages = void (*)(const Passage *);
using FDetruireResultat = void (*)(const void *);

QMutex verrou;
QLibrary *bibliotheque = nullptr;
}

static const char *const SEGMENTATION = "pyannote-segmentation-3-0.onnx";
static const char *const EMPREINTE = "3dspeaker_speech_campplus_sv_en_voxceleb_16k.onnx";

QString dossier_des_voix() {
    const QString impose = qEnvironmentVariable("GRYMOIR_VOIX");
    QStringList candidats;
    if (!impose.isEmpty()) candidats << impose;
    else {
        const QString app = QCoreApplication::applicationDirPath();
        candidats << app + "/../Resources/voix" << app + "/voix" << app + "/../share/grymoir/voix";
#ifdef Q_OS_MACOS
        candidats << QDir::homePath() + "/Library/Application Support/GrymoiR/voix";
#endif
        candidats << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/GrymoiR/voix";
#ifdef GRYMOIR_VOIX_CONSTRUCTION
        candidats << QString::fromUtf8(GRYMOIR_VOIX_CONSTRUCTION);   // téléchargé par CMake, pour l'atelier construit ici
#endif
    }
    for (const QString &c : candidats)
        if (QFileInfo(c + "/" + SEGMENTATION).isFile() && QFileInfo(c + "/" + EMPREINTE).isFile())
            return QFileInfo(c).absoluteFilePath();
    return QString();
}

QString separer_voix_son(const QByteArray &octets, int nombre, QString *erreur) {
    const QString dossier = dossier_des_voix();
    if (dossier.isEmpty()) {
        *erreur = "les modèles des voix sont introuvables (dossier « voix » : " + QString(SEGMENTATION) + ", " + EMPREINTE + ").";
        return QString();
    }
    std::vector<float> pcm;
    if (!wav_en_16k(octets, &pcm) && !decoder_en_16k(octets, &pcm, erreur)) return QString();
    QMutexLocker garde(&verrou);
    if (!bibliotheque) {
        bibliotheque = new QLibrary(dossier + "/sherpa-onnx-c-api");
        if (!bibliotheque->load()) bibliotheque->setFileName(dossier + "/libsherpa-onnx-c-api");
        if (!bibliotheque->isLoaded() && !bibliotheque->load()) {
            *erreur = "la bibliothèque sherpa-onnx ne se charge pas : " + bibliotheque->errorString();
            delete bibliotheque;
            bibliotheque = nullptr;
            return QString();
        }
    }
    auto creer = reinterpret_cast<FCreer>(bibliotheque->resolve("SherpaOnnxCreateOfflineSpeakerDiarization"));
    auto detruire = reinterpret_cast<FDetruire>(bibliotheque->resolve("SherpaOnnxDestroyOfflineSpeakerDiarization"));
    auto frequence = reinterpret_cast<FFrequence>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationGetSampleRate"));
    auto traiter = reinterpret_cast<FTraiter>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationProcess"));
    auto nb_passages = reinterpret_cast<FNombre>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments"));
    auto trier = reinterpret_cast<FTrier>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime"));
    auto detruire_passages = reinterpret_cast<FDetruirePassages>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationDestroySegment"));
    auto detruire_resultat = reinterpret_cast<FDetruireResultat>(bibliotheque->resolve("SherpaOnnxOfflineSpeakerDiarizationDestroyResult"));
    if (!creer || !detruire || !frequence || !traiter || !nb_passages || !trier || !detruire_passages || !detruire_resultat) {
        *erreur = "la bibliothèque sherpa-onnx n'a pas les fonctions attendues (version 2.0.1).";
        return QString();
    }
    const QByteArray seg = (dossier + "/" + SEGMENTATION).toUtf8(), emp = (dossier + "/" + EMPREINTE).toUtf8();
    ConfigSeparation c;
    std::memset(&c, 0, sizeof c);
    const int fils = qMin(4, qMax(1, QThread::idealThreadCount()));
    c.segmentation.pyannote.model = seg.constData();
    c.segmentation.num_threads = fils;
    c.segmentation.provider = "cpu";
    c.embedding.model = emp.constData();
    c.embedding.num_threads = fils;
    c.embedding.provider = "cpu";
    // le nombre de personnes, s'il est connu (ce que sherpa-onnx conseille) ; sinon, le seuil de ses exemples
    c.clustering.num_clusters = nombre > 0 ? nombre : 0;
    c.clustering.threshold = 0.5f;
    c.min_duration_on = 0.3f;
    c.min_duration_off = 0.5f;
    const void *sd = creer(&c);
    if (!sd) {
        *erreur = "les modèles des voix ne se chargent pas (" + dossier + ").";
        return QString();
    }
    if (const int hz = frequence(sd); hz != 16000) {
        detruire(sd);
        *erreur = "les modèles des voix attendent un son à " + QString::number(hz) + " Hz.";
        return QString();
    }
    const void *r = traiter(sd, pcm.data(), (int32_t)pcm.size());
    if (!r) {
        detruire(sd);
        *erreur = "la séparation des voix a échoué.";
        return QString();
    }
    const int n = nb_passages(r);
    const Passage *p = n > 0 ? trier(r) : nullptr;
    // les voix numérotées dans l'ordre où chacune parle la première fois : « voix 1 » ouvre l'entretien
    std::map<int32_t, int> rang;
    QString json = "[";
    for (int i = 0; i < n && p; i++) {
        if (!rang.count(p[i].speaker)) {
            const int suivant = (int)rang.size() + 1;
            rang[p[i].speaker] = suivant;
        }
        if (i) json += ",";
        json += QString("{\"début\":%1,\"fin\":%2,\"voix\":%3}")
                    .arg(QString::number(std::round((double)p[i].start * 100) / 100, 'g', 12),
                         QString::number(std::round((double)p[i].end * 100) / 100, 'g', 12), QString::number(rang[p[i].speaker]));
    }
    if (p) detruire_passages(p);
    detruire_resultat(r);
    detruire(sd);
    return json + "]";
}
