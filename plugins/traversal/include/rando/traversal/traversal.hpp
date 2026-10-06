#pragma once

// Les moyens de traverser la vallée (ADR-0031 de Levain) : marcher, planer, nager, et l'endurance
// qu'ils coûtent. Un état à la fois décide de la vitesse du joueur, au-dessus du character
// controller du moteur ; la marche est celle du plugin `character`, appelée telle quelle.

#include <cstdint>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/animation/animator.hpp"
#include "levain/character/walk.hpp"
#include "levain/physics/character.hpp"
#include "levain/water/lake.hpp"

namespace rando::traversal
{

/// Ce que fait le joueur : un état à la fois, comme les *movement modes* d'Unreal. En l'air sans
/// planeur, il marche encore : la marche sait tomber (ADR-0028 de Levain).
enum class Mode : std::uint8_t
{
    Walk,
    Glide,
    Swim,
};

/// Le planeur : il avance dans la direction où le joueur regarde, que l'input tourne, et descend
/// lentement. Une finesse de 3.
struct Glider
{
    float speed = 6.0f;     ///< Vers l'avant, en m/s.
    float sinkSpeed = 2.0f; ///< Vers le bas, en m/s, une fois le freinage fini.
    /// Ouvert en pleine chute, il freine de 40 m/s par seconde (4 g) : une chute de 20 m/s
    /// rejoint `sinkSpeed` en moins d'une demi-seconde (`brakeToGlide`).
    float brakeDeceleration = 40.0f;
    /// L'élan vers l'avant, de la marche aux 6 m/s du planeur, en m/s² : pas d'un pas à l'autre.
    float acceleration = 8.0f;
    float turnDegreesPerSecond = 90.0f; ///< Un virage large : la marche fait 720°/s.
};

/// La nage : en surface, les pieds sous l'eau, le dos hors de l'eau.
struct Swimmer
{
    float speed = 1.8f;
    float acceleration = 4.0f; ///< En m/s² : l'eau freine les départs et les arrêts.
    /// Il nage quand ses pieds sont plus bas que ça sous la surface, et marche de nouveau, au sol,
    /// quand ils sont moins bas que `leaveDepth` : deux seuils, pour qu'au ras d'une rive en
    /// pente, il ne passe pas d'un état à l'autre à chaque pas.
    float enterDepth = 0.45f;
    float leaveDepth = 0.3f;
    float floatDepth = 0.4f;   ///< Où il flotte : ses pieds sous la surface.
    float floatSeconds = 0.3f; ///< La constante de temps du retour à la surface.
    float maxFloatSpeed = 2.0f;
    /// Ce que l'eau freine d'une chute, en m/s² (`brakeInWater`) : une chute de 15 m/s s'enfonce
    /// un peu avant de remonter, au lieu de s'arrêter net à la surface.
    float brakeDeceleration = 30.0f;
    float turnDegreesPerSecond = 360.0f;
};

/// Ce que coûte chaque effort, en jauge par seconde (la jauge pleine vaut 1), et ce qu'il en
/// revient au repos.
struct StaminaRates
{
    float runPerSecond = 1.0f / 15.0f;
    float glidePerSecond = 1.0f / 45.0f;
    float swimPerSecond = 1.0f / 60.0f;
    float refillPerSecond = 1.0f / 5.0f;
};

/// Les réglages du joueur : du game feel, à régler en jouant (M8.2).
struct TraversalRules
{
    levain::character::Walker walker;
    Glider glider;
    Swimmer swimmer;
    StaminaRates stamina;
};

/// L'endurance, de 0 à 1. **Épuisé**, le joueur ne court plus et ne plane plus jusqu'à ce que la
/// jauge soit pleine : sans ce verrou, elle oscillerait autour de zéro, et la course reprendrait à
/// chaque pas.
struct Stamina
{
    float value = 1.0f;
    bool exhausted = false;
};

/// L'état du joueur, et ce qu'il se rappelle d'un pas à l'autre.
struct Traversal
{
    Mode mode = Mode::Walk;
    /// Où la noyade le ramène : le dernier point où il avait pied, hors de l'eau, ou, entré dans
    /// l'eau par les airs, le sol sec le plus proche de l'amerrissage (choix de Donnovan).
    std::optional<glm::vec3> lastDryFeet;
    int drownings = 0;
};

/// Ce que le joueur trouve autour de lui à ce pas.
struct Situation
{
    levain::physics::GroundState ground = levain::physics::GroundState::InAir;
    float waterDepth = 0.0f; ///< Ses pieds sous la surface ; 0 hors de l'eau.
    bool jump = false;       ///< Un appui de saut que personne n'a encore lu.
};

/// La profondeur de `feet` sous la surface du lac : 0 hors du disque ou au-dessus de l'eau.
[[nodiscard]] float waterDepthAt(const levain::water::Lake& lake, const glm::vec3& feet);

/// L'état du pas qui commence (ADR-0031, « Un pas ») :
/// - les pieds assez profonds : la nage, d'où qu'il vienne ;
/// - en nageant : la marche, une fois au sol et les pieds assez hauts ;
/// - en planant : la marche, au premier contact, sur un nouvel appui, ou épuisé ;
/// - en marchant : le planeur, en l'air, sur un appui, s'il n'est pas épuisé.
[[nodiscard]] Mode nextMode(const TraversalRules& rules, Mode current, const Situation& situation,
                            const Stamina& stamina);

/// La vitesse verticale du planeur. Ouvert en pleine chute, il freine vers `-sinkSpeed` à
/// `brakeDeceleration` : passée d'un coup, une chute de 20 m/s s'arrêterait net, comme contre un
/// mur. Ouvert en montant, la gravité l'amène à `-sinkSpeed`, sans le dépasser.
[[nodiscard]] float brakeToGlide(const TraversalRules& rules, float previous, float seconds);

/// La vitesse du planeur : vers `speed` dans l'avant de `rotation`, à `acceleration`, et
/// `brakeToGlide` vers le bas.
[[nodiscard]] glm::vec3 glideVelocity(const TraversalRules& rules, const glm::quat& rotation,
                                      const glm::vec3& previous, float seconds);

/// La vitesse verticale dans l'eau : vers `wanted`, à `brakeDeceleration` au plus. Sans ce frein,
/// une chute deviendrait d'un pas à l'autre la remontée vers la surface, comme contre un mur.
[[nodiscard]] float brakeInWater(const TraversalRules& rules, float previous, float wanted,
                                 float seconds);

/// La vitesse de la nage : vers la direction demandée, à l'accélération de l'eau, et vers le haut
/// ou le bas pour flotter, les pieds à `floatDepth` sous la surface (`brakeInWater`).
[[nodiscard]] glm::vec3 swimVelocity(const TraversalRules& rules, const glm::vec2& direction,
                                     float waterDepth, const glm::vec3& previous, float seconds);

/// L'endurance après ce pas : la course (au sol, en mouvement), le planeur et la nage la vident ;
/// elle se recharge en marchant ou à l'arrêt, au sol. Le verrou `exhausted` se pose à zéro et se
/// lève jauge pleine.
[[nodiscard]] Stamina staminaAfter(const TraversalRules& rules, const Stamina& stamina, Mode mode,
                                   bool running, bool grounded, float seconds);

/// Ce que lit l'animation (M4.5 de Levain) : la nage et le vol, en plus de la marche. En vol, une
/// vitesse nulle : sans clip de vol, l'animateur garde la locomotion au sol, et le renard prend
/// la pose de repos au lieu de courir dans le vide (choix de Donnovan).
[[nodiscard]] levain::animation::CharacterMotion
motionOf(Mode mode, const levain::physics::CharacterState& state);

} // namespace rando::traversal
