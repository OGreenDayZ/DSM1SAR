# DSM1SAR

Système Scalable de RAM Distribuée et Partagée pour Clusters HPC

DSM1SAR est un intergiciel implémentant un système de Mémoire Partagée Répartie.&#x20;
Reposant sur une architecture scalable, il offre la possibilité à des processus s'exécutant sur des machines distinctes au sein d'un réseau de partager un espace mémoire commun, comme s'ils s'exécutaient sur une seule et même machine physique.

Le système prend la forme d'une API en C permettant aux développeurs d'allouer, de lire, d'écrire et de libérer de la mémoire distribuée en toute transparence, sans se soucier de l'emplacement physique des données, masquant ainsi la complexité.

## Compatibilité

Notre projet est compatible uniquement avec les systèmes Linux, car userfaultfd n'est pas supporté sur les autres OS.

## Documentation
La documentation complète se trouve dans `Documentation/`. 
Elle couvre en détail les aspects suivants :
- Les slides de présentation : [Slides.pdf](Documentation/generated/DSM1SAR_Presentation.pdf)
- Les diagrammes de séquence : [Sequence_Diagrams.pdf](Documentation/generated/DSM1SAR_Diagrammes.pdf)
- Le rapport final du projet : [Rapport.pdf](Documentation/generated/DSM1SAR_Rapport.pdf)

## Commandes
- `make re` : compile le projet et génère les fichiers nécessaires à son exécution.
- `make re tests` : compile le projet et génère les fichiers nécessaires à l'exécution des tests.
- `make clean` : supprime les fichiers générés par la compilation du projet.
- On peut choisir d'afficher les logs de la DSM : 
Vvoir .PHONY dans le Makefile, `debug info warning error max_debug max_info max_warning max_error`

## Tests 
- `test-config` : démontre la capacité du système à agir comme un filtre strict.
- `test-simple` : garantit que les opérations basiques de lecture et d'écriture déclenchent correctement les interceptions mémoire. Elle valide également que le rapatriement des pages et le maintien de la cohérence via l'acquisition des verrous distribués s'effectuent sans corruption de données.
- `test-sync` : assure que notre démon réseau isole et gère parfaitement les accès
concurrents locaux.
- `test-integrity` : valide formellement l'absence de concurrence et de mises à jour perdues sur le réseau.
- `test-profiling` : mesure précisément la latence introduite par l'interception des accès mémoire (via userfaultfd) et la résolution réseau associée.
- `test-false-sharing` : validant la gestion du false sharing par la DSM.
- `test-matrix` : évalue les performances de la DSM dans un scénario de calcul intensif, en mesurant les temps d'exécution et la bande passante effective lors de l'accès à des matrices partagées.