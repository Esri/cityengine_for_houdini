cmake_minimum_required(VERSION 3.13)


### generator detection

if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set(PLD_GEN_VISUAL_STUDIO 1)
endif()


### platform configuration

if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(PLD_WINDOWS 1)
elseif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(PLD_LINUX 1)
endif ()


### toolchain configuration

set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS_MINSIZEREL "")
set(CMAKE_CXX_FLAGS_DEBUG "")

function(pld_set_common_compiler_flags TGT)
    set_target_properties(${TGT} PROPERTIES
            CXX_STANDARD 17
            CXX_EXTENSIONS OFF
            CXX_STANDARD_REQUIRED TRUE)

    if (PLD_WINDOWS)
        target_compile_definitions(${TGT} PRIVATE -DPLD_WINDOWS=1 -DPLD_TC_VC=1)

        if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
            target_compile_options(${TGT} PRIVATE -O2)
            target_compile_definitions(${TGT} PRIVATE -DNDEBUG)
        elseif (${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
            target_compile_options(${TGT} PRIVATE -Od -Zi)
        else()
            message(FATAL_ERROR "Unsupported cmake build type: ${CMAKE_BUILD_TYPE}")
        endif()

        if(PLD_GEN_VISUAL_STUDIO)
            target_compile_options(${TGT} PRIVATE -MP) # enable parallel building in Visual Studio generators
        endif()

    elseif (PLD_LINUX)
        target_compile_definitions(${TGT} PRIVATE -DPLD_LINUX=1 -DPLD_TC_GCC=1)

        if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
            target_compile_options(${TGT} PRIVATE -O3 -flto)
            target_compile_definitions(${TGT} PRIVATE -DNDEBUG)
        elseif (${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
            target_compile_options(${TGT} PRIVATE -O0 -ggdb -pg)
            target_compile_definitions(${TGT} PRIVATE -DNDEBUG)
        elseif (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
            target_compile_options(${TGT} PRIVATE -O0 -ggdb -pg)
            target_compile_definitions(${TGT} PRIVATE -DDEBUG)
        endif ()

        target_compile_options(${TGT} PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)

    endif ()
endfunction()

# flags required to be ABI compatible with PRTX (the codecs code),
# see https://github.com/esri/cityengine-sdk#general-software-requirements
function(pld_set_prtx_compiler_flags TGT)
    if (PLD_WINDOWS)
        target_compile_options(${TGT} PRIVATE -bigobj -GR -EHsc $<IF:$<CONFIG:Debug>,-MDd,-MD>)
    elseif (PLD_LINUX)
        target_link_options(${TGT} PRIVATE "LINKER:SHELL:--exclude-libs ALL")
    endif ()
endfunction()