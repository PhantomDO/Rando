# Rando

Un jeu d'exploration de 5 à 10 minutes, dans l'esprit de *Breath of the Wild* : une vallée, un sanctuaire
visible dès le départ, et le chemin à trouver soi-même. Il est construit avec
**[Levain](https://github.com/PhantomDO/Levain)**, un moteur 3D en C++23 fait pour ça.

- [Page de game design](docs/JEU.md)
- [Pourquoi le jeu et le moteur sont dans deux dépôts](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0018-moteur-plugins-et-jeu.md)
  (ADR-0018 de Levain)

**Statut** : la vallée. Le renard s'y promène au clavier (ZQSD ou WASD, Maj pour courir, Espace pour sauter),
dans l'herbe, au bord du lac, sous le ciel de Kloofendal ; la caméra le suit à distance fixe, en attendant la
caméra à la troisième personne (#2). Le jeu tourne au-dessus du module `app` du moteur
([ADR-0029 de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0029-module-app.md)) : son `main`
pose la vallée et le joueur, le moteur fait la boucle.

## Compiler

Les mêmes outils que le moteur ([SETUP.md de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/SETUP.md)). Puis :

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
# Les assets de test empruntés au moteur, une fois : le renard, les textures du terrain et le ciel.
./build/linux-debug/_deps/levain-src/tools/fetch-assets.sh assets-cache \
    Models/Fox Textures/ HDRIs/kloofendal_48d_partly_cloudy_puresky_2k.hdr
./build/linux-debug/game/rando
```

Les options communes du moteur s'appliquent (`--seconds N`, `--steps N`, `--capture f.png`, `--gpu webgpu`…),
plus `--walk x,z` : la direction que suit le renard, dans le monde, au lieu du clavier, et `--start x,z` : où il
commence. La CI lance
`rando --walk 1,0 --steps 120` et vérifie où finit le renard. Avec `FETCHCONTENT_SOURCE_DIR_LEVAIN`, le script
est dans le clone du moteur : `../Levain/tools/fetch-assets.sh assets-cache …`.

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
