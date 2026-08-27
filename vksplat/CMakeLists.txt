cmake_minimum_required(VERSION 3.24)
project(vksplat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(cmake/Dependencies.cmake)

add_executable(vksplat
    src/main.cpp
    src/app/Application.cpp
    src/core/Config.cpp
    src/core/Logger.cpp
    src/platform/Paths.cpp
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
    VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:vksplat>
)
