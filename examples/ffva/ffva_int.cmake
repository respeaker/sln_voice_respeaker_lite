set(FFVA_INT_SPEAKER_AMP_ENABLED 0 CACHE STRING "Enable ReSpeaker Lite speaker amp in FFVA INT test images (0 or 1)")
if(NOT FFVA_INT_SPEAKER_AMP_ENABLED MATCHES "^[01]$")
    message(FATAL_ERROR "FFVA_INT_SPEAKER_AMP_ENABLED must be 0 or 1")
endif()

set(FFVA_INT_AGC_FIXED_GAIN_25 0 CACHE STRING "Use fixed 25x AGC gain in FFVA INT test images (0 or 1)")
if(NOT FFVA_INT_AGC_FIXED_GAIN_25 MATCHES "^[01]$")
    message(FATAL_ERROR "FFVA_INT_AGC_FIXED_GAIN_25 must be 0 or 1")
endif()

set(FFVA_INT_XMOS_CLOCK_MASTER 0 CACHE STRING "Drive ReSpeaker I2S clocks from XMOS for FFVA INT (0 or 1)")
if(NOT FFVA_INT_XMOS_CLOCK_MASTER MATCHES "^[01]$")
    message(FATAL_ERROR "FFVA_INT_XMOS_CLOCK_MASTER must be 0 or 1")
endif()

if(FFVA_INT_XMOS_CLOCK_MASTER)
    set(FFVA_INT_EXTERNAL_MCLK 0)
    set(FFVA_INT_I2S_MODE appconfI2S_MODE_MASTER)
    set(FFVA_INT_RECOVER_MCLK 0)
    set(FFVA_INT_MCLK_HZ 24576000)
else()
    set(FFVA_INT_EXTERNAL_MCLK 1)
    set(FFVA_INT_I2S_MODE appconfI2S_MODE_SLAVE)
    set(FFVA_INT_RECOVER_MCLK 1)
    set(FFVA_INT_MCLK_HZ 12288000)
endif()

set(FFVA_INT_COMPILE_DEFINITIONS
    ${APP_COMPILE_DEFINITIONS}
    appconfEXTERNAL_MCLK=${FFVA_INT_EXTERNAL_MCLK}
    appconfI2S_ENABLED=1
    appconfUSB_ENABLED=0
    appconfUSB_DFU_ENABLED=1
    appconfI2C_DFU_ENABLED=1
    appconfSPEAKER_AMP_ENABLED=${FFVA_INT_SPEAKER_AMP_ENABLED}
    appconfFFVA_AGC_FIXED_GAIN_25=${FFVA_INT_AGC_FIXED_GAIN_25}
    appconfFFVA_INT_XMOS_CLOCK_MASTER=${FFVA_INT_XMOS_CLOCK_MASTER}
    appconfAEC_REF_DEFAULT=appconfAEC_REF_I2S
    appconfI2S_MODE=${FFVA_INT_I2S_MODE}
    appconfI2S_AUDIO_SAMPLE_RATE=48000
    appconfRECOVER_MCLK_I2S_APP_PLL=${FFVA_INT_RECOVER_MCLK}
    MIC_ARRAY_CONFIG_MCLK_FREQ=${FFVA_INT_MCLK_HZ}
)

set(FFVA_INT_BOARD_TARGET sln_voice::app::ffva::respeaker_lite)
set(FFVA_INT_BOOT_PARTITION_SIZE 0x200000)

query_tools_version()
foreach(FFVA_AP ${FFVA_PIPELINES_INT})
    #**********************
    # Tile Targets
    #**********************
    set(TARGET_NAME tile0_example_ffva_int_${FFVA_AP})
    add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL)
    target_sources(${TARGET_NAME} PUBLIC ${APP_SOURCES})
    target_include_directories(${TARGET_NAME} PUBLIC ${APP_INCLUDES})
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            ${FFVA_INT_COMPILE_DEFINITIONS}
            THIS_XCORE_TILE=0
    )
    target_compile_options(${TARGET_NAME} PRIVATE ${APP_COMPILER_FLAGS})
    target_link_libraries(${TARGET_NAME}
        PUBLIC
            ${APP_COMMON_LINK_LIBRARIES}
            ${FFVA_INT_BOARD_TARGET}
            sln_voice::app::ffva::ap::${FFVA_AP}
    )
    target_link_options(${TARGET_NAME} PRIVATE ${APP_LINK_OPTIONS})
    unset(TARGET_NAME)

    set(TARGET_NAME tile1_example_ffva_int_${FFVA_AP})
    add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL)
    target_sources(${TARGET_NAME} PUBLIC ${APP_SOURCES})
    target_include_directories(${TARGET_NAME} PUBLIC ${APP_INCLUDES})
    target_compile_definitions(${TARGET_NAME}
        PUBLIC
            ${FFVA_INT_COMPILE_DEFINITIONS}
            THIS_XCORE_TILE=1
    )
    target_compile_options(${TARGET_NAME} PRIVATE ${APP_COMPILER_FLAGS})
    target_link_libraries(${TARGET_NAME}
        PUBLIC
            ${APP_COMMON_LINK_LIBRARIES}
            ${FFVA_INT_BOARD_TARGET}
            sln_voice::app::ffva::ap::${FFVA_AP}
    )
    target_link_options(${TARGET_NAME} PRIVATE ${APP_LINK_OPTIONS})
    unset(TARGET_NAME)

    #**********************
    # Merge binaries
    #**********************
    merge_binaries(example_ffva_int_${FFVA_AP} tile0_example_ffva_int_${FFVA_AP} tile1_example_ffva_int_${FFVA_AP} 1)

    #**********************
    # Create run and debug targets
    #**********************
    create_run_target(example_ffva_int_${FFVA_AP})
    create_debug_target(example_ffva_int_${FFVA_AP})
    message(variable="${XTC_VERSION_MAJOR}")
    create_upgrade_img_target(example_ffva_int_${FFVA_AP} ${XTC_VERSION_MAJOR} ${XTC_VERSION_MINOR})

    #**********************
    # Create data partition support targets
    #**********************
    set(TARGET_NAME example_ffva_int_${FFVA_AP})
    set(DATA_PARTITION_FILE ${TARGET_NAME}_data_partition.bin)
    set(FATFS_FILE ${TARGET_NAME}_fat.fs)
    set(FATFS_CONTENTS_DIR ${TARGET_NAME}_fatmktmp)

    add_custom_target(
        ${FATFS_FILE} ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${FATFS_CONTENTS_DIR}/fs/
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FATFS_CONTENTS_DIR}/fs/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/filesystem_support/demo.txt ${FATFS_CONTENTS_DIR}/fs/
        COMMAND fatfs_mkimage --input=${FATFS_CONTENTS_DIR} --output=${FATFS_FILE}
        COMMENT
            "Create filesystem"
        VERBATIM
    )

    set_target_properties(${FATFS_FILE} PROPERTIES
        ADDITIONAL_CLEAN_FILES ${FATFS_CONTENTS_DIR}
    )

    # The filesystem is the only component in the data partition, copy it to
    # the assocated data partition file which is required for CI.
    add_custom_command(
        OUTPUT ${DATA_PARTITION_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy ${FATFS_FILE} ${DATA_PARTITION_FILE}
        DEPENDS
            ${FATFS_FILE}
        COMMENT
            "Create data partition"
        VERBATIM
    )

    list(APPEND DATA_PARTITION_FILE_LIST
        ${FATFS_FILE}
        ${DATA_PARTITION_FILE}
    )

    create_data_partition_directory(
        #[[ Target ]]                   ${TARGET_NAME}
        #[[ Copy Files ]]               "${DATA_PARTITION_FILE_LIST}"
        #[[ Dependencies ]]             "${DATA_PARTITION_FILE_LIST}"
    )

    create_flash_app_target(
        #[[ Target ]]                  ${TARGET_NAME}
        #[[ Boot Partition Size ]]     ${FFVA_INT_BOOT_PARTITION_SIZE}
        #[[ Data Partition Contents ]] ${DATA_PARTITION_FILE}
        #[[ Dependencies ]]            ${DATA_PARTITION_FILE}
    )

    unset(DATA_PARTITION_FILE_LIST)
endforeach()
