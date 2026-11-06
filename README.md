# Compilateur Vitte

[![Vitte CI](https://github.com/vitte-lang/vitte/actions/workflows/ci.yml/badge.svg)](https://github.com/vitte-lang/vitte/actions/workflows/ci.yml)

Ce dépôt contient le compilateur officiel de Vitte. Le seed est en C17 ; le
binaire principal utilise un frontend écrit en Vitte et quelques adaptateurs C
pour l’OS et les outils hôtes. Le pipeline du compilateur est :

```text
source -> lexer -> parser/AST -> semantic analysis -> HIR -> IR -> C17
```

## Build

Prérequis : un compilateur C17, `make` et Python 3 pour les vérifications.

```sh
cc --version
make --version
python3 --version
```

### Seed C : `vitte-bootstrap`

`vitte-bootstrap` est le compilateur seed. Make le construit depuis les sources
C17 du dépôt. Il sert ensuite à compiler l’API du frontend natif, écrite en
Vitte, vers une unité C.

```sh
make bootstrap
./build/bin/vitte-bootstrap --version
```

À la première construction, Make affiche notamment :

```text
Built Vitte bootstrap compiler:
  build/bin/vitte-bootstrap
Bootstrap compiler ready: build/bin/vitte-bootstrap
vitte <version indiquée dans VERSION>
```

Sous Windows avec MSYS, `make bootstrap` construit également le seed ; son
exécutable s’appelle `build/bin/vitte-bootstrap.exe`.

### Frontend natif : `vitte`

La construction normale lance le seed si nécessaire, génère le C de l’API
native, puis lie `build/bin/vitte` :

```sh
make
./build/bin/vitte --version
```

La commande affiche `vitte` suivi de la version inscrite dans `VERSION`.

Les deux exécutables ont des chemins distincts. Utilisez le seed pour examiner
ou déboguer l’étape initiale ; utilisez `vitte` pour le frontend natif habituel.
Les commandes acceptées et les constructions compilées dépendent du frontend
et du support courant ; voir la [matrice de support](docs/reference/support.html).

### Vérifier le seed

Cette cible vérifie le seed directement : version, imports et diagnostics,
puis compilation et exécution de l’exemple `Hello from Vitte!`. Elle ne lance
pas la construction du frontend natif.

```sh
make bootstrap-check
```

Lignes de résultat à repérer, en plus des commandes Make affichées :

```text
vitte <version indiquée dans VERSION>
Vitte compiler
language: Vitte
frontend: C17 bootstrap
Bootstrap targeted import and diagnostic regressions passed.
Hello from Vitte!
Bootstrap seed checks passed.
```

### Stages 0 à 3 du gate self-host

Le gate part du `build/bin/vitte` construit ci-dessus comme stage 0. Le stage 0
génère le C de l’API du stage 1 ; le stage 1 régénère cette API pour le stage 2,
puis le stage 2 la régénère pour le stage 3. Chaque stage est construit dans
son propre sous-dossier de `build/selfhost/` et passe les corpus de grammaire,
frontend natif et parité.

```sh
make MODE=release bootstrap-selfhost
```

Le gate compare les sorties C des compilateurs successifs. Les messages de
réussite attendus incluent :

```text
[SELFHOST] stage0 and stage1 API output match
[SELFHOST] stage1 and stage2 API output match
Native compiler self-host gate passed through stage3.
```

### Critère de fin : compilateur autonome écrit en Vitte

Le gate stage 0–3 prouve que le frontend Vitte se compile de façon stable ; à
lui seul, il ne prouve pas que le binaire principal a cessé d’utiliser le
compilateur C historique. La migration sera terminée quand toutes les
conditions suivantes seront réunies :

1. `build/bin/vitte` entre dans le dispatcher Vitte et retourne son résultat
   sans appeler `vitte_main_entry` ni aucun autre pilote CLI de secours.
2. Toutes les commandes promises par la CLI sont dispatchées et exécutées par
   des modules Vitte. Une commande non disponible doit produire un diagnostic
   Vitte explicite, jamais basculer vers le compilateur historique.
3. Le lien du binaire principal ne contient aucun objet qui implémente le
   lexer, le parseur, l’analyse sémantique, les imports, les diagnostics, le
   backend ou le pilote du compilateur sous `src/bootstrap/`.
4. Le C restant est limité à une liste documentée d’adaptateurs système
   minimaux : entrée du processus, allocation, fichiers et chemins, sortie,
   environnement et lancement des outils hôtes. Ces adaptateurs ne prennent
   aucune décision de compilation.
5. Un audit du lien confirme l’absence des symboles du pilote C historique ;
   la parité CLI, les corpus du langage et les stages de self-host passent sur
   Linux, macOS et Windows.

Le bootstrap peut servir de compilateur seed pendant la construction et rester
archivé pour cette étape. Il ne peut ni participer au lien de `vitte`, ni
prendre en charge une commande à l’exécution. Le travail en cours a déjà
raccordé l’entrée du processus au dispatcher Vitte et limité les objets
sélectionnés par le lien principal aux adaptateurs de frontière. La compilation
release du frontend natif et la génération C17 de l’API passent désormais.
`vitte install` est maintenant dispatché vers un module Vitte avec vérification
SHA-256 et mise à jour transactionnelle des fichiers du projet. L’audit détaillé
des symboles et les gates de parité/self-host sur Linux, macOS et Windows restent
à confirmer.

Les stages vérifiés sont mémorisés sous `build/selfhost/`. Pour reprendre après
un échec, relancez directement la cible du stage concerné :

```sh
make MODE=release bootstrap-stage1
make MODE=release bootstrap-stage2
make MODE=release bootstrap-stage3
```

Chaque cible construit et vérifie les prérequis manquants, puis le stage visé.
Les stages déjà réussis et à jour sont réutilisés. Une erreur au stage 2 se
reprend donc avec `make MODE=release bootstrap-stage2`; le stage 1 ne repasse
ses vérifications que si ses entrées ont changé ou si sa marque de réussite
manque. Le gate complet `bootstrap-selfhost` utilise les mêmes cibles internes.
En cas d’échec, le diagnostic indique le stage et l’opération, les compilateurs
impliqués, les chemins C générés et la cible Make à relancer. La commande
échouée est aussi affichée sous forme de ses arguments.

Chaque opération conserve ses preuves sous
`build/selfhost/evidence/<date>-<commit>-<pid>/stageN/`. Le fichier
`stage-N-summary.txt` indexe les opérations ; chaque fichier `.txt` donne le
résultat, la version Vitte, le commit Git, l’état modifié ou propre du dépôt,
les options de compilation, les compilateurs, les fichiers C et la commande de
reprise. Le `.log` correspondant conserve la sortie complète, notamment les
résultats des corpus de grammaire, du frontend et de parité. Les dossiers des
stages déjà validés restent attachés à leur exécution d’origine quand une
cible reprend plus tard.

### Diagnostiquer une divergence entre sorties C

Les deux comparaisons portent sur ces paires de fichiers :

| Étape de comparaison dans le diagnostic | Sortie du compilateur précédent | Sortie du suivant | Cible à relancer |
| --- | --- | --- | --- |
| `compare generated stage1 and stage2 API C` | `build/selfhost/stage1-api.c` | `build/selfhost/stage2-api.c` | `make MODE=release bootstrap-stage2` |
| `compare generated stage2 and stage3 API C` | `build/selfhost/stage2-api.c` | `build/selfhost/stage3-api.c` | `make MODE=release bootstrap-stage3` |

Exécutez `diff -u` sur la paire indiquée pour repérer la première différence :

```sh
diff -u build/selfhost/stage1-api.c build/selfhost/stage2-api.c
diff -u build/selfhost/stage2-api.c build/selfhost/stage3-api.c
```

Exécutez seulement la commande correspondant à la comparaison en échec.
`diff -u` renvoie le code 1 quand il trouve des différences ; c’est le résultat
attendu pour ce diagnostic. Une sortie vide avec code 0 signifie que les
fichiers sont identiques. Le diagnostic Make donne aussi le manifeste et le
journal de l’étape, sous `build/selfhost/evidence/.../stage2/` ou `stage3/`.
Le manifeste précise le compilateur, le commit, les options et la commande de
reprise ; le journal conserve les sorties standard et d’erreur des commandes,
dont les vérifications du stage ; le manifeste indique le statut exact de la
comparaison `cmp -s`.

Après correction, relancez la cible indiquée avec le même `MODE` que le cycle
initial. L’échec ne rafraîchit pas la marque de réussite du stage ; une marque
plus ancienne reste périmée et Make reprend ce stage. Les stages précédents
validés sont conservés et réutilisés. Pour repasser ensuite toute la chaîne,
lancez `make MODE=release bootstrap-selfhost` : Make ne reconstruit que les
sorties manquantes ou périmées.

Ne lancez pas `make clean`, `make distclean` et ne supprimez pas `build/` ou
`target/` pour diagnostiquer cette divergence. Ces commandes enlèveraient des
artefacts sans aider à localiser la différence ; le parcours de reprise garde
les autres compilateurs, sources C, marques de validation et journaux.

### Gate CI avant publication

La CI appelle le workflow self-host réutilisable sur Linux, macOS et Windows.
Les vérifications d’installation des paquets attendent la réussite de ce gate,
et le job agrégé `Release readiness` échoue si les tests compilateur, self-host
ou paquets échouent ou sont ignorés. Les preuves self-host sont publiées comme
artefacts Actions par système.

#### Résultats observés au 6 novembre 2026

Le [run CI 42292113000](https://github.com/vitte-lang/vitte/actions/runs/42292113000), sur le commit [`874e2e6`](https://github.com/vitte-lang/vitte/commit/874e2e6d3163ccec042d9c5bacc6bba457c5bf07), a terminé avec succès :

| Gate | Résultat observé |
| --- | --- |
| Tests compilateur et runtime | Réussi : `make test` (16 tests de charge inclus), `make test-runtime`, critères de release, invalidation incrémentale et inventaire des paquets. |
| Self-host, parité native, corpus et stages 0–3 | Réussi sous Linux, macOS et Windows (3/3). |
| Construction et installation des paquets natifs | Réussi sous Linux (`.deb`), macOS (`.pkg`/`.dmg`) et Windows (NSIS), avec vérification des manifestes, installation réelle et smoke `vitte install` (3/3). |
| Extension et DAP natif | Réussi. |
| Matrice multiplateforme demandée | **100 % : 6/6 jobs** (3 self-host et 3 installations de paquets). |
| Gate agrégé `Release readiness` | **Réussi ; statut global du gate : 100 %.** |

Le job compilateur a exécuté les 16 tests de charge en 389,6 s sur le runner
Linux, avec une entrée de 2 Mio en profil debug et un délai maximal de 180 s
par processus. Le profil release conserve les entrées de 48 Mio. Sur le poste
macOS local, `make MODE=release SHELL=/bin/zsh compiler-max-gate-fast` et
`make MODE=release SHELL=/bin/zsh bootstrap-selfhost` ont également réussi ; le
corpus a vérifié 93 cas positifs, 18 cas négatifs, zéro écart connu et zéro
échec (2 cas ignorés). Les stages self-host 0–3 ainsi que les commandes CLI
`help`, `version`, `explain` et `install`, y compris le rollback d’installation,
ont été vérifiés.

La branche `main` exige le statut `Release readiness`, avec les vérifications à
jour et sans contournement administrateur. Les règles de protection GitHub sont
une configuration du dépôt distincte des fichiers du workflow ; publiez une
version depuis `main` seulement après la réussite de ce statut.

Pour la comparaison rapide du parseur, de l’AST et de l’analyse sémantique entre
le seed et le frontend natif, utilisez :

```sh
make test-native-bootstrap-parity
```

Sorties attendues pour les cas actuellement suivis :

```text
Native/bootstrap parser and AST parity passed: 7 cases.
Native/bootstrap semantic parity passed: 4 cases.
```

La suite de régression complète se lance avec `make test`. CMake est également pris en
charge avec `cmake -S . -B build && cmake --build build`.

La couverture actuelle du langage et ses limites connues sont répertoriées dans
`src/docs/language_coverage.md`.
