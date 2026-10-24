# Cahier des charges consolidé

Version : 1.2 — 23 octobre 2026  
Statut : proposition consolidée à valider avant implémentation.  
Source : cahier des charges utilisateur version 1.0. Cette consolidation conserve le périmètre fonctionnel, précise les conventions et renvoie les choix techniques de la phase 1 au plan associé.

## 1. Finalité et engagements

Créer un gestionnaire des tâches avancé pour Windows 11 x64, indépendant de TMOG : code original, sans reprise de code propriétaire, ressources protégées ou mécanismes de licence.

Toutes les fonctions seront gratuites : aucun abonnement, publicité, déblocage payant, compte ou activation en ligne obligatoire. Les dons éventuels ne donneront aucun avantage fonctionnel.

Le fonctionnement courant, l'analyse et le stockage seront locaux. Aucune télémétrie ni transmission automatique des données personnelles. Les diagnostics actifs réseau et la recherche automatique de mises à jour seront facultatifs et explicitement activés.

Public : utilisateurs Windows, joueurs, streamers, développeurs et techniciens. Priorités : fiabilité des mesures, sobriété, sécurité et lisibilité.

## 2. Cible et architecture

Windows 11 64 bits, x64 prioritaire. ARM64 est une évolution possible ; Windows 10, Linux et macOS ne sont pas des cibles produit.

Socle proposé : C++20, Qt 6 Widgets, CMake et MSVC. Graphiques personnalisés ou Qt Charts après examen technique et des licences. API Windows, PDH, WMI et ETW selon les besoins et les coûts. SQLite pour l'historique ; JSON pour la configuration. Distribution MSI ou EXE, signature lorsque possible.

Séparer interface, collecte, analyse et stockage. Les collecteurs ont une cadence et un état indépendants. Aucun appel matériel lent ne s'exécute sur le fil de l'interface. Les intégrations susceptibles de se bloquer durablement devront pouvoir être isolées dans un processus auxiliaire ; un simple délai d'attente sur un fil ne garantit pas cette isolation.

Fonctions courantes sans privilèges élevés. Les opérations privilégiées futures utiliseront un composant séparé avec élévation explicite. Aucun contournement des protections Windows, antivirus ou anti-triche.

## 3. Convention des mesures

Chaque indicateur doit documenter sa source, son unité, son périmètre, sa cadence minimale, son horodatage, sa méthode de calcul et ses limitations. Les états « indisponible », « accès refusé », « périmé » et « erreur » sont distincts d'une valeur nulle. Une estimation est explicitement identifiée.

Les différences entre compteurs Windows doivent être expliquées ; l'accord attendu porte sur des fenêtres comparables, sans exiger une égalité instantanée entre outils.

Le CPU global et le CPU d'un processus sont affichés par défaut sur une échelle machine de 0 à 100 %. Un mode par processeur logique peut être proposé, clairement nommé : 100 % représente alors un processeur logique et le total d'un processus peut dépasser 100 %. Les recommandations précisent la convention utilisée.

Les cadences proposées sont 250 ms, 500 ms, 1 s par défaut, 2 s et 5 s, limitées par la source. Collecte et affichage sont indépendants. Les durées utilisent une horloge monotone ; les dates destinées à l'utilisateur utilisent une heure civile explicite.

## 4. Interface et graphiques

Thème sombre par défaut et thème clair, accent personnalisable, navigation latérale, coins arrondis, animations discrètes, tableaux triables et filtrables. Interface redimensionnable, compatible haute résolution et DPI Windows ; navigation clavier et informations accessibles sans dépendre uniquement de la couleur.

Rubriques cibles : tableau de bord, processus, processeur, mémoire, GPU, stockage, réseau, services, démarrage, historique, diagnostics, benchmarks, alertes, informations système et paramètres. Les rubriques suivent les phases de livraison ; leur présence complète n'est pas exigée en phase 1.

Tableau de bord : widgets CPU global/par cœur, RAM, GPU/VRAM/température, disques, réseau, températures, principaux processus, puissance disponible et durée de fonctionnement. Organisation et visibilité enregistrées localement.

Graphiques cibles : séries multiples, survol, zoom, navigation, pause visuelle sans arrêt de collecte, sélection de période, min/moyenne/max et export PNG/CSV. Fenêtres : 60 s, 5 min, 15 min, 1 h, 24 h et périodes personnalisées si enregistrées. Une interruption de collecte reste visible comme une lacune.

## 5. Surveillance matérielle

| Domaine | Informations attendues |
| --- | --- |
| CPU | Modèle, architecture, cœurs physiques et logiques, charge globale/par processeur logique/par processus, fréquences actuelle et nominale, temps noyau/utilisateur, interruptions, changements de contexte, processus et threads. |
| CPU avancé | Températures globale/par cœur, puissance, tensions, limites thermiques, fréquences effectives, throttling et cache lorsque accessibles. |
| Mémoire | Capacité installée, utilisée, disponible, cache, engagement, pagination, pression et usage par processus. Modules, capacité, fréquence, DDR, emplacements, SPD, fabricant et référence selon disponibilité. |
| GPU | Constructeur, modèle, pilote, moteurs, VRAM, usage par processus, encodage/décodage. Température, point chaud, fréquences, puissance, ventilateurs et limites selon constructeur/pilote. |
| Disques | Disques et volumes, capacité/libre, SSD/HDD, débits, activité, latence, file d'attente et E/S par processus. SMART/NVMe selon contrôleur et permissions. |
| Réseau | Ethernet/Wi-Fi, débits, compteurs transférés, vitesse de liaison, IP, DNS, état et courbes. TCP et points de terminaison UDP, adresses, ports, état, PID/nom, filtres. |

Les capteurs ne sont jamais supposés uniformes entre AMD, NVIDIA et Intel. Les limites d'accès sont visibles. Une croissance mémoire peut être signalée, sans conclure automatiquement à une fuite.

Analyse de stockage : dossiers volumineux, classement des fichiers, représentation de l'occupation et export. Aucune suppression automatique. Diagnostics réseau : latence, DNS, pertes par tests actifs volontaires, changements de connexion et historique des déconnexions observées.

## 6. Processus, services et démarrage

Processus : colonnes configurables pour nom, icône, PID, utilisateur, état, CPU, RAM, GPU, disque, réseau mesurable, lancement, threads, priorité, chemin, éditeur et signature. Distinguer mémoire résidente, privée et autres compteurs ; ne pas les présenter comme interchangeables.

Actions cibles : terminer un processus ou une arborescence, priorité, affinité, emplacement, propriétés, enfants, recherche, copie et export. Modules et handles lorsque les droits le permettent. Toute action destructive ou déstabilisante demande confirmation ; un refus d'accès est respecté. La réutilisation des PID doit être prise en compte avant d'agir.

Services : liste, état, mode de démarrage, PID, description et dépendances ; démarrer, arrêter ou redémarrer lorsque permis. Démarrage automatique : liste, activation/désactivation, chemin, éditeur et impact lorsqu'il est mesurable. Modifications réversibles dans la mesure du possible, avec état antérieur conservé.

## 7. Historique, analyse, benchmarks et alertes

Historique : enregistrement manuel, sessions jeu/streaming et automatisation configurable ; choix des capteurs et marqueurs. Relecture synchronisée, comparaison de sessions, pics, min/moyenne/max, exports CSV/JSON. Rétention limitée par durée et taille. Rapports HTML/PDF ultérieurs.

Diagnostics locaux explicables : saturation CPU/GPU, pression mémoire, disque, limitation thermique détectable, processus gourmands, activité réseau inhabituelle et variations de session. Fournir mesures, fenêtres temporelles et limites ; ne pas affirmer systématiquement une cause exacte.

Benchmarks volontaires : CPU mono/multi-thread, bande passante mémoire, latence mémoire si fiable, disque séquentiel/aléatoire. Résultats avec unité, durée, protocole, matériel et historique local exportable. Aucune équivalence revendiquée avec des scores tiers. GPU ultérieur. Les essais disque utilisent des fichiers de test dédiés, jamais des écritures brutes destructrices ; espace, durée et annulation sont contrôlés.

Alertes : seuils CPU/RAM/GPU/température/espace/processus, déconnexion et activité prolongée. Durée minimale, temporisation et activation individuelle ; notifications Windows, son facultatif et journal. Aucun déclenchement en boucle.

## 8. Configuration, sécurité et distribution

Paramètres : thèmes, accent, widgets, graphiques, unités/échelles, cadence, colonnes, notifications, exports, stockage, lancement et zone de notification facultative. Sauvegarde/restauration avec validation des entrées et gestion des versions.

Journaliser les erreurs sans enregistrer inutilement des informations sensibles. Traiter les configurations malformées et borner les ressources allouées. L'historique peut contenir des noms et chemins personnels : documenter son emplacement, sa rétention et sa suppression.

Mises à jour : recherche manuelle, automatisation facultative, version/nouveautés, téléchargement sécurisé, contrôle d'intégrité et authenticité, installation confirmée. Pas de mise à jour obligatoire sauf exception de sécurité documentée.

Licence open source à choisir après audit de Qt, des graphiques, bibliothèques de capteurs, pilotes et autres dépendances. Ne pas ajouter une licence définitive avant cette décision. Signature, distribution des pilotes et financement du certificat restent à étudier.

## 9. Objectifs et validation

Objectifs indicatifs sur des machines de référence à définir : CPU moyen au repos inférieur à 1 %, mémoire standard inférieure à 150 Mo, lancement inférieur à 3 s, collecte par défaut à 1 s, 60 FPS lorsque nécessaire. Ces cibles ne sont pas des garanties universelles.

Moyens : collecte asynchrone, cache, décimation graphique, bornes mémoire, écritures regroupées, mise en veille des modules inutilisés et cadence réduite en arrière-plan.

Validation 1.0 : concordance documentée avec compteurs Windows, graphiques corrects, test prolongé sans croissance mémoire significative, défaillances de capteurs sans blocage, respect des processus protégés, élévation explicite, historique fiable, installation/désinstallation, fonctionnement hors ligne, gratuité et performances mesurées. Les seuils d'acceptation non définis doivent être arrêtés avant les essais de recette.

## 10. Livraisons

| Phase | Livrable et périmètre |
| --- | --- |
| 1 — Fondations | Première application exécutable : architecture, interface, JSON, CPU/RAM, liste des processus, graphiques et thèmes. Détails dans le plan technique. |
| 2 — Matériel | Alpha : GPU/VRAM, capteurs, disques, réseau, widgets personnalisables et erreurs de capteurs. |
| 3 — Fonctions avancées | Bêta : actions processus, services/démarrage, historique, enregistrement/relecture, exports et alertes. |
| 4 — Analyse | Release Candidate : diagnostics, espace disque, benchmarks, comparaison et rapports détaillés selon le périmètre retenu. HTML/PDF restent ultérieurs. |
| 5 — Stabilisation | Version 1.0 : optimisation, essais matériels/sécurité/accessibilité, traductions éventuelles, installateur, mises à jour et documentation. |

Risques majeurs : disponibilité et licences des capteurs, différences de compteurs, permissions, protections des jeux, coût des essais et maintenance Windows/pilotes.

Hors périmètre automatique de la 1.0 : overlay compact, multi-écrans, comparaison avant/après, rapports gaming, API locale et plugins communautaires vérifiés.

## 11. Décisions encore ouvertes

- Nom public, licence et versions exactes des dépendances après audit.
- Version minimale de Windows 11 et matrice de maintenance Qt/MSVC/SDK.
- Machines de référence, marges de mesure et seuils de stabilité/performance.
- Bibliothèques de capteurs et éventuels pilotes, puis politique de signature/distribution.
- Politique détaillée de rétention, format des sessions et canal de mises à jour avant les phases concernées.

Ces points ne constituent pas une autorisation d'implémentation. La validation du cadrage et du plan technique précède le développement.

## 12. Exigences détaillées et traçabilité

Les identifiants ci-dessous permettent de relier réalisation, tests et anomalies. « Obligatoire » désigne une exigence de la phase indiquée, pas une fonctionnalité déjà disponible. Les comportements nouvellement précisés sont des propositions soumises à validation. Une fonctionnalité dépendante du matériel satisfait son exigence si elle fournit une mesure vérifiée ou un état d'indisponibilité explicite, jamais une valeur inventée.

| Identifiant | Phase | Exigence obligatoire | Critère d'acceptation |
| --- | --- | --- | --- |
| SOC-01 | 1 | Démarrer sans compte, Internet ni élévation. | Sur une session utilisateur standard hors ligne, les vues CPU/RAM/processus fonctionnent. |
| SOC-02 | 1 | Isoler collecte et interface. | Un collecteur retardé artificiellement ne bloque ni navigation ni fermeture. |
| MES-01 | 1 | Distinguer zéro, initialisation, indisponibilité et péremption. | Des cas injectés affichent chacun un libellé adapté ; les lacunes ne deviennent pas des zéros. |
| MES-02 | 1 | Exposer unité, source et définition. | Une aide contextuelle explique chaque compteur visible et sa convention. |
| CPU-01 | 1 | Afficher charge globale et par processeur logique. | Les valeurs concordent avec une référence alignée suivant le protocole de recette. |
| RAM-01 | 1 | Séparer mémoire physique, disponible, engagement et mémoire de processus. | Les libellés et formules sont documentés ; aucune somme de working sets n'est assimilée au total physique utilisé. |
| PRO-01 | 1 | Lister, rechercher et trier les processus. | Création/arrêt de processus et changement de tri ne déplacent pas la sélection vers une autre identité. |
| GRA-01 | 1 | Afficher des courbes CPU/RAM bornées. | Après remplissage de la fenêtre, les tampons cessent de croître. |
| GRA-02 | 1 | Permettre la pause visuelle. | La vue reste figée tandis que les nouveaux échantillons continuent d'être collectés. |
| CFG-01 | 1 | Sauvegarder les préférences localement. | Thème, cadence et colonnes persistent après redémarrage ; une configuration invalide est préservée et signalée. |
| UI-01 | 1 | Proposer deux thèmes et une navigation clavier. | Les vues principales restent utilisables sans souris et à 200 % de mise à l'échelle. |
| GPU-01 | 2 | Identifier plusieurs GPU et leurs moteurs. | Une carte sélectionnée conserve ses séries ; des capteurs absents restent indiqués comme tels. |
| CAP-01 | 2 | Résister à la défaillance d'un fournisseur de capteurs. | Une panne n'arrête pas les autres collecteurs ; le fournisseur fautif est identifiable. |
| DIS-01 | 2 | Séparer disques physiques et volumes. | Chaque vue précise son périmètre et ne double pas les totaux à cause des partitions. |
| RES-01 | 2 | Gérer les changements d'interfaces réseau. | Déconnexion, reconnexion et remise à zéro des compteurs ne produisent pas de débit négatif. |
| TAB-01 | 2 | Personnaliser le tableau de bord. | Déplacement et masquage des widgets sont conservés après redémarrage ; une disposition par défaut peut être restaurée. |
| ACT-01 | 3 | Confirmer les actions risquées. | L'annulation ne modifie rien ; l'identité de la cible est revérifiée avant exécution. |
| ADM-01 | 3 | Élever uniquement l'opération nécessaire. | Le refus UAC laisse la surveillance active et l'action est déclarée non exécutée. |
| SER-01 | 3 | Présenter le résultat réel d'une commande de service. | Un délai dépassé ou un refus n'est pas affiché comme un succès. |
| DEM-01 | 3 | Conserver les informations permettant une restauration du démarrage. | Le scénario désactiver/restaurer rétablit la configuration initiale dans les cas supportés. |
| HIS-01 | 3 | Enregistrer puis relire une session. | Valeurs, unités, lacunes et marqueurs sont restitués avec leurs horodatages. |
| HIS-02 | 3 | Borner l'espace historique. | Une limite atteinte déclenche la politique documentée, sans suppression de fichiers extérieurs au stockage géré. |
| EXP-01 | 3 | Exporter CSV et JSON. | Un export relu retrouve valeurs, unités, dates et états ; les erreurs d'écriture sont visibles. |
| ALE-01 | 3 | Appliquer durée minimale et temporisation. | Une oscillation autour d'un seuil ne génère pas une notification à chaque mesure. |
| DIA-01 | 4 | Justifier chaque diagnostic. | Le rapport fournit règle, fenêtre, observations et limites d'interprétation. |
| ESP-01 | 4 | Analyser l'espace sans modifier les fichiers. | L'analyse peut être annulée et les erreurs d'accès sont comptabilisées. |
| BEN-01 | 4 | Encadrer les benchmarks. | Conditions affichées, lancement volontaire, annulation et résultat incomplet clairement signalé. |
| MAJ-01 | 5 | Authentifier les mises à jour. | Une signature invalide ou un artefact altéré empêche l'installation. |
| INS-01 | 5 | Installer et désinstaller proprement. | L'application démarre sans SDK ; la désinstallation distingue programme et données utilisateur. |

## 13. Parcours utilisateur et états d'écran

### 13.1. Premier lancement

L'application ouvre le tableau de bord avec thème sombre et cadence d'une seconde. Elle explique brièvement les unités CPU et mémoire. Aucun assistant de compte, demande administrateur ou accès réseau n'est nécessaire. Les compteurs différentiels affichent « Initialisation » jusqu'à disposer de deux relevés valides. Un inventaire matériel incomplet n'empêche pas l'affichage des données déjà disponibles.

### 13.2. Rechercher une application gourmande

L'utilisateur ouvre Processus, filtre par nom ou PID, trie par CPU ou mémoire et sélectionne une ligne. Le détail indique l'identité, les compteurs disponibles et leur définition. Le rafraîchissement ne déplace pas la sélection vers un autre processus. Si la cible disparaît, le détail signale sa fin ; il ne s'attache pas à un nouveau processus réutilisant le PID.

À partir de la phase 3, la confirmation d'une terminaison rappelle le nom, le PID et le risque de perte de données. Une arborescence est une sélection observée à un instant donné : elle ne garantit pas de capturer tous les enfants créés simultanément. Le résultat distingue succès, disparition préalable et refus pour chaque cible.

### 13.3. Observer une session

L'utilisateur choisit les capteurs, démarre l'enregistrement et ajoute des marqueurs. Un indicateur rappelle que l'enregistrement est actif et affiche sa durée et sa taille. Fermer la fenêtre ne doit pas laisser une collecte en arrière-plan sans indication : selon le réglage explicite, arrêter avec confirmation ou réduire dans la zone de notification.

Après arrêt, la relecture présente les séries sur une même chronologie. Les statistiques excluent les mesures invalides et indiquent la couverture effective. Une session interrompue par un arrêt brutal est récupérée dans la mesure du possible et marquée incomplète.

### 13.4. Capteur absent ou défaillant

Le widget affiche un état et une raison compréhensible : matériel non compatible, permission insuffisante, fournisseur indisponible ou données trop anciennes. La dernière valeur peut rester consultable avec son âge, sans être présentée comme actuelle. Une nouvelle tentative suit une temporisation ; l'utilisateur peut désactiver le fournisseur. Aucun téléchargement de pilote ou passage administrateur automatique.

### 13.5. Diagnostic réseau actif

Avant lancement, afficher la destination, la durée, le nombre de requêtes et la nature du test. Les résultats distinguent résolution DNS, réponse réseau, délai dépassé et erreur locale. Une absence de réponse ICMP ne prouve pas à elle seule une panne Internet. L'annulation arrête les nouvelles requêtes ; aucune répétition en arrière-plan n'est créée implicitement.

### 13.6. États communs

Chaque écran comportant un traitement long expose chargement, résultat vide, résultat partiel, erreur et annulation. « Aucun résultat » ne signifie pas « aucune permission ». Les erreurs donnent une action utile sans codes internes incompréhensibles ; les détails techniques restent copiables. Les confirmations utilisent des verbes précis et rendent l'annulation facilement accessible.

## 14. Règles des données et de persistance

### 14.1. Dictionnaire minimal

| Objet | Champs conceptuels requis | Règles |
| --- | --- | --- |
| Descripteur de mesure | Identifiant stable, source, appareil, unité, convention, cadence et capacité | Un changement de signification impose un nouvel identifiant ou une version. |
| Échantillon | Identifiant de mesure, date UTC, instant monotone relatif, valeur optionnelle, qualité et intervalle | Valeur absente si invalide ; ne pas utiliser NaN comme substitut non documenté dans un export JSON. |
| Identité de processus | PID, création lorsque disponible et identité de session système | Un PID seul ne sert pas de clé persistante fiable. |
| Session | Identifiant, version de format, début/fin, capteurs, contexte matériel minimal et statut | Statuts proposés : active, terminée, interrompue ; distinguer session enregistrée et fenêtre visuelle. |
| Marqueur | Session, instant et libellé utilisateur | Limiter taille et longueur ; échapper le texte lors des exports. |
| Alerte | Règle, seuil/unité, durée minimale, temporisation et activation | Une mesure inconnue ne confirme ni un dépassement ni un retour normal. |
| Diagnostic | Règle/version, fenêtre, preuves et limites | Les conclusions restent rattachées aux données et à la version de règle. |
| Résultat de benchmark | Protocole/version, configuration, durée, unité, valeur et statut | Un test annulé ou interrompu n'est pas classé comme un score terminé. |

Ce dictionnaire décrit les contrats attendus, pas un schéma SQLite définitif.

### 14.2. Unités et temps

Conserver les tailles en octets et les durées avec une précision documentée ; convertir à l'affichage. Distinguer Mo/Mio, débit en octets/s et débit réseau en bits/s. Exporter les dates en ISO 8601 avec fuseau et utiliser UTC pour le stockage. Le passage heure d'été/hiver et une modification de l'horloge système ne doivent pas fausser un débit calculé à partir de l'horloge monotone.

Après veille, hibernation ou changement de topologie, réinitialiser les références dont la continuité n'est plus valide. Ne pas interpoler une interruption comme une mesure réellement observée. Documenter si une moyenne est pondérée par le temps ; le nombre de points seul ne suffit pas lorsque les intervalles varient.

### 14.3. Historique et rétention

Proposition : enregistrement désactivé par défaut ; avant sa première activation, choisir explicitement une limite de durée et de taille. Les valeurs par défaut restent à déterminer. La purge porte uniquement sur les sessions gérées, terminées et éligibles. Les sessions conservées explicitement par l'utilisateur ne sont pas supprimées silencieusement ; si elles empêchent de respecter le quota, suspendre les nouveaux enregistrements avec une explication.

Une session active ne doit pas grossir indéfiniment : prévoir segmentation ou arrêt propre au quota. En cas de disque plein, la surveillance en mémoire continue, l'enregistrement est suspendu et son état est signalé. Les écritures sont transactionnelles et regroupées. Les migrations possèdent une stratégie de sauvegarde et de récupération avant modification du format.

### 14.4. Exports et imports

L'utilisateur choisit destination, plage et champs. L'export ne modifie pas la session source. Un export partiel est identifiable ; un fichier temporaire n'est pas présenté comme un export complet. Un échec ne remplace pas un fichier existant sans confirmation préalable.

CSV : encodage UTF-8, séparateur documenté, échappement correct et convention décimale stable. Neutraliser les cellules textuelles pouvant être interprétées comme formules par un tableur, en documentant la transformation. JSON : version de schéma, nombres valides, unités et états explicites. Toute importation future doit borner taille et profondeur et ne jamais exécuter de contenu.

## 15. Gestion des actions et des privilèges

Le composant privilégié ne sera introduit qu'avec une fonction qui le nécessite. Son protocole doit exposer une liste fermée d'opérations, authentifier le demandeur, valider chaque paramètre et limiter sa durée de vie. Aucune commande arbitraire, chemin exécutable libre ou interprétation de shell ne doit être accepté.

Une confirmation utilisateur ne garantit pas que Windows autorisera l'action. La cible doit être revérifiée juste avant l'opération ; les erreurs partielles sont conservées. L'interface n'affiche jamais « terminé » sur la seule émission d'une requête.

La priorité temps réel n'est pas proposée par défaut. Les affinités tiennent compte des groupes de processeurs et des capacités système. Les manipulations de services exposent les dépendances connues et leurs conséquences ; ne pas arrêter automatiquement une chaîne de services sans consentement détaillé.

La vérification de signature d'un exécutable doit distinguer signé et valide, non signé, invalide et non vérifiable. Le nom d'éditeur ne suffit pas à conclure qu'un fichier est sûr. Une vérification nécessitant Internet respecte le mode hors ligne et n'est pas déclenchée silencieusement.

## 16. Règles des diagnostics et alertes

Les diagnostics seront d'abord fondés sur des règles locales testables. Chaque règle définit métriques, fenêtre, durée minimale, seuils, préconditions, niveau de confiance qualitatif et texte explicatif. Pas de score universel de santé sans méthode documentée.

Une règle de croissance mémoire compare des périodes suffisantes et signale une tendance observée ; elle ne transforme pas un cache applicatif en preuve de fuite. Une activité réseau inhabituelle n'est pas qualifiée automatiquement de malveillante. Un diagnostic de limitation thermique exige un indicateur approprié ; une température élevée seule ne prouve pas le throttling.

Cycle d'alerte proposé : inactive, en attente de durée minimale, active, temporisée puis réarmée. Une hystérésis configurable évite les oscillations. Une lacune de collecte ne vaut pas résolution d'une alerte. Le journal indique les changements d'état ; les notifications Windows peuvent être désactivées sans supprimer le journal local.

## 17. Analyse de stockage et benchmarks

L'analyse de dossiers ne suit pas par défaut les jonctions et liens pouvant provoquer cycles ou sortie du périmètre choisi. Elle distingue taille logique et taille allouée lorsque disponible, documente le traitement des liens physiques et signale les accès refusés. L'ouverture d'un fichier cloud à la demande ne doit pas provoquer un téléchargement massif implicite. Le résultat est un instantané approximatif d'un disque pouvant changer durant l'analyse.

Avant un benchmark, afficher sa nature, sa durée prévue, son impact et les conditions recommandées. Enregistrer versions du protocole, nombre de threads, volume de données, état d'alimentation observable et taille du jeu de travail. Les tests mémoire précisent l'effet des caches ; un test disque précise mise en cache, taille et nettoyage de ses fichiers dédiés.

Le logiciel ne désactive pas les protections système et ne modifie pas l'alimentation ou les fréquences sans fonction spécifique validée. Un benchmark interrompu conserve son statut, nettoie uniquement ses propres fichiers et ne produit pas de classement trompeur.

## 18. Qualification, livrables et gestion des changements

### 18.1. Matrice de qualification

La recette doit couvrir Windows 11 français et anglais, compte standard et élévation refusée, thèmes clair/sombre, DPI 100/150/200 %, veille/reprise, configuration endommagée, disque plein et absence de réseau. Les phases matérielles ajoutent GPU AMD/NVIDIA/Intel selon les équipements disponibles, multi-GPU, SSD/NVMe/HDD et interfaces réseau multiples. Une configuration non testée reste explicitement non vérifiée.

Chaque résultat précise build, versions Windows/pilotes, matériel, protocole, durée, résultat attendu, résultat observé et pièces justificatives. Les tests réussis, échoués, ignorés et non exécutés restent distincts. Les mesures de performance se font sur un build de distribution, hors instrumentation lourde, avec méthode reproductible.

### 18.2. Livrables par jalon

Chaque phase fournit sources, instructions de compilation, dépendances et licences recensées, paquet de développement ou distribution adapté au jalon, notes de version, protocole de test et limites connues. Les identifiants d'exigences couverts figurent dans le rapport de recette. La phase 5 ajoute notices utilisateur, procédure d'installation/désinstallation et politique de mises à jour.

Une phase n'est pas acceptée sur la seule présence d'écrans. Les mesures doivent être réellement collectées, les erreurs visibles et les critères exécutés. Les anomalies bloquant le démarrage, corrompant des données, exécutant une action sur la mauvaise cible ou contournant une permission empêchent l'acceptation.

### 18.3. Décisions et évolution du périmètre

Tenir un registre des décisions avec date, alternatives, justification et conséquences. Toute modification du périmètre indique exigences touchées, phase, dépendances et nouveaux tests. Aucun calendrier chiffré n'est garanti avant estimation de l'équipe et accès au matériel nécessaire.

Prochaines décisions : confirmer le périmètre phase 1, sélectionner versions maintenues de Qt/MSVC/SDK et version minimale Windows 11, définir machines de référence et seuils de recette, puis valider l'audit de licences avant distribution. Le nom public et l'identité graphique peuvent rester provisoires pendant les fondations.
