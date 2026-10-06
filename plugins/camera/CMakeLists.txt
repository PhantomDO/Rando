# La caméra à la troisième personne : un plugin gameplay du jeu (ADR-0018 et ADR-0030 de Levain).
levain_add_plugin(camera
    SOURCES src/third_person.cpp
    DEPENDS levain::app levain::physics levain::scene)
