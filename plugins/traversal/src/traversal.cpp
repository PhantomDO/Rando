#include "rando/traversal/traversal.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

#include "levain/physics/physics.hpp"
#include "levain/physics/queries.hpp"
#include "levain/scene/scene.hpp"

namespace rando::traversal
{

namespace
{

using levain::physics::GroundState;

/// Rapproche `current` de `target` de `step` au plus, en ligne droite.
glm::vec2 approach(const glm::vec2& current, const glm::vec2& target, float step)
{
    const glm::vec2 gap = target - current;
    const float distance = glm::length(gap);
    return distance <= step ? target : current + (gap * (step / distance));
}

/// L'avant (−z) de `rotation`, sur le plan horizontal.
glm::vec2 forwardOf(const glm::quat& rotation)
{
    const glm::vec3 forward = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec2 flat{forward.x, forward.z};
    const float length = glm::length(flat);
    return length > 1e-6f ? flat / length : glm::vec2{0.0f, -1.0f};
}

/// Une marche qui ne tourne qu'à `degreesPerSecond` : le planeur et la nage se tournent comme
/// elle, en plus lent.
levain::character::Walker turningAt(const TraversalRules& rules, float degreesPerSecond)
{
    levain::character::Walker walker = rules.walker;
    walker.turnDegreesPerSecond = degreesPerSecond;
    return walker;
}

/// La course qui coûte : demandée, au sol, et en mouvement. Rester sur place la touche enfoncée
/// ne vide rien.
bool isRunning(const levain::character::WalkInput& input,
               const levain::physics::CharacterState& state)
{
    return input.run && levain::physics::isWalking(state.ground) &&
           glm::length(input.direction) > 1e-3f;
}

/// La noyade, dans la glu : poser le `Transform` téléporte le personnage (ADR-0028 de Levain). Le
/// `set` est différé jusqu'au point de synchronisation qui précède la physique
/// (`SyncBeforePhysics`) : le personnage est déplacé avant le pas de Jolt, dans le même pas.
void teleportIfDrowned(flecs::entity player, const std::optional<levain::scene::Transform>& respawn)
{
    if (respawn)
    {
        player.set(*respawn);
    }
}

} // namespace

float waterDepthAt(const levain::water::Lake& lake, const glm::vec3& feet)
{
    const bool overLake = glm::length(glm::vec2{feet.x, feet.z} - lake.center) <= lake.radius;
    return overLake ? std::max(lake.level - feet.y, 0.0f) : 0.0f;
}

std::optional<glm::vec3> nearestShore(const levain::water::Lake& lake, glm::vec2 entry,
                                      const GroundProbe& ground)
{
    constexpr int Directions = 24;
    const int rings = static_cast<int>(std::ceil(lake.radius * 2.0f));
    for (int ring = 1; ring <= rings; ++ring)
    {
        for (int direction = 0; direction < Directions; ++direction)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(direction) /
                                static_cast<float>(Directions);
            const glm::vec2 spot =
                entry + (static_cast<float>(ring) * glm::vec2{std::cos(angle), std::sin(angle)});
            const std::optional<float> height = ground(spot);
            if (height && *height > lake.level)
            {
                return glm::vec3{spot.x, *height, spot.y};
            }
        }
    }
    return std::nullopt;
}

GroundProbe groundProbe(const levain::physics::PhysicsWorld& physics)
{
    return [&physics](glm::vec2 spot) -> std::optional<float>
    {
        // De 1 km de haut : au-dessus de toute la vallée.
        const auto hit =
            levain::physics::raycast(physics,
                                     {.origin = {spot.x, 1000.0f, spot.y},
                                      .direction = {0.0f, -1.0f, 0.0f},
                                      .maxDistance = 2000.0f},
                                     levain::physics::maskOf({levain::physics::Layer::Static}));
        return hit ? std::optional{hit->point.y} : std::nullopt;
    };
}

Mode nextMode(const TraversalRules& rules, Mode current, const Situation& situation,
              const Stamina& stamina)
{
    if (situation.waterDepth > rules.swimmer.enterDepth)
    {
        return Mode::Swim;
    }
    switch (current)
    {
    case Mode::Swim:
        return situation.ground == GroundState::OnGround &&
                       situation.waterDepth < rules.swimmer.leaveDepth
                   ? Mode::Walk
                   : Mode::Swim;
    case Mode::Glide:
        // Au premier contact, même une paroi trop raide : le planeur se replie contre elle.
        return situation.ground != GroundState::InAir || situation.jump || stamina.exhausted
                   ? Mode::Walk
                   : Mode::Glide;
    case Mode::Walk:
        return situation.ground == GroundState::InAir && situation.jump && !stamina.exhausted
                   ? Mode::Glide
                   : Mode::Walk;
    }
    return current;
}

float brakeToGlide(const TraversalRules& rules, float previous, float seconds)
{
    const float sink = -rules.glider.sinkSpeed;
    if (previous > sink)
    {
        return std::max(previous - (rules.walker.gravity * seconds), sink);
    }
    return std::min(previous + (rules.glider.brakeDeceleration * seconds), sink);
}

glm::vec3 glideVelocity(const TraversalRules& rules, const glm::quat& rotation,
                        const glm::vec3& previous, float seconds)
{
    const glm::vec2 horizontal =
        approach(glm::vec2{previous.x, previous.z}, forwardOf(rotation) * rules.glider.speed,
                 rules.glider.acceleration * seconds);
    return {horizontal.x, brakeToGlide(rules, previous.y, seconds), horizontal.y};
}

float brakeInWater(const TraversalRules& rules, float previous, float wanted, float seconds)
{
    const float step = rules.swimmer.brakeDeceleration * seconds;
    return std::clamp(wanted, previous - step, previous + step);
}

glm::vec3 swimVelocity(const TraversalRules& rules, const glm::vec2& direction, float waterDepth,
                       const glm::vec3& previous, float seconds)
{
    const Swimmer& swimmer = rules.swimmer;
    const float length = glm::length(direction);
    const glm::vec2 wanted = (length > 1.0f ? direction / length : direction) * swimmer.speed;
    const glm::vec2 horizontal =
        approach(glm::vec2{previous.x, previous.z}, wanted, swimmer.acceleration * seconds);
    // Trop bas, il remonte ; trop haut (il vient d'y tomber, ou la rive le soulève), il descend.
    const float rise = std::clamp((waterDepth - swimmer.floatDepth) / swimmer.floatSeconds,
                                  -swimmer.maxFloatSpeed, swimmer.maxFloatSpeed);
    return {horizontal.x, brakeInWater(rules, previous.y, rise, seconds), horizontal.y};
}

Stamina staminaAfter(const TraversalRules& rules, const Stamina& stamina, Mode mode, bool running,
                     bool grounded, float seconds)
{
    const StaminaRates& rates = rules.stamina;
    float change = 0.0f;
    switch (mode)
    {
    case Mode::Walk:
        change = running ? -rates.runPerSecond : (grounded ? rates.refillPerSecond : 0.0f);
        break;
    case Mode::Glide:
        change = -rates.glidePerSecond;
        break;
    case Mode::Swim:
        change = -rates.swimPerSecond;
        break;
    }
    const float value = std::clamp(stamina.value + (change * seconds), 0.0f, 1.0f);
    const bool exhausted = value <= 0.0f || (stamina.exhausted && value < 1.0f);
    return {.value = value, .exhausted = exhausted};
}

levain::animation::CharacterMotion motionOf(Mode mode, const levain::physics::CharacterState& state)
{
    levain::animation::CharacterMotion motion = levain::character::motionOf(state);
    motion.swimming = mode == Mode::Swim;
    motion.gliding = mode == Mode::Glide;
    if (mode == Mode::Glide)
    {
        motion.speed = 0.0f;
    }
    return motion;
}

std::optional<levain::scene::Transform>
stepTraversal(const TraversalRules& rules, const levain::water::Lake& lake, Traversal& traversal,
              Stamina& stamina, levain::character::WalkInput& input,
              const levain::physics::CharacterState& state,
              levain::physics::CharacterVelocity& velocity, levain::scene::Transform& transform,
              const GroundProbe& ground, float seconds)
{
    const float depth = waterDepthAt(lake, transform.position);
    // Le saut appartient à l'état qui le lit : au planeur s'il s'ouvre ou se replie, à la marche
    // sinon. Lu ici avant elle, il ne sert qu'une fois.
    const Mode before = traversal.mode;
    traversal.mode =
        nextMode(rules, before,
                 {.ground = state.ground.state, .waterDepth = depth, .jump = input.jump}, stamina);
    if (traversal.mode != before)
    {
        input.jump = false;
    }
    // Entré dans l'eau par les airs : la noyade le ramènera à la rive la plus proche, pas au
    // promontoire d'où il a sauté (choix de Donnovan). Sans point sec du tout (posé dans l'eau au
    // départ), de même : sinon, à bout de forces, il flotterait sans fin, sans que rien le dise.
    if (traversal.mode == Mode::Swim && before != Mode::Swim &&
        (state.ground.state == GroundState::InAir || !traversal.lastDryFeet))
    {
        if (auto shore =
                nearestShore(lake, glm::vec2{transform.position.x, transform.position.z}, ground))
        {
            traversal.lastDryFeet = shore;
        }
    }
    // Épuisé, il ne court plus : la marche ne voit pas la touche.
    if (stamina.exhausted)
    {
        input.run = false;
    }
    const bool running = traversal.mode == Mode::Walk && isRunning(input, state);
    const bool grounded = levain::physics::isWalking(state.ground);

    switch (traversal.mode)
    {
    case Mode::Walk:
        levain::character::stepWalk(rules.walker, input, state, velocity, transform, seconds);
        break;
    case Mode::Glide:
        // Par référence, sans `set` : un `set<Transform>` téléporterait le personnage.
        transform.rotation =
            levain::character::turnTowards(turningAt(rules, rules.glider.turnDegreesPerSecond),
                                           transform.rotation, input.direction, seconds);
        velocity.value = glideVelocity(rules, transform.rotation, velocity.value, seconds);
        break;
    case Mode::Swim:
        transform.rotation =
            levain::character::turnTowards(turningAt(rules, rules.swimmer.turnDegreesPerSecond),
                                           transform.rotation, input.direction, seconds);
        velocity.value = swimVelocity(rules, input.direction, depth, velocity.value, seconds);
        break;
    }
    input.jump = false;

    stamina = staminaAfter(rules, stamina, traversal.mode, running, grounded, seconds);
    // Le dernier point sec : au sol (pas sur une pente trop raide), hors de l'eau. Un saut
    // au-dessus du lac ou un vol ne le déplacent pas : on revient là où on avait pied.
    if (traversal.mode == Mode::Walk && grounded && depth <= 0.0f)
    {
        traversal.lastDryFeet = transform.position;
    }
    if (traversal.mode != Mode::Swim || stamina.value > 0.0f || !traversal.lastDryFeet)
    {
        return std::nullopt;
    }
    // La noyade (choix de Donnovan) : retour au dernier point sec, jauge pleine. C'est ici que
    // M8.2 retirera un cœur.
    ++traversal.drownings;
    traversal.mode = Mode::Walk;
    stamina = Stamina{};
    levain::scene::Transform respawn = transform;
    respawn.position = *traversal.lastDryFeet;
    return respawn;
}

TraversalModule::TraversalModule(flecs::world& world)
{
    world.module<TraversalModule>();
    world.import<levain::physics::PhysicsModule>();
    // Ce qu'il faut à un joueur qui traverse, ajouté avec ses réglages.
    world.component<TraversalRules>()
        .add(flecs::With, world.component<levain::character::WalkInput>())
        .add(flecs::With, world.component<Traversal>())
        .add(flecs::With, world.component<Stamina>())
        .add(flecs::With, world.component<levain::animation::CharacterMotion>());
    // Un lac vide tant que le jeu n'a pas posé le sien : sans singleton, la requête ne
    // correspondrait à rien, et le joueur resterait figé sans un mot (règle n°7 de Levain).
    if (!world.has<levain::water::Lake>())
    {
        world.set(levain::water::Lake{});
    }

    // La glu : une instruction par système (ADR-0011 de Levain). Dans la phase Simulation, avant
    // le pas de physique qui jouera la vitesse ; elle lit l'état du pas précédent.
    world
        .system<const TraversalRules, const levain::water::Lake, Traversal, Stamina,
                levain::character::WalkInput, const levain::physics::CharacterState,
                levain::physics::CharacterVelocity, levain::scene::Transform>("Traverse")
        .term_at(1)
        .src<levain::water::Lake>() // un singleton, lu une fois par table
        .kind<levain::scene::Simulation>()
        .each(
            [](flecs::iter& it, std::size_t row, const TraversalRules& rules,
               const levain::water::Lake& lake, Traversal& traversal, Stamina& stamina,
               levain::character::WalkInput& input, const levain::physics::CharacterState& state,
               levain::physics::CharacterVelocity& velocity, levain::scene::Transform& transform)
            {
                teleportIfDrowned(
                    it.entity(row),
                    stepTraversal(rules, lake, traversal, stamina, input, state, velocity,
                                  transform,
                                  groundProbe(it.world().get<levain::physics::PhysicsWorld>()),
                                  it.delta_time()));
            });
    world
        .system<levain::animation::CharacterMotion, const Traversal,
                const levain::physics::CharacterState>("TraversalMotion")
        .kind<levain::scene::Simulation>()
        .each([](levain::animation::CharacterMotion& motion, const Traversal& traversal,
                 const levain::physics::CharacterState& state)
              { motion = motionOf(traversal.mode, state); });
}

} // namespace rando::traversal
