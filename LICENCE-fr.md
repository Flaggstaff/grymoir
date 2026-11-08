# Licence de GrymoiR

Copyright © 2026 Alain Thiémard (Flaggstaff).

GrymoiR (le langage, `grym`, l'atelier, la documentation et les essais de ce dépôt) est distribué sous la **licence
publique de l'Union européenne, version 1.2 seulement** (EUPL v. 1.2), avec l'exception ci-dessous.

## Le texte de la licence

Le fichier `LICENCE` reproduit le texte anglais de l'EUPL v. 1.2, tel que le publie la liste SPDX
(https://spdx.org/licenses/EUPL-1.2.html). La version française a la même valeur : l'article 13 de la licence dit que
toutes les versions linguistiques approuvées par la Commission européenne ont une valeur identique, et que chaque partie
choisit la sienne. Le texte français officiel est l'annexe de la décision d'exécution (UE) 2017/863 de la Commission
du 18 mai 2017 (JO L 128 du 19.5.2017, p. 59 à 64) :

- https://eur-lex.europa.eu/eli/dec_impl/2017/863 (choisir « FR ») ;
- https://interoperable-europe.ec.europa.eu/sites/default/files/custom-page/attachment/eupl_v1.2_fr.pdf.

En résumé, sans valeur juridique : chacun peut utiliser, étudier, copier, modifier et distribuer GrymoiR ; qui distribue
une version modifiée de GrymoiR la distribue sous l'EUPL (ou une licence compatible de son annexe) et en publie le code
source.

## Exception : les applications écrites en GrymoiR

Un programme écrit en GrymoiR appartient à son auteur, qui le distribue sous la licence de son choix, ouverte ou non.
Cela vaut pour ses sources (`.grym`, `.grymc`), pour leur forme compilée (`.grymb`) et pour l'application fabriquée par
l'atelier, même quand elle embarque la machine de GrymoiR, ses bibliothèques et ses ressources (polices, icônes,
documentation) pour s'exécuter.

En complément de l'EUPL, et sans rien retirer à ce qu'elle accorde, le titulaire des droits sur GrymoiR permet donc de
distribuer une application ainsi fabriquée sous d'autres conditions que l'EUPL, à deux conditions :

1. les parties de GrymoiR embarquées n'ont pas été modifiées ; une version modifiée de GrymoiR reste soumise à l'EUPL ;
2. l'application dit qu'elle contient GrymoiR, sous l'EUPL v. 1.2, et où se trouve son code source (ce dépôt).

Une bibliothèque GrymoiR (`bibliothèques/`, grammaire § 21.9) et le code natif d'une fonction C (§ 21.10) sont des
programmes au sens de cette exception : leur auteur choisit leur licence. Le fichier `src/grym_natif.h`, qu'une
bibliothèque native inclut, peut être copié dans son code sous la même exception.

## Ce qui n'est pas à GrymoiR

Les dossiers `vendor/` gardent la licence de leurs auteurs, indiquée dans chacun : SQLite et SQLite3MC, Monocypher,
QR Code generator, Mbed TLS (Apache-2.0), les polices Atkinson Hyperlegible (OFL 1.1), les icônes Lucide. L'atelier s'appuie sur Qt 6
(LGPL v3) et sur whisper.cpp (MIT) ; une application fabriquée qui les embarque respecte aussi leurs licences.
