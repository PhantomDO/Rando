#include <cmath>
#include <optional>

#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rando/camera/third_person.hpp"

using rando::camera::CameraOrbit;
using rando::camera::ThirdPersonCamera;

TEST_CASE("la sphère contient les coins du plan proche, pas seulement son centre")
{
    // 60° et 16:9 : le coin est à 1,545 fois le plan proche.
    const levain::app::CameraLens lens{.verticalFovDegrees = 60.0f, .nearPlane = 0.2f};
    CHECK(rando::camera::nearPlaneRadius(lens, 16.0f / 9.0f) ==
          doctest::Approx(0.309f).epsilon(1e-3));
    // Un téléphone en portrait : l'image est plus haute que large, le coin plus près.
    CHECK(rando::camera::nearPlaneRadius(lens, 0.5f) < rando::camera::nearPlaneRadius(lens, 2.0f));
}

TEST_CASE("le bras rentre aussitôt contre un obstacle, et ressort en douceur")
{
    // Touché à 1,2 m : le bras y passe tout de suite.
    CHECK(rando::camera::armLengthAfter(3.5f, 3.5f, 1.2f, 0.4f, 1.0f / 60.0f) == 1.2f);
    // Dégagé : il revient vers 3,5 m, sans y sauter.
    const float back = rando::camera::armLengthAfter(1.2f, 3.5f, std::nullopt, 0.4f, 1.0f / 60.0f);
    CHECK(back > 1.2f);
    CHECK(back < 1.4f);
    // Un obstacle plus loin que le bras actuel ne le fait pas rentrer, mais borne son retour.
    CHECK(rando::camera::armLengthAfter(1.2f, 3.5f, 1.25f, 0.4f, 10.0f) == 1.25f);
}

TEST_CASE("le recentrage attend, ne suit qu'un joueur qui s'éloigne, et prend le plus court chemin")
{
    ThirdPersonCamera camera;
    CameraOrbit orbit{.yawDegrees = 0.0f, .secondsWithoutLook = 2.0f};
    // La caméra regarde vers −Z ; le joueur marche vers −Z, lacet 30°.
    const glm::vec2 away{0.0f, -1.0f};
    const float turned = rando::camera::recenteredYaw(orbit, camera, 30.0f, away, 0.1f);
    CHECK(turned > 0.0f);
    CHECK(turned < 30.0f);
    // Trop tôt après un regard : rien.
    orbit.secondsWithoutLook = 1.0f;
    CHECK(rando::camera::recenteredYaw(orbit, camera, 30.0f, away, 0.1f) == 0.0f);
    // De profil (vers +X), ou à l'arrêt : rien, sinon le renard tournerait en rond.
    orbit.secondsWithoutLook = 2.0f;
    CHECK(rando::camera::recenteredYaw(orbit, camera, 30.0f, {1.0f, 0.0f}, 0.1f) == 0.0f);
    CHECK(rando::camera::recenteredYaw(orbit, camera, 30.0f, {0.0f, -0.1f}, 0.1f) == 0.0f);
    // De 350° vers 10° : le lacet croît de quelques degrés, il ne fait pas le tour par 180°.
    orbit.yawDegrees = 350.0f;
    const glm::vec2 awayAt350{-std::sin(glm::radians(350.0f)), -std::cos(glm::radians(350.0f))};
    const float wrapped = rando::camera::recenteredYaw(orbit, camera, 10.0f, awayAt350, 0.1f);
    CHECK(wrapped > 350.0f);
    CHECK(wrapped < 352.0f);
}

TEST_CASE("le bras part du pivot à l'opposé du regard, et la caméra s'arrête au premier obstacle")
{
    // Lacet 0, tangage 0 : la caméra regarde vers −Z, elle est donc vers +Z du pivot.
    const glm::vec3 direction = rando::camera::armDirection(0.0f, 0.0f);
    CHECK(direction.z == doctest::Approx(1.0f));
    // Regarder vers le bas place la caméra au-dessus.
    CHECK(rando::camera::armDirection(0.0f, -30.0f).y > 0.0f);

    ThirdPersonCamera camera;
    CameraOrbit orbit{.yawDegrees = 0.0f, .pitchDegrees = 0.0f, .armLength = 3.5f};
    const auto wallAtOneMeter = [](glm::vec3, glm::vec3, float, float) -> std::optional<float>
    { return 1.0f; };
    const levain::scene::Transform pose = rando::camera::stepCamera(
        camera, orbit, {}, 0.3f, {.feet = {0.0f, 0.0f, 0.0f}, .yawDegrees = 0.0f, .velocity = {}},
        wallAtOneMeter, 1.0f / 60.0f);
    CHECK(pose.position.z == doctest::Approx(1.0f));
    CHECK(pose.position.y == doctest::Approx(camera.pivotHeight));
    // Sans collision, le bras traverse.
    camera.collides = false;
    orbit.armLength = 3.5f;
    const levain::scene::Transform through = rando::camera::stepCamera(
        camera, orbit, {}, 0.3f, {.feet = {}, .yawDegrees = 0.0f, .velocity = {}}, wallAtOneMeter,
        1.0f / 60.0f);
    CHECK(through.position.z == doctest::Approx(3.5f));
}

TEST_CASE("le lacet d'un personnage tourné vers +X vaut −90°")
{
    const glm::quat facingPlusX = glm::angleAxis(glm::radians(-90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    CHECK(rando::camera::yawDegreesOf(facingPlusX) == doctest::Approx(-90.0f));
}

TEST_CASE("la diagonale du clavier ne déclenche pas le recentrage")
{
    // La caméra regarde vers −Z ; avant et droite ensemble, la marche fait 45° avec le regard.
    const glm::vec2 diagonal = glm::normalize(glm::vec2{1.0f, -1.0f});
    CHECK_FALSE(
        rando::camera::walksAwayFrom(diagonal, 0.0f, ThirdPersonCamera{}.recenterMaxDegrees));
    CHECK(rando::camera::walksAwayFrom({0.0f, -1.0f}, 0.0f, 40.0f));
    CHECK_FALSE(rando::camera::walksAwayFrom({0.0f, 0.0f}, 0.0f, 40.0f));
}

TEST_CASE("le regard tourne, se borne, et compte le temps sans y toucher")
{
    ThirdPersonCamera camera;
    CameraOrbit orbit{.yawDegrees = 0.0f, .pitchDegrees = 0.0f, .secondsWithoutLook = 3.0f};
    // « Droite » fait décroître le lacet, qui croît vers la gauche ; le regard remet le compteur à
    // zéro.
    rando::camera::orbit(orbit, camera, {.look = {90.0f, 0.0f}}, 0.5f);
    CHECK(orbit.yawDegrees == doctest::Approx(-45.0f));
    CHECK(orbit.secondsWithoutLook == 0.0f);
    // Lever les yeux longtemps : le tangage s'arrête à sa borne haute.
    rando::camera::orbit(orbit, camera, {.look = {0.0f, 90.0f}}, 10.0f);
    CHECK(orbit.pitchDegrees == doctest::Approx(camera.maxPitchDegrees));
    // Sans regard, le temps s'ajoute ; le lacet reste dans un tour.
    rando::camera::orbit(orbit, camera, {}, 0.25f);
    CHECK(orbit.secondsWithoutLook == doctest::Approx(0.25f));
    rando::camera::orbit(orbit, camera, {.look = {720.0f, 0.0f}}, 1.0f);
    CHECK(std::abs(orbit.yawDegrees) <= 180.0f);
}

TEST_CASE("la caméra regarde le pivot, quel que soit son lacet et son tangage")
{
    ThirdPersonCamera camera;
    CameraOrbit orbit{.yawDegrees = 130.0f, .pitchDegrees = -35.0f, .armLength = 3.5f};
    const auto noWall = [](glm::vec3, glm::vec3, float, float) -> std::optional<float>
    { return std::nullopt; };
    const levain::scene::Transform pose = rando::camera::stepCamera(
        camera, orbit, {}, 0.3f, {.feet = {10.0f, 2.0f, -4.0f}, .yawDegrees = 0.0f, .velocity = {}},
        noWall, 1.0f / 60.0f);
    const glm::vec3 pivot{10.0f, 2.0f + camera.pivotHeight, -4.0f};
    const glm::vec3 look = pose.rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 toPivot = glm::normalize(pivot - pose.position);
    CHECK(glm::dot(look, toPivot) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("le retour du bras ne dépend pas de la durée des pas")
{
    // Deux demi-pas mènent où mène un pas entier : l'approche est exponentielle.
    const float whole = rando::camera::armLengthAfter(1.0f, 3.5f, std::nullopt, 0.4f, 0.1f);
    const float half = rando::camera::armLengthAfter(1.0f, 3.5f, std::nullopt, 0.4f, 0.05f);
    const float twice = rando::camera::armLengthAfter(half, 3.5f, std::nullopt, 0.4f, 0.05f);
    CHECK(twice == doctest::Approx(whole));
}

TEST_CASE("le bras raccourci par le jeu rentre en douceur, pas d'un coup")
{
    // À l'atterrissage, le jeu repasse de 6 à 3,5 m (ADR-0031) : sans obstacle, le bras y va
    // exponentiellement, comme il en revient.
    const float landing =
        rando::camera::armLengthAfter(6.0f, 3.5f, std::nullopt, 0.4f, 1.0f / 60.0f);
    CHECK(landing < 6.0f);
    CHECK(landing > 5.8f);
    // Un obstacle plus près que le bras actuel l'arrête toujours net.
    CHECK(rando::camera::armLengthAfter(6.0f, 3.5f, 4.0f, 0.4f, 1.0f / 60.0f) == 4.0f);
}

TEST_CASE("sous le plancher de l'eau, la caméra se relève et vise le joueur")
{
    const glm::vec3 pivot{0.0f, -1.3f, 0.0f};
    // Derrière le pivot (+z), plus bas que lui : le tangage vers le haut d'un nageur.
    const levain::scene::Transform under{.position = {0.0f, -2.0f, 3.0f},
                                         .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}};
    const levain::scene::Transform raised = rando::camera::aboveFloor(under, pivot, -1.14f);
    CHECK(raised.position.y == doctest::Approx(-1.14f));
    CHECK(raised.position.z == 3.0f);
    // Elle regarde le pivot : son avant (−z) pointe vers lui, un peu vers le bas.
    const glm::vec3 forward = raised.rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 toPivot = glm::normalize(pivot - raised.position);
    CHECK(glm::dot(forward, toPivot) == doctest::Approx(1.0f));
    // Au-dessus du plancher, ou sans plancher, rien ne change.
    CHECK(rando::camera::aboveFloor(under, pivot, -2.5f).position == under.position);
    CHECK(rando::camera::aboveFloor(under, pivot, std::nullopt).position == under.position);
}

TEST_CASE("une cible téléportée fait couper la caméra, pas une cible qui court")
{
    rando::camera::CameraOrbit orbit;
    // Aucun pas encore : rien à comparer.
    CHECK_FALSE(rando::camera::targetJumped(orbit, {0.0f, 0.0f, 0.0f}, 5.0f));
    orbit.lastTargetFeet = glm::vec3{0.0f};
    // Un planeur à 6 m/s fait 10 cm par pas ; une noyade ramène à 30 m.
    CHECK_FALSE(rando::camera::targetJumped(orbit, {0.1f, 0.0f, 0.0f}, 5.0f));
    CHECK(rando::camera::targetJumped(orbit, {30.0f, 1.0f, 0.0f}, 5.0f));
}
