# Charte de GrymoiR

Version 1.71, révisée le 10 novembre 2026.
Toute modification passe par une révision numérotée.

---

## 1. Raison d'être

GrymoiR sert à écrire des applications de gestion fiables, en réunissant langage, base de données et déploiement dans un seul outil, autour d'un modèle objet.

Cible : le développeur seul ou la petite équipe.
Hors cible : calcul intensif, programmation système, web à grande échelle.

## 2. Hiérarchie des principes

En cas de conflit entre deux choix, le rang supérieur l'emporte, sans débat.

1. **Intégrité des données** : une écriture réussit entièrement ou ne laisse aucune trace.
2. **Lisibilité** : un non-programmeur devine ce que fait une formule.
3. **Concision** : on ne tape rien que le compilateur peut déduire.
4. **Performance.**

## 3. Exécution

- Le compilateur `grym` produit un bytecode unique, un seul fichier pour tout un projet, même écrit sur plusieurs fichiers (grammaire, § 21).
- Une VM écrite en C portable exécute ce bytecode sous Windows, Linux et macOS.
- Deux exécutables : `grym` (compilateur, VM, console, navigateur, aide à la saisie), en C99 sans dépendance ; `grym-atelier`, l'environnement de développement, en C++ avec Qt 6 (`docs/atelier.md`).
- Les sources sont en UTF-8, normalisées NFC à la lecture.
- Extensions : `.grym` (forme littéraire), `.grymc` (forme compacte).

## 4. Syntaxe : deux formes, un seul langage

GrymoiR s'écrit sous deux formes équivalentes.

- **Forme littéraire** (`.grym`) : français courant, selon une grammaire contrôlée où chaque phrase acceptée a un sens unique. Elle gère l'élision, les articles, le pluriel et les accords. Le pluriel irrégulier se déclare dans l'entité.
- **Forme compacte** (`.grymc`) : mots-clés français préfixés d'un souligné, assignation `<<`.

Règles communes :

- Les deux formes produisent le même arbre syntaxique. Tout ce qui suit l'analyse (vérification, bytecode, VM) ignore la forme d'origine.
- `grym traduire` convertit un fichier d'une forme à l'autre sans perte de programme ni de remarque. Compacte → littéraire → compacte redonne le texte compact à l'identique ; littéraire → compacte → littéraire redonne le même programme en forme littéraire canonique. L'arbre conserve remarques, lignes vides et choix d'écriture ; `grym formater` écrit la forme canonique (grammaire, § 12).
- Une seule forme par fichier. Un projet peut mêler les deux : `Utiliser « données ».` trouve `données.grym` ou `données.grymc` (grammaire, § 21).
- Toute construction existe dans les deux formes.
- La forme littéraire fait référence : chaque nouvelle fonction se conçoit d'abord en français courant, puis se dérive en forme compacte.
- Les calculs gardent la notation mathématique dans les deux formes (`Le total vaut prix × quantité.`).
- Identifiants avec accents autorisés, insensibles à la casse (`Solde` = `solde`). Un sigle toujours écrit en capitales (`numéro RCC`) les garde à l'affichage, sans cesser d'être le même nom (grammaire, § 2.4).
- L'interprétation est déterministe, sans modèle de langage : même texte, même programme.

Forme littéraire :

```
Pour relancer un client :
    Si le solde du client < 0 :
        Le r vaut un nouveau rappel :
            Le débiteur vaut client.
        Conserver le r.
```

Forme compacte :

```
_action relancer(_un client)
    _si client.solde < 0 _alors
        _le r << _nouveau rappel _avec
            débiteur << client
        _fin
        _conserver r
    _fin
_fin
```

## 5. Typage

- Code ordinaire : typage dynamique, que des annotations facultatives resserrent. Une variable ou un paramètre annoté (`Le total (nombre) vaut 0.`, `un statut (état)`) est vérifié à chaque écriture : à l'analyse quand la valeur y est connue, sinon à l'exécution (grammaire, § 16.16).
- Entités persistées : typage strict, vérifié à la compilation quand le type de la valeur y est connu, sinon avant toute écriture. Aucune valeur d'un mauvais type n'atteint la base.
- Un type nommé (`Un état est « à faire » ou « fini ».`) se partage entre champs, variables et paramètres (grammaire, § 16.16).

Justification : l'erreur la plus coûteuse est celle qui corrompt la base (principe 1). La rigueur se concentre là.

## 6. Modèle objet

- Les classes et les entités donnent des **objets** : des champs, une identité, des formules qui leur sont propres. Les nombres, textes, dates, heures, vrai ou faux, listes et tables sont des **valeurs** : sans identité, comparées par ce qu'elles contiennent, copiées quand on les range. (Jusqu'à la révision 1.63, cet article disait « Tout est objet », ce que le langage n'a jamais fait.)
- Héritage simple.
- Aptitude : paquet de formules et de champs qu'une classe adopte sans en hériter. Une aptitude peut exiger des formules de ses classes (contrat) ; l'analyse vérifie que chacune en a une version (grammaire, § 13.7 bis).
- Si deux aptitudes définissent la même formule, la compilation échoue jusqu'à ce que le développeur tranche explicitement. Aucune résolution implicite.

Forme littéraire :

```
Une personne, conservée, a :
    un nom (texte).

Une chose horodatée a :
    une date de création (date),
    une date de modification (date).

Un membre, conservé, est une personne horodatée.
Un membre a :
    une licence (texte), unique.
```

Forme compacte :

```
_classe _une personne _conservé
    _un nom (texte)
_fin

_aptitude horodatée
    _une date_de_création (date)
    _une date_de_modification (date)
_fin

_classe _un membre _conservé _est _une personne _adopte horodatée
    _une licence (texte) _unique
_fin
```

Ces exemples se compilent tels quels ; la grammaire exacte est `docs/grammaire.md` (§ 13 et § 16).

## 7. Persistance

- L'entité est une construction du langage.
- Moteur : SQLite embarqué, invisible pour le développeur, qui n'écrit jamais de SQL.
- Chaque exécution forme une transaction implicite : un programme lancé, ou une saisie de la boucle interactive, qui échoue ou qu'on interrompt n'écrit rien, ni en mémoire ni dans la base. Une question posée à l'utilisateur referme cette transaction et en ouvre une autre : ce qu'il a vu confirmé est acquis, et la base n'est pas verrouillée pendant qu'elle attend. Une formule qui échoue n'écrit donc rien non plus. Ce qu'elle a affiché reste affiché, suivi d'une phrase qui dit ce qui a été annulé. Un bloc `Essayer` rattrape l'erreur à sa mesure : il annule tout ce qu'il a fait, depuis son début ou depuis la dernière question, puis exécute son bloc `En cas d'échec` (grammaire, § 18).
- Exception, à partir des écrans (grammaire, § 22) : `Ouvrir` un écran valide ce qui précède, puis découpe l'exécution en transactions, une par événement. Un programme sans écran reste une seule transaction.
- Ce qui sort du programme ne s'annule pas : un envoi web, un courriel parti, une commande lancée (grammaire, § 22.18, § 22.27, § 15.10). Une transaction annulée n'efface que ce qu'elle a écrit en mémoire et dans la base ; le programme envoie en dernier ce qui ne doit partir qu'après réussite.
- Migrations de schéma automatiques pour tout ajout ; aucune donnée détruite en silence.
- Tout nombre est un décimal exact, jamais un flottant, en mémoire comme en base. Un type `montant` (devise, arrondi) n'est pas prévu pour la v0.3.
- Versions parallèles des données : horizon post-v1.

## 8. Erreurs

- Messages en français, localisés par fichier, ligne et colonne, au tutoiement, comme le guide « Premiers pas ».
- Le message s'exprime dans la forme du fichier concerné.
- Correction proposée quand c'est possible (« `soldee` inconnu, voulais-tu `solde` ? »).
- Une phrase littéraire hors grammaire produit une erreur qui propose la tournure valide la plus proche.
- Aucune erreur avalée en silence : un `Essayer` exige au moins un bloc `En cas d'échec`, qui reçoit le motif et la sorte de l'erreur. Un bloc peut ne rattraper que certaines sortes (`En cas d'échec de réseau :`) ; une erreur qu'aucun bloc ne rattrape remonte, intacte (grammaire, § 18.2).

## 9. Aide à la saisie

À toute position d'un fichier, l'outillage propose les suites valides selon la grammaire et les noms déclarés.

- La grammaire contrôlée permet un calcul exact des suites possibles, sans heuristique.
- Le même calcul alimente les messages d'erreur (« attendu : … »).
- Diffusion dans les éditeurs par un serveur conforme au Language Server Protocol.

Justification : une grammaire contrôlée se lit facilement mais s'écrit mal si l'on ignore les tournures acceptées. L'aide à la saisie rend la grammaire visible.

## 10. Développement vivant

L'architecture de la VM prévoit la recompilation à chaud d'une formule dans une application en cours d'exécution. Livraison après la v1.

## 11. Hors périmètre v1

Interface graphique, réseau, concurrence, optimisation.

Depuis la v2.0 : interface graphique par le navigateur, servie par `grym` ; écoute limitée à la machine locale (127.0.0.1), pendant l'exécution d'un programme (`docs/v2.md`, § 5). Depuis le 8 novembre 2026, plusieurs navigateurs à la fois, une session chacune, sur une même base (`grym servir --sessions`) ; et le réseau local, en TLS seulement, avec le certificat du cabinet (`--reseau`, `docs/v2.md`, § 12) : jamais en clair, jamais directement depuis internet. Le langage lui-même reste sans concurrence : chaque session exécute son programme seule, la base arbitre (grammaire, § 16.6).

Depuis, le réseau sortant existe, à l'initiative du programme seulement : les envois web en HTTPS (grammaire 2.42, § 22.18), les courriels (2.94, § 22.27). Un programme lance aussi d'autres programmes, sans shell (3.04, § 15.10). Une bibliothèque écrit un calcul en C, chargé par le système, jamais compilé par GrymoiR (3.14, § 21.10). Une minuterie d'écran (3.03) n'est pas de la concurrence : ses échéances passent une à une, entre les événements.

Depuis l'atelier (décidé le 24 septembre 2026, `docs/atelier.md`) : interface graphique native par Qt 6, pour l'atelier et pour les applications qu'il produit ; `grym servir` reste un mode d'affichage parmi d'autres.

Horizon post-v1, sans date :

- Éditeur par blocs, conçu comme une troisième forme de l'arbre syntaxique (art. 4), convertible sans perte vers les deux autres.
- Éditeur visuel d'interfaces : il quitte l'horizon pour les jalons A3 et A4 de l'atelier.

## 12. Jalons

| Version | Contenu | État |
|---------|---------|------|
| v0.1 | Lexer, parser de la forme littéraire, suites attendues, boucle interactive de calcul | livrée |
| v0.2 | Bytecode et VM (remplacent l'évaluateur provisoire de la v0.1), conditions (`Si`, comparaisons, booléens), boucles et `Selon`, forme compacte et `grym traduire` dans les deux sens, forme canonique et `grym formater`, formules, objets, héritage, méthodes, aptitudes, ramasse-miettes | livrée le 21 septembre 2026 |
| v0.3 | SQLite embarqué, entités conservées (types, unicité, liens, héritage, aptitudes), conserver, modifier, supprimer, retrouver (`dont`, tri, comptage), migrations, transaction par exécution, dates, fichiers et images | livrée le 21 septembre 2026 |
| v0.4 | Serveur d'aide à la saisie (`grym lsp`, protocole LSP) : erreurs en direct, autocomplétion, mise en forme ; extension VS Code avec coloration des deux formes | livrée le 22 septembre 2026 |
| v1.0 | Application console complète : questions à l'utilisateur (grammaire, § 17), mise en forme de l'affichage (§ 4), reprise après erreur (§ 18), formulaire déduit de l'entité (§ 19) ; première application réelle : `exemples/partotheque.grym` ; sortie d'une question par un point seul (§ 17), assemblage de textes (§ 4.4) | livrée le 24 septembre 2026 |
| v2.0 | L'interface par le navigateur (`docs/v2.md`) : `grym servir`, tout programme v1 affiché sans modification ; formulaires typés, liens avec suggestions, téléversement, images | livrée le 24 septembre 2026 |
| A1 | L'atelier (`docs/atelier.md`) : fenêtre Qt, projet, éditeur avec coloration par le lexeur et erreurs en direct, « Lancer » dans un processus à part | fait le 24 septembre 2026 |
| A2 | Fichiers utilisés (grammaire, § 21), puis éditeur de données : schéma des entités, propriétés, réécriture chirurgicale, aperçu des migrations | fait le 25 septembre 2026 |
| A3 | Les écrans dans le langage (`docs/ecrans.md`) : fenêtres, listes, fiches, boutons, événements | fait le 25 septembre 2026 |
| A4 | Éditeur d'écrans : liste et fiche générées par entité, agencement à la souris | fait le 26 septembre 2026 |
| A5 | Les projets et leurs fichiers (`docs/atelier.md`, § 5 quater) : nouveau projet, projets récents, renommer, corbeille, fermer ; première application créée ainsi, `projets/Gestion de projet` | fait le 10 octobre 2026 |
| W1 | Le web (`docs/web.md`) : les écrans rendus en HTML, avec le thème ; `grym servir` montre les programmes à écrans | fait le 29 septembre 2026 |
| W2 | `grym publier` : un site de pages fixes tiré de la base, adresses stables, sortie déterministe, refus des écrans impubliables | à venir |
| W3 | Une machine par visiteur, comptes dans la grammaire : `grym servir --sessions` (une session par navigateur, `docs/v2.md`, § 11), `Les utilisateurs se connectent.` (grammaire, § 16.20), les conflits entre sessions (§ 16.6) ; les droits restent écrits par le programme (`Refuser`) | fait le 8 novembre 2026 |
| W4 | `grym` sur le réseau d'un cabinet : TLS, certificats, adresse stable, frein par adresse, démarrage automatique, guide de déploiement (`docs/v2.md`, § 12 ; `docs/installation-multiposte.md`) ; jamais directement en public, sur internet | fait le 8 novembre 2026, sur un réseau local |
| v3.0 | La première version publiée : l'atelier, `grym` et les exemples, pour macOS, Linux et Windows (`.github/workflows/publication.yml`), sous EUPL 1.2 ; langage à la grammaire 3.18 ; notes : `docs/notes-de-version.md` | publiée le 8 novembre 2026 |
| v3.1 | Grammaire 3.19 (petits manques, conflits des champs multiples et des suppressions) ; démarrage automatique sous Linux et Windows ; apprendre GrymoiR dans le navigateur, avec le vrai moteur (`docs/site/apprendre.html`, `apprendre/LISEZMOI.md`) | publiée le 10 novembre 2026 |
| v3.2 | Grammaire 3.20 (le premier avertissement : l'espace perdue des guillemets) ; sous Linux, l'application fabriquée embarque Qt et la transcription ; les corrections des 3.1.1 et 3.1.2 | publiée le 10 novembre 2026 |

Critères de sortie de la v1.0, tous remplis :

1. Intégration continue : compilation sans avertissement et tests sous Linux, macOS et Windows à chaque poussée (`.github/workflows/tests.yml`). Fait.
2. Essai manuel, dans la console Windows, d'une saisie accentuée (la Partothèque avec un compositeur « Élodie Müller »). Fait.
3. Modifier un objet conservé par formulaire (grammaire, § 19 : `Saisir à nouveau`). Fait.
4. README et charte à jour. Fait.
5. Compatibilité promise (art. 13) et numéro de format de chaque base. Fait.

Reportés sans jalon fixé, chacun à concevoir avant d'entrer dans un jalon :

- intégrité : une règle ne se revérifie que si une entité qu'elle a lue a été écrite (grammaire, § 16.14, fait le 13 octobre 2026) ; quand elle se revérifie, c'est sur tous les objets qu'elle concerne. Ne revérifier que les objets touchés, en prouvant que c'est exact, reste à faire. Mesuré le 13 octobre 2026 : 0,5 s par écriture comptable ajoutée sur 50'000 ;
- objets (grammaire, § 13) : racine commune `objet` de toutes les classes. Les listes ordonnées (§ 13.10), les tables (§ 13.11) et le tri (§ 13.12) sont faits (grammaire 2.89 à 2.97) ; les nombres, textes et dates restent des valeurs (art. 6) ;
- relations : test d'appartenance en mémoire (`Si baroque est parmi les genres de o`), gagner plusieurs objets en une phrase, champs multiples de textes ou de nombres ; lien facultatif qui devient absent quand son objet est effacé ;
- corbeille (grammaire, § 16.12) : la vider des objets supprimés depuis longtemps ;
- saisie par l'utilisateur de l'application : autocomplétion des valeurs déjà saisies, y compris celles des objets de la corbeille (avec l'interface des applications) ;
- base (grammaire, § 16.7) : commande de `grym` qui retire ou renomme un champ, lève les limites des migrations imposées par SQLite, et reconstruit une table ;
- dates (grammaire, § 14) : moment complet (date et heure) et fuseaux horaires, après l'heure seule (faite) ;
- langage : modifier le champ d'une recherche sans passer par un nom (`L'âge du client conservé dont … devient …`) ;
- aide à la saisie (`docs/lsp.md`, § 8) : survol d'un nom, aller à la définition, renommer dans l'éditeur VS Code, rechercher les usages, suites en forme compacte, Neovim ;
- installation pour l'utilisateur final : `grym` embarqué dans l'extension (un paquet par système, construit par GitHub Actions), aucun réglage, fonctionnement en mode restreint, bouton « Lancer », publication sur la place de marché de VS Code et sur Open VSX (licence et comptes d'éditeur requis).

## 13. Compatibilité

Une version 1.x promet ce qui suit. Toute exception passe par une révision de la charte.

1. **Bases.** Une base créée ou modifiée par une version 1.x s'ouvre dans toute version 1.x ultérieure, sans perte, par migration automatique (art. 7). Chaque base porte le numéro de son format. Une version qui rencontre une base d'un format plus récent que le sien la refuse, avant toute écriture, et dit qu'une version plus récente de `grym` l'ouvrira.
2. **Sources.** Un programme accepté par une version 1.x est accepté par les versions ultérieures et y produit le même effet. Seule exception : un mot nouvellement réservé ou un nouveau mot de construction. Chacun est alors inscrit au journal de la grammaire, et le message d'erreur propose la correction (le nom entre crochets, ou un autre verbe).
3. **Comportement.** Même programme, même base, mêmes réponses : même résultat (art. 4). Corriger un écart entre le programme et la spécification n'est pas une rupture.
4. **Sortes d'échec.** La sorte d'une erreur (grammaire, § 18.2 : `réseau`, `fichier`, `unicité`…) est promise, comme le reste du comportement : un programme qui rattrape une sorte la rattrape dans les versions ultérieures. Une sorte peut s'ajouter (`commande`, 3.04 ; `natif`, 3.14 ; `conflit`, 3.16) ; aucune ne disparaît ni ne change de sens.
5. **Hors promesse.** Le libellé des messages d'erreur (c'est pourquoi un programme décide par la sorte, jamais par le motif), le bytecode `.grymb` (on recompile depuis les sources ; la machine lit les anciens formats par commodité), l'interface des fichiers de `src/`, sauf `src/grym_natif.h`. Ce contrat des fonctions C est promis : il ne change qu'avec `GRYM_NATIF_VERSION`, et une bibliothèque compilée pour un contrat que `grym` ne suit pas est refusée avec un message, jamais appelée (grammaire, § 21.10).
6. **Version 2.** Elle peut rompre les sources. Elle ne rompt jamais une base : elle l'ouvre, ou fournit la commande qui la convertit (principe 1).


## 14. Pérennité

GrymoiR doit pouvoir évoluer par une seule personne, sans aide extérieure, humaine ou logicielle.

- `docs/mainteneur.md` décrit l'architecture, les outils de mise au point et la recette pour ajouter une phrase de bout en bout, avec sa liste de contrôle.
- Il se révise, par une révision numérotée, à chaque changement d'architecture : un fichier qui naît, disparaît ou change de rôle.
- Le code reste écrit en français, dans le vocabulaire de la grammaire, et chaque construction garde ses essais : ils sont le filet de qui modifie le langage.
- GrymoiR est sous licence EUPL v. 1.2 seulement (`LICENCE`), choisie le 7 novembre 2026 : quiconque peut le reprendre, et qui le modifie et le distribue en publie le code. Une exception (`LICENCE-fr.md`) laisse les applications écrites en GrymoiR à leur auteur, sous la licence de son choix, même quand elles embarquent la machine sans la modifier.
---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-20 | Charte initiale |
| 1.1 | 2026-09-20 | Double syntaxe : forme littéraire (référence) et forme compacte, traduction sans perte (art. 3, 4, 6, 8) |
| 1.2 | 2026-09-20 | Jalons : forme littéraire dès v0.1, forme compacte et traduction en v0.2 (art. 11) |
| 1.3 | 2026-09-21 | Nouvel art. 9 : aide à la saisie. Renumérotation des art. 9 à 11 en 10 à 12. Jalons : suites attendues en v0.1, serveur LSP en v0.4 |
| 1.4 | 2026-09-21 | Art. 11 : horizon post-v1, éditeur par blocs et éditeur visuel d'interfaces |
| 1.5 | 2026-09-21 | Jalons : bytecode et VM en v0.2 (art. 3 et 12) |
| 1.6 | 2026-09-21 | Jalons : conditions en v0.2 (art. 12) |
| 1.7 | 2026-09-21 | Art. 4 : traduction sans perte de programme ni de remarque, vers une forme littéraire canonique ; `grym formater` |
| 1.8 | 2026-09-21 | Art. 12 : colonne « État » ; v0.2 livrée, contenu complété (boucles, `Selon`, forme canonique, méthodes, ramasse-miettes) ; suites reportées sans jalon |
| 1.9 | 2026-09-21 | Art. 5 : typage des entités vérifié à l'analyse ou avant toute écriture. Art. 7 : transaction par exécution ; migrations automatiques pour les ajouts ; type `montant` retiré, tout nombre étant exact |
| 1.10 | 2026-09-21 | Art. 7 : l'affichage d'une exécution ratée reste, suivi d'une phrase d'annulation |
| 1.11 | 2026-09-21 | Art. 12 : v0.3 livrée, contenu détaillé ; suites reportées regroupées par domaine |
| 1.12 | 2026-09-22 | Art. 12 : v0.4 livrée ; reports de l'aide à la saisie et de l'installation pour l'utilisateur final |
| 1.13 | 2026-09-22 | Art. 12 : l'absence de valeur est faite (grammaire, § 16.9) ; reports précisés : racine `objet`, relations |
| 1.14 | 2026-09-22 | Art. 12 : les relations inverses sont faites (grammaire, § 16.10) |
| 1.15 | 2026-09-22 | Art. 12 : corbeille et cascade faites (grammaire, § 16.12) ; reports : vider la corbeille, autocomplétion des saisies |
| 1.16 | 2026-09-22 | Art. 12 : « plusieurs vers plusieurs » fait (grammaire, § 16.13) ; reports : test d'appartenance hors `dont`, plusieurs objets gagnés à la fois, champs multiples de valeurs |
| 1.17 | 2026-09-22 | Art. 12 : le type `(année)` est fait (grammaire, § 14.5) ; l'ajout de mois et d'années aux dates reste reporté |
| 1.18 | 2026-09-22 | Art. 4 : le groupement des chiffres par milliers compte parmi les choix d'écriture conservés (grammaire, § 12) |
| 1.19 | 2026-09-22 | Art. 7 : une question à l'utilisateur referme la transaction en cours (grammaire, § 17) ; art. 12 : questions à l'utilisateur faites, en route vers la v1.0 |
| 1.20 | 2026-09-22 | Art. 12 : mise en forme de l'affichage faite (grammaire, § 4) ; la v1.0 attend une application réelle |
| 1.21 | 2026-09-23 | Art. 7 et 8 : reprise après erreur explicite (`Essayer`, `En cas d'échec`) ; art. 12 : faite, en route vers la v1.0 |
| 1.22 | 2026-09-23 | Art. 12 : formulaire déduit de l'entité fait (grammaire, § 19) |
| 1.23 | 2026-09-23 | Art. 12 : première application réelle (Partothèque) et ses retours |
| 1.24 | 2026-09-23 | Art. 12 : annulation d'une saisie par un point seul |
| 1.25 | 2026-09-23 | Art. 12 : assemblage de textes (`suivi de`, élision) |
| 1.26 | 2026-09-24 | Art. 12 : critères de sortie de la v1.0 ; intégration continue sur les trois systèmes |
| 1.27 | 2026-09-24 | Art. 12 : modification par formulaire faite |
| 1.28 | 2026-09-24 | Nouvel art. 13 : compatibilité (bases, sources, comportement, hors promesse, version 2) ; art. 12 : critère 5 |
| 1.29 | 2026-09-24 | Art. 12 : critères 2 et 4 faits ; la v1.0 attend un passage vert de l'intégration continue sur les trois systèmes |
| 1.30 | 2026-09-24 | Art. 12 : v1.0 livrée ; v2.0 inscrite, à concevoir (`docs/v2.md`) |
| 1.31 | 2026-09-24 | Art. 12 : v2.0 en conception, thèse et décisions validées (`docs/v2.md` 0.2) |
| 1.32 | 2026-09-24 | Art. 12 : v2.0-a faite (`grym servir`, serveur local) |
| 1.33 | 2026-09-24 | Art. 12 : v2.0-a éprouvée dans un vrai navigateur |
| 1.34 | 2026-09-24 | Art. 12 : v2.0-b faite (liens, fichiers, images dans le navigateur) |
| 1.35 | 2026-09-24 | Art. 11 : interface par le navigateur et écoute locale depuis la v2.0 ; art. 12 : v2.0 livrée |
| 1.36 | 2026-09-24 | Art. 12 : affichage des objets fait, par la fiche (grammaire, § 20.1) ; v2.x : les objets à l'écran en cours |
| 1.37 | 2026-09-24 | Art. 3 : deux exécutables, `grym` et `grym-atelier` (Qt 6), un bytecode par projet ; art. 4 : un projet sur plusieurs fichiers (grammaire, § 21) ; art. 11 : interface native par Qt, éditeur visuel d'interfaces en A3 et A4 ; art. 12 : jalons A1 (fait) à A4 |
| 1.38 | 2026-09-24 | Nouvel art. 14 : pérennité ; le manuel du mainteneur (`docs/mainteneur.md`), révisé à chaque changement d'architecture |
| 1.39 | 2026-09-25 | Art. 12 : A2 fait (fichiers utilisés, schéma des données, modification à la souris, aperçu des migrations) |
| 1.40 | 2026-09-25 | Art. 7 : exception des écrans, une transaction par événement ; art. 12 : A3 conçu |
| 1.41 | 2026-09-25 | Art. 12 : A3 fait (écrans, listes, boutons, zones de saisie, événements, fiche, écrans empilés, colonnes, disposition) |
| 1.42 | 2026-09-26 | Art. 12 : A4 fait (onglet « Écrans », écrans générés, propriétés modifiables, glisser-déposer) ; le plan de l'atelier (A1 à A4) est achevé |
| 1.43 | 2026-09-26 | Apparence : un seul thème fixé par GrymoiR (polices, espacements, couleurs neutres, clair ou sombre selon le système) ; un programme n'en règle que l'accent, dans une palette fermée de cinq couleurs, et le logo (grammaire, § 22.5 ; `docs/ecrans.md`, § 5) |
| 1.44 | 2026-09-27 | Art. 12 : `Refuser` est fait (grammaire, § 18.1) ; report : contrainte d'entité vérifiée par le langage |
| 1.45 | 2026-09-27 | Affichage des nombres : un entier de quatre chiffres ou moins ne se groupe pas (grammaire, § 4.1), comme une année ou un numéro en français |
| 1.46 | 2026-09-28 | Art. 12 : la somme d'un champ sur la base est faite (grammaire, § 16.4) : premier pas vers la contrainte d'entité reportée |
| 1.47 | 2026-09-28 | Art. 12 : les conditions `dont` à travers les liens sont faites (grammaire, § 16.4) |
| 1.48 | 2026-09-28 | Un calcul lit la base, sans jamais y écrire (grammaire, § 9.4) : le solde d'un compte devient un calcul |
| 1.49 | 2026-09-28 | Art. 12 : la contrainte d'entité est faite, sous forme de règles vérifiées à la fin de chaque transaction (grammaire, § 16.14) ; report : le ciblage des objets touchés |
| 1.50 | 2026-09-28 | Art. 12 : coût des règles mesuré ; le ciblage attend un besoin réel |
| 1.51 | 2026-09-28 | Art. 8 : les messages tutoient, comme le guide (« voulais-tu `solde` ? ») |
| 1.52 | 2026-09-29 | Art. 12 : l'ajout de mois et d'années aux dates est fait, ramené au dernier jour du mois (grammaire, § 14.2) ; report : l'écart en mois ou en années |
| 1.53 | 2026-09-29 | Art. 12 : jalons du web W1 à W4 (`docs/web.md`) ; W1 fait. Les art. 1, 7 et 11 changeront avec W3 et W4 |
| 1.54 | 2026-10-10 | Art. 12 : A5 fait (les projets dans l'atelier) ; l'application « Gestion de projet » rejoint `projets/` |
| 1.55 | 2026-10-12 | Art. 12 : l'appel de la version parente est fait (grammaire 1.65, § 13.8) ; restent les listes d'objets |
| 1.56 | 2026-10-12 | Art. 12 : les ensembles sont faits (grammaire 1.66, § 13.9) ; les listes ordonnées attendent un besoin réel |
| 1.57 | 2026-10-12 | Art. 12 : l'écart entre deux dates est fait (grammaire 1.68, § 14.6) ; reste l'heure |
| 1.58 | 2026-10-12 | Art. 12 : l'heure seule est faite (grammaire 1.69, § 14.7) ; le moment complet avec fuseau attend un besoin réel |
| 1.59 | 2026-10-13 | Art. 12 : règles ciblées par entité (grammaire 1.71, § 16.14) ; reste le ciblage par objet. Format 2 des bases, fichiers à part (docs/vm.md 1.57, § 8), permis par l'art. 13 |
| 1.60 | 2026-10-18 | Apparence assouplie (révise 1.43) : une application peut porter l'image de marque de celui pour qui elle est faite. Couleur d'accent libre en hexadécimal, fond, police embarquée et logo SVG, en plus de la palette de cinq ; GrymoiR en dérive les autres couleurs et refuse dès l'analyse toute couleur illisible (WCAG 2.1, 4,5:1 pour le texte). Espacements, tailles et formes restent fixés par GrymoiR (grammaire 2.13, § 22.5 ; `docs/ecrans.md`, § 5) |
| 1.61 | 2026-10-19 | Art. 4 : les sigles. Un nom reste insensible à la casse ; un mot qu'on écrit toujours en capitales les garde dans les libellés et la forme canonique, parce que « Numéro rcc » sur la fiche d'un thérapeute serait une faute (grammaire 2.21, § 2.4) |
| 1.62 | 2026-11-01 | Art. 12 : plusieurs erreurs à la fois, faites (l'atelier et `grym lsp` montrent, après la première, celles des déclarations suivantes) |
| 1.63 | 2026-11-01 | Art. 12 : renommer un nom dans tout le projet, fait dans l'atelier (vérifié dans une copie avant d'écrire) ; reste à faire dans `grym lsp` |
| 1.64 | 2026-11-07 | Relecture après l'audit du langage (grammaire 2.89 à 3.06). Art. 6 : « Tout est objet » était faux ; objets et valeurs distingués ; contrats d'aptitude. Art. 1 : « modèle objet pur » devient « autour d'un modèle objet ». Art. 4 et 6 : exemples réécrits dans la syntaxe réelle, compilés et traduits par `grym`, au lieu d'esquisses qui ne se compilaient pas. Art. 5 : annotations facultatives et types nommés. Art. 7 : ce qui sort du programme (envoi, courriel, commande) ne s'annule pas. Art. 8 : sortes d'échec, plusieurs blocs, l'erreur non rattrapée remonte. Art. 11 : réseau sortant, commandes, minuterie. Art. 12 : listes, tables et tri faits ; « nombres comme objets » abandonné. Art. 13 : nouveau point 4, les sortes d'échec sont promises (le motif reste hors promesse) ; points 4 et 5 renumérotés 5 et 6 |
| 1.65 | 2026-11-07 | Art. 11 : les fonctions C des bibliothèques (grammaire 3.14). Art. 13 : la sorte `natif` s'ajoute (point 4) ; le contrat `src/grym_natif.h` est promis, seule exception à « l'interface des fichiers de `src/` » hors promesse (point 5) |
| 1.66 | 2026-11-07 | Art. 14 : la licence, EUPL v. 1.2 seulement, et l'exception pour les applications écrites en GrymoiR |
| 1.67 | 2026-11-07 | Art. 13 : la sorte `conflit` s'ajoute (grammaire 3.16) |
| 1.68 | 2026-11-08 | Art. 11 : plusieurs sessions sur une base, et le réseau local en TLS seulement (`docs/v2.md`, § 11, § 12) ; le langage reste sans concurrence |
| 1.69 | 2026-11-08 | Art. 12 : W3 et W4 faits (sessions, comptes, réseau local) ; v3.0, la première version publiée |
| 1.70 | 2026-11-10 | Art. 12 : v3.1 |
| 1.71 | 2026-11-10 | Art. 12 : v3.2 |
