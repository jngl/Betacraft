betacraft_add_module(render DEPS betacraft::core betacraft::math betacraft::voxel betacraft::gameplay
                     betacraft::crafting betacraft::lighting betacraft::world betacraft::physics
                     betacraft::streaming PkgConfig::SDL2 OpenGL::GL)
# TextureAtlas exports assets/terrain.png with libpng when it is available.
if(PNG_FOUND)
    target_compile_definitions(render PRIVATE BETACRAFT_HAS_LIBPNG=1)
    target_link_libraries(render PRIVATE PNG::PNG)
endif()
