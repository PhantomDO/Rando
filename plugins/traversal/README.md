# Plugin `traversal`

Les moyens de traverser la vallée de *Rando* (M6.5, [ADR-0031 de
Levain](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0031-nage-planeur-endurance.md)) : marcher,
planer, nager, et l'endurance qu'ils coûtent. Un plugin gameplay, dans le dépôt du jeu (ADR-0018).

## Rôle

Un état à la fois décide de la vitesse du joueur, au-dessus du character controller du moteur :

- **la marche** : celle du plugin `character` du moteur (`stepWalk`), telle quelle, chute comprise ;
- **le planeur** : un second appui de saut en l'air l'ouvre (choix de Donnovan). Il avance à 6 m/s dans la
  direction où le joueur regarde, que l'input tourne à 90°/s, et descend à 2 m/s. Il se replie au premier
  contact, sur un nouvel appui, ou épuisé ;
- **la nage** : les pieds à plus de 0,45 m sous la surface du lac. Le joueur flotte en surface et nage à
  1,8 m/s ; il ressort au sol, les pieds à moins de 0,3 m ;
- **l'endurance** : la course, le planeur et la nage la vident ; elle se recharge au sol, en marchant ou à
  l'arrêt. À zéro, le joueur est **épuisé** jusqu'au plein : il ne court plus et ne plane plus ;
- **la noyade** : à zéro dans l'eau, le joueur revient sur la rive, jauge pleine (choix de Donnovan) : au
  dernier point où il avait pied s'il est entré dans l'eau en marchant, sur le sol sec le plus proche de
  l'amerrissage s'il y est entré par les airs (`nearestShore`). M8.2 y retirera un cœur.

## Invariants

- **Un seul système déplace le joueur** : le jeu importe le `TraversalModule` **à la place** du `WalkModule`.
- **Le saut appartient à l'état qui le lit** : au planeur s'il s'ouvre ou se replie, à la marche sinon. L'appui
  qui fait sauter n'ouvre donc pas le planeur : il en faut un second, comme dans *Breath of the Wild*. Il est
  consommé dans **tous** les états : dans une image qui joue deux pas, le même appui ne sert qu'une fois.
- **L'endurance ne se recharge qu'au sol** : en l'air, rien ne change, sinon replier le planeur une seconde
  rendrait 9 s de vol.
- **Le dernier point sec ne s'écrit qu'au sol, hors de l'eau** : un saut au-dessus du lac ou un vol ne le
  déplacent pas.
- **Ce qu'il faut au joueur vient avec ses réglages** (`TraversalRules`, le trait `With` de flecs) :
  `WalkInput`, `Traversal`, `Stamina` et `animation::CharacterMotion`.
- **Le lac est un singleton du monde** (`levain::water::Lake`), un disque et un niveau. Le module en pose un
  vide si le jeu ne l'a pas fait : sans lui, la requête ne correspondrait à rien, et le joueur resterait figé.
- **La rotation s'écrit par référence** dans le `Transform` ; seule la noyade pose (`set`) un `Transform`, et le
  moteur téléporte alors le personnage (ADR-0028 de Levain).

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/rando/traversal/traversal.hpp`](include/rando/traversal/traversal.hpp) | `Mode`, `Glider`, `Swimmer`, `StaminaRates`, `TraversalRules`, `Stamina`, `Traversal`, `waterDepthAt`, `nextMode`, `brakeToGlide`, `glideVelocity`, `brakeInWater`, `swimVelocity`, `staminaAfter`, `motionOf` |

Les tests : `tests/traversal_test.cpp`.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Les *movement modes* du `UCharacterMovementComponent` : `MOVE_Walking`, `MOVE_Falling`, `MOVE_Swimming`, `MOVE_Custom` | Un mode actif à la fois, comme ici ; l'eau est un `APhysicsVolume` dont `bWaterVolume` est vrai (**documenté**, ADR-0031). |
| **Unity** | Une machine à états dans le script du joueur, au-dessus du `CharacterController` | Rien d'intégré : chaque jeu l'écrit (**déduit**). |
| **Godot** | Le script du `CharacterBody3D`, et un `Area3D` pour l'eau | L'`Area3D` peut même changer la gravité de ce qui y entre (**documenté**, ADR-0031). |
