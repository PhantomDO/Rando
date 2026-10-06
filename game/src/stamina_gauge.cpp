#include "stamina_gauge.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace rando
{

namespace
{

constexpr int Segments = 32;   ///< Pour un tour complet.
constexpr float Radius = 0.2f; ///< En mètres, à 3,5 m de la caméra.
// En lumière linéaire, avant le tonemapping, comme toute l'image HDR (`DebugLine`).
constexpr glm::vec3 Ring{0.15f, 0.15f, 0.15f};
constexpr glm::vec3 Fresh{0.1f, 4.0f, 0.3f};
constexpr glm::vec3 Exhausted{4.0f, 0.4f, 0.3f};

} // namespace

std::vector<levain::render::DebugLine> staminaGauge(const traversal::Stamina& stamina,
                                                    const glm::vec3& center,
                                                    const glm::quat& cameraRotation)
{
    if (stamina.value >= 1.0f && !stamina.exhausted)
    {
        return {};
    }
    const glm::vec3 right = cameraRotation * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 up = cameraRotation * glm::vec3{0.0f, 1.0f, 0.0f};
    // Le point du cercle au tour `turn` (0 en haut, 1 un tour complet, dans le sens horaire).
    const auto at = [&](float turn)
    {
        const float angle = glm::two_pi<float>() * turn;
        return center + (Radius * ((std::sin(angle) * right) + (std::cos(angle) * up)));
    };
    const int filled = static_cast<int>(std::ceil(stamina.value * Segments));
    std::vector<levain::render::DebugLine> lines;
    lines.reserve(Segments);
    for (int segment = 0; segment < Segments; ++segment)
    {
        const float from = static_cast<float>(segment) / Segments;
        const float to = static_cast<float>(segment + 1) / Segments;
        const glm::vec3 color = segment < filled ? (stamina.exhausted ? Exhausted : Fresh) : Ring;
        lines.push_back({.from = at(from), .to = at(to), .color = color});
    }
    return lines;
}

glm::vec3 gaugeCenter(const glm::vec3& feet, const glm::quat& cameraRotation)
{
    const glm::vec3 right = cameraRotation * glm::vec3{1.0f, 0.0f, 0.0f};
    return feet + glm::vec3{0.0f, 0.9f, 0.0f} + (0.5f * right);
}

} // namespace rando
