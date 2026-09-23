# AGENTS.md — instructions pour les agents

*Rando* est le jeu construit avec le moteur [Levain](https://github.com/PhantomDO/Levain). **Les règles du
projet sont celles de Levain** (son `AGENTS.md`) : rôles, règles non négociables, forme du code, skills. Ce
fichier ne dit que ce qui change ici.

- **Une seule PR ouverte à la fois, pour les deux dépôts ensemble** : il n'y a qu'un Donnovan (ADR-0018).
- Le **journal, la roadmap, le board et les ADR** restent dans Levain : *Rando* en est un milestone.
- Le code du jeu vit dans `game/` ; ses **plugins gameplay** dans `plugins/<nom>/`, déclarés par
  `levain_add_plugin` (ADR-0018).
- Le moteur se modifie dans son dépôt, jamais ici. Pour les deux à la fois :
  `-DFETCHCONTENT_SOURCE_DIR_LEVAIN=../Levain`.
