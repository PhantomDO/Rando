# `plugins/camera`

## Rôle

La caméra à la troisième personne de *Rando*, un plugin gameplay
([ADR-0030 de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0030-camera-troisieme-personne.md)) :
elle tourne autour du renard à la souris ou au stick droit, rentre contre la roche que coupe son bras, en
ressort en douceur, et se replace derrière lui quand il marche sans qu'on y touche.

## Invariants

1. **La caméra ne traverse jamais la roche** (le critère de M6.4) : un *sphere cast* part du pivot vers la
   caméra, avec une sphère qui contient le plan proche (`nearPlaneRadius`) plus 5 cm. La CI le vérifie sur un
   tour complet de la caméra le long d'un versant, aux poses d'entre deux pas, et vérifie aussi que la même
   mesure, collision coupée, trouve la caméra dans la roche (le contrôle mord).
2. **Le bras rentre aussitôt contre un obstacle, et en douceur sinon** (`armLengthAfter`) : la constante de
   temps du retour (`returnSeconds`) joue dans les deux sens, pour que le jeu puisse allonger le bras en vol et
   le raccourcir à l'atterrissage (ADR-0031 de Levain). Le sphere cast va jusqu'au plus long du bras voulu et
   du bras actuel : un bras qui rentre en douceur voit la roche sur toute sa longueur.
6. **Le plancher** (`floorHeight`, posé par le jeu : la surface du lac plus la sphère du plan proche) : la
   caméra ne descend pas dessous, et vise alors le pivot (`aboveFloor`). Le moteur ne dessine rien sous l'eau.
3. **Les angles sont la source** : `CameraOrbit` garde le lacet et le tangage, la rotation du `Transform` en est
   calculée à chaque pas, comme la caméra libre du moteur.
4. **Le bras ne voit ni le personnage ni l'eau** : le pivot est dans la capsule du renard, et le lac est un
   volume déclencheur. Le masque est `Static` et `Dynamic`.
5. **La logique est en fonctions libres** (ADR-0011 de Levain), testées sans monde (`tests/camera_test.cpp`) ;
   le système qui les enchaîne, dans la phase `PostPhysics`, tient en un appel.

## Pièges connus

- **La sphère doit contenir le plan proche**, pas seulement le centre optique : sinon les coins de l'image
  entrent dans la roche. Le rayon dépend de la forme de l'image, que le jeu pose à chaque image
  (`aspectRatio`).
- **Le recentrage qui tourne en rond** : tenir « droite » fait marcher le renard vers la droite de la caméra ;
  recentrée derrière lui, la caméra tourne, la droite avec elle, et le renard décrit un cercle. Le recentrage
  ne suit donc qu'un joueur qui s'éloigne de la caméra (`recenteredYaw`).
- **La caméra part du bout du bras** : posée sur les pieds du renard, la première image interpolerait depuis
  l'intérieur du sol (vu par la mesure de la CI, −0,16 m).
- **La cible est une racine** : la caméra lit son `Transform`, sa pose locale. Sans `CharacterState`, elle n'a pas
  de vitesse, et la caméra ne se recentre jamais. Une cible disparue arrête la caméra, avec une erreur au
  journal, une fois.
- **La diagonale du clavier fait 45° avec le regard** : le recentrage ne suit qu'une marche à moins de 40°
  (`recenterMaxDegrees`), sans quoi il s'enclencherait ou non selon l'arrondi de la vitesse.
- **Le plan proche est à 0,2 m**, et non 0,5 m comme le sandbox du moteur : à 0,5 m, la sphère ferait 0,82 m, et
  la caméra ne s'approcherait jamais d'une paroi.

## Points d'entrée

| Fichier | Contenu |
|---|---|
| [`include/rando/camera/third_person.hpp`](include/rando/camera/third_person.hpp) | `ThirdPersonCamera` (les réglages), `CameraOrbit` (l'état), `OrbitInput` ; `nearPlaneRadius`, `orbit`, `walksAwayFrom`, `recenteredYaw`, `armLengthAfter`, `armDirection`, `yawDegreesOf`, `aboveFloor`, `stepCamera` ; `armSphereCast`, `cameraTargetOf` ; `ThirdPersonCameraModule` |
| [`src/third_person.cpp`](src/third_person.cpp) | La logique, et le système flecs qui la branche sur la physique |

## Équivalents ailleurs

| Moteur | Où | Ce qu'on y trouve |
|---|---|---|
| **Unreal** | `USpringArmComponent` | Un bras qui rentre contre ce qu'il touche, par une sphère de `ProbeSize` (12 cm), sans amortissement de la collision (**documenté**, ADR-0030). |
| **Unity** | Cinemachine, *Deoccluder* et *Orbital Follow* | Deux amortissements, à l'aller et au retour, et un recentrage avec une attente et une durée (**documenté**, ADR-0030). |
| **Godot** | `SpringArm3D` | Un rayon ou une forme le long de l'axe Z, sans lissage intégré (**documenté**, ADR-0030). |
