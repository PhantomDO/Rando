cmake_minimum_required(VERSION 3.28)
project(Rando VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Le moteur, à un commit figé (ADR-0018 de Levain). Pour travailler sur les deux dépôts à la fois,
# pointer vers un clone local, sans rien publier :
#   cmake --preset linux-debug -DFETCHCONTENT_SOURCE_DIR_LEVAIN=../Levain
include(FetchContent)
FetchContent_Declare(levain
    GIT_REPOSITORY https://github.com/PhantomDO/Levain.git
    GIT_TAG 873a6e30b3d41105751a2fbcd47c6f7d5890ea0d) # main après Levain#304 (le module app, ADR-0029)
FetchContent_MakeAvailable(levain)

add_compile_options(-pedantic-errors -Wall -Wextra -Werror)

add_subdirectory(game)
