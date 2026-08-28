cmake_minimum_required(VERSION 3.24)
project(vksplat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

add_executable(vksplat
    src/main.cpp
    src/app/Application.cpp
    src/core/Config.cpp
    src/core/Logger.cpp
    src/platform/Paths.cpp
    src/render/GpuBuffer.cpp
    src/render/GpuImage.cpp
    src/render/SplatPass.cpp
    src/render/TonemapPass.cpp
    src/render/Upload.cpp
    src/render/VmaImplementation.cpp
    src/scene/OrbitCamera.cpp
    src/scene/SplatScene.cpp
    src/platform/Window.cpp
    src/render/Renderer.cpp
    src/render/Swapchain.cpp
    src/render/VulkanContext.cpp
)

target_include_directories(vksplat PRIVATE src)
target_compile_definitions(vksplat PRIVATE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE)
target_link_libraries(vksplat PRIVATE
    spdlog::spdlog
    tomlplusplus::tomlplusplus
    glfw
    volk
    vk-bootstrap::vk-bootstrap
    glm::glm
    GPUOpen::VulkanMemoryAllocator
)

set_source_files_properties(src/render/VmaImplementation.cpp PROPERTIES
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>"
)

target_compile_shaders(vksplat ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/project.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sort_clear.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sort_histogram.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sort_scan.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/sort_scatter.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/splat.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/splat.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/tonemap.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/tonemap.frag
)

if(MSVC)
    target_compile_options(vksplat PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(vksplat PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_custom_command(TARGET vksplat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:vksplat>/config
    COMMENT "Staging config next to the executable"
)

set_target_properties(vksplat PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
