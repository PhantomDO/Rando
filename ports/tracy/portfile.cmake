# Port overlay de Tracy 0.14.1 (ADR-0007, amendement du 2026-09-21 ; issue #38).
#
# Pourquoi : le client et le profileur Tracy doivent parler la même version de protocole. La
# baseline vcpkg s'arrête au client 0.13.1, alors que les binaires Linux officiels du profileur
# commencent à 0.14.0 — et vcpkg master n'a toujours rien de plus récent au 2026-09-21.
#
# Client seul : les quatre patches du port officiel ne concernent que les outils (profileur,
# capture, capstone, imgui). Le profileur vient du binaire officiel de la même version.
#
# À supprimer, avec la ligne "overlay-ports" de vcpkg.json, dès qu'une baseline propose
# Tracy 0.14.1 ou plus.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wolfpld/tracy
    REF "v${VERSION}"
    SHA512 53912d7563e595812b37bc55fd40508cfd8e5c42d48d957a73b6b7d18bf1287b3f795c10c9a986bf7b906d5b5bebe13b02216e563e794d0a82b2783e8ce5510b
    HEAD_REF master
)

# TRACY_ENABLE est passé de ON à OFF par défaut en 0.14. Sans lui, les macros de Tracy se
# compilent en rien : le build réussit et le binaire ne profile rien, sans un mot. Le port
# officiel (0.13.1) n'avait pas à le dire ; ici, il le faut. engine/core/CMakeLists.txt vérifie.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DTRACY_ENABLE=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Git=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Tracy CONFIG_PATH "lib/cmake/Tracy")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
