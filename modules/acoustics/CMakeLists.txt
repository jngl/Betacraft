betacraft_add_module(acoustics DEPS betacraft::core betacraft::voxel betacraft::world TEST_NAME Phase2_acoustics)

# P4.2 DDA ray budget: a wall-clock benchmark (label and switch explained in the root CMakeLists.txt).
add_test(NAME Phase2_acoustics_perf COMMAND betacraft_acoustics_tests --gtest_filter=AcousticPipeline.*Budget*)
set_tests_properties(Phase2_acoustics_perf PROPERTIES LABELS benchmark ENVIRONMENT BETACRAFT_PERF_TESTS=1)
