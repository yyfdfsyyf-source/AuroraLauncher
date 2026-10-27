cmake_minimum_required(VERSION 3.28)

project(AuroraLauncher VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

add_library(aurora_miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)
target_include_directories(aurora_miniz PUBLIC third_party/miniz)
if(MSVC)
    target_compile_options(aurora_miniz PRIVATE /W0)
endif()

add_library(aurora_core STATIC
    src/core/config.cpp
    src/core/hash.cpp
    src/core/http.cpp
    src/core/install.cpp
    src/core/java.cpp
    src/core/launch.cpp
    src/core/log.cpp
    src/core/maven.cpp
    src/core/manifest.cpp
    src/core/paths.cpp
    src/core/process.cpp
    src/core/repository.cpp
    src/core/rules.cpp
    src/core/strings.cpp
    src/core/version.cpp
    src/core/zip.cpp
)
target_include_directories(aurora_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_definitions(aurora_core PUBLIC
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)
target_link_libraries(aurora_core PUBLIC aurora_miniz)

if(WIN32)
    target_link_libraries(aurora_core PUBLIC winhttp bcrypt advapi32 shell32)
endif()

if(MSVC)
    target_compile_options(aurora_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(aurora src/main.cpp src/cli.cpp)
target_link_libraries(aurora PRIVATE aurora_core)
if(MSVC)
    target_compile_options(aurora PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(aurora_tests tests/test_main.cpp)
    target_link_libraries(aurora_tests PRIVATE aurora_core)
    target_compile_definitions(aurora_tests PRIVATE
        AURORA_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(aurora_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_tests COMMAND aurora_tests)

    add_executable(aurora_manifest_tests tests/manifest_test.cpp)
    target_link_libraries(aurora_manifest_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_manifest_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_manifest_tests COMMAND aurora_manifest_tests)
endif()
