# Rando

Un jeu d'exploration de 5 à 10 minutes, dans l'esprit de *Breath of the Wild* : une vallée, un sanctuaire
visible dès le départ, et le chemin à trouver soi-même. Il est construit avec
**[Levain](https://github.com/PhantomDO/Levain)**, un moteur 3D en C++23 fait pour ça.

- [Page de game design](docs/JEU.md)
- [Pourquoi le jeu et le moteur sont dans deux dépôts](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0018-moteur-plugins-et-jeu.md)
  (ADR-0018 de Levain)

**Statut** : la vallée. Le renard s'y promène au clavier (ZQSD ou WASD, Maj pour courir, Espace pour sauter,
Espace de nouveau en l'air pour planer), dans l'herbe, au bord du lac, sous le ciel de Kloofendal ; il nage dans
le lac, et s'y noie si son endurance s'épuise. Une jauge près de sa tête montre son endurance quand elle n'est
pas pleine. Le jeu tourne au-dessus du module `app` du moteur
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
`rando --walk 1,0 --steps 120` et vérifie où finit le renard.

**La caméra** ([ADR-0030 de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0030-camera-troisieme-personne.md),
`plugins/camera`) tourne autour du renard à la souris, capturée au démarrage (Échap la libère, un clic la
reprend), ou au stick droit. Elle rentre contre la roche que coupe son bras, et se replace derrière le renard
quand il marche sans qu'on y touche. Pour la CI, `--orbit N` la fait tourner à N°/s avec `--steps`, et
`--camera-collision off` lui fait traverser la roche, pour prouver que la mesure de la marge au relief mord.
En vol, son bras s'allonge à 6 m ; dans l'eau, elle ne descend pas sous la surface.

**Marcher, planer, nager** ([ADR-0031 de Levain](https://github.com/PhantomDO/Levain/blob/main/docs/adr/0031-nage-planeur-endurance.md),
`plugins/traversal`) : un état à la fois décide de la vitesse du renard ; l'endurance limite la course, le vol
et la nage. Le critère de M6.5 se joue dans les tests, sur la vraie vallée, sans GPU (`tests/valley_test.cpp`) :
la descente du promontoire en planant, la traversée du lac, et deux noyades. Pour la CI, `--glide N` fait
sauter le renard au pas N et ouvre le planeur 20 pas plus tard :
`rando --start 70,280 --walk 1,0 --glide 60 --steps 400` part du promontoire.
Les tests du plugin : `ctest --test-dir build/linux-debug`. Avec `FETCHCONTENT_SOURCE_DIR_LEVAIN`, le script
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
