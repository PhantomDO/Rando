#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include <flecs.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rando/camera/third_person.hpp"
#include "stamina_gauge.hpp"
#include "valley_world.hpp"

#include "levain/animation/animator.hpp"
#include "levain/app/app.hpp"
#include "levain/app/camera.hpp"
#include "levain/app/load_model.hpp"
#include "levain/app/player_input.hpp"
#include "levain/character/walk.hpp"
#include "levain/core/log.hpp"
#include "levain/grass/grass_pass.hpp"
#include "levain/input/bindings.hpp"
#include "levain/input/state.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/physics.hpp"
#include "levain/physics/physics_world.hpp"
#include "levain/platform/input.hpp"
#include "levain/platform/window.hpp"
#include "levain/render/debug_lines.hpp"
#include "levain/render/renderer.hpp"
#include "levain/render/stages.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"
#include "levain/scene/transform.hpp"
#include "levain/terrain/collision.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/terrain_pass.hpp"
#include "levain/water/water_pass.hpp"

namespace
{

/// Les options du jeu ; les options communes sont celles de `levain::app` (`--seconds`, `--steps`,
/// `--capture`…).
struct RandoOptions
{
    /// `--walk x,z` : la direction que suit le renard, dans le monde, au lieu du clavier. Pour la
    /// CI : ce que fait le joueur ne dépend plus de personne.
    std::optional<glm::vec2> walk;
    /// `--start x,z` : où le renard commence, au lieu du fond de la vallée à l'ouest du lac. Ses
    /// pieds se posent sur le relief.
    std::optional<glm::vec2> start;
    /// `--orbit N` : la caméra tourne autour du renard à N°/s, et son tangage va et vient d'une
    /// borne à l'autre, au lieu de la souris. Le regard scripté du critère de M6.4, compté en pas :
    /// à utiliser avec `--steps` (ADR-0030 de Levain).
    std::optional<float> orbitDegreesPerSecond;
    /// `--camera-collision off` : le bras traverse la roche. Seulement pour prouver que la mesure
    /// de la CI mord (règle n°7 de Levain).
    bool cameraCollides = true;
    /// `--glide N` : le renard saute au pas N, et ouvre le planeur 20 pas plus tard, d'un second
    /// appui (ADR-0031 de Levain). Avec `--walk` et `--steps`, la descente de la CI.
    std::optional<int> glideAtStep;
};

/// Le bras de la caméra en vol : plus long, pour voir où l'on va se poser (ADR-0031 de Levain).
constexpr float GlideArmLength = 6.0f;
constexpr float WalkArmLength = 3.5f;

/// Les actions et les axes que le jeu lit, résolus une fois par leurs noms. Un nom absent de
/// `input.cfg` est une erreur au démarrage : sans ça, la commande ne répondrait jamais, sans que
/// rien ne le dise (règle n°7 de Levain).
struct Controls
{
    int moveRight = 0;
    int moveForward = 0;
    int sprint = 0;
    int jump = 0;
    int lookRightMouse = 0;
    int lookUpMouse = 0;
    int lookRightPad = 0;
    int lookUpPad = 0;
    int freeMouse = 0;
    int captureMouse = 0;
};

levain::core::Result<Controls> controlsOf(const levain::input::Bindings& bindings)
{
    const auto axis = [&bindings](std::string_view name) -> levain::core::Result<int>
    {
        if (const auto index = levain::input::axisIndex(bindings, name))
        {
            return *index;
        }
        return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                       std::format("axe « {} » absent de input.cfg", name));
    };
    const auto action = [&bindings](std::string_view name) -> levain::core::Result<int>
    {
        if (const auto index = levain::input::actionIndex(bindings, name))
        {
            return *index;
        }
        return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                       std::format("action « {} » absente de input.cfg", name));
    };
    auto moveRight = axis("move_right");
    auto moveForward = axis("move_forward");
    auto sprint = action("sprint");
    auto jump = action("jump");
    auto lookRightMouse = axis("look_right_mouse");
    auto lookUpMouse = axis("look_up_mouse");
    auto lookRightPad = axis("look_right_pad");
    auto lookUpPad = axis("look_up_pad");
    auto freeMouse = action("free_mouse");
    auto captureMouse = action("capture_mouse");
    for (const auto* found : {&moveRight, &moveForward, &sprint, &jump, &lookRightMouse,
                              &lookUpMouse, &lookRightPad, &lookUpPad, &freeMouse, &captureMouse})
    {
        if (!*found)
        {
            return std::unexpected(found->error());
        }
    }
    return Controls{.moveRight = *moveRight,
                    .moveForward = *moveForward,
                    .sprint = *sprint,
                    .jump = *jump,
                    .lookRightMouse = *lookRightMouse,
                    .lookUpMouse = *lookUpMouse,
                    .lookRightPad = *lookRightPad,
                    .lookUpPad = *lookUpPad,
                    .freeMouse = *freeMouse,
                    .captureMouse = *captureMouse};
}

/// Le monde du jeu, gardé par ses points d'accroche (ADR-0029 de Levain) : la vallée et ses
/// passes, le joueur, ce qu'il commande. Ses passes tiennent des ressources du GPU : `app` le
/// détruit avant le device.
struct Valley
{
    levain::app::App& app;
    RandoOptions options;
    Controls controls;
    levain::terrain::Heightmap heightmap;
    levain::terrain::TerrainPass terrain;
    levain::water::WaterPass water;
    levain::grass::GrassPass grass;
    levain::terrain::TerrainStats terrainCamera;
    levain::terrain::TerrainStats terrainShadows;
    levain::grass::GrassStats grassStats;
    /// Les lignes de la jauge d'endurance (ADR-0031 de Levain).
    levain::render::DebugLinesPass gaugePass;
    flecs::entity player;
    flecs::entity camera;
    /// La souris tourne la caméra tant qu'elle est capturée (ADR-0030 de Levain).
    bool mouseCaptured = false;
    /// Le critère de M6.4 : la plus petite hauteur d'un point du plan proche au-dessus du relief,
    /// sur toutes les poses mesurées, et leur nombre.
    float minimumMargin = std::numeric_limits<float>::infinity();
    int measuredPoses = 0;
    /// Lus par la CI : les images où le renard planait, le plus long bras de la caméra, et les
    /// traits de jauge dessinés.
    int glidingFrames = 0;
    float longestArm = 0.0f;
    std::uint64_t gaugeLines = 0;
};

/// Ce que le joueur demande au renard, à chaque image : la direction des axes **tournée selon le
/// lacet de la caméra** (« avant » est là où elle regarde), ou celle de `--walk`, une direction du
/// monde ; la course ; et le saut, lu dans les appuis qu'aucun pas n'a encore vus
/// (`pressedSinceLastStep`) : la marche le consomme au pas suivant, et le moteur l'oublie à la fin
/// de ce pas. Ni perdu, ni doublé. La marche borne elle-même la longueur de la direction.
levain::character::WalkInput walkInputOf(const levain::app::PlayerInput& input,
                                         const Controls& controls, float cameraYawDegrees,
                                         std::optional<glm::vec2> scripted)
{
    const levain::scene::HorizontalBasis basis =
        levain::scene::horizontalBasisFrom(cameraYawDegrees);
    const glm::vec3 axes =
        (basis.forward * levain::input::axisValue(input.state, controls.moveForward)) +
        (basis.right * levain::input::axisValue(input.state, controls.moveRight));
    return {.direction = scripted.value_or(glm::vec2{axes.x, axes.z}),
            .run = levain::input::actionHeld(input.state, controls.sprint),
            .jump = levain::app::pressedSinceLastStep(input, controls.jump)};
}

/// Les appuis de `--glide N` : le saut au pas N, le second appui, celui qui ouvre le planeur,
/// 20 pas plus tard, en l'air.
bool scriptedGlidePress(int jumpStep, int frame)
{
    constexpr int GlideDelaySteps = 20;
    return frame == jumpStep || frame == jumpStep + GlideDelaySteps;
}

/// La caméra selon l'état du renard (ADR-0031 de Levain) : le bras plus long en vol, et le
/// plancher de l'eau, la surface du lac plus la sphère du plan proche. Le jeu les pose, comme la
/// forme de l'image : la caméra ne connaît ni le planeur ni le lac.
void frameCamera(rando::camera::ThirdPersonCamera& camera, const levain::app::CameraLens& lens,
                 rando::traversal::Mode mode)
{
    camera.armLength = mode == rando::traversal::Mode::Glide ? GlideArmLength : WalkArmLength;
    camera.floorHeight = rando::LakeLevel +
                         rando::camera::nearPlaneRadius(lens, camera.aspectRatio) +
                         camera.probeMargin;
}

/// Le regard scripté de `--orbit` : le lacet à vitesse constante, et le tangage qui va et vient
/// d'une borne à l'autre en 7 s. Les deux périodes ne se calent pas l'une sur l'autre : toutes les
/// directions du bras passent contre la pente, à tous les tangages, la caméra basse comprise, le
/// pire cas (ADR-0030 de Levain).
glm::vec2 scriptedLook(float yawDegreesPerSecond, float seconds)
{
    constexpr float PitchPeriodSeconds = 7.0f;
    constexpr float PitchDegreesPerSecond = 55.0f;
    return {yawDegreesPerSecond,
            PitchDegreesPerSecond * std::cos(glm::two_pi<float>() * seconds / PitchPeriodSeconds)};
}

/// Le regard du joueur : la souris seulement capturée (libre, on s'en sert hors de la fenêtre), le
/// stick droit toujours.
glm::vec2 lookOf(const levain::app::PlayerInput& input, const Controls& controls,
                 bool mouseCaptured)
{
    const glm::vec2 pad{levain::input::axisValue(input.state, controls.lookRightPad),
                        levain::input::axisValue(input.state, controls.lookUpPad)};
    const glm::vec2 mouse{levain::input::axisValue(input.state, controls.lookRightMouse),
                          levain::input::axisValue(input.state, controls.lookUpMouse)};
    return pad + (mouseCaptured ? mouse : glm::vec2{0.0f});
}

/// La souris est capturée à la première image ; Échap la libère, un clic la reprend.
// ponytail: dans le navigateur, le *pointer lock* ne s'obtient que dans un geste du joueur : la
// première capture y échouera en silence, et la caméra tournera au survol jusqu'au premier clic. À
// régler avec Rando dans le navigateur.
void captureMouse(Valley& valley, const levain::app::PlayerInput& input)
{
    const Controls& controls = valley.controls;
    bool wanted = valley.mouseCaptured;
    if (valley.app.frameCount == 0 ||
        levain::input::actionPressed(input.state, controls.captureMouse))
    {
        wanted = true;
    }
    if (levain::input::actionPressed(input.state, controls.freeMouse))
    {
        wanted = false;
    }
    if (wanted != valley.mouseCaptured)
    {
        levain::platform::setMouseCaptured(valley.app.window, wanted);
        valley.mouseCaptured = wanted;
    }
}

/// Ce que le joueur demande, à chaque image : la souris, la caméra (son regard, et la forme de
/// l'image, dont dépend la sphère de son bras), puis le renard.
void steer(Valley& valley)
{
    const auto& input = valley.app.world.get<levain::app::PlayerInput>();
    captureMouse(valley, input);
    const levain::platform::PixelSize pixels = levain::platform::windowPixelSize(valley.app.window);
    if (pixels.width > 0 && pixels.height > 0)
    {
        valley.camera.get_mut<rando::camera::ThirdPersonCamera>().aspectRatio =
            static_cast<float>(pixels.width) / static_cast<float>(pixels.height);
    }
    valley.camera.get_mut<rando::camera::OrbitInput>().look =
        valley.options.orbitDegreesPerSecond
            ? scriptedLook(*valley.options.orbitDegreesPerSecond,
                           static_cast<float>(valley.app.frameCount) *
                               valley.app.fixedStep.stepSeconds)
            : lookOf(input, valley.controls, valley.mouseCaptured);
    auto& walk = valley.player.get_mut<levain::character::WalkInput>();
    walk = walkInputOf(input, valley.controls,
                       valley.camera.get<rando::camera::CameraOrbit>().yawDegrees,
                       valley.options.walk);
    if (valley.options.glideAtStep)
    {
        walk.jump = scriptedGlidePress(*valley.options.glideAtStep, valley.app.frameCount);
    }
    const rando::traversal::Mode mode = valley.player.get<rando::traversal::Traversal>().mode;
    valley.glidingFrames += mode == rando::traversal::Mode::Glide ? 1 : 0;
    frameCamera(valley.camera.get_mut<rando::camera::ThirdPersonCamera>(),
                valley.camera.get<levain::app::CameraLens>(), mode);
}

/// La marge au relief d'une pose de la caméra : la hauteur, au-dessus du terrain, du centre et des
/// quatre coins de son plan proche. Négative, l'image entre dans la roche. Le relief de la vallée
/// n'a ni surplomb ni grotte : passer sous lui, c'est traverser la roche.
float marginOf(const levain::scene::Transform& pose, const levain::app::CameraLens& lens,
               float aspectRatio, const levain::terrain::Heightmap& heightmap)
{
    const float halfHeight =
        lens.nearPlane * std::tan(glm::radians(lens.verticalFovDegrees) / 2.0f);
    const float halfWidth = halfHeight * aspectRatio;
    float margin = std::numeric_limits<float>::infinity();
    for (const glm::vec3 corner : {glm::vec3{0.0f, 0.0f, -lens.nearPlane},
                                   glm::vec3{-halfWidth, -halfHeight, -lens.nearPlane},
                                   glm::vec3{halfWidth, -halfHeight, -lens.nearPlane},
                                   glm::vec3{-halfWidth, halfHeight, -lens.nearPlane},
                                   glm::vec3{halfWidth, halfHeight, -lens.nearPlane}})
    {
        const glm::vec3 point = pose.position + (pose.rotation * corner);
        margin = std::min(
            margin, point.y - levain::terrain::heightAt(heightmap, glm::vec2{point.x, point.z}));
    }
    return margin;
}

/// Le critère de M6.4, mesuré à chaque pas : la marge au relief de la caméra aux poses qu'une image
/// verrait entre le pas précédent et celui-ci (fractions 0, ¼, ½ et ¾), interpolées comme le rendu
/// les interpole. Avec `--steps`, le rendu ne voit jamais que la première ; c'est entre deux pas
/// qu'un bras qui rentre d'un coup passerait sous une arête (ADR-0030 de Levain).
void measureMargin(Valley& valley)
{
    const auto& lens = valley.camera.get<levain::app::CameraLens>();
    const float aspect = valley.camera.get<rando::camera::ThirdPersonCamera>().aspectRatio;
    const auto& previous = valley.camera.get<levain::scene::PreviousTransform>().transform;
    const auto& current = valley.camera.get<levain::scene::Transform>();
    for (const float fraction : {0.0f, 0.25f, 0.5f, 0.75f})
    {
        valley.minimumMargin = std::min(
            valley.minimumMargin, marginOf(levain::scene::interpolate(previous, current, fraction),
                                           lens, aspect, valley.heightmap));
        ++valley.measuredPoses;
    }
    valley.longestArm =
        std::max(valley.longestArm, valley.camera.get<rando::camera::CameraOrbit>().armLength);
}

/// La jauge d'endurance, à la pose interpolée du renard et de la caméra, celle de l'image. Par-
/// dessus ce qui est déjà dessiné (`OnTop`) : inscrite après l'herbe et l'eau, rien ne la couvre.
void drawGauge(Valley& valley, const levain::render::StageContext& context)
{
    const auto* feet = valley.player.try_get<levain::scene::WorldTransform>();
    const auto* eye = valley.camera.try_get<levain::scene::WorldTransform>();
    if (feet == nullptr || eye == nullptr)
    {
        return;
    }
    const glm::quat cameraRotation = levain::scene::worldRotation(*eye);
    const auto lines = rando::staminaGauge(
        valley.player.get<rando::traversal::Stamina>(),
        rando::gaugeCenter(levain::scene::worldPosition(*feet), cameraRotation), cameraRotation);
    if (lines.empty())
    {
        return;
    }
    valley.gaugeLines += lines.size();
    levain::render::drawDebugLines(context.commandList, valley.gaugePass, context.target,
                                   context.viewProjection, lines,
                                   levain::render::DebugDepth::OnTop);
}

/// Le nom d'un état du sol, pour le journal.
std::string_view groundName(levain::physics::GroundState state)
{
    switch (state)
    {
    case levain::physics::GroundState::OnGround:
        return "au sol";
    case levain::physics::GroundState::OnSteepGround:
        return "sur une pente trop raide";
    case levain::physics::GroundState::NotSupported:
        return "sans appui";
    case levain::physics::GroundState::InAir:
        break;
    }
    return "en l'air";
}

/// Les bilans du jeu, lus par la CI : où sont les pieds du renard, et sur quoi ; ce que le terrain
/// et l'herbe ont dessiné. L'état du personnage n'existe qu'après le premier pas de physique : une
/// boucle trop courte n'en a pas.
bool finishValley(const Valley& valley)
{
    // Lu par la CI : le critère de M6.4.
    levain::core::log("rando", levain::core::LogLevel::Info,
                      "caméra : marge minimale au relief {:.3f} m sur {} poses",
                      valley.minimumMargin, valley.measuredPoses);
    // Lu par la CI : la descente en planeur (ADR-0031 de Levain).
    levain::core::log("rando", levain::core::LogLevel::Info,
                      "planeur : {} images en vol, bras jusqu'à {:.2f} m ; jauge : {} traits "
                      "dessinés ; noyades : {}",
                      valley.glidingFrames, valley.longestArm, valley.gaugeLines,
                      valley.player.get<rando::traversal::Traversal>().drownings);
    const glm::vec3 feet = valley.player.get<levain::scene::Transform>().position;
    const auto* state = valley.player.try_get<levain::physics::CharacterState>();
    levain::core::log("rando", levain::core::LogLevel::Info,
                      "renard : pieds à ({:.2f}, {:.2f}, {:.2f}), {}, {:.2f} m/s", feet.x, feet.y,
                      feet.z, state ? groundName(state->ground.state) : "sans personnage",
                      state ? glm::length(state->velocity) : 0.0f);
    const auto perFrame = [&valley](std::uint64_t count)
    { return static_cast<double>(count) / std::max(valley.app.frameCount, 1); };
    levain::core::log("rando", levain::core::LogLevel::Info,
                      "terrain, par image : {:.1f} parcelles dessinées et {:.0f} triangles ; "
                      "herbe : {:.0f} brins demandés",
                      perFrame(valley.terrainCamera.drawn),
                      perFrame(valley.terrainCamera.triangles), perFrame(valley.grassStats.blades));
    return true;
}

/// La fonction de démarrage du jeu (ADR-0029 de Levain) : la vallée, son lac et son herbe, le
/// renard, et la caméra qui le suit.
levain::core::Result<levain::app::FrameHooks> startRando(levain::app::App& app,
                                                         const RandoOptions& options)
{
    auto controls = controlsOf(app.bindings);
    if (!controls)
    {
        return std::unexpected(controls.error());
    }
    // La vallée d'abord : la physique en a besoin pour le sol.
    const levain::terrain::ValleySettings settings;
    levain::terrain::Heightmap heightmap = levain::terrain::valleyOf(settings);
    flecs::world& world = app.world;
    rando::spawnValley(world, heightmap, settings);

    // Le joueur, une racine sans échelle (ADR-0028 de Levain), et le renard, son enfant, avec son
    // échelle : 0,01, et un demi-tour, son avant étant +z quand celui du personnage est −z.
    const glm::vec3 start = options.start ? rando::feetAt(heightmap, *options.start)
                                          : rando::startOf(heightmap, settings);
    const flecs::entity player = rando::spawnPlayer(world, start, rando::facingPlusX());
    auto fox = levain::app::loadModel(
        app,
        {.path = std::filesystem::path{RANDO_ASSETS_DIR} / "Models/Fox/glTF/Fox.gltf",
         .placement = {.position = {},
                       .rotation = glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                       .scale = glm::vec3{0.01f}},
         .name = "fox",
         .clip = std::nullopt,
         .locomotion = levain::app::LocomotionClips{.names = {"Survey", "Walk", "Run"},
                                                    .walkSpeed = rando::FoxWalker.walkSpeed,
                                                    .runSpeed = rando::FoxWalker.runSpeed}});
    if (!fox)
    {
        return std::unexpected(fox.error());
    }
    // Par flecs::Parent, comme toute la hiérarchie (ADR-0015 de Levain) : le renard suit le joueur.
    fox->root.set(flecs::Parent{player});

    // La caméra à la troisième personne (ADR-0030 de Levain), son plan proche à 0,2 m pour qu'elle
    // puisse approcher la roche. Elle part du bout de son bras, derrière le renard qui regarde vers
    // +x, contre la roche s'il y en a (`rando::heightmapArmCast`) : posée sur ses pieds, la
    // première image interpolerait depuis l'intérieur du sol.
    world.import<rando::camera::ThirdPersonCameraModule>();
    rando::camera::ThirdPersonCamera cameraSettings{.target = player.id(),
                                                    .collides = options.cameraCollides};
    const levain::app::CameraLens lens{
        .verticalFovDegrees = 60.0f, .nearPlane = 0.2f, .farPlane = 1000.0f};
    rando::camera::CameraOrbit orbit{.yawDegrees =
                                         rando::camera::yawDegreesOf(rando::facingPlusX())};
    const levain::scene::Transform cameraStart =
        rando::camera::stepCamera(cameraSettings, orbit, {},
                                  rando::camera::nearPlaneRadius(lens, cameraSettings.aspectRatio) +
                                      cameraSettings.probeMargin,
                                  {.feet = start, .yawDegrees = orbit.yawDegrees, .velocity = {}},
                                  rando::heightmapArmCast(heightmap), 0.0f);
    const flecs::entity camera =
        world.entity("camera").set(cameraSettings).set(orbit).set(lens).set(cameraStart);

    // Le terrain, le lac et l'herbe : des plugins du moteur, inscrits dans le rendu d'`app`. Un
    // envoi qui échoue est soumis quand même (`submitAbandonedUpload`, models.hpp de Levain).
    nvrhi::IDevice& device = *app.gpu.nvrhi;
    const nvrhi::CommandListHandle upload = device.createCommandList();
    upload->open();
    auto terrain = levain::terrain::createTerrainPass(
        device, *upload, heightmap, app.renderer.frame, app.renderer.shadows,
        std::filesystem::path{RANDO_ASSETS_DIR} / "Textures");
    if (!terrain)
    {
        levain::app::submitAbandonedUpload(device, *upload);
        return std::unexpected(terrain.error());
    }
    auto water = levain::water::createWaterPass(device, *upload, rando::lakeOf(settings), *terrain,
                                                heightmap, app.renderer.frame);
    if (!water)
    {
        levain::app::submitAbandonedUpload(device, *upload);
        return std::unexpected(water.error());
    }
    auto grass = levain::grass::createGrassPass(device, *upload, *terrain, heightmap,
                                                rando::LakeLevel, app.renderer.frame);
    if (!grass)
    {
        levain::app::submitAbandonedUpload(device, *upload);
        return std::unexpected(grass.error());
    }
    upload->close();
    device.executeCommandList(upload);
    auto gaugePass =
        levain::render::createDebugLinesPass(device, levain::render::sceneTargetInfo());
    if (!gaugePass)
    {
        return std::unexpected(gaugePass.error());
    }

    const auto valley =
        std::make_shared<Valley>(Valley{.app = app,
                                        .options = options,
                                        .controls = *controls,
                                        .heightmap = std::move(heightmap),
                                        .terrain = std::move(*terrain),
                                        .water = std::move(*water),
                                        .grass = std::move(*grass),
                                        .terrainCamera = {},
                                        .terrainShadows = {},
                                        .grassStats = {},
                                        .gaugePass = std::move(*gaugePass),
                                        .player = player,
                                        .camera = camera,
                                        .mouseCaptured = false,
                                        .minimumMargin = std::numeric_limits<float>::infinity(),
                                        .measuredPoses = 0,
                                        .glidingFrames = 0,
                                        .longestArm = 0.0f,
                                        .gaugeLines = 0});
    // La mesure du critère, après la caméra dans la même phase : déclarée après l'import de son
    // module, elle passe après son système (l'ordre des déclarations, README de scene). Un
    // pointeur nu : les points d'accroche gardent la vallée, et meurent avant le monde.
    world.system("MeasureCameraMargin")
        .kind<levain::scene::PostPhysics>()
        .run([valley = valley.get()](flecs::iter&) { measureMargin(*valley); });
    levain::terrain::addTerrainPasses(app.renderer.stages, valley->terrain, valley->heightmap,
                                      valley->terrainCamera, valley->terrainShadows);
    levain::grass::addGrassPasses(app.renderer.stages, valley->grass, valley->grassStats);
    levain::water::addWaterPasses(app.renderer.stages, valley->water);
    // Après l'herbe et l'eau : la jauge passe par-dessus tout (ADR-0031 de Levain).
    levain::render::addStageFunction(
        app.renderer.stages, levain::render::RenderStage::Transparent, "jauge",
        [valley = valley.get()](const levain::render::StageContext& context)
        { drawGauge(*valley, context); });
    levain::core::log("rando", levain::core::LogLevel::Info, "étapes du rendu : {}",
                      levain::render::describeStages(app.renderer.stages));
    return levain::app::FrameHooks{
        .frame = [valley](levain::app::App&) { steer(*valley); },
        .record = nullptr,
        .finish = [valley](levain::app::App&) { return finishValley(*valley); },
        // Le renard joue le mouvement du joueur : il marche quand le joueur marche. Tout autre
        // modèle animé reste au repos.
        .motionOf =
            [valley, fox = fox->id](const levain::assets::AssetId& model, double)
        {
            return model == fox ? valley->player.get<levain::animation::CharacterMotion>()
                                : levain::animation::CharacterMotion{};
        }};
}

/// Deux nombres finis séparés par une virgule, « 1,0 ». Le piège : `from_chars` lit aussi « nan »
/// et « inf », que la physique ne doit jamais recevoir.
std::optional<glm::vec2> parsePair(std::string_view text)
{
    const std::optional<glm::vec3> triple = levain::app::parseVector(std::string{text} + ",0");
    if (!triple || !std::isfinite(triple->x) || !std::isfinite(triple->y))
    {
        return std::nullopt;
    }
    return glm::vec2{triple->x, triple->y};
}

/// Les options communes et celles du jeu, dans n'importe quel ordre. Vide si elles sont invalides.
std::optional<RandoOptions> parseOptions(std::span<char* const> arguments,
                                         levain::app::AppSettings& settings)
{
    RandoOptions options;
    for (std::size_t i = 1; i < arguments.size(); i += 2)
    {
        if (i + 1 >= arguments.size())
        {
            return std::nullopt;
        }
        const std::string_view name{arguments[i]};
        const std::string_view value{arguments[i + 1]};
        const levain::app::OptionUse use = levain::app::parseCommonOption(settings, name, value);
        if (use == levain::app::OptionUse::Taken)
        {
            continue;
        }
        if (use == levain::app::OptionUse::Invalid)
        {
            return std::nullopt;
        }
        if (name == "--camera-collision" && (value == "on" || value == "off"))
        {
            options.cameraCollides = value == "on";
            continue;
        }
        if (name == "--orbit")
        {
            const std::optional<double> speed = levain::app::parsePositive(value);
            if (!speed)
            {
                return std::nullopt;
            }
            options.orbitDegreesPerSecond = static_cast<float>(*speed);
            continue;
        }
        if (name == "--glide")
        {
            // Un numéro de pas entier : « 60 », pas « 60.5 ».
            const std::optional<double> step = levain::app::parsePositive(value);
            if (!step || *step != std::floor(*step) || *step > 1e6)
            {
                return std::nullopt;
            }
            options.glideAtStep = static_cast<int>(*step);
            continue;
        }
        if (name != "--walk" && name != "--start")
        {
            return std::nullopt;
        }
        const std::optional<glm::vec2> pair = parsePair(value);
        if (!pair)
        {
            return std::nullopt;
        }
        (name == "--walk" ? options.walk : options.start) = *pair;
    }
    // `--glide` compte en pas, un par image : sans `--steps`, une image sans pas effacerait
    // l'appui scripté avant qu'un pas le lise, et le planeur ne s'ouvrirait pas, sans un mot.
    if (options.glideAtStep && !settings.steps)
    {
        return std::nullopt;
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    // std::print et std::format peuvent lever : on rattrape au sommet (ADR-0008 de Levain).
    try
    {
        levain::app::AppSettings settings;
        settings.title = "Rando";
        settings.width = 1280;
        settings.height = 720;
        settings.assetRoots = {RANDO_DATA_DIR, RANDO_ASSETS_DIR};
        settings.bindingsFile = RANDO_DATA_DIR "/input.cfg";
        // Le ciel de Kloofendal, qui éclaire la vallée et lui donne son soleil (M5.4 de Levain) ;
        // sans lui, une ambiance uniforme, et le journal le dit.
        settings.defaultSky = std::filesystem::path{RANDO_SKY};
        settings.shaderBuild = {.cmakeCommand = RANDO_CMAKE_COMMAND,
                                .buildDir = RANDO_BUILD_DIR,
                                .sourceDir = RANDO_SHADER_SOURCE_DIR};
        const std::optional<RandoOptions> options =
            parseOptions(std::span{argv, static_cast<std::size_t>(argc)}, settings);
        if (!options)
        {
            std::println(stderr,
                         "usage : rando {} [--walk x,z] [--start x,z] [--orbit degrés/s] "
                         "[--camera-collision on|off] [--glide pas, avec --steps]",
                         levain::app::CommonOptionsUsage);
            return 2;
        }
        return levain::app::runApp(settings, [options = *options](levain::app::App& app)
                                   { return startRando(app, options); });
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
