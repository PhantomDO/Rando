#pragma once

// La vallée de Rando sans son rendu : le relief qui porte, le lac, et le joueur qui y traverse.
// Le jeu y ajoute ses passes et son renard ; les tests y jouent le critère de M6.5 sans GPU
// (ADR-0031 de Levain), des milliers de pas en quelques secondes.

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rando/camera/third_person.hpp"
#include "rando/traversal/traversal.hpp"

#include "levain/character/walk.hpp"
#include "levain/physics/character.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/water/lake.hpp"

namespace rando
{

/// La hauteur de l'eau du lac, sous le fond plat de la vallée : elle ne remplit que son creux.
constexpr float LakeLevel = -1.5f;

/// La marche du renard : les réglages par défaut du plugin `character`. Le joueur les porte, et
/// l'animation y règle ses vitesses de marche et de course.
constexpr levain::character::Walker FoxWalker{};

/// La capsule du renard : 0,8 m de haut, pour un renard de 0,79 m à l'échelle 0,01 (Fox mesure
/// 155 × 79 unités). Elle ne couvre ni son museau ni sa queue : le compromis habituel d'un
/// quadrupède sur une capsule debout.
constexpr levain::physics::CharacterController FoxController{
    .shape = {.halfHeight = 0.1f, .radius = 0.3f}};

/// Le promontoire de M6.5 : la crête ouest, face au lac (ADR-0031 de Levain). Le vrai viendra
/// avec l'éditeur (M7.6, M8.2).
constexpr glm::vec2 Promontory{70.0f, 280.0f};

/// Le lac que dessine le jeu : le creux de la vallée, rempli jusqu'à `LakeLevel`.
[[nodiscard]] levain::water::Lake lakeOf(const levain::terrain::ValleySettings& valley);

/// Les pieds posés sur le relief, au point (x, z).
[[nodiscard]] glm::vec3 feetAt(const levain::terrain::Heightmap& heightmap, glm::vec2 spot);

/// Où le renard commence : sur le fond plat de la vallée, 1,4 rayon à l'ouest du lac, face à lui.
[[nodiscard]] glm::vec3 startOf(const levain::terrain::Heightmap& heightmap,
                                const levain::terrain::ValleySettings& valley);

/// L'orientation qui tourne l'avant d'un personnage (−z) vers +x : un quart de tour autour de Y,
/// dans le sens horaire vu d'en haut, d'où le signe moins.
[[nodiscard]] glm::quat facingPlusX();

/// La vallée qui porte : la physique, les moyens de traverser, le relief qui collisionne, le lac.
void spawnValley(flecs::world& world, const levain::terrain::Heightmap& heightmap,
                 const levain::terrain::ValleySettings& valley);

/// Le bras de la caméra au démarrage, mesuré sur la heightmap : la physique ne crée le relief qu'au
/// premier pas (les corps naissent dans sa phase, ADR-0026 de Levain), et un sphere cast lancé
/// avant ne touche rien. Sur le versant d'un promontoire, la première pose serait dans la roche, et
/// la première image interpolerait depuis elle. Le centre de la sphère avance par pas de 5 cm, et
/// s'arrête dès que son dessous passe sous le relief le plus haut de son empreinte : le centre et
/// quatre points à un rayon de lui. Sous le seul centre, la sphère s'enfoncerait de r(1 − cos θ)
/// dans une pente θ, 5 cm à 30°.
[[nodiscard]] camera::SphereCast heightmapArmCast(const levain::terrain::Heightmap& heightmap);

/// Le joueur, une racine sans échelle (ADR-0028 de Levain), ses pieds à `feet`. Ses réglages
/// amènent le reste : son input, son état, son endurance (le trait `With` du plugin `traversal`).
flecs::entity spawnPlayer(flecs::world& world, const glm::vec3& feet, const glm::quat& rotation);

} // namespace rando
