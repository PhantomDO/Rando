#include "valley_world.hpp"

#include <glm/gtc/constants.hpp>

#include "levain/physics/physics.hpp"
#include "levain/scene/components.hpp"
#include "levain/terrain/collision.hpp"

namespace rando
{

levain::water::Lake lakeOf(const levain::terrain::ValleySettings& valley)
{
    return {.center = valley.lakeCenter, .radius = valley.lakeRadius, .level = LakeLevel};
}

glm::vec3 feetAt(const levain::terrain::Heightmap& heightmap, glm::vec2 spot)
{
    return {spot.x, levain::terrain::heightAt(heightmap, spot), spot.y};
}

glm::vec3 startOf(const levain::terrain::Heightmap& heightmap,
                  const levain::terrain::ValleySettings& valley)
{
    return feetAt(heightmap, valley.lakeCenter - glm::vec2{valley.lakeRadius * 1.4f, 0.0f});
}

glm::quat facingPlusX()
{
    return glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
}

void spawnValley(flecs::world& world, const levain::terrain::Heightmap& heightmap,
                 const levain::terrain::ValleySettings& valley)
{
    world.import<levain::physics::PhysicsModule>();
    // À la place du WalkModule : un seul système déplace le joueur (ADR-0031 de Levain).
    world.import<traversal::TraversalModule>();
    world.set(lakeOf(valley));
    world.entity("terrain")
        .set(levain::scene::Transform{})
        .set(levain::terrain::colliderOf(heightmap));
}

flecs::entity spawnPlayer(flecs::world& world, const glm::vec3& feet, const glm::quat& rotation)
{
    return world.entity("player")
        .set(levain::scene::Transform{.position = feet, .rotation = rotation})
        .set(FoxController)
        .set(traversal::TraversalRules{
            .walker = FoxWalker, .glider = {}, .swimmer = {}, .stamina = {}});
}

} // namespace rando
