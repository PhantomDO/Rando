#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rando/traversal/traversal.hpp"

using levain::physics::GroundState;
using rando::traversal::Mode;
using rando::traversal::Stamina;
using rando::traversal::TraversalRules;

namespace
{

constexpr float Step = 1.0f / 60.0f;

/// Un lac de 10 m de rayon, à −1,5 m, comme celui de la vallée.
constexpr levain::water::Lake Pond{.center = {0.0f, 0.0f}, .radius = 10.0f, .level = -1.5f};

levain::physics::CharacterState inAir(float verticalSpeed)
{
    return {.ground = {.state = GroundState::InAir}, .velocity = {0.0f, verticalSpeed, 0.0f}};
}

} // namespace

TEST_CASE("la profondeur sous la surface, et zéro hors du lac")
{
    CHECK(rando::traversal::waterDepthAt(Pond, {3.0f, -2.5f, 0.0f}) == doctest::Approx(1.0f));
    CHECK(rando::traversal::waterDepthAt(Pond, {3.0f, -1.0f, 0.0f}) == 0.0f);  // au-dessus
    CHECK(rando::traversal::waterDepthAt(Pond, {11.0f, -5.0f, 0.0f}) == 0.0f); // hors du disque
}

TEST_CASE(
    "le planeur s'ouvre en l'air sur un appui, et se replie au contact, sur un appui ou épuisé")
{
    const TraversalRules rules;
    const Stamina fresh;
    const Stamina exhausted{.value = 0.5f, .exhausted = true};
    using rando::traversal::nextMode;
    CHECK(nextMode(rules, Mode::Walk, {.ground = GroundState::InAir, .jump = true}, fresh) ==
          Mode::Glide);
    // Au sol, l'appui fait sauter : c'est l'affaire de la marche.
    CHECK(nextMode(rules, Mode::Walk, {.ground = GroundState::OnGround, .jump = true}, fresh) ==
          Mode::Walk);
    CHECK(nextMode(rules, Mode::Walk, {.ground = GroundState::InAir, .jump = true}, exhausted) ==
          Mode::Walk);
    CHECK(nextMode(rules, Mode::Glide, {.ground = GroundState::InAir}, fresh) == Mode::Glide);
    CHECK(nextMode(rules, Mode::Glide, {.ground = GroundState::OnSteepGround}, fresh) ==
          Mode::Walk);
    CHECK(nextMode(rules, Mode::Glide, {.ground = GroundState::InAir, .jump = true}, fresh) ==
          Mode::Walk);
    CHECK(nextMode(rules, Mode::Glide, {.ground = GroundState::InAir}, exhausted) == Mode::Walk);
}

TEST_CASE("deux seuils pour la nage : au ras d'une rive, l'état ne bat pas d'un pas à l'autre")
{
    const TraversalRules rules;
    const Stamina fresh;
    using rando::traversal::nextMode;
    // Plus de 0,45 m : il nage, d'où qu'il vienne, le planeur compris.
    CHECK(nextMode(rules, Mode::Glide, {.ground = GroundState::InAir, .waterDepth = 0.5f}, fresh) ==
          Mode::Swim);
    // Entre les deux seuils, chacun garde son état.
    CHECK(nextMode(rules, Mode::Walk, {.ground = GroundState::OnGround, .waterDepth = 0.4f},
                   fresh) == Mode::Walk);
    CHECK(nextMode(rules, Mode::Swim, {.ground = GroundState::OnGround, .waterDepth = 0.4f},
                   fresh) == Mode::Swim);
    // Il ne sort de l'eau qu'au sol : flotter au-dessus de 0,2 m de fond n'est pas avoir pied.
    CHECK(nextMode(rules, Mode::Swim, {.ground = GroundState::InAir, .waterDepth = 0.2f}, fresh) ==
          Mode::Swim);
    CHECK(nextMode(rules, Mode::Swim, {.ground = GroundState::OnGround, .waterDepth = 0.2f},
                   fresh) == Mode::Walk);
}

TEST_CASE(
    "ouvert en pleine chute, le planeur freine sans à-coup ; ouvert en montant, il ne remonte pas")
{
    const TraversalRules rules;
    float fall = -20.0f;
    float previous = fall;
    for (int step = 0; step < 30; ++step) // une demi-seconde
    {
        fall = rando::traversal::brakeToGlide(rules, fall, Step);
        // Pas d'arrêt net : moins de 1 m/s gagné par pas, et jamais au-delà de la cible.
        CHECK(fall - previous < 1.0f);
        CHECK(fall <= -rules.glider.sinkSpeed);
        previous = fall;
    }
    CHECK(fall == doctest::Approx(-2.0f));

    float rise = 3.0f;
    for (int step = 0; step < 120; ++step)
    {
        rise = rando::traversal::brakeToGlide(rules, rise, Step);
    }
    CHECK(rise == doctest::Approx(-2.0f));
}

TEST_CASE("le planeur prend son élan dans la direction où le joueur regarde")
{
    const TraversalRules rules;
    // Un quart de tour à gauche : l'avant, −z, devient −x.
    const glm::quat left = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    glm::vec3 velocity{0.0f, -2.0f, 0.0f};
    velocity = rando::traversal::glideVelocity(rules, left, velocity, Step);
    // Pas d'un coup : 8 m/s² ; 6 m/s en trois quarts de seconde.
    CHECK(velocity.x == doctest::Approx(-8.0f * Step));
    for (int step = 0; step < 60; ++step)
    {
        velocity = rando::traversal::glideVelocity(rules, left, velocity, Step);
    }
    CHECK(velocity.x == doctest::Approx(-6.0f));
    CHECK(velocity.z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(velocity.y == doctest::Approx(-2.0f));
}

TEST_CASE("à la nage, il flotte les pieds à 0,4 m sous la surface, sans être projeté")
{
    const TraversalRules rules;
    using rando::traversal::swimVelocity;
    // Trop bas, il remonte, mais pas plus vite que 2 m/s ; trop haut, il descend.
    // (Partis déjà à cette vitesse : le frein de l'eau est vérifié plus bas.)
    CHECK(swimVelocity(rules, {}, 0.7f, {0.0f, 1.0f, 0.0f}, Step).y == doctest::Approx(1.0f));
    CHECK(swimVelocity(rules, {}, 5.0f, {0.0f, 2.0f, 0.0f}, Step).y == doctest::Approx(2.0f));
    CHECK(swimVelocity(rules, {}, 0.1f, {}, Step).y < 0.0f);
    CHECK(swimVelocity(rules, {}, 0.4f, {}, Step).y == doctest::Approx(0.0f));
    // Il part lentement : l'eau freine.
    const glm::vec3 start = swimVelocity(rules, {1.0f, 0.0f}, 0.4f, {}, Step);
    CHECK(start.x == doctest::Approx(4.0f * Step));
    // Tombé à 15 m/s, il ne remonte pas d'un coup : l'eau freine sa chute de 30 m/s².
    CHECK(swimVelocity(rules, {}, 1.0f, {0.0f, -15.0f, 0.0f}, Step).y ==
          doctest::Approx(-15.0f + (30.0f * Step)));
}

TEST_CASE("l'endurance : l'effort la vide, le repos au sol la remplit, et l'épuisement dure "
          "jusqu'au plein")
{
    const TraversalRules rules;
    using rando::traversal::staminaAfter;
    // Une seconde de nage : un soixantième de la jauge.
    CHECK(staminaAfter(rules, {}, Mode::Swim, false, false, 1.0f).value ==
          doctest::Approx(1.0f - (1.0f / 60.0f)));
    // En l'air, sans planeur, rien ne change.
    CHECK(staminaAfter(rules, {.value = 0.5f}, Mode::Walk, false, false, 1.0f).value == 0.5f);
    // Courir coûte, au sol ; une seconde, un quinzième.
    CHECK(staminaAfter(rules, {}, Mode::Walk, true, true, 1.0f).value ==
          doctest::Approx(1.0f - (1.0f / 15.0f)));
    // À zéro, épuisé ; à moitié remplie, toujours épuisé ; pleine, plus du tout.
    Stamina stamina = staminaAfter(rules, {.value = 0.01f}, Mode::Walk, true, true, 1.0f);
    CHECK(stamina.value == 0.0f);
    CHECK(stamina.exhausted);
    stamina = staminaAfter(rules, stamina, Mode::Walk, false, true, 2.5f);
    CHECK(stamina.value == doctest::Approx(0.5f));
    CHECK(stamina.exhausted);
    stamina = staminaAfter(rules, stamina, Mode::Walk, false, true, 2.5f);
    CHECK(stamina.value == 1.0f);
    CHECK_FALSE(stamina.exhausted);
}

TEST_CASE("en vol, l'animation reçoit une vitesse nulle : le renard ne court pas dans le vide")
{
    levain::physics::CharacterState state = inAir(-2.0f);
    state.velocity.x = 6.0f;
    const auto gliding = rando::traversal::motionOf(Mode::Glide, state);
    CHECK(gliding.gliding);
    CHECK(gliding.speed == 0.0f);
    const auto swimming = rando::traversal::motionOf(Mode::Swim, state);
    CHECK(swimming.swimming);
    CHECK(swimming.speed == doctest::Approx(6.0f));
}
