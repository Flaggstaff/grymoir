# GrymoiR : le web

Conception décidée le 29 septembre 2026. W1 implémenté le 29 septembre 2026.
Référence : charte 1.53, art. 1, 4, 7, 11 et 12 ; grammaire 1.57, § 16 et § 22 ; `docs/ecrans.md` ; `docs/v2.md` ; `docs/atelier.md`.
Toute décision prise sur ce document passe dans la charte et la grammaire par une révision numérotée.

---

## 1. Thèse

Un programme GrymoiR doit pouvoir sortir sur le web de trois façons, selon le besoin :

1. **publié** : un dossier de pages fixes, tiré de la base, qu'on dépose chez n'importe quel hébergeur ;
2. **servi en local** : l'application dans le navigateur, sur la machine du développeur ou d'un poste de confiance ;
3. **servi en public** : un site ouvert, où des visiteurs se connectent et saisissent des données.

Ces trois sorties ne font pas trois modules. Elles partagent une seule description, les écrans d'A3 (grammaire, § 22), et un seul moteur de rendu HTML.

## 2. Principes

### 2.1 Le développeur n'écrit jamais de HTML

Il décrit ses écrans en GrymoiR ; `grym` en fait des pages. Conséquences :

- l'atelier continue de n'écrire que du GrymoiR (`docs/atelier.md`, § 2), et un écran dessiné dans l'atelier sort en Qt comme en HTML ;
- toute valeur affichée passe par le moteur de rendu, qui l'échappe. Un titre qui contient `<script>` s'affiche comme du texte : l'injection de code dans la page est impossible par construction, pas par vigilance.

### 2.2 Même thème partout

La phrase des écrans (`Les écrans ont la couleur verte et le logo « logo.png ».`) vaut pour le web. Le rendu HTML reprend les mêmes jetons de couleur, les mêmes polices (Atkinson Hyperlegible Next et Mono, sous licence OFL, embarquées dans le site) et les mêmes icônes que Qt. Clair ou sombre selon le système du visiteur.

### 2.3 Sans JavaScript nécessaire

Décision reprise de `docs/v2.md`, § 7 : chaque page fonctionne avec des liens et des formulaires ordinaires. Un script peut améliorer le confort, jamais conditionner le fonctionnement.

## 3. Le rendu HTML (commun aux trois sorties)

| Élément d'écran | Page |
|---|---|
| liste d'une recherche | tableau ; chaque ligne mène à l'objet si un événement `Quand on choisit` existe |
| `un bouton « … »` | bouton d'un formulaire envoyé en POST |
| `le texte « … »` | paragraphe |
| zone `(texte)`, `(nombre)` | champ de texte ; le nombre se vérifie côté `grym`, en décimal exact (la virgule suisse n'est pas un nombre pour un champ HTML numérique) |
| zone `(date)` | champ de texte au format `jour.mois.année`, vérifié par l'analyseur de dates |
| zone `(vrai ou faux)` | case à cocher |
| lien vers une entité | menu déroulant jusqu'à 1000 objets, ligne vide en tête ; texte à suggestions au-delà (décision de la v2.0) |
| `(fichier)`, `(image)` | envoi de fichier, 50 Mo au plus (décision de la v2.0-b) |
| `côte à côte :`, `l'un sous l'autre :` | grille CSS, qui passe l'un sous l'autre sur un écran étroit |
| fiche déduite (`Ouvrir la fiche de c.`) | page de l'objet, liens cliquables vers les objets désignés |

Écrans empilés : le navigateur montre l'écran du dessus, avec un fil qui rappelle les écrans ouverts dessous. `Fermer l'écran.` ramène au précédent.

### 3.1 Ce que W1 a fixé *(29 septembre 2026)*

- **Un écran, un formulaire.** Chaque bouton, chaque ligne de liste et la croix du titre envoient le même formulaire, zones comprises. `grym` en tire les événements dans l'ordre : d'abord un `Quand on change` par zone modifiée, dans l'ordre de l'écran, puis le clic, le choix ou la fermeture. Une zone refusée (« abc » dans un nombre) arrête là : le clic envoyé avec elle ne s'exécute pas, comme dans l'atelier.
- **Entrée dans une zone** valide les zones, et rien d'autre : le premier bouton du formulaire est un bouton invisible réservé à cela, jamais un bouton de l'écran.
- **Choisir une ligne** : la première cellule de chaque ligne est un bouton. Le choix déclenche `Quand on choisit …`, et la ligne reste choisie pour `le … choisi de l'écran`. Elle suit son contenu, pas son rang : une ligne insérée au-dessus ne déplace pas le choix ; une ligne disparue le retire.
- **Case à cocher** : décochée, un navigateur n'envoie rien. Un champ caché signale sa présence, pour distinguer « décochée » de « absente de l'envoi ».
- **Écran d'hier.** Chaque attente porte un numéro ; un envoi venu d'un autre onglet ou d'une page ancienne ne déclenche rien, et la page le dit.
- **Croix du titre** : la fermeture de l'écran, comme la croix d'une fenêtre dans l'atelier (`Quand on ferme l'écran` s'exécute).
- **Thème** : les couleurs viennent de `src/atelier/theme/grymoir-jetons.json`, recopiées dans `src/serveur.c` ; `test_serveur` échoue si les deux divergent. Le logo est grand sur l'écran du fond, petit sur un écran ouvert par-dessus, comme dans l'atelier.
- **Limite connue** : les polices Atkinson ne sont pas encore embarquées dans `grym`. La page les emploie si elles sont installées sur la machine, sinon la police du système. Les embarquer ajoute environ 130 Ko à `grym` ; à trancher avec W2, qui devra de toute façon les copier dans le site publié.
- **Ce que le programme affiche** (`Afficher`) apparaît sous l'écran, comme la console de l'atelier.

## 4. Publier *(sortie 1)*

```
Le site montre l'écran des œuvres.

grym publier partotheque.grym          →  partotheque.site/
```

- **Le site se déclare.** La phrase `Le site montre l'écran … .` désigne l'écran d'accueil du site. Sans elle, `grym publier` refuse et propose la phrase. Un programme n'a qu'une telle phrase. Raison : publier tous les écrans publiables mettrait en ligne, par mégarde, un écran d'administration qui se trouve ne rien modifier.
- Se publient l'écran d'accueil et, de proche en proche, les écrans qu'il ouvre par un lien (§ 4.1).
- `grym` ouvre la base en lecture seule et écrit un dossier de pages. Rien n'est écrit dans la base, et le programme ne s'exécute pas : seules ses déclarations servent.
- **Adresses stables.** Une page d'objet porte l'identifiant de l'objet en base : `oeuvres/12.html`. Un identifiant n'est jamais réattribué (grammaire, § 16.3) : un lien vers une œuvre, partagé ou mis en favori, ne désignera jamais une autre œuvre.
- **Sortie déterministe.** Même programme, même base, même dossier, octet pour octet. Le site se compare avec `git diff` et se téléverse en ne changeant que les pages modifiées. Seule exception : une page dont une liste dépend d'`aujourd'hui` (§ 4.1).
- La corbeille reste hors du site : une recherche l'écarte déjà (grammaire, § 16.12).
- Les images et fichiers des objets publiés sont copiés dans le dossier.

### 4.1 Ce qui se publie

Une page fixe ne peut rien exécuter. Un écran se publie si chacun de ses événements se résout à la publication :

- `Quand on choisit un … : Ouvrir la fiche du … .` devient un lien vers la page de l'objet ;
- `Quand on clique sur « … » : Ouvrir l'écran … .` devient un lien vers la page de l'écran.

Tout autre événement (conserver, modifier, supprimer, poser une question), toute zone de saisie, et toute condition `dont` qui lit une variable du programme rendent l'écran impubliable. Une variable n'a de valeur qu'en exécutant le programme, et l'exécuter pourrait écrire dans la base : figer sa valeur est donc exclu.

`aujourd'hui` fait exception : une liste des cotisations échues se publie, figée au jour de la publication. Ces pages seules portent en pied de page « Publié le 29.09.2026 », pour qu'aucun visiteur ne prenne une liste d'hier pour celle du jour. Les autres pages restent identiques d'une publication à l'autre. `grym publier` le refuse avant d'écrire quoi que ce soit, avec la phrase fautive : « L'écran des œuvres ne se publie pas : l'événement « Supprimer » (ligne 14) modifie la base, ce qu'une page fixe ne sait pas faire. »

Un bouton muet sur une page publiée tromperait le visiteur : le refus explicite l'évite (charte, art. 8 : aucune erreur avalée).

### 4.2 Ce qui devient public

Une entité est publique si une liste d'un écran publié la montre. Seuls ses objets atteints par une liste publiée reçoivent une page.

- La fiche d'un objet public montre ses champs, comme la fiche déduite. Un lien vers un objet public devient un hyperlien ; un lien vers une entité qui n'est pas publique s'affiche en texte (sa clé), sans page.
- Raison : une fiche déduite suit les liens de proche en proche. Publier toute fiche atteignable mettrait en ligne, depuis une partition, l'adresse de l'arrangeur, puis ses cotisations. Le développeur choisit ce qui sort en choisissant ses listes, et rien d'autre ne sort.
- Limite connue : la clé d'un objet non public reste visible (le nom de l'arrangeur, par exemple). Masquer un champ d'une fiche publiée demandera sa propre phrase.

### 4.3 Republier

`grym` n'écrase jamais un fichier (grammaire, § 15.2). Pour republier :

1. `grym` écrit le nouveau site dans un dossier neuf, `partotheque.site.nouveau` ;
2. si `partotheque.site` existe, il doit contenir le fichier `.grym-site`, que `grym` y dépose à chaque publication ; sinon, refus : « « partotheque.site » n'a pas été produit par grym : il n'est jamais remplacé. » ;
3. l'ancien dossier est renommé, le nouveau prend sa place, puis l'ancien est retiré.

Si une étape échoue, l'ancien site reste en place et le dossier neuf est retiré : une publication réussit entièrement ou ne laisse aucune trace (charte, principe 1).

## 5. Servir en local *(sortie 2)*

`grym servir` garde son protocole (v2.0-a : 127.0.0.1, jeton, port choisi par le système, navigateur ouvert automatiquement) et passe sur le moteur de rendu du § 3.

- Le refus actuel d'un programme à écrans par `grym servir` (grammaire, § 22.3) tombe.
- Un seul utilisateur : la machine garde son état entre deux clics, comme l'atelier. `Ouvrir` qui attend jusqu'à `Fermer`, y compris dans un événement, fonctionne tel quel.
- Le modèle séquentiel de la v2.0 (questions numérotées, formulaires saisis) continue de fonctionner pour les programmes sans écran.

## 6. Servir en public *(sortie 3)*

Ici, la question change de nature. Quatre problèmes, chacun à concevoir avant tout code.

### 6.1 Plusieurs visiteurs à la fois : une machine par visiteur

En local, une machine sert un utilisateur. En public, chaque visiteur reçoit sa propre machine : ses écrans ouverts, ses zones de saisie, ses variables. Elle reste en mémoire entre deux clics et se libère après une inactivité.

Voie écartée : exécuter chaque événement d'un seul tenant et refuser `Ouvrir` dans un événement. Elle économise la mémoire, mais crée un dialecte public du langage, où un programme qui marche en local est refusé en ligne. La charte ne veut qu'un langage (art. 4). Et elle n'économise pas tant : les variables d'un visiteur doivent de toute façon vivre quelque part entre deux clics.

- Les variables du programme appartiennent au visiteur. Deux visiteurs ne partagent que la base.
- Inactivité : 30 minutes pour un visiteur connecté, 5 minutes pour un anonyme. Un visiteur qui revient après retrouve l'écran d'accueil.
- Plafond de machines simultanées, réglable. Au-delà, une page dit que le site est plein, plutôt que de ralentir tout le monde.
- Un redémarrage de `grym` perd les machines, jamais la base : les écritures sont validées à chaque événement.
- Risque à traiter en W4 : un robot qui ouvre des milliers de sessions anonymes. Le plafond le borne ; une machine n'est créée qu'à la première page qui en demande une.
- La mémoire d'une machine n'est pas mesurée. Le plafond par défaut se fixera sur une mesure, en W3, pas sur une estimation.
- Transactions : aucune règle nouvelle. `docs/ecrans.md` fixe déjà qu'`Ouvrir` valide ce qui précède et que chaque événement forme sa transaction ; un événement suspendu par `Ouvrir` ne tient donc aucun verrou pendant que le visiteur réfléchit. En public, chaque requête exécute au plus un événement, dans sa transaction.

### 6.2 La base

A3 fait déjà une transaction par événement : les écritures sont courtes. SQLite en mode WAL laisse les lecteurs travailler pendant qu'un écrivain écrit, avec un seul écrivain à la fois, et ne fonctionne pas sur un système de fichiers réseau (documentation SQLite, « Write-Ahead Logging », https://www.sqlite.org/wal.html ; à relire au moment de l'implémentation). Pour le site d'une association ou d'un club, c'est largement assez. Pour un site marchand à fort trafic, non, et ce n'est pas la cible (charte, art. 1).

La transaction par exécution (charte, art. 7) devient, en public, une transaction par requête. La règle d'annulation reste : un événement raté n'écrit rien, et le visiteur voit le message.

### 6.3 Visiteurs, comptes, droits

Le plus gros morceau, et entièrement dans la grammaire. Il faut pouvoir dire, en français courant :

- qu'une entité sert de compte (qui se connecte, avec quoi) ;
- quels écrans un visiteur anonyme peut ouvrir ;
- quels événements exigent un visiteur connecté, ou un rôle ;
- quels objets un visiteur connecté voit (ses cotisations, pas celles des autres).

Esquisse, pour fixer les idées, **rien n'est proposé à la validation ici** :

```
Un membre, conservé, se connecte avec son courriel et son mot de passe.
L'écran des cotisations est réservé aux membres connectés.
Quand on clique sur « Payer » dans l'écran des cotisations : …
Pour chaque cotisation du visiteur : …
```

La dernière ligne montre l'enjeu : `le visiteur` serait un nom fourni par le serveur, qui borne ce que le programme peut atteindre. Une erreur de droits dans une application de bureau gêne un utilisateur ; sur un site public, elle expose les données de tous.

### 6.4 Sécurité et hébergement

Ce que `grym` doit faire seul, sans que le développeur y pense :

- jeton contre la falsification de requêtes intersites (CSRF) dans chaque formulaire ;
- cookies de session `HttpOnly`, `Secure`, `SameSite` ;
- mots de passe hachés avec Argon2id, recommandé par l'OWASP (« Password Storage Cheat Sheet ») et normalisé par la RFC 9106 (IRTF, 2021) ; cité de mémoire, à relire. L'implémentation de référence d'Argon2 s'embarquerait dans le dépôt comme SQLite, sans dépendance extérieure ;
- limitation des tentatives de connexion ;
- taille des envois bornée (50 Mo, comme la v2.0-b).

Ce que `grym` ne fait pas : le chiffrement HTTPS. Un serveur frontal s'en charge (Caddy, par exemple, qui obtient ses certificats tout seul), devant `grym` qui écoute en local. Le guide de déploiement explique ce montage pas à pas, pour un développeur qui n'est pas administrateur système.

## 7. Jalons proposés

| Jalon | Contenu |
|---|---|
| W1 *(fait le 29 septembre 2026)* | Moteur de rendu HTML des écrans (§ 3), thème compris ; `grym servir` rebranché dessus, programmes à écrans acceptés (§ 5) |
| W2 | `grym publier` (§ 4) : adresses stables, sortie déterministe, refus des écrans impubliables |
| W3 | Conception, sans code : contexte par visiteur (§ 6.1), comptes et droits dans la grammaire (§ 6.3) |
| W4 | `grym` en public : sessions, WAL, sécurité (§ 6.4), guide de déploiement |

W1 et W2 livrent chacun un outil utilisable. W3 décide si le public tient debout avant d'écrire une ligne de C.

## 8. Décisions du 29 septembre 2026

1. Le site se déclare par une phrase ; les écrans atteints par lien depuis l'accueil se publient (§ 4).
2. Republier passe par un dossier neuf et un remplacement, seulement sur un dossier marqué par `grym` (§ 4.3).
3. Une machine par visiteur ; la voie qui refuserait `Ouvrir` en public est écartée (§ 6.1).
4. Transactions : alignées sur `docs/ecrans.md`, sans règle nouvelle (§ 6.1).
5. Un `dont` qui lit une variable rend l'écran impubliable ; `aujourd'hui` se fige, avec la date en pied de page (§ 4.1).
6. Seuls les objets atteints par une liste publiée reçoivent une page ; les liens vers une entité non publique restent du texte (§ 4.2).

Restent ouverts : masquer un champ d'une fiche publiée ; le plafond de machines, à mesurer ; toute la grammaire des comptes et des droits (§ 6.3, W3).

## 9. Ce que la charte devra dire

- Art. 1 : le web à grande échelle reste hors cible ; le site d'une petite structure entre dans la cible.
- Art. 7 : en public, une transaction par requête.
- Art. 11 : le réseau et la concurrence entrent dans le périmètre, bornés à `grym servir`.
- Art. 12 : les jalons W1 à W4.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 0.1 | 2026-09-29 | Proposition initiale : une description, trois sorties (publier, servir en local, servir en public), rendu HTML des écrans, adresses stables, refus des écrans impubliables, problèmes du public, jalons W1 à W4, questions ouvertes |
| 0.2 | 2026-09-29 | Questions 1 à 6 tranchées : `Le site montre …`, entités publiques par leurs listes (§ 4.2), republication par remplacement d'un dossier marqué (§ 4.3), `aujourd'hui` figé, une machine par visiteur (§ 6.1), transactions alignées sur les écrans |
| 0.3 | 2026-09-29 | W1 fait : § 3.1, ce que l'implémentation a fixé (un formulaire par écran, ordre des événements, Entrée, choix qui suit la ligne, case à cocher, écran d'hier, croix, thème vérifié contre les jetons) ; polices pas encore embarquées |
