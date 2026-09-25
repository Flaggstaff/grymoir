# GrymoiR : manuel du mainteneur

Version 1.0, rédigée le 24 septembre 2026.
Référence : Charte de GrymoiR v1.38, art. 14 ; grammaire 1.38 ; `docs/vm.md` 1.33.
Toute modification passe par une révision numérotée. Ce manuel se révise à chaque changement d'architecture : un fichier qui naît, disparaît ou change de rôle.

Il s'adresse à qui veut faire évoluer le langage seul, sans aide extérieure. Il suppose qu'on sait lire du C sans en être spécialiste. Le code est écrit en français, dans le vocabulaire de la grammaire : une fonction `utiliser()` traite la phrase `Utiliser`, un nœud `P_EFFACER` représente `Effacer l'écran.`.

---

## 1. Le trajet d'un programme

Un fichier `.grym` traverse sept étapes. Chacune a son fichier, et chacune ne connaît que la précédente.

```
texte ──► lexeur ──► analyseur ──► arbre ──► compilateur ──► bytecode ──► machine
          (jetons)   (phrases,     (nœuds)   (instructions)   (.grymb)     (exécution,
                      noms, accords)                                        base SQLite)
                         ▲                │
                         │                ├──► imprimeur (forme canonique, forme compacte)
          forme compacte ┘                └──► suites attendues (aide à la saisie)
```

| Étape | Fichiers | Rôle |
|---|---|---|
| Lecture | `src/lexeur.c`, `lexeur.h` | Découpe le texte en jetons : mots, nombres, textes, dates, opérateurs, remarques. Normalise en NFC, reconnaît les apostrophes et les guillemets (grammaire, § 1). |
| Forme compacte | `src/compact.c` | Réécrit chaque instruction compacte (`_si x > 3 _alors`) en jetons de phrase littéraire. Tout ce qui suit ne voit donc que la forme littéraire (grammaire, § 11.4). |
| Analyse | `src/analyseur.c`, `analyseur.h` | Le cœur du langage, environ 5 100 lignes. Lit les phrases, résout les noms (plus longue correspondance, § 2.2), vérifie `vaut` et `devient`, le genre et les accords, la pureté des calculs, le typage des entités à l'analyse. Calcule aussi les suites attendues (§ 8). |
| Arbre | `src/arbre.c`, `arbre.h` | Les nœuds : `N_…` pour les valeurs, `P_…` pour les phrases. `arbre.h` documente les champs de chaque sorte de nœud. |
| Impression | `src/imprimeur.c` | Réécrit un arbre en forme littéraire canonique ou en forme compacte (`grym formater`, `grym traduire`, grammaire § 12). |
| Compilation | `src/compilateur.c` | Transforme l'arbre en instructions de machine à pile, un bloc par formule. |
| Bytecode | `src/bytecode.c`, `bytecode.h` | Les instructions (`CodeInstruction`), leur vérification avant exécution, le format du fichier `.grymb` et le désassemblage. |
| Machine | `src/vm.c`, `vm.h`, `vm_interne.h` | Exécute le bytecode : pile, cadres d'appel, objets, ramasse-miettes, journal d'annulation. |
| Base | `src/base.c`, `base.h` | Tout ce qui parle à SQLite : tables des entités, recherches, migrations, corbeille. |
| Affichage | `src/interface.h`, `console.c`, `serveur.c` | L'interface entre la machine et l'utilisateur (questions, formulaires, images). La console, le navigateur (`grym servir`) et l'atelier l'implémentent chacun. |

Autour : `decimal.c` (nombres décimaux exacts), `date.c` (calendrier), `texte.c` (chaînes et allocation), `chemins.c` (fichiers utilisés, § 21), `grym.c` (les commandes), `lsp.c` et `json.c` (aide à la saisie pour les éditeurs), `src/atelier/` (l'atelier en C++ et Qt).

## 2. Construire et essayer

```
make                 # grym, les outils de mise au point, les programmes d'essai
make test            # tous les essais du cœur ; chaque programme affiche « n/n tests réussis »
cmake -B construction && cmake --build construction    # l'atelier (Qt 6)
ctest --test-dir construction                           # ses essais, sans fenêtre
```

Sous sanitizers, qui détectent les fuites de mémoire et les comportements indéfinis du C :

```
make clean && make test CFLAGS="-std=c99 -Wall -Wextra -pedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
```

L'intégration continue (`.github/workflows/tests.yml`) fait tout cela à chaque `git push`, sous Linux, macOS et Windows. Un job rouge n'est jamais à ignorer.

## 3. Voir ce que le compilateur comprend

Quatre outils montrent chaque étape. On s'en sert avant d'écrire la moindre ligne de C, pour savoir où se situe un problème.

```
./grym-lexeur essai.grym         # les jetons :      1:1  MOT  le
./grym-arbre essai.grym          # l'arbre :         (créer [x] (+ 1 2))
./grym-suites essai.grym         # ce qui peut suivre la fin du fichier (§ 8)
./grym desassembler essai.grym   # les instructions : 0006  ADDITION
```

Si `grym-lexeur` découpe mal, le problème est dans `lexeur.c`. S'il découpe bien et que `grym-arbre` se trompe, il est dans `analyseur.c`. Si l'arbre est juste et que l'exécution se trompe, il est dans `compilateur.c` ou `vm.c` : `grym desassembler` tranche.

## 4. La recette : ajouter une phrase

Exemple déroulé : `Effacer l'écran.` (grammaire, § 4.3), une phrase simple qui ajoute aussi une instruction à la machine. Pour la voir en entier : `git log -S "P_EFFACER"`, puis `git show` sur le commit trouvé.

Un exemple plus riche, qui touche aussi les fichiers, les chemins et les messages : la phrase `Utiliser` (grammaire, § 21), dans le commit « Fichiers utilisés : Utiliser « données ». ».

### 4.1 D'abord la grammaire, jamais le code

La charte l'exige (art. 4) : une construction se conçoit d'abord en français courant. Avant toute ligne de C, on écrit dans `docs/grammaire.md` :

- la phrase, avec des exemples ;
- ses règles : où elle se place, ce qu'elle accepte, ce qu'elle refuse ;
- les messages d'erreur, mot pour mot ;
- sa forme compacte ;
- une ligne au journal des révisions, avec un numéro de version nouveau.

Un mot qui devient réservé, ou qui commence désormais une construction, casse les programmes qui l'utilisaient comme nom. La charte (art. 13.2) exige alors de l'inscrire au journal de la grammaire.

### 4.2 Le nœud : `src/arbre.h` et `src/arbre.c`

Ajouter une sorte de nœud à l'énumération `TypeNoeud`, avec un commentaire qui dit ce que portent ses champs :

```c
P_EFFACER,       /* « Effacer l'écran. » (§ 4.3) */
```

Puis sa description dans `decrire()` (`arbre.c`), qui sert à `grym-arbre` et aux essais :

```c
case P_EFFACER:
    chaine_ajouter(c, "(effacer-écran)");
    return;
```

### 4.3 L'analyse : `src/analyseur.c`

La fonction `phrase()` reconnaît chaque phrase par son premier mot. On y ajoute un cas :

```c
if (est_mot(t, "effacer") && voir(a, 1)->type == J_ELISION && est_mot(voir(a, 2), "écran")) {
    if (a->formule == 1)   /* un calcul ne produit aucun effet (§ 9.4) */
        return erreur(a, t, grym_dupliquer("Un calcul n'affiche rien : …"));
    a->i += 3;
    Noeud *n = noeud_creer(P_EFFACER, t->ligne, t->colonne, t->debut);
    if (!fin_phrase(a, 0)) { noeud_liberer(n); return NULL; }
    n->fin = fin_jeton(&a->j[a->i - 1]);
    return n;
}
```

Les outils de l'analyse :

- `cour(a)` est le jeton courant, `voir(a, k)` le k-ième suivant, `avancer(a)` passe au suivant ;
- `est_mot(t, "…")` compare un mot, sans tenir compte de la casse ;
- `erreur(a, jeton, message)` enregistre la première erreur, à la position du jeton, et renvoie `NULL` ;
- `fin_phrase()` exige le point final ;
- `valeur(a)` et `expression(a)` lisent une valeur ou un calcul.

Si le premier mot commence désormais une construction, l'ajouter à `mot_de_construction()` : il ne pourra plus commencer le nom d'une action. Pour qu'il soit proposé par l'aide à la saisie, l'ajouter aussi aux propositions de `suites_valides_fichier()` (le bloc `if (m & A_DEBUT)`).

### 4.4 L'impression : `src/imprimeur.c`

Chaque sorte de phrase s'imprime dans les deux formes. La variable `c` vaut vrai pour la forme compacte :

```c
case P_EFFACER:
    aj(im, c ? "_effacer\n" : "Effacer l'écran.\n");
    return;
```

Oublier cette étape casse `grym formater` et `grym traduire` : l'essai d'aller-retour le signalera.

### 4.5 La forme compacte : `src/compact.c`

La fonction de réécriture traite chaque mot-clé à souligné, et émet les jetons de la phrase littéraire équivalente :

```c
} else if (!strcmp(c, "effacer")) {   /* _effacer → Effacer l'écran. */
    if (d + 1 != f) { echouer(r, t, grym_dupliquer("« _effacer » s'écrit seul.")); return; }
    mot(r, "effacer", t);
    emettre(r, J_ELISION, "l", t, 1);
    mot(r, "écran", t);
    point(r, f);
```

### 4.6 La compilation : `src/compilateur.c`

La fonction `phrase()` du compilateur traduit chaque nœud en instructions :

```c
case P_EFFACER:
    emettre(c, I_EFFACER, 0, ph->ligne, ph->colonne);
    return;
```

Une phrase qui ne fait que déclarer (une classe, une remarque) n'émet rien.

### 4.7 L'instruction : `src/bytecode.h`, `src/bytecode.c`, `src/vm.c`

Seulement si la phrase demande à la machine un geste nouveau. Quatre endroits :

1. `bytecode.h` : ajouter `I_EFFACER` **à la fin** de `CodeInstruction`. Les numéros existants ne changent jamais : les anciens fichiers `.grymb` doivent rester lisibles.
2. `bytecode.c`, `instruction_nom()` : son nom en toutes lettres, pour le désassemblage (`"EFFACER"`).
3. `bytecode.c`, la vérification avant exécution : combien de valeurs l'instruction prend sur la pile (`besoin`) et combien elle en laisse (`effet`). Une erreur ici fait refuser des programmes justes, ou accepter des fichiers dangereux.
4. `vm.c`, la grande boucle de `machine_executer()` : ce que fait l'instruction.

```c
case I_EFFACER:
    m->iface.effacer(m->iface.contexte, sortie);
    break;
```

Puis augmenter `VERSION_FORMAT` dans `bytecode.c`, et décrire le changement dans `docs/vm.md` (§ 3 pour la table des instructions, § 11 pour le format, plus une ligne au journal).

### 4.8 Les essais : `tests/`

| Fichier | Ce qu'il éprouve | Forme d'un essai |
|---|---|---|
| `test_lexeur.c` | les jetons | voir le fichier |
| `test_analyseur.c` | l'arbre, les erreurs d'analyse, les suites | `V(source, "(effacer-écran)")`, `VS(source, "Le \| La …")` |
| `test_machine.c` | l'exécution, de bout en bout | `PROG(source, "sortie attendue")` ; `"ERREUR l:c message"` ; un attendu qui commence par `~` est un fragment |
| `test_imprimeur.c` | la forme canonique | `LITT(source, attendu)`, `COMP(source, attendu)`, `FIXE(source)` |
| `test_compact.c` | la forme compacte, l'aller-retour | voir le fichier |
| `test_base.c` | la base SQLite | voir le fichier |
| `test_lsp.c` | l'aide à la saisie des éditeurs | messages JSON échangés |
| `test_serveur.c` | le navigateur | pages HTML produites |
| `test_atelier.cpp` | l'atelier | fenêtres sans écran |

Pour une phrase nouvelle, au minimum : un essai d'arbre, un essai d'exécution, un essai de chaque message d'erreur, un essai d'impression dans les deux formes. Un essai qui échoue affiche sa ligne dans le fichier de test, la source, l'attendu et l'obtenu :

```
ÉCHEC (test ligne 383)
  source  : Pour utiliser un x : …
  attendu : ERREUR 1:6 « utiliser » commence une construction …
  obtenu  : …
```

### 4.9 Les éditeurs

- Coloration : `editeurs/vscode/syntaxes/grymoir.tmLanguage.json` (VS Code) et `src/atelier/coloration.cpp` (l'atelier), qui partagent la même liste de mots.
- L'aide de l'atelier se met à jour seule : elle embarque `docs/grammaire.md` à la construction.

### 4.10 La liste de contrôle

Pour chaque phrase nouvelle, cocher :

1. `docs/grammaire.md` : section, messages, forme compacte, journal.
2. `arbre.h` et `arbre.c` : le nœud et sa description.
3. `analyseur.c` : `phrase()`, et si besoin `mot_de_construction()` et les suites attendues.
4. `imprimeur.c` : les deux formes.
5. `compact.c` : la réécriture du mot-clé à souligné.
6. `compilateur.c` : les instructions émises.
7. Si instruction nouvelle : `bytecode.h`, `bytecode.c` (nom, vérification, `VERSION_FORMAT`), `vm.c`, `docs/vm.md`.
8. Les essais, dont chaque message d'erreur.
9. La coloration des deux éditeurs.
10. `make test`, les sanitizers, `ctest`, puis la charte si un jalon change.

## 5. Les règles qu'on ne transgresse pas

Elles viennent de la charte. Chacune a une raison, qu'un changement pressé oublie facilement.

- **Aucune donnée perdue en silence** (principe 1). Une exécution qui échoue n'écrit rien : le journal d'annulation (`vm.c`) et la transaction SQLite (`base.c`) y veillent. Toute écriture nouvelle passe par eux.
- **Même texte, même programme** (art. 4). Aucune heuristique, aucun hasard dans l'analyse.
- **Les deux formes produisent le même arbre** (art. 4). Tout ce qui s'écrit en forme littéraire s'écrit en forme compacte, et l'aller-retour ne perd rien.
- **Aucune erreur avalée** (art. 8). Chaque refus a son message, en français, à sa position.
- **Compatibilité** (art. 13). Un programme accepté hier l'est demain ; une base créée hier s'ouvre demain.

## 6. Ce qui ne demande pas de C

Certaines évolutions ne touchent que les documents ou des listes de mots :

- corriger ou préciser un message d'erreur : chercher son texte dans `src/` avec `grep`, le modifier, puis l'essai qui l'attend ;
- corriger la grammaire, l'aide ou ce manuel : du texte seulement.

Le reste du vocabulaire (mots de construction, synonymes) est encore dans le code C. Le déplacer dans des tables de données est prévu (`docs/atelier.md`, § 6.6).

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-24 | Manuel initial : trajet d'un programme, construction et essais, outils de mise au point, recette d'une phrase nouvelle (`Effacer l'écran.`), liste de contrôle, règles de la charte |
