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
    src/core/activity.cpp
    src/core/auth.cpp
    src/core/config.cpp
    src/core/curseforge.cpp
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
    src/core/mirrors.cpp
    src/core/modpack.cpp
    src/core/modrinth.cpp
    src/core/mods.cpp
    src/core/paths.cpp
    src/core/pcl.cpp
    src/core/process.cpp
    src/core/proxy.cpp
    src/core/repository.cpp
    src/core/resourcepacks.cpp
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

    add_executable(aurora_modrinth_tests tests/modrinth_test.cpp)
    target_link_libraries(aurora_modrinth_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_modrinth_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_modrinth_tests COMMAND aurora_modrinth_tests)

    add_executable(aurora_mods_tests tests/mods_test.cpp)
    target_link_libraries(aurora_mods_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_mods_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_mods_tests COMMAND aurora_mods_tests)

    add_executable(aurora_curseforge_tests tests/curseforge_test.cpp)
    target_link_libraries(aurora_curseforge_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_curseforge_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_curseforge_tests COMMAND aurora_curseforge_tests)

    add_executable(aurora_pcl_tests tests/pcl_test.cpp)
    target_link_libraries(aurora_pcl_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_pcl_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_pcl_tests COMMAND aurora_pcl_tests)

    add_executable(aurora_resourcepacks_tests tests/resourcepacks_test.cpp)
    target_link_libraries(aurora_resourcepacks_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_resourcepacks_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_resourcepacks_tests COMMAND aurora_resourcepacks_tests)

    add_executable(aurora_activity_tests tests/activity_test.cpp)
    target_link_libraries(aurora_activity_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_activity_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_activity_tests COMMAND aurora_activity_tests)

    add_executable(aurora_modpack_tests tests/modpack_test.cpp)
    target_link_libraries(aurora_modpack_tests PRIVATE aurora_core)
    if(MSVC)
        target_compile_options(aurora_modpack_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_test(NAME aurora_modpack_tests COMMAND aurora_modpack_tests)
endif()

option(AURORA_BUILD_GUI "Build the WebView2 desktop UI (aurora_gui)" OFF)
if(AURORA_BUILD_GUI AND WIN32)
    set(AURORA_WEBVIEW2_ROOT "D:/tools/webview2" CACHE PATH
        "Directory that contains build/native/include and build/native/x64 from the Microsoft.Web.WebView2 package")
    set(webview2_include "${AURORA_WEBVIEW2_ROOT}/build/native/include")
    set(webview2_loader "${AURORA_WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
    if(NOT EXISTS "${webview2_include}/WebView2.h")
        message(FATAL_ERROR
            "WebView2.h not found under ${webview2_include}. Install the SDK with: "
            "python -m aqt ... or extract Microsoft.Web.WebView2 and set AURORA_WEBVIEW2_ROOT.")
    endif()
    if(NOT EXISTS "${webview2_loader}")
        message(FATAL_ERROR "WebView2LoaderStatic.lib not found at ${webview2_loader}")
    endif()

    add_executable(aurora_gui WIN32 src/gui/main.cpp)
    target_include_directories(aurora_gui PRIVATE "${webview2_include}")
    target_link_libraries(aurora_gui PRIVATE aurora_core "${webview2_loader}")
    target_compile_definitions(aurora_gui PRIVATE
        AURORA_GUI_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui"
        AURORA_GUI_VERSION="${PROJECT_VERSION}"
        WINVER=0x0A00
        _WIN32_WINNT=0x0A00
    )
    if(MSVC)
        target_compile_options(aurora_gui PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    aurora_apply_branding(aurora_gui ${CMAKE_CURRENT_SOURCE_DIR}/resources/aurora_gui.rc)
    # Keep a copy next to the binary so a packaged build runs without the source tree.
    add_custom_command(TARGET aurora_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui"
            "$<TARGET_FILE_DIR:aurora_gui>/ui")
endif()
