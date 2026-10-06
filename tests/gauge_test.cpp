#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "stamina_gauge.hpp"

namespace
{

constexpr glm::quat Facing{1.0f, 0.0f, 0.0f, 0.0f};

/// Les traits de la couleur de l'arc rempli : ni gris, ni sombres.
int filledOf(const std::vector<levain::render::DebugLine>& lines)
{
    int filled = 0;
    for (const auto& line : lines)
    {
        filled += (line.color.x + line.color.y) > 1.0f ? 1 : 0;
    }
    return filled;
}

} // namespace

TEST_CASE("la jauge ne se montre que quand elle sert, et son arc suit l'endurance")
{
    CHECK(rando::staminaGauge({}, {}, Facing).empty());
    const auto half = rando::staminaGauge({.value = 0.5f}, {}, Facing);
    CHECK(half.size() == 32); // le cercle entier, sombre là où la jauge est vide
    CHECK(filledOf(half) == 16);
    CHECK(filledOf(rando::staminaGauge({.value = 0.01f}, {}, Facing)) == 1);
    // Épuisé, à moitié remontée : rouge, jusqu'à ce qu'elle soit pleine.
    const auto tired = rando::staminaGauge({.value = 0.5f, .exhausted = true}, {}, Facing);
    CHECK(tired.front().color.x > tired.front().color.y);
}

TEST_CASE("la jauge est dans le plan de l'image, à droite de la tête du renard")
{
    // Une caméra qui regarde vers −Z : le cercle est dans le plan (x, y).
    const glm::vec3 center = rando::gaugeCenter({10.0f, 0.0f, 5.0f}, Facing);
    CHECK(center.x == doctest::Approx(10.5f));
    CHECK(center.y == doctest::Approx(0.9f));
    for (const auto& line : rando::staminaGauge({.value = 0.3f}, center, Facing))
    {
        CHECK(line.from.z == doctest::Approx(5.0f));
        CHECK(glm::distance(line.from, center) == doctest::Approx(0.2f));
    }
}
