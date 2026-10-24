# Utiliser Baawiil Monitor

## Démarrage

Sur Windows 11 x64, extraire le paquet portable puis ouvrir `baawiil.exe`, ou utiliser l'installateur de développement. Ne pas séparer les exécutables des bibliothèques et des plugins. L’installation et la désinstallation attendent la fermeture des composants actifs. Aucune connexion Internet ou élévation n'est requise pour le tableau de bord.

Le premier relevé CPU initialise les différences de compteurs. « Indisponible / initialisation » ne signifie pas zéro. CPU processus = pourcentage de la capacité machine ; mémoire résidente et mémoire privée désignent des compteurs différents.

## Graphiques et processus

Choisir la fenêtre dans le tableau de bord (ou Période pour une durée personnalisée), déplacer le curseur temporel, survoler les courbes et utiliser Pause pour figer l'affichage sans arrêter la collecte. Les boutons CPU/RAM exportent les relevés en CSV ou l'image en PNG. Les moyennes affichées sont arithmétiques et portent sur les relevés visibles disponibles.

Le bouton Widgets masque les courbes CPU/RAM et permet d'inverser leur ordre. Le clic droit sur les en-têtes de processus choisit les colonnes. Cliquer une colonne pour trier ; le champ de recherche filtre les noms, PID et chemins.

Un clic droit sur une ligne ouvre les propriétés, les modules, les handles, l'arborescence et les actions. Une confirmation précède les actions risquées. L'identité PID/date de création est revérifiée. La terminaison des processus critiques et de l'application elle-même est refusée. Un refus Windows est conservé ; la commande Administrateur utilise un composant séparé, puis demande une nouvelle confirmation de l'opération précise.

## Matériel, stockage et réseau

Dans les rubriques d'inventaire, cliquer Actualiser ou activer l'actualisation toutes les cinq secondes. Les commandes supplémentaires sont disponibles dans la barre, et dans son menu de débordement lorsque la fenêtre est étroite. Les actualisations automatiques d'inventaire s'arrêtent lorsque la page n'est pas visible.

GPU propose identité, moteurs, VRAM et capteurs. NVML est chargée uniquement depuis le répertoire système ; aucun pilote n'est téléchargé. Les capteurs WMI exigent un fournisseur LibreHardwareMonitor/OpenHardwareMonitor déjà disponible. Leur absence est un état attendu, pas une température de zéro. Les données avancées AMD/Intel dépendent du fournisseur installé.

Stockage propose disques, volumes, activité, santé disponible et analyse de dossiers. L'analyse ne supprime rien et ne suit pas les points de réanalyse des sous-dossiers. Elle classe les grands fichiers, agrège les dossiers de premier niveau et affiche leur proportion sous forme de barres ; les liens physiques ne sont pas dédupliqués. Une annulation arrête son processus de travail ; elle ne prétend pas fournir un résultat complet.

Réseau affiche interfaces, débits et connexions. Les tests DNS et ICMP partent uniquement après saisie volontaire de la destination. Le test ICMP envoie quatre requêtes ; une absence de réponse ne prouve pas une panne Internet. Les débits par processus nécessitant ETW ne sont pas encore disponibles.

## Services et démarrage

Choisir un service, puis démarrer, arrêter ou redémarrer. Les dépendances ne sont pas arrêtées automatiquement. Un état cible non atteint après dix secondes reste indiqué comme non confirmé. Administrateur déclenche explicitement UAC.

Démarrage inventorie les clés Run et les dossiers utilisateur/machine. La modification est limitée à l'utilisateur courant. Une entrée Run retirée est conservée sous `Software/Baawiil/DisabledRun`. Un fichier du dossier de démarrage est déplacé dans la sauvegarde locale `disabled-startup`, avec ses métadonnées de restauration. Une collision ou un déplacement non supporté est refusé, sans remplacement. Les politiques Windows, StartupApproved, tâches planifiées et StartupTask peuvent imposer un état différent de la seule présence d'une entrée.

## Historique et rapports

Choisir CPU, RAM et éventuellement les mesures supplémentaires disponibles, puis démarrer une session. Définir la durée de rétention et le quota de données actives. L'option automatique n'est activable qu'après configuration d'une première session. La fréquence des mesures supplémentaires est de cinq secondes ; leurs dates source sont conservées pour distinguer une valeur tenue d'une nouvelle mesure.

Ajouter des marqueurs et arrêter la session. Revenir à Sessions pour choisir une relecture ou une comparaison. Les courbes sont synchronisées CPU/RAM ; en comparaison, la fin des deux sessions est alignée. Les valeurs supplémentaires sont conservées dans les données exportables. Au-delà de 100 000 relevés, la relecture est décimée sur toute la période en préservant les extrêmes CPU/RAM et des lacunes ; elle n'est pas tronquée aux 100 000 premiers points.

Statistiques exactes calcule les agrégats sur tous les relevés valides. Exporter la session complète écrit les originaux CSV/JSON sans décimation. Les exports génériques d'une vue peuvent aussi produire HTML/PDF. Les fichiers historiques et exports peuvent révéler des informations sur la machine : conserver les exports dans un dossier choisi.

Conserver une session l'exclut de la purge automatique. Les sessions actives sont également exclues. Si la limite empêche de nouveaux enregistrements, ceux-ci s'arrêtent plutôt que supprimer silencieusement une session protégée. Le quota porte actuellement sur les pages SQLite actives ; des pages libres, sauvegardes de migration ou fichiers WAL peuvent occuper un espace supplémentaire. Après un arrêt brutal, les transactions non validées peuvent perdre jusqu'à dix relevés et la session est marquée interrompue à la prochaine ouverture.

## Diagnostics, alertes et benchmarks

Les diagnostics analysent la fenêtre observée, au maximum deux minutes : CPU/RAM, processus les plus actifs et croissance mémoire privée soutenue. Ils fournissent les seuils et les observations ; ils ne prouvent ni fuite mémoire ni cause certaine d'un ralentissement.

Les alertes sont désactivées par défaut. Définir seuils, durée minimale et temporisation ; les règles supplémentaires couvrent GPU/température GPU lorsque disponibles, occupation du volume système, perte des interfaces physiques observées et CPU d'un processus lié à sa date de création. Les valeurs absentes n'activent pas une alerte et ne valent pas retour à la normale. Les règles utilisent une hystérésis de trois points.

Chaque benchmark demande confirmation et dure cinq secondes hors préparation éventuelle. Les tests disque utilisent uniquement des fichiers temporaires dédiés, avec cache Windows ; ils ne mesurent pas un débit matériel brut et ne sont pas comparables à des suites tierces. Annuler ne crée pas de score terminé. La conservation d'un résultat dans l'historique est explicite.

## Paramètres et mises à jour

Les réglages courants sont en JSON dans le dossier de configuration utilisateur Qt, et les sessions dans `history.sqlite` dans le dossier local de l'application. Le journal borné `events.jsonl` ne contient que catégories et états, sans noms de processus, chemins ou adresses. Une configuration invalide ou de version future est conservée.

La sauvegarde/restauration des préférences couvre les fichiers connus uniquement et sauvegarde les originaux avant remplacement. Les automatismes réseau, alertes et enregistrements doivent être réactivés manuellement après import. Redémarrer après restauration.

L'option de zone de notification conserve l'application après fermeture de la fenêtre. Utiliser Afficher ou Quitter dans le menu de l'icône. Le lancement à l'ouverture de session est facultatif et ne nécessite pas de compte administrateur.

Le centre de mises à jour est inactif tant qu'aucun manifeste HTTPS et certificat signataire ne sont configurés. Ne renseigner que les paramètres publiés par l'éditeur de confiance. Aucune publication n'existe encore. Le téléchargement vérifie taille, SHA-256, signature Authenticode et certificat épinglé ; la révocation en ligne n'est pas vérifiée. Un paquet de développement non signé est refusé par ce mécanisme.

## Horodatages et disponibilité

La durée des nouvelles sessions et les délais des alertes utilisent une horloge monotone, indépendante des corrections de l’heure Windows. Les exports conservent l’heure UTC originale et `elapsedMs`. Une valeur matérielle ancienne est marquée périmée et ne compte pas comme un nouveau relevé. Un diagnostic demandé après l’arrêt de la collecte indique son indisponibilité.
