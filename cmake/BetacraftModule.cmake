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

# Header-only helpers shared by every test executable (test_support/TempDir.hpp).
add_library(betacraft_test_support INTERFACE)
target_include_directories(betacraft_test_support INTERFACE ${PROJECT_SOURCE_DIR}/tests/support)

# betacraft_add_module(<name> [DEPS <target>...] [TEST_NAME <ctest name>] [STANDALONE_TESTS <file>...])
# Builds the STATIC library <name> (alias betacraft::<name>) from src/*.cpp, and, when
# test/*.cpp exist, the GoogleTest executable betacraft_<name>_tests run as ctest Module_<name>
# (or TEST_NAME, for a module whose suite CI already runs under another name).
# The test sources are also collected for betacraft_all_tests (BETACRAFT_MODULE_TEST_SOURCES),
# except STANDALONE_TESTS (paths under test/): those replace a global such as operator new and
# only hold in a binary of their own.
function(betacraft_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "TEST_NAME" "DEPS;STANDALONE_TESTS")
    if(NOT arg_TEST_NAME)
        set(arg_TEST_NAME Module_${name})
    endif()
    set(dir ${CMAKE_CURRENT_SOURCE_DIR})

    file(GLOB sources CONFIGURE_DEPENDS ${dir}/src/*.cpp)
    add_library(${name} STATIC ${sources})
    add_library(betacraft::${name} ALIAS ${name})
    target_include_directories(${name} PUBLIC ${dir}/include PRIVATE ${dir}/src)
    target_link_libraries(${name} PUBLIC ${arg_DEPS} PRIVATE betacraft_warnings)

    file(GLOB tests CONFIGURE_DEPENDS ${dir}/test/*.cpp)
    if(tests)
        add_executable(betacraft_${name}_tests ${tests})
        target_link_libraries(betacraft_${name}_tests PRIVATE ${name} betacraft_warnings betacraft_test_support
                              GTest::gtest GTest::gtest_main GTest::gmock)
        # Next to the other test executables: ./build/betacraft_<name>_tests.
        set_target_properties(betacraft_${name}_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
        add_test(NAME ${arg_TEST_NAME} COMMAND betacraft_${name}_tests)
        set(shared_tests ${tests})
        foreach(standalone IN LISTS arg_STANDALONE_TESTS)
            if(NOT EXISTS ${dir}/test/${standalone})
                message(FATAL_ERROR "${name}: STANDALONE_TESTS names a missing file test/${standalone}")
            endif()
            list(REMOVE_ITEM shared_tests ${dir}/test/${standalone})
        endforeach()
        set_property(GLOBAL APPEND PROPERTY BETACRAFT_MODULE_TEST_SOURCES ${shared_tests})
    endif()
endfunction()
