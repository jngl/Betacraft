# Third-party miniaudio in its own translation unit, so this module keeps strict warnings. It sits
# outside src/, which betacraft_add_module globs into the warning-checked audio library.
add_library(miniaudio_lib STATIC miniaudio_impl.cpp)
target_include_directories(miniaudio_lib SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/miniaudio)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(miniaudio_lib PRIVATE -w -O2)
endif()
target_link_libraries(miniaudio_lib PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)

betacraft_add_module(audio DEPS betacraft::core betacraft::voxel betacraft::acoustics
                     STANDALONE_TESTS test_realtime_no_alloc.cpp)
# AudioEngine.hpp hides the device behind a pimpl: no other module sees miniaudio.h.
target_link_libraries(audio PRIVATE miniaudio_lib)
