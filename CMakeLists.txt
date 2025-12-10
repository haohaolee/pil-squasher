cmake_minimum_required(VERSION 3.20)
project(pil-tools VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Modern C++ pil-squasher
add_executable(pil-squasher src/pil-squasher.cpp)

target_include_directories(pil-squasher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(pil-squasher PRIVATE /W4 /WX-)
else()
    target_compile_options(pil-squasher PRIVATE -Wall -Wextra -pedantic)
endif()

if(MINGW)
    target_link_options(pil-squasher PRIVATE 
        -static-libgcc 
        -static-libstdc++
    )
    
    target_compile_definitions(pil-squasher PRIVATE __USE_MINGW_ANSI_STDIO=1)    
    target_link_options(pil-squasher PRIVATE -mconsole)
endif()

target_compile_options(pil-squasher PRIVATE
    $<$<CONFIG:Release>:
        -Os
        -ffunction-sections
        -fdata-sections
        -fno-rtti
        -flto
    >
)

if(APPLE)
    target_link_options(pil-squasher PRIVATE
        $<$<CONFIG:Release>:
            -Wl,-dead_strip
        >
    )
else()
    target_link_options(pil-squasher PRIVATE
        $<$<CONFIG:Release>:
            -Wl,--gc-sections
            -flto               
        >
    )
endif()

# Optional: print build info
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: C++${CMAKE_CXX_STANDARD}")
