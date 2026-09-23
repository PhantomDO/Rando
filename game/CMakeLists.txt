add_executable(rando src/main.cpp)
target_link_libraries(rando PRIVATE levain::core levain::platform levain::gpu levain::scene)
