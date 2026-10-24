// L'accord du micro (24 octobre 2026). Sous macOS, AVFoundation directement (micro.mm) : la couche de permissions de Qt
// répondait « refusé » sans jamais poser la question, et l'application n'apparaissait pas dans Réglages Système, Micro.
// Ailleurs (micro.cpp) : la permission de Qt quand elle existe, sinon l'accord.
#pragma once
#include <QObject>
#include <functional>

enum class EtatMicro { Indecis, Accorde, Refuse };

EtatMicro etat_micro();
// pose la question une fois ; la réponse arrive dans le fil principal, si le contexte vit encore
void demander_micro(QObject *contexte, std::function<void(EtatMicro)> fin);
