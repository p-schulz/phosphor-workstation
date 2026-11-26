# Warning flags for own targets only. Third-party targets keep their defaults.
function(phos_set_warnings target)
    if(MSVC)
        # C4324: "structure was padded due to alignment specifier" is intended (cache-line aligned
        # SPSC indices in src/core/spsc_ring.h).
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /wd4324)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
        if(PHOS_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
        if(PHOS_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
