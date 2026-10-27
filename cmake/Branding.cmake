# Applies AuroraLauncher's own Windows product metadata and application manifest.
# Include from CMakeLists.txt after the executable target exists.
function(aurora_apply_branding target)
    if(NOT WIN32)
        return()
    endif()

    target_sources(${target} PRIVATE
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/aurora.rc
    )

    set(_manifest ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/aurora.manifest)
    target_link_options(${target} PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${_manifest}"
    )
endfunction()
