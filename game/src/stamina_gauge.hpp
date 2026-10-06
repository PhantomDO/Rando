#pragma once

// La jauge d'endurance de Rando, en attendant ImGui (M7.1) : un arc près de la tête du renard,
// tourné vers la caméra, dessiné avec les lignes de debug du moteur (ADR-0031 de Levain, choix de
// Donnovan). Comme la roue d'endurance de Breath of the Wild.

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rando/traversal/traversal.hpp"

#include "levain/render/debug_lines.hpp"

namespace rando
{

/// Les traits de la jauge pour `stamina`, autour de `center`, dans le plan de l'image d'une caméra
/// tournée de `cameraRotation` : un cercle sombre, et un arc qui part du haut et tourne dans le
/// sens des aiguilles d'une montre, vert, rouge quand le renard est épuisé. Aucun trait quand la
/// jauge est pleine : elle ne se montre que quand elle sert.
[[nodiscard]] std::vector<levain::render::DebugLine> staminaGauge(const traversal::Stamina& stamina,
                                                                  const glm::vec3& center,
                                                                  const glm::quat& cameraRotation);

/// Où se dessine la jauge : à droite de la tête du renard, vue de la caméra.
[[nodiscard]] glm::vec3 gaugeCenter(const glm::vec3& feet, const glm::quat& cameraRotation);

} // namespace rando
