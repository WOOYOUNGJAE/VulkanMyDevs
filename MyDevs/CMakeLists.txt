# Copyright (c) 2016-2025, Sascha Willems
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.10)
project(VulkanExamples CXX)

find_package(Vulkan REQUIRED)

function(buildMyBase)
    message(STATUS "Building myBase library...")
    file(GLOB MYBASE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/myBase/*.cpp")
    file(GLOB MYBASE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/myBase/*.h")

    if(NOT MYBASE_SOURCES)
        message(WARNING "No .cpp files found for myBase in ${CMAKE_CURRENT_SOURCE_DIR}/myBase. Please check the path and file existence.")
    endif()

    add_library(myBase STATIC ${MYBASE_SOURCES} ${MYBASE_HEADERS})

    target_link_libraries(myBase PUBLIC base)
    target_link_libraries(myBase PUBLIC meshoptimizer)

    target_include_directories(myBase PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/myBase")
    set_target_properties(myBase PROPERTIES FOLDER "myDevs")

    message(STATUS "myBase library built.")
endfunction(buildMyBase)


# Function for building single example
function(buildExample EXAMPLE_NAME)
    SET(EXAMPLE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_NAME})
    message(STATUS "Generating project file for example in ${EXAMPLE_FOLDER}")
    # Main
    file(GLOB SOURCE *.cpp ${BASE_HEADERS} ${EXAMPLE_FOLDER}/*.cpp)
    SET(MAIN_CPP ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.cpp)
    if(EXISTS ${EXAMPLE_FOLDER}/main.cpp)
        SET(MAIN_CPP ${EXAMPLE_FOLDER}/main.cpp)
    ENDIF()
    if(EXISTS ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.h)
        SET(MAIN_HEADER ${EXAMPLE_FOLDER}/${EXAMPLE_NAME}.h)
    ENDIF()
    # imgui example requires additional source files
    IF(${EXAMPLE_NAME} STREQUAL "imgui")
        file(GLOB ADD_SOURCE "../external/imgui/*.cpp")
        SET(SOURCE ${SOURCE} ${ADD_SOURCE})
    ENDIF()
    # wayland requires additional source files
    IF(USE_WAYLAND_WSI)
        SET(SOURCE ${SOURCE} ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)
    ENDIF()
    # Add shaders
    set(SHADER_DIR_GLSL "../shaders/glsl/${EXAMPLE_NAME}")
    file(GLOB SHADERS_GLSL "${SHADER_DIR_GLSL}/*.vert" "${SHADER_DIR_GLSL}/*.frag" "${SHADER_DIR_GLSL}/*.comp" "${SHADER_DIR_GLSL}/*.geom" "${SHADER_DIR_GLSL}/*.tesc" "${SHADER_DIR_GLSL}/*.tese" "${SHADER_DIR_GLSL}/*.mesh" "${SHADER_DIR_GLSL}/*.task" "${SHADER_DIR_GLSL}/*.rgen" "${SHADER_DIR_GLSL}/*.rchit" "${SHADER_DIR_GLSL}/*.rmiss" "${SHADER_DIR_GLSL}/*.rcall" "${SHADER_DIR_GLSL}/*.rahit" "${SHADER_DIR_GLSL}/*.rint" "${SHADER_DIR_GLSL}/*.glsl")
    set(SHADER_DIR_HLSL "../shaders/hlsl/${EXAMPLE_NAME}")
    file(GLOB SHADERS_HLSL "${SHADER_DIR_HLSL}/*.vert" "${SHADER_DIR_HLSL}/*.frag" "${SHADER_DIR_HLSL}/*.comp" "${SHADER_DIR_HLSL}/*.geom" "${SHADER_DIR_HLSL}/*.tesc" "${SHADER_DIR_HLSL}/*.tese" "${SHADER_DIR_HLSL}/*.mesh" "${SHADER_DIR_HLSL}/*.task" "${SHADER_DIR_HLSL}/*.rgen" "${SHADER_DIR_HLSL}/*.rchit" "${SHADER_DIR_HLSL}/*.rmiss" "${SHADER_DIR_HLSL}/*.rcall" "${SHADER_DIR_HLSL}/*.rahit" "${SHADER_DIR_HLSL}/*.rint")
    set(SHADER_DIR_SLANG "../shaders/slang/${EXAMPLE_NAME}")
    file(GLOB SHADERS_SLANG "${SHADER_DIR_SLANG}/*.slang")
    source_group("Shaders\\GLSL" FILES ${SHADERS_GLSL})
    source_group("Shaders\\HLSL" FILES ${SHADERS_HLSL})
    source_group("Shaders\\SLANG" FILES ${SHADERS_SLANG})
    # Add optional readme / tutorial
    file(GLOB README_FILES "${EXAMPLE_FOLDER}/*.md")
    if(WIN32)
        add_executable(${EXAMPLE_NAME} WIN32 ${MAIN_CPP} ${SOURCE} ${MAIN_HEADER} ${SHADERS_GLSL} ${SHADERS_HLSL} ${SHADERS_SLANG} ${README_FILES})
        set_target_properties(${EXAMPLE_NAME} PROPERTIES FOLDER "myDevs")
        target_link_libraries(${EXAMPLE_NAME} myBase Vulkan::Vulkan ${Vulkan_LIBRARY} ${WINLIBS})
    else(WIN32)
        add_executable(${EXAMPLE_NAME} ${MAIN_CPP} ${SOURCE} ${MAIN_HEADER} ${SHADERS_GLSL} ${SHADERS_HLSL} ${SHADERS_SLANG} ${README_FILES})
        target_link_libraries(${EXAMPLE_NAME} myBase )
    endif(WIN32)

    file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
    set_target_properties(${EXAMPLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
    if(${EXAMPLE_NAME} STREQUAL "texture3d")
        if(APPLE)
            # SRS - Use MacPorts paths as default since the same on x86 and Apple Silicon, can override for homebrew on cmake command line
            if(NOT OpenMP_omp_LIBRARY AND EXISTS /opt/local/lib/libomp/libomp.dylib)
                set(OpenMP_omp_LIBRARY /opt/local/lib/libomp/libomp.dylib)
            endif()
            if(CMAKE_C_COMPILER_ID MATCHES "Clang\$")
                set(OpenMP_C_FLAGS "-Xclang -fopenmp")
                set(OpenMP_C_LIB_NAMES "omp")
                if(NOT OpenMP_C_INCLUDE_DIR AND EXISTS /opt/local/include/libomp)
                    set(OpenMP_C_INCLUDE_DIR /opt/local/include/libomp)
                endif()
            endif()
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang\$")
                set(OpenMP_CXX_FLAGS "-Xclang -fopenmp")
                set(OpenMP_CXX_LIB_NAMES "omp")
                if(NOT OpenMP_CXX_INCLUDE_DIR AND EXISTS /opt/local/include/libomp)
                    set(OpenMP_CXX_INCLUDE_DIR /opt/local/include/libomp)
                endif()
            endif()
        endif()
        find_package(OpenMP)
        if(OpenMP_CXX_FOUND)
            set_target_properties(${EXAMPLE_NAME} PROPERTIES COMPILE_FLAGS ${OpenMP_CXX_FLAGS})
            target_include_directories(${EXAMPLE_NAME} PRIVATE ${OpenMP_CXX_INCLUDE_DIRS})
            target_link_libraries(${EXAMPLE_NAME} ${OpenMP_CXX_LIBRARIES})
        endif()
    endif()

    if(RESOURCE_INSTALL_DIR)
        install(TARGETS ${EXAMPLE_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endfunction(buildExample)


# Build all examples
function(buildExamples)
    foreach(EXAMPLE ${EXAMPLES})
        buildExample(${EXAMPLE})
    endforeach(EXAMPLE)
endfunction(buildExamples)

set(EXAMPLES
    playground
    myMeshShader
    myRayTracingBasic
    myClusterAccelerationStructureNV
)

buildMyBase()
buildExamples()