// La séparation des voix d'un son (grammaire 3.40, « les voix de x ») : qui parle quand, par sherpa-onnx chargé à
// l'exécution. Voir voix.cpp.
#pragma once

#include <QByteArray>
#include <QString>

// Le dossier « voix » (la bibliothèque sherpa-onnx, onnxruntime, les deux modèles) : GRYMOIR_VOIX s'il est donné ; sinon à
// côté de l'application (Contents/Resources/voix sur Mac, voix/ ailleurs), dans le dossier de données de GrymoiR, ou
// celui que CMake a téléchargé. Vide si les modèles sont introuvables.
QString dossier_des_voix();

// Les voix d'un son : un tableau JSON [{"début", "fin", "voix"}], les voix numérotées 1, 2… dans l'ordre où chacune parle
// la première fois ; nombre : les personnes, 0 si inconnu. Vide avec *erreur si rien ne se fait.
QString separer_voix_son(const QByteArray &octets, int nombre, QString *erreur);
