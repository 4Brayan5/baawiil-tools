# Plan technique — Phase 1

Version : 1.0 — 23 octobre 2026  
Statut : plan initial de référence ; le développement est désormais autorisé et en cours. Voir [l’état courant](etat-implementation.md).

## 1. Résultat attendu

Une application Windows 11 x64 exécutable avec tableau de bord CPU/RAM, vues processeur et mémoire, liste des processus triable/filtrable, courbes temps réel et paramètres locaux. Thèmes sombre/clair, adaptation DPI et navigation clavier dès cette phase.

Les données proviennent de Windows. Aucun jeu de données simulé ne sera présenté comme une mesure réelle. Les actions destructrices, GPU, pilotes, services, démarrage, historique persistant, benchmarks et mises à jour attendent les phases suivantes.

## 2. Socle et organisation proposés

C++20, Qt 6 Widgets, CMake, Ninja ou générateur Visual Studio, MSVC x64 et SDK Windows. Choisir et verrouiller les versions compatibles avant le premier build ; documenter provenance et licences. Préférer Qt dynamique et un tracé QWidget/QPainter simple pour les premières courbes ; ce choix évite d'ajouter immédiatement une dépendance graphique. L'audit de licence reste nécessaire.

Structure proposée, à créer pendant le développement :

```text
CMakeLists.txt
CMakePresets.json
src/app/                 assemblage et cycle de vie
src/ui/                  fenêtres, modèles Qt et courbes
src/core/                échantillons, cadence et tampons bornés
src/platform/windows/    collecteurs et wrappers de handles
src/config/              lecture, validation et écriture JSON
tests/                   tests du cœur et intégration Windows
docs/                    architecture, mesures et validation
```

Le cœur expose des types indépendants de l'interface ; les composants Windows restent derrière des interfaces explicites. Pas de service administrateur ni de pilote en phase 1. SQLite n'est pas nécessaire avant l'historique persistant.

## 3. Contrats de données

Un échantillon contient valeur/unité, instant monotone, date UTC, source et état de disponibilité. Pour une valeur issue d'une différence, conserver aussi l'intervalle réellement observé. Le premier relevé initialise la référence sans afficher un faux zéro.

Les instantanés sont immuables après publication. Les files sont bornées : l'interface peut consommer le dernier instantané plutôt qu'accumuler du retard. Les séries ont une capacité liée à la fenêtre demandée. La pause visuelle conserve la collecte et fige une copie bornée de la vue.

Un processus est identifié par PID et date de création lorsque accessible. En cas d'accès refusé, garder l'entrée avec ses champs indisponibles et ne pas associer des deltas à un PID dont l'identité ne peut pas être établie. À la disparition ou au changement d'identité, abandonner la référence précédente.

## 4. Collecte CPU et mémoire

| Indicateur phase 1 | Source proposée | Calcul et limites |
| --- | --- | --- |
| Charge CPU globale | GetSystemTimes | Différences des temps : actif = noyau + utilisateur − inactif ; le temps noyau inclut l'inactif. Échantillons invalides écartés. Vérifier les limites des groupes de processeurs. |
| CPU par processeur logique | PDH, famille Processor Information | Résoudre les compteurs sans dépendre de la langue Windows ; établir la topologie et les groupes. Ne pas confondre Processor Time et Processor Utility. |
| Topologie CPU | GetLogicalProcessorInformationEx | Cœurs, processeurs logiques et groupes ; ne pas supposer au plus 64 processeurs logiques. |
| Mémoire physique | GlobalMemoryStatusEx | Total disponible pour Windows et disponible ; utilisée = total − disponible. La RAM installée physiquement est une information distincte. |
| Engagement et système | GetPerformanceInfo | Engagement et limite en octets après conversion depuis les pages ; compteurs processus/threads si pertinents. |
| Inventaire processus | CreateToolhelp32Snapshot | PID, parent et nom ; le lien parent peut devenir périmé après réutilisation du PID. |
| CPU processus | GetProcessTimes | Delta noyau + utilisateur / durée réelle / nombre de processeurs logiques pour l'échelle machine ; topologie et changements à gérer. |
| Mémoire processus | GetProcessMemoryInfo | WorkingSetSize et PrivateUsage nommés séparément ; aucun total naïf des working sets présenté comme RAM système utilisée. |
| Chemin et lancement | QueryFullProcessImageNameW, GetProcessTimes | Accès minimal nécessaire, champs indisponibles sur refus. Aucun recours systématique à l'élévation. |

GetSystemTimes possède des limites sur les machines à plusieurs groupes : valider une agrégation PDH cohérente et l'utiliser si nécessaire. Les compteurs PDH seront testés sur Windows français et anglais, avec noms indépendants de la langue lorsque supportés. Aucun repli silencieux vers une mesure différente.

La phase 1 garantit la charge CPU, la topologie, la mémoire physique, les compteurs processus disponibles et une liste lisible. Fréquences avancées, interruptions, pagination détaillée, cache, SPD et capteurs sont différés ou signalés indisponibles ; ils restent dans le périmètre global.

## 5. Ordonnancement et interface

Collecteurs CPU/mémoire et processus séparés sur des travailleurs dédiés, communication avec Qt par signaux en file. Cadence par défaut 1 s ; les préférences 250/500 ms, 2/5 s sont appliquées uniquement aux sources compatibles. Mesurer le coût de l'énumération processus avant d'autoriser sa cadence la plus courte.

Annulation coopérative et arrêt ordonné, sans arrêt forcé d'un fil. Les appels Windows de phase 1 doivent être bornés autant que possible ; une intégration future pouvant se bloquer impose une isolation plus forte.

Courbes CPU/RAM sur 60 s et 5 min, avec survol, pause et statistiques de la fenêtre visible. Les fenêtres longues, le zoom avancé et les exports graphiques suivent les phases ultérieures. Décimer selon la largeur en conservant les extrêmes. Repeindre seulement lorsque nécessaire ; 60 FPS n'impose pas 60 lectures système par seconde.

QAbstractTableModel et proxy de tri/filtre pour les processus, rafraîchissement incrémental. Colonnes initiales : nom, PID, CPU machine, mémoire résidente, mémoire privée, threads, date de lancement et chemin accessible. Conserver la sélection par identité, pas par numéro de ligne. Toutes les actions de gestion de processus attendent la phase 3.

## 6. Configuration et confidentialité

JSON versionné sous QStandardPaths::AppConfigLocation, écrit atomiquement avec QSaveFile. Valider types, bornes, taille maximale et valeurs d'énumération. Préserver un fichier invalide avant d'appliquer les valeurs par défaut et informer l'utilisateur. Ne pas écraser silencieusement une version de schéma future.

Enregistrer thème, cadence, fenêtre graphique et colonnes. Aucun secret, compte ni accès réseau requis. Journaux locaux bornés ; pas de dump automatique des chemins/processus. Les paramètres de démarrage automatique et de tray restent ultérieurs.

## 7. Séquence de réalisation après validation

1. Fixer versions Windows/Qt/MSVC/SDK, méthode d'acquisition, licences et machines de référence ; créer build et CI Windows.
2. Définir types, horloges, états et tampons ; vérifier les calculs et limites.
3. Implémenter et comparer CPU/mémoire aux outils Windows sur des fenêtres alignées.
4. Ajouter l'inventaire processus, gérer disparitions, refus d'accès et réutilisation des PID.
5. Assembler les vues, courbes, thèmes, DPI et configuration persistante.
6. Produire un exécutable de développement et ses dépendances Qt, vérifier sur une machine Windows 11 sans Qt installé, puis effectuer la recette.

Chaque étape produit un résultat vérifiable avant la suivante. Aucun installateur signé n'est exigé en phase 1 ; le paquet de développement doit contenir les dépendances et notices nécessaires.

## 8. Validation proposée

Les valeurs ci-dessous sont des critères de recette proposés, à confirmer avec les machines de référence ; elles ne sont pas des résultats acquis.

| Contrôle | Méthode et résultat attendu |
| --- | --- |
| Cœur | Tests de deltas, première mesure, intervalles nuls, états indisponibles, réutilisation PID, bornes des tampons et JSON invalide. |
| Build | Build MSVC x64 et exécution des tests sous Windows ; échec explicite du build applicatif sur une plateforme non supportée. |
| CPU | Repos puis charge contrôlée, mêmes compteurs et fenêtres de 60 s ; écart moyen absolu proposé ≤ 5 points de pourcentage avec la référence documentée. Distinguer Utility et Time. |
| RAM | Comparer les mêmes compteurs sur prélèvements rapprochés ; cible proposée : écart ≤ max(64 Mio, 1 % du total physique), en tenant compte du décalage temporel. |
| Processus | Charge CPU connue, allocation/libération mémoire, création/arrêt rapides ; pas de crash, fausse continuité ou valeur négative. Accès refusés visibles. |
| Résilience | Échec/retard injecté d'un collecteur sans blocage de navigation ; données périmées indiquées et arrêt propre. |
| Configuration | Redémarrage avec thèmes/colonnes/cadence conservés ; fichiers tronqués et schémas inconnus traités sans perte silencieuse. |
| Interface | Clavier, lecteur d'écran sur contrôles principaux, contraste et DPI 100/150/200 %, déplacement entre écrans. |
| Hors ligne | Démarrage et surveillance sans réseau, sans demande de compte ni tentative réseau applicative. |
| Déploiement | Lancement sur Windows 11 x64 sans outils de développement/Qt préinstallés, avec vérification des dépendances. |
| Endurance | Session de 8 h avec charge variable ; mémoire bornée après remplissage des tampons, handles stabilisés, collecte et interface toujours actives. Toute croissance persistante est investiguée. |
| Sobriété | Mesurer CPU moyen sur 10 min au repos après préchauffage, mémoire privée et working set, lancement à froid/à chaud ; objectifs globaux < 1 % CPU, < 150 Mo et < 3 s sur référence. Documenter versions et protocole. |

Les essais matériels incluront au minimum Windows 11 français/anglais et un CPU courant multicœur. Une machine à plusieurs groupes de processeurs est nécessaire pour revendiquer cette prise en charge ; à défaut, limitation explicite et contrôle de cohérence, sans prétendre l'avoir testée.

Une CI Windows vérifie compilation et tests automatisés ; elle ne prouve ni compatibilité Windows 11 réelle, ni précision matérielle, ni consommation au repos. Les validations manuelles doivent nommer OS, matériel, versions, durée et résultats.

## 9. Environnement cloud et conditions de démarrage

Lors de la rédaction initiale de ce plan, le dépôt ne contenait pas encore de code applicatif. Le cloud Linux permet l'édition et la revue documentaire ; il ne fournit pas MSVC ni l'exécution des API Windows. Aucun script d'installation n'est ajouté tant que les versions et dépendances ne sont pas fixées.

Avant l'implémentation : valider ce périmètre, choisir les versions et disposer d'une chaîne de compilation Windows ainsi que d'une machine Windows 11 de recette. Les tâches cloud utiliseront le checkout existant ; aucun worktree supplémentaire n'est nécessaire par défaut.

La phase 1 est terminée lorsque les capacités annoncées fonctionnent dans le paquet Windows et que la recette est consignée. Les objectifs non atteints, tests non exécutés et fonctionnalités différées doivent être explicitement recensés avant acceptation.
