function(target_compile_shaders TARGET SHADER_INCLUDE_DIR)
    set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${OUTPUT_DIR})

    foreach(SOURCE ${ARGN})
        get_filename_component(NAME ${SOURCE} NAME)
        set(OUTPUT ${OUTPUT_DIR}/${NAME}.spv)
        add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND $<TARGET_FILE:glslang-standalone> --target-env vulkan1.3 "-I${SHADER_INCLUDE_DIR}" -o ${OUTPUT} ${SOURCE}
            DEPENDS ${SOURCE} ${SHADER_INCLUDE_DIR}/splat_common.glsl glslang-standalone
            COMMENT "Compiling ${NAME}"
            VERBATIM
        )
        list(APPEND OUTPUTS ${OUTPUT})
    endforeach()

    add_custom_target(${TARGET}_shaders DEPENDS ${OUTPUTS})
    add_dependencies(${TARGET} ${TARGET}_shaders)

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${OUTPUT_DIR} $<TARGET_FILE_DIR:${TARGET}>/shaders
        COMMENT "Staging shaders next to the executable"
    )
endfunction()
