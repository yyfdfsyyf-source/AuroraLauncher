cmake_minimum_required(VERSION 3.28)

project(AuroraLauncher VERSION 0.3.0 LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
endif()

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
    src/core/auth.cpp
    src/core/config.cpp
    src/core/forge.cpp
    src/core/hash.cpp
    src/core/http.cpp
    src/core/http_post.cpp
    src/core/install.cpp
    src/core/java.cpp
    src/core/java_install.cpp
    src/core/launch.cpp
    src/core/loader.cpp
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
    target_link_libraries(aurora_core PUBLIC winhttp bcrypt advapi32 crypt32 shell32)
endif()

if(MSVC)
    target_compile_options(aurora_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

add_executable(aurora src/main.cpp src/cli.cpp)
target_link_libraries(aurora PRIVATE aurora_core)
if(MSVC)
    target_compile_options(aurora PRIVATE /W4 /permissive- /utf-8 /EHsc)
endif()

include(cmake/Branding.cmake)
aurora_apply_branding(aurora)

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

    add_executable(aurora_http_tests tests/http_test.cpp)
    target_link_libraries(aurora_http_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_http_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_http_tests COMMAND aurora_http_tests)

    add_executable(aurora_loader_tests tests/loader_test.cpp)
    target_link_libraries(aurora_loader_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_loader_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_loader_tests COMMAND aurora_loader_tests)

    add_executable(aurora_manifest_tests tests/manifest_test.cpp)
    target_link_libraries(aurora_manifest_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_manifest_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_manifest_tests COMMAND aurora_manifest_tests)

    add_executable(aurora_auth_tests tests/auth_test.cpp)
    target_link_libraries(aurora_auth_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_auth_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_auth_tests COMMAND aurora_auth_tests)

    add_executable(aurora_java_install_tests tests/java_install_test.cpp)
    target_link_libraries(aurora_java_install_tests PRIVATE aurora_core)
    target_compile_definitions(aurora_java_install_tests PRIVATE
        AURORA_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(MSVC)
        target_compile_options(aurora_java_install_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_java_install_tests COMMAND aurora_java_install_tests)

    add_executable(aurora_forge_tests tests/forge_test.cpp)
    target_link_libraries(aurora_forge_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_forge_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_forge_tests COMMAND aurora_forge_tests)
endif()
