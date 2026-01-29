cmake_minimum_required(VERSION 3.20)
project(pil-tools VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Helper function for common target configuration
function(configure_pil_tool target_name source_file)
    add_executable(${target_name} ${source_file})

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /WX-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -pedantic)
    endif()

    if(MINGW)
        target_link_options(${target_name} PRIVATE
            -static-libgcc
            -static-libstdc++
        )
        target_compile_definitions(${target_name} PRIVATE __USE_MINGW_ANSI_STDIO=1)
        target_link_options(${target_name} PRIVATE -mconsole)
    endif()

    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Release>:
                -Os
                -ffunction-sections
                -fdata-sections
                -fno-rtti
                -flto
            >
        )

        if(APPLE)
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,-dead_strip
                >
            )
        else()
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:
                    -Wl,--gc-sections
                    -flto
                >
            )
        endif()
    endif()
endfunction()

# Build tools
configure_pil_tool(pil-squasher src/pil-squasher.cpp)
configure_pil_tool(pil-splitter src/pil-splitter.cpp)

# Optional: print build info
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
