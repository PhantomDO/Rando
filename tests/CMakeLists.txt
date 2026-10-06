# Les tests du jeu : la logique de ses plugins, sans fenêtre ni GPU (forme de l'ADR-0011 de Levain).
find_package(doctest CONFIG REQUIRED)
add_executable(rando_tests doctest_main.cpp camera_test.cpp gauge_test.cpp traversal_test.cpp
    valley_test.cpp)
target_link_libraries(rando_tests PRIVATE
    plugin::camera plugin::traversal rando_gauge rando_valley doctest::doctest)
add_test(NAME rando_tests COMMAND rando_tests)
# Le main de doctest, dans sa seule unité de compilation (comme les tests du moteur).
set_source_files_properties(doctest_main.cpp
    PROPERTIES COMPILE_DEFINITIONS DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
