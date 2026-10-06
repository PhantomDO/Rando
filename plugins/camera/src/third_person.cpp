#include "rando/camera/third_person.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/constants.hpp>

#include "levain/physics/character.hpp"
#include "levain/physics/layers.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/physics/queries.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/scene.hpp"

namespace rando::camera
{

namespace
{

/// Ce que `current` gagne vers `target` en `seconds`, avec la constante de temps `timeConstant` :
/// une approche exponentielle, la même quelle que soit la durée du pas.
float approach(float current, float target, float timeConstant, float seconds)
{
    return current + ((target - current) * (1.0f - std::exp(-seconds / timeConstant)));
}

/// Le bras touche le décor et ce qui roule, pas le personnage : le pivot est dans sa capsule, et
/// une sphère partie de l'intérieur d'un corps qui avance vers lui le touche à la distance 0
/// (queries.hpp de Levain). Ni les volumes déclencheurs : le lac ne repousse pas la caméra.
constexpr levain::physics::LayerMask ArmMask =
    levain::physics::maskOf({levain::physics::Layer::Static, levain::physics::Layer::Dynamic});

} // namespace

float nearPlaneRadius(const levain::app::CameraLens& lens, float aspectRatio)
{
    const float tangent = std::tan(glm::radians(lens.verticalFovDegrees) / 2.0f);
    return lens.nearPlane *
           std::sqrt(1.0f + (tangent * tangent * (1.0f + (aspectRatio * aspectRatio))));
}

void orbit(CameraOrbit& orbit, const ThirdPersonCamera& camera, const OrbitInput& input,
           float seconds)
{
    // Tourner à droite fait décroître le lacet, qui croît vers la gauche (camera_control.cpp).
    const float scale = camera.lookDegreesPerUnit * seconds;
    orbit.yawDegrees -= input.look.x * scale;
    orbit.pitchDegrees = levain::scene::clampPitch(orbit.pitchDegrees + (input.look.y * scale),
                                                   camera.minPitchDegrees, camera.maxPitchDegrees);
    const bool looking = glm::length(input.look) > 1e-3f;
    orbit.secondsWithoutLook = looking ? 0.0f : orbit.secondsWithoutLook + seconds;
}

float recenteredYaw(const CameraOrbit& orbit, const ThirdPersonCamera& camera,
                    float targetYawDegrees, glm::vec2 targetVelocity, float seconds)
{
    const float speed = glm::length(targetVelocity);
    if (orbit.secondsWithoutLook < camera.recenterWaitSeconds || speed < camera.recenterMinSpeed)
    {
        return orbit.yawDegrees;
    }
    // Le regard de la caméra à plat, sur le plan (x, z) : 0 regarde vers −Z.
    const float yaw = glm::radians(orbit.yawDegrees);
    const glm::vec2 forward{-std::sin(yaw), -std::cos(yaw)};
    if (glm::dot(targetVelocity / speed, forward) < std::cos(glm::radians(45.0f)))
    {
        return orbit.yawDegrees;
    }
    const float gap =
        glm::degrees(levain::scene::shortestYawDelta(yaw, glm::radians(targetYawDegrees)));
    return approach(orbit.yawDegrees, orbit.yawDegrees + gap, camera.recenterSeconds, seconds);
}

float armLengthAfter(float current, float wanted, std::optional<float> hitDistance,
                     float returnSeconds, float seconds)
{
    if (hitDistance && *hitDistance < current)
    {
        return std::max(*hitDistance, 0.0f);
    }
    const float free = hitDistance.value_or(wanted);
    return std::min(approach(current, wanted, returnSeconds, seconds), free);
}

glm::vec3 armDirection(float yawDegrees, float pitchDegrees)
{
    const float yaw = glm::radians(yawDegrees);
    const float pitch = glm::radians(pitchDegrees);
    const glm::vec3 look{-std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                         -std::cos(yaw) * std::cos(pitch)};
    return -look;
}

float yawDegreesOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    return glm::degrees(std::atan2(-forward.x, -forward.z));
}

levain::scene::Transform stepCamera(const ThirdPersonCamera& camera, CameraOrbit& orbit,
                                    const OrbitInput& input, float probeRadius,
                                    const CameraTarget& target, const SphereCast& sphereCast,
                                    float seconds)
{
    rando::camera::orbit(orbit, camera, input, seconds);
    orbit.yawDegrees = recenteredYaw(orbit, camera, target.yawDegrees, target.velocity, seconds);
    const glm::vec3 pivot = target.feet + glm::vec3{0.0f, camera.pivotHeight, 0.0f};
    const glm::vec3 direction = armDirection(orbit.yawDegrees, orbit.pitchDegrees);
    const std::optional<float> hit =
        camera.collides ? sphereCast(pivot, direction, camera.armLength, probeRadius)
                        : std::nullopt;
    orbit.armLength =
        armLengthAfter(orbit.armLength, camera.armLength, hit, camera.returnSeconds, seconds);
    // Lacet d'abord, tangage ensuite, comme la caméra libre : sinon elle s'incline sur le côté.
    return {.position = pivot + (direction * orbit.armLength),
            .rotation =
                glm::angleAxis(glm::radians(orbit.yawDegrees), glm::vec3{0.0f, 1.0f, 0.0f}) *
                glm::angleAxis(glm::radians(orbit.pitchDegrees), glm::vec3{1.0f, 0.0f, 0.0f}),
            .scale = glm::vec3{1.0f}};
}

ThirdPersonCameraModule::ThirdPersonCameraModule(flecs::world& world)
{
    world.module<ThirdPersonCameraModule>();
    // Après le pas de physique : la cible a déjà bougé, et le bras voit le décor de ce pas.
    world
        .system<const ThirdPersonCamera, CameraOrbit, const OrbitInput,
                const levain::app::CameraLens, levain::scene::Transform>("ThirdPersonCamera")
        .kind<levain::scene::PostPhysics>()
        .each(
            [](flecs::iter& it, std::size_t, const ThirdPersonCamera& camera, CameraOrbit& orbit,
               const OrbitInput& input, const levain::app::CameraLens& lens,
               levain::scene::Transform& transform)
            {
                const flecs::world world = it.world();
                const flecs::entity target = world.entity(camera.target);
                const auto& feet = target.get<levain::scene::Transform>();
                const auto* state = target.try_get<levain::physics::CharacterState>();
                const auto& physics = world.get<levain::physics::PhysicsWorld>();
                const SphereCast cast = [&physics](glm::vec3 origin, glm::vec3 direction,
                                                   float length,
                                                   float radius) -> std::optional<float>
                {
                    const auto hit = levain::physics::sphereCast(
                        physics, {.origin = origin, .direction = direction, .maxDistance = length},
                        radius, ArmMask);
                    return hit ? std::optional{hit->distance} : std::nullopt;
                };
                // Par référence : un `set` remettrait l'état précédent à jour, et le rendu ne
                // l'interpolerait plus (ADR-0016 de Levain).
                transform =
                    stepCamera(camera, orbit, input,
                               nearPlaneRadius(lens, camera.aspectRatio) + camera.probeMargin,
                               {.feet = feet.position,
                                .yawDegrees = yawDegreesOf(feet.rotation),
                                .velocity = state ? glm::vec2{state->velocity.x, state->velocity.z}
                                                  : glm::vec2{0.0f}},
                               cast, it.delta_time());
            });
}

} // namespace rando::camera
