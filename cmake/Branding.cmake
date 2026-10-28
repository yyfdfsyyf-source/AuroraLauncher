# Applies AuroraLauncher's own Windows product metadata and application manifest.
# Include from CMakeLists.txt after the executable target exists.
function(aurora_apply_branding target)
    if(NOT WIN32)
        return()
    endif()

    # Optional second argument overrides the resource script, so each binary
    # reports its own OriginalFilename in the version block.
    set(_resource ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/aurora.rc)
    if(ARGC GREATER 1)
        set(_resource ${ARGV1})
    endif()

    target_sources(${target} PRIVATE ${_resource})

    set(_manifest ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/aurora.manifest)
    target_link_options(${target} PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${_manifest}"
    )
endfunction()
