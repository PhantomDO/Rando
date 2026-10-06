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
