// L'accord du micro sous macOS : AVFoundation (voir micro.h).
// AVFoundation d'abord : ses en-têtes passent avant les mots-clés de Qt (emit, slots)
#import <AVFoundation/AVFoundation.h>
#include "micro.h"
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

EtatMicro etat_micro() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized: return EtatMicro::Accorde;
    case AVAuthorizationStatusNotDetermined: return EtatMicro::Indecis;
    default: return EtatMicro::Refuse;   // refusé, ou restreint par un profil
    }
}

void demander_micro(QObject *contexte, std::function<void(EtatMicro)> fin) {
    QPointer<QObject> garde(contexte);
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL accorde) {
        const EtatMicro e = accorde ? EtatMicro::Accorde : EtatMicro::Refuse;
        QMetaObject::invokeMethod(qApp, [garde, fin, e]() { if (garde) fin(e); }, Qt::QueuedConnection);
    }];
}
