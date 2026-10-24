// L'accord du micro hors de macOS (voir micro.h) : la permission de Qt quand elle existe, sinon l'accord.
#include "micro.h"
#include <QCoreApplication>
#include <QPointer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#  if QT_CONFIG(permissions)
#    include <QPermission>
#    define GRYM_PERMISSION_QT 1
#  endif
#endif

EtatMicro etat_micro() {
#ifdef GRYM_PERMISSION_QT
    switch (qApp->checkPermission(QMicrophonePermission())) {
    case Qt::PermissionStatus::Undetermined: return EtatMicro::Indecis;
    case Qt::PermissionStatus::Denied: return EtatMicro::Refuse;
    case Qt::PermissionStatus::Granted: break;
    }
#endif
    return EtatMicro::Accorde;
}

void demander_micro(QObject *contexte, std::function<void(EtatMicro)> fin) {
#ifdef GRYM_PERMISSION_QT
    qApp->requestPermission(QMicrophonePermission(), contexte, [fin](const QPermission &p) {
        fin(p.status() == Qt::PermissionStatus::Granted ? EtatMicro::Accorde : EtatMicro::Refuse);
    });
#else
    QPointer<QObject> garde(contexte);
    if (garde) fin(EtatMicro::Accorde);
#endif
}
