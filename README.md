# DSM1SAR

Système Scalable de RAM Distribuée et Partagée pour Clusters HPC

DSM1SAR est un intergiciel implémentant un système de Mémoire Partagée Répartie.&#x20;
Reposant sur une architecture scalable, il offre la possibilité à des processus s'exécutant sur des machines distinctes au sein d'un réseau de partager un espace mémoire commun, comme s'ils s'exécutaient sur une seule et même machine physique.

Le système prend la forme d'une API en C permettant aux développeurs d'allouer, de lire, d'écrire et de libérer de la mémoire distribuée en toute transparence, sans se soucier de l'emplacement physique des données, masquant ainsi la complexité.

### Compatibilité

Notre projet est compatible uniquement avec les systemes Linux, car userfaultfd n'est pas supporté sur les autres OS.

### Commande de tests 
`make test-config` : exécute la suite de tests validant la robustesse du système face aux configurations invalides et aux échecs de découverte réseau.

​
