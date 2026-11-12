# Module JSON pour Vitte

Le module `json` fournit un lexer, un parser strict, une représentation
`JsonValue`, des diagnostics structurés et un encodeur JSON. Il est écrit en
Vitte et n’a aucune dépendance native.

La release officielle est **0.1.1**. Elle est associée au tag immuable
[`v0.1.1`](https://github.com/Vitte-Foundation/json/releases/tag/v0.1.1).
L’archive et sa somme SHA-256 sont jointes à cette release. Le guide complet
se trouve dans [DOCUMENTATION.md](DOCUMENTATION.md), et la référence en ligne
sur [vitte-lang.org](https://vitte-lang.org/reference/json/).

## Installation depuis l’archive

Téléchargez `vitte-json-0.1.1.tar.gz` depuis la
[release 0.1.1](https://github.com/Vitte-Foundation/json/releases/tag/v0.1.1),
vérifiez le fichier `.sha256` joint, puis extrayez et installez le paquet :

```sh
tar -xzf vitte-json-0.1.1.tar.gz
cd vitte-json-0.1.1
make VITTE=/chemin/vers/vitte verify
make install PREFIX="$HOME/.local"
```

Le Makefile détecte un compilateur `vitte` dans le `PATH` ; le paramètre
`VITTE` permet de donner son chemin complet si nécessaire. Le module est installé dans
`$HOME/.local/share/vitte/modules/json`. Pour une installation en staging :

```sh
make install PREFIX=/usr/local DESTDIR="$PWD/build/stage"
```

Le parent `share/vitte/modules` peut être ajouté à `VITTE_MODULE_PATH` si le
compilateur ne le découvre pas automatiquement :

```sh
export VITTE_MODULE_PATH="$HOME/.local/share/vitte/modules"
```

## Compatibilité et vérification

Cette version a été vérifiée avec le bootstrap Vitte 0.1.1. Depuis la racine
du paquet, lancez `make VITTE=/chemin/vers/vitte verify`. Cette cible vérifie
la disposition, installe le module dans un répertoire temporaire, compile et
exécute un consommateur du parser et de l’encodeur, puis vérifie la
concaténation de chaînes.

Les sources internes conservées comme brouillons dans le dépôt ne sont pas
installées. Les API maintenues du parser et de l’encodeur sont
`json::parser::parser` et `json::encode::encoder`.

Le compilateur ne fournit pas encore de commande générale `vitte install` ;
utilisez l’archive versionnée et les instructions ci-dessus.

## Licence

Distribué sous la [VitteFoundation Public License 1.0](LICENSE).
