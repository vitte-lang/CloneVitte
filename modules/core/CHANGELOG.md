# 0.1.0

- Ajout du type bottom `never` à l'analyse sémantique bootstrap et native, au
  HIR et au backend C17; les appels divergents sont compatibles avec le type
  attendu et les fonctions Core `panic`/`unreachable` exposent maintenant ce
  type. Un test couvre aussi l'alias public `Never` et sa conversion vers
  `usize`.
- Ajout des comparaisons de tolérance testées, des algorithmes sur tranches
  empruntées et des tests runtime pour les limites arithmétiques, les décalages,
  la recherche de chaînes et les cellules inline.
- Les recherches de chaînes, LCM, puissances de deux et décalages vérifiés
  utilisent un booléen et une sortie mutable, sans allocation ni pointeur
  optionnel vers une valeur temporaire.
- Les parseurs bootstrap et natif acceptent les formes génériques `Box[T] { ... }`;
  un fixture runtime couvre cette syntaxe sur les deux chemins.
- Correction des pointeurs de sortie des formateurs en `*mut u8`; ajout d'un
  gate `make test-core` exécutant 18 suites sur le bootstrap C17.
- Correction des rotations 8 et 16 bits par réduction explicite à la largeur
  déclarée; le statut des sources non encore compilables est chiffré dans
  `README.md`.
- Ajout du smoke Core natif au runtime et au corpus, avec propagation du chemin
  des modules; la matrice self-host exécute maintenant ce smoke sur les trois
  systèmes supportés par ses runners.
- Validation Actions observée : runtime, Core, parité, corpus, self-host 0 à 3
  et installation de paquets passent sous Linux, macOS et Windows.
- Harmonisation des résultats fallibles autour de `success`, `value` et
  `error`; `types::Result.ok` devient `types::Result.success`.
- Renommage canonique des sorties arithmétiques en `CheckedI64Result` et
  `CheckedU64Result`, avec alias source conservés pour les anciens noms.
- Retrait des constructeurs génériques de `Result<T, E>` tant que le compilateur
  ne peut pas construire ses charges optionnelles; les accesseurs restent
  disponibles et la limite est documentée dans `README.md`.
- Ajout de `CheckedError` pour distinguer débordement et division par zéro;
  les résultats de parsing étroits et UTF-8 exposent désormais `error_index`.
- Génération de l'index complet des déclarations exportées dans `API.md` et
  documentation des contrats de résultat, d'erreur et d'absence dans `README.md`.
- Ajout d'implémentations Vitte dans les modules sources de `core`.
- Ajout d'API pour les primitives, les vues de collections, les itérateurs, les
  conversions, les nombres, la mémoire, le formatage, le hachage, les erreurs,
  les cellules et la synchronisation.
- Déclaration des opérations dépendant du compilateur sous forme d'intrinsics
  Vitte, sans imports C.
- Stabilisation des résultats de comparaison, des plages, des options et des
  opérations arithmétiques vérifiées.
- Ajout d'un ordre total documenté pour les flottants, de contrats de traits
  d'affectation et de formatage sans allocation.
- Retrait du faux marqueur `PhantomPinned` et du doublon `traits::Copy`.
- Documentation du travail d'intégration encore requis dans le backend natif.
- Validation des layouts de tableaux avec padding, décalages génériques de
  pointeurs et vérification d'alignement avant accès typé.
- Ajout de variantes d'allocation alignée, de primitives volatiles explicites,
  de comptages de bits et de validations d'ordres atomiques.

## Avancées des modules ciblés

- Complétion des conversions entières vérifiées jusqu'aux largeurs 8/16/32/64
  bits, des parseurs entiers étroits et des formats signés par base.
- Ajout des opérations d'exposant vérifiées, de reste vérifié, de calcul modulo
  sans débordement intermédiaire et de multiplication saturante/débordante.
- Arrondi du formatage fixe flottant, écriture UTF-8 des caractères et rendu
  direct des arguments typés dans un formateur borné.
- Durcissement des hachages modulo 2^64, validation des plages source et ajout
  d'erreurs localisées.
- Extension des wrappers atomiques, attente dans `Once` et déverrouillage
  vérifié de `SpinLock`.
- L'abaissement des intrinsics atomiques et de piège par le backend reste
  nécessaire avant que ces APIs puissent être considérées natives de bout en
  bout.
