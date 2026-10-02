# Les cibles d'ozz-animation, que son install ne déclare pas (port overlay de Levain, ADR-0022) :
#   find_package(ozz-animation CONFIG REQUIRED)
#   target_link_libraries(<cible> PRIVATE ozz::animation_offline)   # ou ozz::animation, ozz::base
get_filename_component(_ozz_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Chacune dépend de la précédente, comme dans le CMake d'ozz.
set(_ozz_previous "")
foreach(_ozz_lib IN ITEMS base animation animation_offline)
    if(NOT TARGET ozz::${_ozz_lib})
        set(_ozz_file "${CMAKE_STATIC_LIBRARY_PREFIX}ozz_${_ozz_lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        add_library(ozz::${_ozz_lib} STATIC IMPORTED)
        set_target_properties(ozz::${_ozz_lib} PROPERTIES
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            IMPORTED_LOCATION_RELEASE "${_ozz_root}/lib/${_ozz_file}"
            IMPORTED_LOCATION_DEBUG "${_ozz_root}/debug/lib/${_ozz_file}"
            # Les autres configurations (RelWithDebInfo…) prennent la version Release.
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
            INTERFACE_INCLUDE_DIRECTORIES "${_ozz_root}/include"
            INTERFACE_LINK_LIBRARIES "${_ozz_previous}")
        if(NOT EXISTS "${_ozz_root}/lib/${_ozz_file}")
            message(FATAL_ERROR "ozz-animation : ${_ozz_root}/lib/${_ozz_file} introuvable")
        endif()
    endif()
    set(_ozz_previous "ozz::${_ozz_lib}")
endforeach()
unset(_ozz_previous)
unset(_ozz_file)
unset(_ozz_root)
set(ozz-animation_FOUND TRUE)
