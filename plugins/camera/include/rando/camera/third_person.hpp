#pragma once

// La caméra à la troisième personne de Rando (ADR-0030 de Levain) : elle tourne autour du joueur,
// rentre contre la roche que coupe son bras, en ressort en douceur, et se replace derrière lui
// quand il marche sans qu'on y touche.

#include <cstdint>
#include <functional>
#include <optional>

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "levain/app/camera.hpp"
#include "levain/scene/components.hpp"

namespace rando::camera
{

/// Les réglages de la caméra, avec leurs valeurs de départ (ADR-0030) : du game feel, à régler en
/// jouant.
struct ThirdPersonCamera
{
    flecs::entity_t target = 0; ///< L'entité suivie : ses pieds, son lacet, sa vitesse.
    float pivotHeight = 0.6f;   ///< Le pivot au-dessus des pieds : le dos d'un renard de 0,79 m.
    float armLength = 3.5f;     ///< Le bras sans obstacle.
    /// Vers le bas, on ne passe pas par-dessus la tête ; vers le haut, la caméra glisse sur le sol
    /// et lève les yeux vers les crêtes (choix de Donnovan).
    float minPitchDegrees = -70.0f;
    float maxPitchDegrees = 30.0f;
    float lookDegreesPerUnit = 1.0f;  ///< Comme la caméra libre (ADR-0017 de Levain).
    float returnSeconds = 0.4f;       ///< La constante de temps du retour, l'obstacle dégagé.
    float recenterWaitSeconds = 1.5f; ///< Sans regard, l'attente avant le recentrage.
    float recenterSeconds = 1.0f;     ///< La constante de temps du recentrage.
    float recenterMinSpeed = 0.5f;    ///< En dessous, en m/s, le joueur est à l'arrêt.
    /// Ce que la sphère garde en plus du plan proche : collision, image et mesure ne voient pas
    /// exactement le même relief (ADR-0030).
    float probeMargin = 0.05f;
    /// La forme de l'image, que le jeu pose à chaque image : le rayon de la sphère en dépend.
    float aspectRatio = 16.0f / 9.0f;
    /// Faux : le bras traverse tout. Seulement pour prouver que le contrôle de la CI mord.
    bool collides = true;
};

/// Où regarde la caméra, et ce qu'elle se rappelle d'un pas à l'autre. Les angles sont la source :
/// la rotation de son `Transform` en est calculée, comme celle de la caméra libre.
struct CameraOrbit
{
    float yawDegrees = 0.0f;     ///< 0 : la caméra regarde vers −Z. Croît vers la gauche.
    float pitchDegrees = -15.0f; ///< Positif vers le haut.
    float armLength = 3.5f;      ///< La longueur du bras à ce pas, obstacles compris.
    float secondsWithoutLook = 0.0f;
};

/// Ce que le joueur demande à la caméra pour ce pas : x tourne à droite, y lève les yeux. Une
/// vitesse, comme les axes de l'input (ADR-0017 de Levain) ; le jeu la pose avant les pas.
struct OrbitInput
{
    glm::vec2 look{0.0f};
};

/// Le rayon de la sphère qui contient le **plan proche** de l'objectif : la distance de son coin au
/// centre optique, `near × √(1 + tan²(fov/2) × (1 + aspect²))`. Une sphère plus petite laisserait
/// les coins de l'image entrer dans la roche. 1,54 fois le plan proche à 60° en 16:9.
[[nodiscard]] float nearPlaneRadius(const levain::app::CameraLens& lens, float aspectRatio);

/// Le regard, tourné par l'input et borné, et le temps passé sans y toucher.
void orbit(CameraOrbit& orbit, const ThirdPersonCamera& camera, const OrbitInput& input,
           float seconds);

/// Le lacet recentré vers celui du joueur, s'il le faut : le regard resté à zéro depuis
/// `recenterWaitSeconds`, le joueur qui avance plus vite que `recenterMinSpeed`, **et qui
/// s'éloigne de la caméra**, sa marche à moins de 45° de son regard. Le piège : sans cette dernière
/// condition, tenir « droite » ferait tourner le renard en rond, la caméra et sa droite tournant
/// derrière lui.
[[nodiscard]] float recenteredYaw(const CameraOrbit& orbit, const ThirdPersonCamera& camera,
                                  float targetYawDegrees, glm::vec2 targetVelocity, float seconds);

/// La longueur du bras à ce pas. Touché plus court que le bras actuel : il prend la distance
/// touchée **tout de suite**. Sinon, il revient vers `armLength` exponentiellement, sans dépasser
/// la distance libre que le sphere cast vient de trouver.
[[nodiscard]] float armLengthAfter(float current, float wanted, std::optional<float> hitDistance,
                                   float returnSeconds, float seconds);

/// La direction du pivot vers la caméra : l'inverse de son regard.
[[nodiscard]] glm::vec3 armDirection(float yawDegrees, float pitchDegrees);

/// Le lacet d'une rotation autour de Y, dans la convention de la caméra : 0 regarde vers −Z.
[[nodiscard]] float yawDegreesOf(const glm::quat& rotation);

/// Ce que la caméra suit : les pieds du joueur, son lacet, et sa vitesse sur le plan horizontal.
struct CameraTarget
{
    glm::vec3 feet{0.0f};
    float yawDegrees = 0.0f;
    glm::vec2 velocity{0.0f};
};

/// Le premier obstacle d'une sphère de rayon `radius` lancée de `origin` vers `direction`, sur
/// `length` : la distance parcourue par son centre, ou rien. Dans le jeu, `physics::sphereCast`.
using SphereCast = std::function<std::optional<float>(glm::vec3 origin, glm::vec3 direction,
                                                      float length, float radius)>;

/// Un pas de caméra (ADR-0030) : le regard, le recentrage, le bras contre la roche, puis la pose,
/// au bout du bras, regardant le pivot.
[[nodiscard]] levain::scene::Transform stepCamera(const ThirdPersonCamera& camera,
                                                  CameraOrbit& orbit, const OrbitInput& input,
                                                  float probeRadius, const CameraTarget& target,
                                                  const SphereCast& sphereCast, float seconds);

/// Le module flecs de la caméra : `world.import<rando::camera::ThirdPersonCameraModule>()`. Une
/// entité qui porte `ThirdPersonCamera`, `CameraOrbit`, `OrbitInput`, un `CameraLens` et un
/// `Transform` suit sa cible, dans la phase `PostPhysics`, après que la cible a bougé. Elle doit
/// porter un `PreviousTransform` : le rendu l'interpole entre deux pas.
struct ThirdPersonCameraModule
{
    explicit ThirdPersonCameraModule(flecs::world& world);
};

} // namespace rando::camera
