// Unique unité de compilation qui porte le main de doctest. La macro qui le génère est
// posée par le CMakeLists sur ce seul fichier : la définir ici heurterait la règle de
// nommage des macros de l'ADR-0009 (préfixe LEVAIN_), qui parle de nos macros à nous, pas
// de l'API d'une bibliothèque tierce.
#include <doctest/doctest.h>
