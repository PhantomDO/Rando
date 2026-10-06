# Marcher, planer, nager, et l'endurance : un plugin gameplay du jeu (ADR-0018 et ADR-0031 de Levain).
levain_add_plugin(traversal
    SOURCES src/traversal.cpp
    DEPENDS levain::animation levain::physics levain::scene plugin::character plugin::water)
