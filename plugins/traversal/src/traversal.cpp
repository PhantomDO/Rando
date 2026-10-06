#include "rando/traversal/traversal.hpp"

#include <algorithm>

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

} // namespace

float waterDepthAt(const levain::water::Lake& lake, const glm::vec3& feet)
{
    const bool overLake = glm::length(glm::vec2{feet.x, feet.z} - lake.center) <= lake.radius;
    return overLake ? std::max(lake.level - feet.y, 0.0f) : 0.0f;
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

} // namespace rando::traversal
