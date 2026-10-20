# Engine modules (docs/adr/0001-modules-as-static-libraries.md): modules/<m>/{include/<m>,src,test}.
# A module only sees its own headers and the include/ of the modules it links, so an
# undeclared dependency fails to compile.

add_library(betacraft_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(betacraft_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-missing-field-initializers
    )
endif()

# betacraft_add_module(<name> [DEPS <target>...])
# Builds the STATIC library <name> (alias betacraft::<name>) from src/*.cpp, and, when
# test/*.cpp exist, the GoogleTest executable betacraft_<name>_tests run as ctest Module_<name>.
# The test sources are also collected for betacraft_all_tests (BETACRAFT_MODULE_TEST_SOURCES).
function(betacraft_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "DEPS")
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})

    file(GLOB sources CONFIGURE_DEPENDS ${dir}/src/*.cpp)
    add_library(${name} STATIC ${sources})
    add_library(betacraft::${name} ALIAS ${name})
    target_include_directories(${name} PUBLIC ${dir}/include PRIVATE ${dir}/src)
    target_link_libraries(${name} PUBLIC ${arg_DEPS} PRIVATE betacraft_warnings)

    file(GLOB tests CONFIGURE_DEPENDS ${dir}/test/*.cpp)
    if(tests)
        add_executable(betacraft_${name}_tests ${tests})
        target_link_libraries(betacraft_${name}_tests PRIVATE ${name} betacraft_warnings
                              GTest::gtest GTest::gtest_main GTest::gmock)
        # Next to the other test executables: ./build/betacraft_<name>_tests.
        set_target_properties(betacraft_${name}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
        add_test(NAME Module_${name} COMMAND betacraft_${name}_tests)
        set_property(GLOBAL APPEND PROPERTY BETACRAFT_MODULE_TEST_SOURCES ${tests})
    endif()
endfunction()
