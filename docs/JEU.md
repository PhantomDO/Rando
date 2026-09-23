# Rando — page de game design

> Version 1.0 — 23/09/2026 — statut : **validé par Donnovan** (M3.5, #68)
>
> Chaque choix de cette page vient d'une question posée à Donnovan le 23/09/2026 : voir « Décisions » en bas.
> Écrite dans le dépôt du moteur (Levain, M3.5), déménagée ici en M3.6. Les milestones cités sont ceux de
> la [ROADMAP de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/ROADMAP.md).

## Intention

Un **vertical slice de 5 à 10 minutes**, dans l'esprit de *Breath of the Wild* : le joueur arrive au bord d'une
vallée, voit un sanctuaire au loin, et **trouve lui-même son chemin** pour l'atteindre. Le plaisir vient du
mouvement et du regard, pas du combat : il n'y a pas d'ennemis.

**Titre : *Rando***, choisi par Donnovan le 23/09/2026. C'est aussi le nom du dépôt du jeu. Les autres
titres proposés, *Mère*, *Vigie* et *Vallée*, sont écartés.

## Le parcours type

Le joueur fait ceci en 5 à 10 minutes. L'ordre n'est pas imposé : c'est le chemin le plus probable, pas un
couloir.

1. **Le point de vue.** Départ sur un promontoire. Le sanctuaire est visible de l'autre côté d'un lac, sur une
   hauteur. C'est la promesse du jeu : « tu le vois, tu peux y aller ».
2. **La descente.** Le joueur saute et **plane** vers la vallée. L'endurance limite la distance : trop
   ambitieux, il tombe et perd des cœurs.
3. **Le lac.** Le traverser à la nage coûte de l'endurance. Le contourner prend plus de temps mais passe par un
   premier point de contrôle et quelques pommes.
4. **La montée.** Pas d'escalade (v2) : on monte par des sentiers et des rampes naturelles. Un passage piégé
   (rochers qui roulent, piques) coûte des cœurs à qui s'y précipite.
5. **Les énigmes.** Une porte du sanctuaire s'ouvre en posant un poids sur une plaque : pousser un bloc,
   empiler, faire rouler une boule. Une ou deux énigmes, avec Jolt et des déclencheurs.
6. **Le sanctuaire.** Y entrer termine le slice : un message de fin, le temps de parcours.

## La boucle de jeu

**Voir** un but → **choisir** un chemin → **traverser** en gérant l'endurance → **franchir** un obstacle
(énigme ou piège) → **être récompensé** (une pomme, un point de contrôle, une nouvelle vue) → voir le but
suivant.

## Les mécaniques

Les valeurs chiffrées sont des points de départ, à régler en jouant (M8.2).

| Mécanique | Règle |
|---|---|
| **Marche, course, saut** | Le character controller de M6.3. La course consomme de l'endurance. |
| **Planeur** | Déployé en l'air : chute lente et poussée vers l'avant. Consomme de l'endurance ; à vide, il se replie. |
| **Nage** | Dans le volume d'eau du lac. Consomme de l'endurance ; à vide, noyade : un cœur perdu, retour sur la rive. |
| **Endurance** | Une jauge, qui se recharge au sol à l'arrêt ou en marchant. |
| **Cœurs** | 3 au départ. Perdus par la chute (selon la hauteur), la noyade et les pièges. Une pomme en rend un. |
| **Mort** | Reprise au dernier des 2 ou 3 points de contrôle (des autels), cœurs pleins. Pas de sauvegarde entre deux parties. |
| **Pièges** | Des piques et des rochers qui roulent : des objets Jolt, et des zones de dégâts. |
| **Énigmes** | Des objets physiques et des déclencheurs : une plaque qui attend un poids, une porte qui s'ouvre. Câblées dans l'éditeur par des composants, sans code (M8.2 se fait « avec l'éditeur »). |
| **Caméra** | À la troisième personne : orbite autour du joueur, collision avec le décor (sphere cast), recentrage automatique derrière lui et cadrage propre au vol plané. |
| **Interface** | Cœurs, endurance et message de fin, dessinés avec ImGui (M7.1). Une vraie UI de jeu est en v2. |

## Le contenu minimal

Tout vient de bibliothèques **CC0** : Donnovan n'a pas le temps de modéliser. Les assets tiers **ne sont pas
versionnés** : un script les télécharge depuis leur source, vérifie leur hash, et la CI les garde en cache.
Git ne garde que ce qu'on crée : la heightmap et les scènes.

| Contenu | Source |
|---|---|
| Le personnage et ses animations : repos, marche, course, saut, chute, nage, pose de vol plané | Quaternius, *Universal Animation Library* (CC0, glTF). La version gratuite n'a qu'une partie des animations : vérifier au téléchargement que la nage en fait partie. |
| Arbres, rochers, herbe | Quaternius (CC0) |
| Le sanctuaire, les autels, les blocs, les plaques, les pièges | Un kit modulaire CC0, à choisir en M8.2 (Quaternius ou Kenney) |
| Textures du sol et de la roche | Poly Haven (CC0) |
| Le ciel | Une HDRI Poly Haven (CC0), la même que pour l'IBL de M5.4 : une seule heure de la journée |
| Le terrain | Une vallée d'environ **500 m** de côté, sculptée dans l'éditeur (M7.6). Tenue entièrement en mémoire ; l'horizon est fermé par le relief et le brouillard. |
| Sons | Pas, eau, vent, porte : bibliothèques CC0, choisies en M8.1 |

## Ce que le moteur doit savoir faire

La colonne « Niveau » est **provisoire** : Donnovan la reprend ligne par ligne dans l'ADR moteur, jeu et
plugins (M3.6), avec le pour et le contre de chaque choix. La règle, de lui : ce dont tout jeu a besoin va
dans le **moteur** ; ce qui sert au level design, dans un **plugin moteur** ; ce qui sert au gameplay, dans un
**plugin gameplay**, qui vit dans le dépôt du jeu.

| Besoin | Niveau (provisoire) | Milestone |
|---|---|---|
| Deux dépôts, le jeu lié au moteur | Architecture | **M3.6** (nouveau) |
| Personnage animé | Moteur | **M4.5** (nouveau) |
| Rendu PBR, ombres, HDRI et IBL | Moteur | M5.1 à M5.4 (prévu) |
| Terrain : rendu, LOD, mélange de textures | Plugin moteur | **M5.6** (nouveau) |
| Lac et herbe dense | Plugin moteur | **M5.7** (nouveau) |
| Physique, collision du terrain, déclencheurs | Moteur | M6.1, M6.2 (étendu) |
| Character controller | Moteur | M6.3 (prévu) |
| Caméra à la troisième personne | Plugin gameplay | **M6.4** (nouveau) |
| Nage, planeur, endurance | Plugin gameplay | **M6.5** (nouveau) |
| Éditeur, réflexion, sérialisation, Play/Stop | Moteur | M7.1 à M7.5 (prévu) |
| Sculpt, peinture du terrain et de la végétation | Plugin moteur | **M7.6** (nouveau) |
| Sons 3D | Moteur | M8.1 (prévu) |
| Cœurs, pièges, pommes, points de contrôle, énigmes | Plugin gameplay | M8.2 (étendu) |

## Hors v1

Ces idées sont venues pendant le choix et reviennent en **candidats v2** : l'escalade, les ennemis et le combat,
un cycle jour/nuit ou un ciel procédural, une rivière qui coule, les réflexions sur l'eau, l'inventaire et la
cuisine, une vraie UI de jeu, le streaming du monde, le scripting, le chargement dynamique des plugins, le
moteur installé comme paquet.

## Critère de réussite (M8.2)

Quelqu'un qui n'a jamais vu le jeu va du promontoire au sanctuaire en **5 à 10 minutes**, sans aide ; le jeu
est fait uniquement avec le moteur et l'éditeur ; les binaires sont produits par la CI.

## Décisions

Posées à Donnovan le 23/09/2026, dans cet ordre. « Reco. » : l'option recommandée par Claude.

| Question | Réponse |
|---|---|
| Le verbe central | Explorer, avec un peu d'énigmes (reco.) |
| Le personnage, alors que l'animation était en v2 | **Ajouter l'animation squelettique en v1** |
| La taille du monde | Une vallée d'environ 500 m (reco.) |
| La direction artistique | Stylisée, en PBR, assets CC0 (reco.) |
| Un besoin absent de la roadmap | **Arbitré au cas par cas** |
| Le terrain | **Un vrai système** : LOD, sculpt et peinture dans l'éditeur |
| Les moyens de traverser | Marche, course, saut, planeur, nage ; **l'escalade passe en v2** à l'arbitrage |
| Le but du slice | Atteindre un lieu visible (reco.) |
| L'arbitrage des ajouts | Animation, terrain complet, eau et nage : gardés |
| Les énigmes | Des objets physiques (reco.) |
| La source du personnage | Quaternius, CC0 (reco.) |
| L'endurance et l'interface | Une jauge dessinée avec ImGui (reco.) |
| L'ordre des ajouts | Chacun après ce dont il dépend (reco.) |
| L'eau, le ciel | Un lac calme, une HDRI fixe (reco.) |
| La végétation | **Avec de l'herbe dense** |
| L'échec | **Des cœurs et des dégâts** |
| La caméra | **Orbite, collision et cadrage** |
| La logique de jeu | En C++, les énigmes câblées par composants (reco.) |
| Les dégâts | **L'environnement et des pièges**, pas d'ennemis |
| Le soin et la reprise | Des pommes et des points de contrôle (reco.) |
| Le total (v1 d'environ 31 h à 40 h) | **Tout est gardé**, et **le jeu vit dans un autre dépôt** que le moteur |
| La liaison du jeu au moteur | FetchContent figé avec surcharge locale ; **à terme, le moteur installé comme paquet** |
| L'éditeur | Une bibliothèque du moteur ; le jeu construit son exécutable (reco.) |
| La frontière | **Moteur, plugins moteur (level design), plugins gameplay** ; le pour et le contre de chaque fonctionnalité dans l'ADR |
| Un plugin, techniquement | **Lié à la compilation en v1, chargé dynamiquement plus tard** |
| Les plugins gameplay | **Dans le dépôt du jeu** |
| Le dépôt du jeu | Public, sous la même licence que le moteur (reco.) |
| Le titre | ***Rando*** (proposition de Donnovan, hors des trois pitchs) |
| Les assets tiers | Un script de téléchargement vérifié par hash (reco.) |
