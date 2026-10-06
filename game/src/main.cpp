#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
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
#include "levain/render/stages.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/scene.hpp"
#include "levain/terrain/collision.hpp"
#include "levain/terrain/heightmap.hpp"
#include "levain/terrain/terrain_pass.hpp"
#include "levain/water/water_pass.hpp"

namespace
{

/// La hauteur de l'eau du lac, sous le fond plat de la vallée : elle ne remplit que son creux.
constexpr float LakeLevel = -1.5f;

/// La marche du renard : les réglages par défaut du plugin `character`. Le joueur les porte, et
/// l'animation y règle ses vitesses de marche et de course.
constexpr levain::character::Walker FoxWalker{};

/// La capsule du renard : 0,8 m de haut, pour un renard de 0,79 m à l'échelle 0,01 (Fox mesure
/// 155 × 79 unités). Elle ne couvre ni son museau ni sa queue : le compromis habituel d'un
/// quadrupède sur une capsule debout.
constexpr levain::physics::CharacterController FoxController{
    .shape = {.halfHeight = 0.1f, .radius = 0.3f}};

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
};

/// Les actions et les axes que le jeu lit, résolus une fois par leurs noms. Un nom absent de
/// `input.cfg` est une erreur au démarrage : sans ça, la commande ne répondrait jamais, sans que
/// rien ne le dise (règle n°7 de Levain).
struct Controls
{
    int moveRight = 0;
    int moveForward = 0;
    int sprint = 0;
    int jump = 0;
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
    for (const auto* found : {&moveRight, &moveForward, &sprint, &jump})
    {
        if (!*found)
        {
            return std::unexpected(found->error());
        }
    }
    return Controls{
        .moveRight = *moveRight, .moveForward = *moveForward, .sprint = *sprint, .jump = *jump};
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
    flecs::entity player;
};

/// Où le renard commence : sur le fond plat de la vallée, 1,4 rayon à l'ouest du lac, face à lui.
glm::vec3 startOf(const levain::terrain::Heightmap& heightmap,
                  const levain::terrain::ValleySettings& valley)
{
    const glm::vec2 spot = valley.lakeCenter - glm::vec2{valley.lakeRadius * 1.4f, 0.0f};
    return {spot.x, levain::terrain::heightAt(heightmap, spot), spot.y};
}

/// L'orientation qui tourne l'avant d'un personnage (−z) vers +x : un quart de tour autour de Y,
/// dans le sens horaire vu d'en haut, d'où le signe moins.
glm::quat facingPlusX()
{
    return glm::angleAxis(-glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
}

/// La caméra qui suit le joueur à distance fixe, sans collision : celle de M6.3, en attendant la
/// caméra à la troisième personne (#2). 3,5 m en arrière (−x), 1,6 m au-dessus, le regard sur son
/// dos.
levain::scene::Transform followCamera(const glm::vec3& player)
{
    const glm::vec3 eye = player + glm::vec3{-3.5f, 1.6f, 0.0f};
    const glm::vec3 target = player + glm::vec3{0.0f, 0.5f, 0.0f};
    return {.position = eye,
            .rotation = glm::quatLookAt(glm::normalize(target - eye), glm::vec3{0.0f, 1.0f, 0.0f})};
}

/// Ce que le joueur demande au renard, à chaque image : la direction des axes, dans le monde (la
/// caméra regarde vers +x : « avant » est +x, « droite » +z), la course, et le saut. Le saut se lit
/// dans les appuis qu'aucun pas n'a encore vus (`pressedSinceLastStep`) : la marche le consomme au
/// pas suivant.
void steerFox(Valley& valley)
{
    const auto& input = valley.app.world.get<levain::app::PlayerInput>();
    const Controls& controls = valley.controls;
    auto& walk = valley.player.get_mut<levain::character::WalkInput>();
    walk.direction = valley.options.walk.value_or(
        glm::vec2{levain::input::axisValue(input.state, controls.moveForward),
                  levain::input::axisValue(input.state, controls.moveRight)});
    walk.run = levain::input::actionHeld(input.state, controls.sprint);
    walk.jump = levain::app::pressedSinceLastStep(input, controls.jump);
}

/// Les bilans du jeu, lus par la CI : où sont les pieds du renard, et sur quoi.
bool finishValley(const Valley& valley)
{
    const glm::vec3 feet = valley.player.get<levain::scene::Transform>().position;
    const auto& state = valley.player.get<levain::physics::CharacterState>();
    levain::core::log(
        "rando", levain::core::LogLevel::Info,
        "renard : pieds à ({:.2f}, {:.2f}, {:.2f}), {}, {:.2f} m/s", feet.x, feet.y, feet.z,
        state.ground.state == levain::physics::GroundState::OnGround ? "au sol" : "pas au sol",
        glm::length(state.velocity));
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
    world.import<levain::physics::PhysicsModule>();
    world.import<levain::character::WalkModule>();
    world.entity("terrain")
        .set(levain::scene::Transform{})
        .set(levain::terrain::colliderOf(heightmap));

    // Le joueur, une racine sans échelle (ADR-0028 de Levain), et le renard, son enfant, avec son
    // échelle : 0,01, et un demi-tour, son avant étant +z quand celui du personnage est −z.
    const glm::vec3 start =
        options.start
            ? glm::vec3{options.start->x, levain::terrain::heightAt(heightmap, *options.start),
                        options.start->y}
            : startOf(heightmap, settings);
    const flecs::entity player =
        world.entity("player")
            .set(levain::scene::Transform{.position = start, .rotation = facingPlusX()})
            .set(FoxController)
            .set(FoxWalker)
            .set(levain::character::WalkInput{})
            .set(levain::animation::CharacterMotion{});
    auto fox = levain::app::loadModel(
        app,
        {.path = std::filesystem::path{RANDO_ASSETS_DIR} / "Models/Fox/glTF/Fox.gltf",
         .placement = {.position = {},
                       .rotation = glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}),
                       .scale = glm::vec3{0.01f}},
         .name = "fox",
         .clip = std::nullopt,
         .locomotion = levain::app::LocomotionClips{.names = {"Survey", "Walk", "Run"},
                                                    .walkSpeed = FoxWalker.walkSpeed,
                                                    .runSpeed = FoxWalker.runSpeed}});
    if (!fox)
    {
        return std::unexpected(fox.error());
    }
    // Par flecs::Parent, comme toute la hiérarchie (ADR-0015 de Levain) : le renard suit le joueur.
    fox->root.set(flecs::Parent{player});

    // La caméra : une entité qui porte un objectif, que le rendu interpole entre deux pas.
    const flecs::entity camera =
        world.entity("camera")
            .set(followCamera(start))
            .set(levain::app::CameraLens{
                .verticalFovDegrees = 60.0f, .nearPlane = 0.5f, .farPlane = 1000.0f})
            .add<levain::scene::PreviousTransform>();
    world.system("FollowPlayer")
        .kind<levain::scene::PostPhysics>()
        .run(
            [camera, player](flecs::iter&)
            {
                // Par référence : un `set` remettrait l'état précédent à jour, et le rendu ne
                // l'interpolerait plus (ADR-0016 de Levain).
                camera.get_mut<levain::scene::Transform>() =
                    followCamera(player.get<levain::scene::Transform>().position);
            });

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
    auto water = levain::water::createWaterPass(
        device, *upload,
        {.center = settings.lakeCenter, .radius = settings.lakeRadius, .level = LakeLevel},
        *terrain, heightmap, app.renderer.frame);
    if (!water)
    {
        levain::app::submitAbandonedUpload(device, *upload);
        return std::unexpected(water.error());
    }
    auto grass = levain::grass::createGrassPass(device, *upload, *terrain, heightmap, LakeLevel,
                                                app.renderer.frame);
    if (!grass)
    {
        levain::app::submitAbandonedUpload(device, *upload);
        return std::unexpected(grass.error());
    }
    upload->close();
    device.executeCommandList(upload);

    const auto valley = std::make_shared<Valley>(Valley{.app = app,
                                                        .options = options,
                                                        .controls = *controls,
                                                        .heightmap = std::move(heightmap),
                                                        .terrain = std::move(*terrain),
                                                        .water = std::move(*water),
                                                        .grass = std::move(*grass),
                                                        .terrainCamera = {},
                                                        .terrainShadows = {},
                                                        .grassStats = {},
                                                        .player = player});
    levain::terrain::addTerrainPasses(app.renderer.stages, valley->terrain, valley->heightmap,
                                      valley->terrainCamera, valley->terrainShadows);
    levain::grass::addGrassPasses(app.renderer.stages, valley->grass, valley->grassStats);
    levain::water::addWaterPasses(app.renderer.stages, valley->water);
    levain::core::log("rando", levain::core::LogLevel::Info, "étapes du rendu : {}",
                      levain::render::describeStages(app.renderer.stages));
    return levain::app::FrameHooks{
        .frame = [valley](levain::app::App&) { steerFox(*valley); },
        .record = nullptr,
        .finish = [valley](levain::app::App&) { return finishValley(*valley); },
        // Le renard joue le mouvement du joueur : il marche quand le joueur marche.
        .motionOf = [valley](const levain::assets::AssetId&, double)
        { return valley->player.get<levain::animation::CharacterMotion>(); }};
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
        if (use == levain::app::OptionUse::Invalid || (name != "--walk" && name != "--start"))
        {
            return std::nullopt;
        }
        const std::optional<glm::vec3> pair = levain::app::parseVector(std::string{value} + ",0");
        if (!pair)
        {
            return std::nullopt;
        }
        (name == "--walk" ? options.walk : options.start) = glm::vec2{pair->x, pair->y};
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
            std::println(stderr, "usage : rando {} [--walk x,z] [--start x,z]",
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
