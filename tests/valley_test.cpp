#include <algorithm>
#include <optional>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/glm.hpp>

#include "rando/traversal/traversal.hpp"
#include "valley_world.hpp"

#include "levain/physics/character.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"
#include "levain/terrain/heightmap.hpp"

// Le critère de M6.5 (ADR-0031 de Levain), sur la vraie vallée, sa collision et la vraie marche,
// sans GPU : descendre du promontoire en planant, traverser le lac à la nage, et se noyer si
// l'endurance s'épuise. Chaque test vérifie aussi qu'il a fait ce qu'il prétend (règle n°7) : un
// renard qui ne nagerait jamais marcherait au fond du lac et ressortirait sur l'autre rive.

using rando::traversal::Mode;

namespace
{

constexpr float Step = 1.0f / 60.0f;
constexpr glm::vec2 East{1.0f, 0.0f};

/// La vallée du jeu, sans son rendu, et le renard posé en `spot`, face à l'est.
struct Hike
{
    flecs::world world;
    levain::terrain::ValleySettings settings;
    levain::terrain::Heightmap heightmap = levain::terrain::valleyOf(settings);
    levain::water::Lake lake = rando::lakeOf(settings);
    levain::scene::FixedStep fixedStep;
    flecs::entity player;

    explicit Hike(glm::vec2 spot)
    {
        rando::spawnValley(world, heightmap, settings);
        player = rando::spawnPlayer(world, rando::feetAt(heightmap, spot), rando::facingPlusX());
    }

    void step(glm::vec2 direction, bool jump = false)
    {
        auto& input = player.get_mut<levain::character::WalkInput>();
        input.direction = direction;
        input.jump = jump;
        levain::scene::advanceWorld(world, fixedStep, Step);
    }

    [[nodiscard]] glm::vec3 feet() const { return player.get<levain::scene::Transform>().position; }

    [[nodiscard]] Mode mode() const { return player.get<rando::traversal::Traversal>().mode; }

    [[nodiscard]] int drownings() const
    {
        return player.get<rando::traversal::Traversal>().drownings;
    }

    [[nodiscard]] const rando::traversal::Stamina& stamina() const
    {
        return player.get<rando::traversal::Stamina>();
    }

    /// Au sol, les pieds hors de l'eau : ce que le test retient lui-même comme dernier point sec,
    /// sans lire celui du plugin qu'il vérifie.
    [[nodiscard]] bool onDryGround() const
    {
        return levain::physics::isWalking(player.get<levain::physics::CharacterState>().ground) &&
               rando::traversal::waterDepthAt(lake, feet()) == 0.0f;
    }
};

/// Le renard part du promontoire, marche une seconde vers le lac et saute ; avec `secondPress`,
/// un second appui en l'air ouvre le planeur. Rend les pieds au moment du saut.
glm::vec3 leapFromPromontory(Hike& hike, bool secondPress)
{
    for (int i = 0; i < 60; ++i)
    {
        hike.step(East);
    }
    const glm::vec3 jumped = hike.feet();
    hike.step(East, true);
    for (int i = 0; i < 20; ++i)
    {
        hike.step(East);
    }
    hike.step(East, secondPress);
    return jumped;
}

} // namespace

TEST_CASE("du promontoire, le renard saute, ouvre le planeur, et descend vers le lac")
{
    Hike hike{rando::Promontory};
    (void)leapFromPromontory(hike, true);
    REQUIRE(hike.mode() == Mode::Glide);
    const glm::vec3 opened = hike.feet();

    int glidingSteps = 0;
    float fastestDescent = 0.0f; // de 0,5 s après l'ouverture jusqu'au contact
    while (hike.mode() == Mode::Glide && glidingSteps < 60 * 60)
    {
        const float before = hike.feet().y;
        hike.step(East);
        ++glidingSteps;
        if (glidingSteps > 30 && hike.mode() == Mode::Glide)
        {
            fastestDescent = std::max(fastestDescent, (before - hike.feet().y) / Step);
        }
    }
    const glm::vec3 landed = hike.feet();
    const float seconds = static_cast<float>(glidingSteps) * Step;
    const float distance = glm::length(glm::vec2{landed.x - opened.x, landed.z - opened.z});
    CAPTURE(seconds);
    CAPTURE(distance);
    CAPTURE(fastestDescent);
    CAPTURE(landed.x);
    CAPTURE(hike.stamina().value);
    // Les mesures du journal : `rando_tests -tc='*promontoire*'`.
    MESSAGE("vol : " << seconds << " s, " << distance << " m, descente au plus " << fastestDescent
                     << " m/s, posé en x = " << landed.x << ", endurance " << hike.stamina().value);
    CHECK(seconds > 25.0f);
    CHECK(distance > 150.0f);
    CHECK(fastestDescent < 2.05f);
    // Replié par le sol ou par l'eau, pas par l'épuisement.
    CHECK(hike.stamina().value > 0.0f);
}

TEST_CASE("sans le second appui, le même saut retombe sur la pente du promontoire")
{
    // Le témoin du test précédent : c'est bien le planeur qui porte le renard.
    Hike hike{rando::Promontory};
    const glm::vec3 jumped = leapFromPromontory(hike, false);
    REQUIRE(hike.mode() == Mode::Walk);
    for (int i = 0; i < 60; ++i)
    {
        hike.step({});
    }
    const glm::vec3 feet = hike.feet();
    CHECK(glm::length(glm::vec2{feet.x - jumped.x, feet.z - jumped.z}) < 20.0f);
}

TEST_CASE("le renard traverse le lac à la nage, d'ouest en est, sans se noyer")
{
    // Sur la rive ouest, au sec ; l'eau va de x = 268 à 358 sur cette ligne : 90 m.
    Hike hike{{255.0f, 280.0f}};
    int swimmingSteps = 0;
    float lowestStamina = 1.0f;
    float deepestSwim = 0.0f;
    for (int i = 0; i < 90 * 60 && hike.feet().x < 366.0f; ++i)
    {
        hike.step(East);
        lowestStamina = std::min(lowestStamina, hike.stamina().value);
        if (hike.mode() == Mode::Swim)
        {
            ++swimmingSteps;
            deepestSwim =
                std::max(deepestSwim, rando::traversal::waterDepthAt(hike.lake, hike.feet()));
        }
    }
    const glm::vec3 feet = hike.feet();
    CAPTURE(feet.x);
    CAPTURE(feet.y);
    CAPTURE(swimmingSteps);
    CAPTURE(lowestStamina);
    CAPTURE(deepestSwim);
    MESSAGE("traversée : " << static_cast<float>(swimmingSteps) * Step
                           << " s de nage, endurance au plus bas " << lowestStamina
                           << ", pieds au plus " << deepestSwim << " m sous l'eau");
    // Il a vraiment nagé, en surface, et la nage lui a coûté : 88 m environ, à 1,8 m/s.
    CHECK(static_cast<float>(swimmingSteps) * Step > 40.0f);
    CHECK(deepestSwim < 0.6f);
    CHECK(lowestStamina < 0.3f);
    CHECK(lowestStamina > 0.0f);
    CHECK(hike.drownings() == 0);
    CHECK(feet.x >= 366.0f);
    CHECK(hike.mode() == Mode::Walk);
    CHECK(feet.y > rando::LakeLevel);
}

TEST_CASE("le renard qui nage sur place se noie, et revient sur la rive d'où il est parti")
{
    Hike hike{{255.0f, 280.0f}};
    std::optional<glm::vec3> shore;
    // Il entre dans l'eau ; le test retient le dernier point où il l'a vu au sec.
    while (hike.mode() != Mode::Swim && hike.feet().x < 300.0f)
    {
        hike.step(East);
        if (hike.onDryGround())
        {
            shore = hike.feet();
        }
    }
    REQUIRE(hike.mode() == Mode::Swim);
    REQUIRE(shore);
    const glm::vec3 expected = shore.value_or(glm::vec3{0.0f});
    CHECK(expected.x < 268.0f); // sur la rive ouest
    // Dix secondes vers le large, puis sur place.
    int steps = 0;
    while (hike.drownings() == 0 && steps < 90 * 60)
    {
        hike.step(steps < 600 ? East : glm::vec2{0.0f});
        ++steps;
    }
    CAPTURE(steps);
    REQUIRE(hike.drownings() == 1);
    MESSAGE("noyade sur place après " << static_cast<float>(steps) * Step << " s");
    // La jauge pleine vaut 60 s de nage.
    CHECK(static_cast<float>(steps) * Step < 61.0f);
    // Le renard est déjà sur la rive : le `set` de la noyade passe avant le pas de Jolt.
    const glm::vec3 feet = hike.feet();
    CAPTURE(feet.x);
    CAPTURE(expected.x);
    CHECK(glm::distance(feet, expected) < 1.0f);
    CHECK(hike.mode() == Mode::Walk);
    CHECK(hike.stamina().value > 0.99f);
}

TEST_CASE("tombé dans le lac depuis le planeur, il se noie et revient sur la rive la plus proche")
{
    // Le vol droit vers le lac (ADR-0031) : il amerrit avec peu d'endurance.
    Hike hike{rando::Promontory};
    (void)leapFromPromontory(hike, true);
    REQUIRE(hike.mode() == Mode::Glide);
    int steps = 0;
    while (hike.mode() == Mode::Glide && steps < 60 * 60)
    {
        hike.step(East);
        ++steps;
    }
    REQUIRE(hike.mode() == Mode::Swim);
    const glm::vec3 splash = hike.feet();
    while (hike.drownings() == 0 && steps < 120 * 60)
    {
        hike.step({});
        ++steps;
    }
    REQUIRE(hike.drownings() == 1);
    const glm::vec3 feet = hike.feet();
    CAPTURE(splash.x);
    CAPTURE(feet.x);
    CAPTURE(feet.y);
    MESSAGE("noyade après le vol : amerri en x = "
            << splash.x << ", revenu à "
            << glm::length(glm::vec2{feet.x - splash.x, feet.z - splash.z}) << " m, en (" << feet.x
            << " ; " << feet.z << ")");
    // Pas au promontoire, à 230 m : sur le sec, à moins de 40 m de l'amerrissage.
    CHECK(glm::length(glm::vec2{feet.x - splash.x, feet.z - splash.z}) < 40.0f);
    CHECK(feet.y > rando::LakeLevel);
    CHECK(rando::traversal::waterDepthAt(hike.lake, feet) == 0.0f);
}

TEST_CASE(
    "au démarrage, le bras de la caméra se mesure sur la heightmap, sans entrer dans la pente")
{
    // La physique n'a pas encore de relief : le sphere cast du jeu ne toucherait rien.
    const levain::terrain::ValleySettings settings;
    const levain::terrain::Heightmap heightmap = levain::terrain::valleyOf(settings);
    const rando::camera::SphereCast cast = rando::heightmapArmCast(heightmap);
    // Du promontoire vers l'ouest, la crête qui monte : le bras de 3,5 m la touche.
    const glm::vec3 pivot =
        rando::feetAt(heightmap, rando::Promontory) + glm::vec3{0.0f, 0.6f, 0.0f};
    const glm::vec3 west = glm::normalize(glm::vec3{-1.0f, 0.2f, 0.0f});
    const std::optional<float> hit = cast(pivot, west, 3.5f, 0.36f);
    REQUIRE(hit);
    // Au point trouvé, la sphère est au-dessus du relief de toute son empreinte.
    const glm::vec3 center = pivot + (west * hit.value_or(0.0f));
    for (const glm::vec2 offset : {glm::vec2{0.0f}, glm::vec2{0.36f, 0.0f}, glm::vec2{-0.36f, 0.0f},
                                   glm::vec2{0.0f, 0.36f}, glm::vec2{0.0f, -0.36f}})
    {
        CHECK(center.y - 0.36f >
              levain::terrain::heightAt(heightmap, glm::vec2{center.x, center.z} + offset));
    }
    // Vers le haut, rien.
    CHECK_FALSE(cast(pivot, glm::vec3{0.0f, 1.0f, 0.0f}, 3.5f, 0.36f));
}
