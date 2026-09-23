#include <charconv>
#include <chrono>
#include <cstdio>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include <flecs.h>
#include <nvrhi/nvrhi.h>

#include "levain/core/log.hpp"
#include "levain/gpu/device.hpp"
#include "levain/platform/window.hpp"
#include "levain/scene/fixed_step.hpp"
#include "levain/scene/scene.hpp"

namespace
{

using Clock = std::chrono::steady_clock;

#ifdef NDEBUG
constexpr bool EnableValidation = false;
#else
constexpr bool EnableValidation = true; // exigée en Debug (règle n°4 de Levain)
#endif

double secondsBetween(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double>(end - start).count();
}

/// `--seconds N` arrête la boucle après N secondes : c'est ce que lance la CI. Sans option, la
/// boucle tourne jusqu'à la fermeture de la fenêtre.
std::optional<double> loopSecondsOf(std::span<char* const> arguments)
{
    if (arguments.size() == 1)
    {
        return std::numeric_limits<double>::infinity();
    }
    if (arguments.size() == 3 && std::string_view{arguments[1]} == "--seconds")
    {
        // from_chars plutôt que sscanf : il dit si le texte entier était un nombre.
        const std::string_view text{arguments[2]};
        double seconds = 0.0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
        if (error == std::errc{} && end == text.data() + text.size() && seconds > 0.0)
        {
            return seconds;
        }
    }
    return std::nullopt;
}

/// Efface l'image au bleu du ciel, et la présente. Le jeu ne dessine encore rien d'autre : la
/// vallée viendra avec le terrain (M5.6).
void renderSky(levain::gpu::GpuDevice& gpu, const levain::platform::Window& window,
               nvrhi::ICommandList& commandList)
{
    nvrhi::ITexture* backBuffer = levain::gpu::beginFrame(gpu, window);
    if (backBuffer == nullptr)
    {
        return;
    }
    const nvrhi::Color sky{0.45f, 0.68f, 0.92f, 1.0f};
    commandList.open();
    commandList.clearTextureFloat(backBuffer, nvrhi::AllSubresources, sky);
    commandList.close();
    gpu.nvrhi->executeCommandList(&commandList);
    levain::gpu::presentFrame(gpu);
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::optional<double> loopSeconds =
            loopSecondsOf(std::span{argv, static_cast<std::size_t>(argc)});
        if (!loopSeconds)
        {
            std::fputs("usage : rando [--seconds N]\n", stderr);
            return 2;
        }

        auto window = levain::platform::createWindow("Rando", 1280, 720);
        if (!window)
        {
            levain::core::log("rando", levain::core::LogLevel::Critical, "{}",
                              window.error().message);
            return 1;
        }
        // Déclaré après la fenêtre, détruit avant elle : la surface Vulkan en dépend.
        auto gpu = levain::gpu::createGpuDevice(*window, {.enableValidation = EnableValidation});
        if (!gpu)
        {
            levain::core::log("rando", levain::core::LogLevel::Critical, "{}", gpu.error().message);
            return 1;
        }

        flecs::world world;
        world.import<levain::scene::SceneModule>();
        levain::scene::FixedStep fixedStep;

        const nvrhi::CommandListHandle commandList = gpu->nvrhi->createCommandList();
        const Clock::time_point loopStart = Clock::now();
        Clock::time_point previousFrameEnd = loopStart;
        double lastFrameSeconds = fixedStep.stepSeconds;
        int frameCount = 0;
        bool running = true;

        while (running && secondsBetween(loopStart, Clock::now()) < *loopSeconds)
        {
            for (const auto& event : levain::platform::pollEvents(*window).window)
            {
                running =
                    running && event.type != levain::platform::WindowEventType::CloseRequested;
            }
            levain::scene::advanceWorld(world, fixedStep, static_cast<float>(lastFrameSeconds));
            renderSky(*gpu, *window, *commandList);

            const Clock::time_point frameEnd = Clock::now();
            lastFrameSeconds = secondsBetween(previousFrameEnd, frameEnd);
            previousFrameEnd = frameEnd;
            ++frameCount;
        }

        // Lu par la CI, qui échoue si la boucle a tourné moins d'une seconde.
        levain::core::log("rando", levain::core::LogLevel::Info,
                          "boucle arrêtée après {:.1f} s et {} frames",
                          secondsBetween(loopStart, Clock::now()), frameCount);
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    return 0;
}
