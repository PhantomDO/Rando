# Rando

Un jeu d'exploration de 5 à 10 minutes, dans l'esprit de *Breath of the Wild* : une vallée, un sanctuaire
visible dès le départ, et le chemin à trouver soi-même. Il est construit avec
**[Levain](https://github.com/PhantomDO/Levain)**, un moteur 3D en C++23 fait pour ça.

- [Page de game design](docs/JEU.md)
- [Pourquoi le jeu et le moteur sont dans deux dépôts](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0018-moteur-plugins-et-jeu.md)
  (ADR-0018 de Levain)

**Statut** : une coquille. Le jeu ouvre une fenêtre, fait tourner le monde flecs du moteur et peint le ciel ;
la vallée arrive avec le terrain (M5.6 de Levain).

## Compiler

Les mêmes outils que le moteur ([SETUP.md de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/SETUP.md)). Puis :

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/game/rando
```

Le moteur est récupéré par `FetchContent`, **au commit figé** dans `CMakeLists.txt`. Monter de version est
une PR de ce dépôt, qui change ce commit ; la CI vérifie que tout compile encore.

## Travailler sur le moteur et le jeu en même temps

Sans rien publier, en pointant vers un clone local du moteur :

```bash
cmake --preset linux-debug -DFETCHCONTENT_SOURCE_DIR_LEVAIN=../Levain
```

Une modification du moteur se voit alors au build suivant du jeu.

## Le manifeste vcpkg

`vcpkg.json` et `ports/` **recopient ceux du moteur** : vcpkg ne lit que le manifeste du projet principal.
Si le moteur change ses dépendances, la configuration du jeu échoue en disant quoi recopier. Le jeu peut
ajouter les siennes.

## Licence

[MIT](LICENSE). Les assets tiers sont CC0 et ne sont pas versionnés ([JEU.md](docs/JEU.md)).
