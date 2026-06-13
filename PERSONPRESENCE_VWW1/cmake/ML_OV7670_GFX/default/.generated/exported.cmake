set(DEPENDENT_MP_BIN2HEXML_OV7670_GFX_default_YE_sShx5 "c:/Program Files/Microchip/xc32/v4.60/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFML_OV7670_GFX_default_YE_sShx5 ${CMAKE_CURRENT_LIST_DIR}/../../../../out/ML_OV7670_GFX/default.elf)
set(DEPENDENT_TARGET_DIRML_OV7670_GFX_default_YE_sShx5 ${CMAKE_CURRENT_LIST_DIR}/../../../../out/ML_OV7670_GFX)
set(DEPENDENT_BYPRODUCTSML_OV7670_GFX_default_YE_sShx5 ${DEPENDENT_TARGET_DIRML_OV7670_GFX_default_YE_sShx5}/${sourceFileNameML_OV7670_GFX_default_YE_sShx5}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRML_OV7670_GFX_default_YE_sShx5}/${sourceFileNameML_OV7670_GFX_default_YE_sShx5}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXML_OV7670_GFX_default_YE_sShx5} --image ${DEPENDENT_DEPENDENT_TARGET_ELFML_OV7670_GFX_default_YE_sShx5} --image-generated-c ${sourceFileNameML_OV7670_GFX_default_YE_sShx5}.c --image-generated-h ${sourceFileNameML_OV7670_GFX_default_YE_sShx5}.h --image-copy-mode ${modeML_OV7670_GFX_default_YE_sShx5} --image-offset ${addressML_OV7670_GFX_default_YE_sShx5} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRML_OV7670_GFX_default_YE_sShx5}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFML_OV7670_GFX_default_YE_sShx5})
add_custom_target(
    dependent_produced_source_artifactML_OV7670_GFX_default_YE_sShx5 
    DEPENDS ${DEPENDENT_TARGET_DIRML_OV7670_GFX_default_YE_sShx5}/${sourceFileNameML_OV7670_GFX_default_YE_sShx5}.c
    )
