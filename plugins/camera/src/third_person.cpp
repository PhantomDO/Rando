#include "rando/camera/third_person.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/gtc/constants.hpp>

#include "levain/core/log.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/layers.hpp"
#include "levain/physics/physics.hpp"
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
    // Ramené dans un tour : sur une longue partie, un lacet qui grandit sans fin perdrait sa
    // précision.
    orbit.yawDegrees = std::remainder(orbit.yawDegrees - (input.look.x * scale), 360.0f);
    orbit.pitchDegrees = levain::scene::clampPitch(orbit.pitchDegrees + (input.look.y * scale),
                                                   camera.minPitchDegrees, camera.maxPitchDegrees);
    const bool looking = glm::length(input.look) > 1e-3f;
    orbit.secondsWithoutLook = looking ? 0.0f : orbit.secondsWithoutLook + seconds;
}

bool walksAwayFrom(glm::vec2 velocity, float yawDegrees, float maxDegrees)
{
    const float speed = glm::length(velocity);
    if (speed < 1e-6f)
    {
        return false;
    }
    // Le regard de la caméra à plat, sur le plan (x, z) : 0 regarde vers −Z.
    const float yaw = glm::radians(yawDegrees);
    const glm::vec2 forward{-std::sin(yaw), -std::cos(yaw)};
    return glm::dot(velocity / speed, forward) > std::cos(glm::radians(maxDegrees));
}

float recenteredYaw(const CameraOrbit& orbit, const ThirdPersonCamera& camera,
                    float targetYawDegrees, glm::vec2 targetVelocity, float seconds)
{
    if (orbit.secondsWithoutLook < camera.recenterWaitSeconds ||
        glm::length(targetVelocity) < camera.recenterMinSpeed ||
        !walksAwayFrom(targetVelocity, orbit.yawDegrees, camera.recenterMaxDegrees))
    {
        return orbit.yawDegrees;
    }
    const float gap = glm::degrees(levain::scene::shortestYawDelta(glm::radians(orbit.yawDegrees),
                                                                   glm::radians(targetYawDegrees)));
    return approach(orbit.yawDegrees, orbit.yawDegrees + gap, camera.recenterSeconds, seconds);
}

float armLengthAfter(float current, float wanted, std::optional<float> hitDistance,
                     float returnSeconds, float seconds)
{
    if (hitDistance && *hitDistance < current)
    {
        return std::max(*hitDistance, 0.0f);
    }
    const float eased = approach(current, wanted, returnSeconds, seconds);
    return hitDistance ? std::min(eased, *hitDistance) : eased;
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

levain::scene::Transform aboveFloor(const levain::scene::Transform& pose, const glm::vec3& pivot,
                                    std::optional<float> floorHeight)
{
    if (!floorHeight || pose.position.y >= *floorHeight)
    {
        return pose;
    }
    levain::scene::Transform raised = pose;
    raised.position.y = *floorHeight;
    // Vers le pivot : le lacet, puis le tangage, dans l'ordre de la caméra libre.
    const glm::vec3 look = pivot - raised.position;
    const float yaw = std::atan2(-look.x, -look.z);
    const float pitch = std::atan2(look.y, glm::length(glm::vec2{look.x, look.z}));
    raised.rotation = glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f}) *
                      glm::angleAxis(pitch, glm::vec3{1.0f, 0.0f, 0.0f});
    return raised;
}

bool targetJumped(const CameraOrbit& orbit, const glm::vec3& feet, float cutDistance)
{
    return orbit.lastTargetFeet && glm::distance(*orbit.lastTargetFeet, feet) > cutDistance;
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
        camera.collides
            ? sphereCast(pivot, direction, std::max(camera.armLength, orbit.armLength), probeRadius)
            : std::nullopt;
    orbit.armLength =
        armLengthAfter(orbit.armLength, camera.armLength, hit, camera.returnSeconds, seconds);
    orbit.lastTargetFeet = target.feet;
    // Lacet d'abord, tangage ensuite, comme la caméra libre : sinon elle s'incline sur le côté.
    const levain::scene::Transform pose{
        .position = pivot + (direction * orbit.armLength),
        .rotation = glm::angleAxis(glm::radians(orbit.yawDegrees), glm::vec3{0.0f, 1.0f, 0.0f}) *
                    glm::angleAxis(glm::radians(orbit.pitchDegrees), glm::vec3{1.0f, 0.0f, 0.0f}),
        .scale = glm::vec3{1.0f}};
    return aboveFloor(pose, pivot, camera.floorHeight);
}

SphereCast armSphereCast(const levain::physics::PhysicsWorld& physics)
{
    return [&physics](glm::vec3 origin, glm::vec3 direction, float length,
                      float radius) -> std::optional<float>
    {
        const auto hit = levain::physics::sphereCast(
            physics, {.origin = origin, .direction = direction, .maxDistance = length}, radius,
            ArmMask);
        return hit ? std::optional{hit->distance} : std::nullopt;
    };
}

std::optional<CameraTarget> cameraTargetOf(flecs::entity target)
{
    const auto* feet = target.is_alive() ? target.try_get<levain::scene::Transform>() : nullptr;
    if (feet == nullptr)
    {
        return std::nullopt;
    }
    const auto* state = target.try_get<levain::physics::CharacterState>();
    return CameraTarget{.feet = feet->position,
                        .yawDegrees = yawDegreesOf(feet->rotation),
                        .velocity = state ? glm::vec2{state->velocity.x, state->velocity.z}
                                          : glm::vec2{0.0f}};
}

namespace
{

/// Un pas de la caméra d'une entité, ou une erreur au journal, une fois, si sa cible a disparu :
/// la caméra s'arrête là où elle est, plutôt que de lire une entité morte.
void followTarget(const flecs::world& world, ThirdPersonCamera& camera, CameraOrbit& orbit,
                  const OrbitInput& input, const levain::app::CameraLens& lens,
                  levain::scene::Transform& transform, levain::scene::PreviousTransform& previous,
                  float seconds)
{
    const std::optional<CameraTarget> target = cameraTargetOf(world.entity(camera.target));
    if (!target)
    {
        if (!camera.targetLost)
        {
            levain::core::log("camera", levain::core::LogLevel::Error,
                              "la cible de la caméra n'existe pas, ou n'a pas de Transform");
            camera.targetLost = true;
        }
        return;
    }
    // Par référence : un `set` remettrait l'état précédent à jour, et le rendu ne l'interpolerait
    // plus (ADR-0016 de Levain).
    const bool cut = targetJumped(orbit, target->feet, camera.cutDistance);
    transform = stepCamera(camera, orbit, input,
                           nearPlaneRadius(lens, camera.aspectRatio) + camera.probeMargin, *target,
                           armSphereCast(world.get<levain::physics::PhysicsWorld>()), seconds);
    if (cut)
    {
        previous.transform = transform;
    }
}

} // namespace

ThirdPersonCameraModule::ThirdPersonCameraModule(flecs::world& world)
{
    world.module<ThirdPersonCameraModule>();
    world.import<levain::physics::PhysicsModule>();
    // Ce dont la caméra a besoin, ajouté avec ses réglages : sans, la requête ne la trouverait pas,
    // et elle ne ferait rien sans rien dire (le trait With de flecs, comme le personnage du
    // moteur).
    world.component<ThirdPersonCamera>()
        .add(flecs::With, world.component<CameraOrbit>())
        .add(flecs::With, world.component<OrbitInput>())
        .add(flecs::With, world.component<levain::scene::Transform>())
        .add(flecs::With, world.component<levain::scene::PreviousTransform>());
    // Après le pas de physique : la cible a déjà bougé, et le bras voit le décor de ce pas.
    world
        .system<ThirdPersonCamera, CameraOrbit, const OrbitInput, const levain::app::CameraLens,
                levain::scene::Transform, levain::scene::PreviousTransform>("ThirdPersonCamera")
        .kind<levain::scene::PostPhysics>()
        .each(
            [](flecs::iter& it, std::size_t, ThirdPersonCamera& camera, CameraOrbit& orbit,
               const OrbitInput& input, const levain::app::CameraLens& lens,
               levain::scene::Transform& transform, levain::scene::PreviousTransform& previous)
            {
                followTarget(it.world(), camera, orbit, input, lens, transform, previous,
                             it.delta_time());
            });
}

} // namespace rando::camera
