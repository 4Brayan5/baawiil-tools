# Recette obligatoire avant version 1.0

Cette liste n'est pas un rapport de tests réussis. Les essais suivants restent à exécuter sur Windows 11 x64, avec versions OS/pilotes, matériel et résultats consignés.

| Domaine | Scénarios requis |
| --- | --- |
| Installation | Machine sans Qt/SDK, installateur utilisateur, chemin avec espaces et accents, lancement des trois exécutables, désinstallation préservant données et fichiers étrangers au paquet. |
| Droits | Compte standard, accès refusés, annulation UAC, succès/échec du composant administrateur, refus de processus critiques et de cibles dont le PID a été réutilisé. |
| Mesures | Repos/charge contrôlée, fenêtres alignées avec les mêmes compteurs Windows, CPU par groupe, RAM/cache/engagement, mémoire privée distincte du working set. |
| Processus | Créations et disparitions rapides, tri, filtres, sélection stable, modules et handles accessibles, masquage des colonnes, priorités et affinité sur un processus de test jetable. |
| Matériel | AMD/NVIDIA/Intel selon disponibilité, plusieurs GPU, NVML absente, fournisseur WMI absent/défaillant, capteurs inconnus, disques NVMe/SATA et permissions SMART limitées. |
| Isolation | Collecteur dépassant son délai, interruption pendant scan/benchmark, arrêt des descendants du collecteur, nettoyage des fichiers temporaires, autres rubriques toujours utilisables. |
| Réseau | IPv4/IPv6, Wi-Fi/Ethernet/VPN, interfaces déconnectées/reconnectées, débits de référence, DNS/ICMP volontaire uniquement, aucune requête de mise à jour sans activation. |
| Services/démarrage | Service de test, dépendances, transition lente, refus, suppression/restauration de Run utilisateur et d'un raccourci de démarrage, collision de sauvegarde, conservation des originaux. |
| Historique | CPU/RAM et mesures supplémentaires disponibles, marqueurs, arrêt brutal, migration avec sauvegarde, comparaison, plus de 100 000 points, export complet, session protégée, quota et disque plein. |
| Alertes | Durée minimale, temporisation, hystérésis, données inconnues/périmées, PID remplacé, notifications désactivées, absence de boucle. |
| Benchmarks | Mono/multithread, mémoire, lecture/écriture séquentielle/aléatoire, espace insuffisant, annulation, fichiers temporaires et unités explicites. |
| Mises à jour | Aucun canal configuré, TLS invalide, manifeste malformé, dépassement de taille, somme incorrecte, signature invalide, mauvais certificat, certificat expiré, annulation de lancement. Utiliser des fixtures maîtrisées. |
| Préférences | JSON tronqué, types invalides, version future, import de confiance, tentative de nom de fichier hors liste, échec d'écriture et restauration des originaux, redémarrage après import. |
| Accessibilité | Clavier et lecteur d'écran, contraste, thèmes, DPI 100/150/200 %, plusieurs écrans, fenêtres petites et menus de débordement. |
| Endurance | Au moins huit heures, veille/reprise, horloge modifiée, CPU moyen au repos, working set/mémoire privée, handles, écritures, lancement à froid/chaud. |

Comparer les objectifs CPU < 1 %, mémoire standard < 150 Mo et lancement < 3 s uniquement sur des machines de référence définies. Le succès d'une compilation croisée ou d'un test d'interface Linux ne satisfait pas ces critères.
