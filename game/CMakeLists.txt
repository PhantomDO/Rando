add_executable(rando src/main.cpp)
target_link_libraries(rando PRIVATE
    levain::app levain::core levain::input levain::physics levain::scene
    plugin::character plugin::grass plugin::terrain plugin::water)

# Les fichiers du jeu, lus dans le dépôt : ses liaisons d'entrée (data/), et les assets de test qu'il
# emprunte au moteur, téléchargés dans assets-cache/ par le script de Levain (README) : le renard, les
# textures du terrain et le ciel. Jamais versionnés (ADR-0018 de Levain).
target_compile_definitions(rando PRIVATE
    RANDO_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    RANDO_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets-cache"
    RANDO_SKY="${PROJECT_SOURCE_DIR}/assets-cache/HDRIs/kloofendal_48d_partly_cloudy_puresky_2k.hdr")

# Le hot-reload des shaders du moteur (ADR-0014 de Levain) : le CMake qui a configuré le jeu, son
# dossier de build, où la cible levain_shaders existe aussi, et les sources du moteur récupéré.
target_compile_definitions(rando PRIVATE
    RANDO_CMAKE_COMMAND="${CMAKE_COMMAND}"
    RANDO_BUILD_DIR="${CMAKE_BINARY_DIR}"
    RANDO_SHADER_SOURCE_DIR="${levain_SOURCE_DIR}/shaders")
