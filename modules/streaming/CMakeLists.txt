betacraft_add_module(streaming DEPS betacraft::core betacraft::voxel betacraft::world)

# Snapshot capture ceiling: a wall-clock benchmark (label and switch explained in the root CMakeLists.txt).
add_test(NAME Module_streaming_perf COMMAND betacraft_streaming_tests --gtest_filter=SnapshotCaptureFastPath.CaptureCostIsMeasured)
set_tests_properties(Module_streaming_perf PROPERTIES LABELS benchmark ENVIRONMENT BETACRAFT_PERF_TESTS=1)
