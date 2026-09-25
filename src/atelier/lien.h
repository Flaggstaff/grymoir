// GrymoiR : l'atelier, dialogue d'un lien entre deux entités (A2-b). Chaque sorte de relation correspond
// à une tournure du langage : le dialogue ne propose que ce que GrymoiR sait dire (docs/atelier.md, § 5).
#ifndef GRYM_ATELIER_LIEN_H
#define GRYM_ATELIER_LIEN_H

#include "reecriture.h"

class QWidget;

// Demande le nom et la sorte d'un lien de `de` vers `vers`. `champ` arrive prérempli (modification) ou vide
// (création), et repart rempli si l'utilisateur confirme. Faux s'il renonce.
bool demander_lien(QWidget *parent, const QString &de, bool de_feminin, const QString &vers, bool vers_feminin,
                   ChampVoulu *champ, bool modification);

// La sorte d'un lien, en toutes lettres (« plusieurs œuvres pour un compositeur »), pour les messages et les essais.
QString sorte_de_lien(const QString &de, bool de_feminin, const QString &vers, bool vers_feminin, const ChampVoulu &c);

#endif
