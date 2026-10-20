betacraft_add_module(storage DEPS betacraft::core betacraft::voxel betacraft::worldgen TEST_NAME Phase2_storage)

# S7.1: zstd-compressed region chunks when libzstd is found (RegionFile::kZstdAvailable).
if(BETACRAFT_WITH_ZSTD AND ZSTD_FOUND)
    target_compile_definitions(storage PUBLIC BETACRAFT_HAS_ZSTD=1)
    target_link_libraries(storage PRIVATE PkgConfig::ZSTD)
endif()

# Frozen reference saves (tests/data/saves/vN) for the migration tests.
target_compile_definitions(betacraft_storage_tests PRIVATE BETACRAFT_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
