betacraft_add_module(core DEPS Threads::Threads)

target_compile_definitions(core PUBLIC BETACRAFT_VERSION="${PROJECT_VERSION}")
