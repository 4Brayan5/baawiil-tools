# Baawiil Monitor — Windows 11 x64

Gestionnaire des tâches local en C++20 / Qt 6 Widgets. Version de développement **0.2.0**, gratuite, sans compte, publicité ou télémétrie. Le nom public et la licence définitive restent à valider.

Le développement couvre désormais les quinze rubriques du cahier des charges. La couverture détaillée et les limites figurent dans [l'état d'implémentation](docs/etat-implementation.md). La présence d'une rubrique ne signifie pas que tous les capteurs et tous les critères de la version 1.0 sont validés.

- [Cahier des charges](docs/cahier-des-charges.md)
- [Plan technique initial](docs/phase-1-plan-technique.md)
- [Couverture et vérifications](docs/etat-implementation.md)
- [Guide utilisateur](docs/utilisation.md)
- [Recette Windows 11](docs/recette-windows.md)

## Télécharger l’installateur

**[Télécharger Baawiil Monitor pour Windows 11 x64 (.exe)](https://github.com/4Brayan5/baawiil-tools/raw/refs/heads/main/downloads/BaawiilMonitor-0.2.0-dev-setup.exe)**

Version de développement non signée, environ 78 Mio. [Empreinte SHA-256](downloads/SHA256SUMS.txt) · [État des fonctionnalités et limites](docs/etat-implementation.md).

## Fonctions présentes

CPU global et par processeur logique, RAM et engagement, processus avec recherche/tri, propriétés et signature locale, modules et handles accessibles, terminaison confirmée et arborescence, priorité et affinité. Graphiques horodatés, pause, survol, fenêtres jusqu'à 24 h, navigation temporelle et exports PNG/CSV.

Inventaires GPU, RAM, disques et système ; compteurs GPU/VRAM, stockage et réseau ; capteurs NVIDIA NVML ou fournisseur WMI facultatif ; connexions TCP/UDP IPv4/IPv6 ; tests DNS et ICMP volontaires ; services et démarrage utilisateur réversible ; analyse de dossiers en lecture seule.

Historique SQLite, marqueurs, sessions manuelles ou automatiques facultatives, sélection de mesures, relecture et comparaison, rétention et protection des sessions, export complet CSV/JSON. Diagnostics explicables, alertes avec durée minimale, temporisation et hystérésis, benchmarks CPU/mémoire/disque, rapports JSON/CSV/HTML/PDF. Thèmes, accentuation, colonnes, widgets CPU/RAM et zone de notification.

Les collectes longues s'exécutent dans `baawiil-worker.exe`, séparé de l'interface et arrêtable en cas de dépassement de délai. Les opérations administrateur passent par `baawiil-admin.exe`, avec UAC et confirmation précise. Le centre de mises à jour vérifie HTTPS, SHA-256 et le certificat signataire épinglé ; il ne contacte aucun serveur par défaut.

## Compiler sous Windows

Prérequis : Windows 11 x64, Visual Studio 2022 avec outils C++ et SDK Windows, CMake 3.24+, Qt 6.8.3 MSVC 2022 x64. Depuis un terminal développeur x64 :

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64" -DBAAWIIL_QT_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix out
./out/baawiil.exe
```

Conserver les trois exécutables et les DLL/plugins Qt ensemble. L'installateur généré dans cette session utilise Qt 6.8.2 et MinGW x64. La compilation MSVC du workflow est distincte de cette compilation croisée et doit encore être exécutée.

Le workflow [Windows](.github/workflows/windows.yml) compile, teste et prépare un artefact quand le code est envoyé sur GitHub. Aucun envoi ni workflow distant n'a été effectué depuis cette session.

## Tests sur le cloud Linux

Sur le cloud Debian 13 de cette session, `bash scripts/bootstrap-cloud.sh` prépare les outils, vérifie les tests et compile les exécutables Windows. Le script conserve les sources Qt à un commit fixé et garde les vérifications des paquets et de TLS. Une première construction de Qt est longue ; les sorties valides sont réutilisées aux exécutions suivantes.

L'application reste exclusivement Windows. Le cœur, le stockage et les composants d'interface peuvent être testés séparément sous Linux avec les dépendances Qt de développement :

```sh
cmake -S . -B build -DBAAWIIL_BUILD_APP=OFF -DBAAWIIL_QT_TESTS=ON -DBAAWIIL_UI_TESTS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Les tests d'interface utilisent un adaptateur sans appels Windows, uniquement pour contrôler navigation, modèles et rendu des graphiques. Cet adaptateur n'est jamais lié au logiciel Windows. Ces tests ne remplacent pas les essais matériels.

## Distribution et limites

Les sorties de cette session sont placées sous `dist/` : paquet portable, installateur de développement et sources. Le script [package_windows.py](scripts/package_windows.py) prépare les DLL, plugins, empreintes et notices du paquet croisé. [windows.nsi](packaging/windows.nsi) construit un installateur utilisateur et supprime à la désinstallation uniquement les fichiers explicitement installés.

Le paquet de développement n'est pas signé. Le canal de mises à jour n'est pas publié. La licence de l'application, l'audit final des dépendances et la signature de distribution restent à finaliser. Les objectifs de ressources, la précision des mesures, l'accessibilité et l'endurance doivent être mesurés sur de vrais Windows 11 avant toute annonce de version 1.0 stable.
