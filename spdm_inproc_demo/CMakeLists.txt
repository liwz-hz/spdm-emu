cmake_minimum_required(VERSION 3.5)

include_directories(${PROJECT_SOURCE_DIR}/spdm_inproc_demo/include
                    ${PROJECT_SOURCE_DIR}/include
                    ${LIBSPDM_DIR}/include
                    ${LIBSPDM_DIR}/os_stub/include
                    ${LIBSPDM_DIR}/os_stub
                    ${LIBSPDM_DIR}/os_stub/spdm_device_secret_lib_sample
)

set(src_spdm_inproc_demo
    src/internal_transport.c
    src/spdm_context.c
    src/support.c
    src/main.c
)

set(spdm_inproc_demo_LIBRARY
    memlib
    ${SPDM_DEBUG_LIB_NAME}
    spdm_requester_lib
    spdm_responder_lib
    spdm_common_lib
    ${CRYPTO_LIB_PATHS}
    rnglib
    cryptlib_${CRYPTO}
    malloclib
    spdm_crypt_lib
    spdm_crypt_ext_lib
    spdm_secured_message_lib
    spdm_transport_mctp_lib
    pci_doe_requester_lib
    pci_doe_responder_lib
    pci_tdisp_requester_lib
    pci_tdisp_responder_lib
    pci_tdisp_device_lib_sample
    spdm_device_secret_lib_${DEVICE}
    platform_lib
)

if((TOOLCHAIN STREQUAL "KLEE") OR (TOOLCHAIN STREQUAL "CBMC"))
    add_executable(spdm_inproc_demo
                   ${src_spdm_inproc_demo}
                   $<TARGET_OBJECTS:memlib>
                   $<TARGET_OBJECTS:debuglib>
                   $<TARGET_OBJECTS:spdm_requester_lib>
                   $<TARGET_OBJECTS:spdm_responder_lib>
                   $<TARGET_OBJECTS:spdm_common_lib>
                   $<TARGET_OBJECTS:${CRYPTO_LIB_PATHS}>
                   $<TARGET_OBJECTS:rnglib>
                   $<TARGET_OBJECTS:cryptlib_${CRYPTO}>
                   $<TARGET_OBJECTS:malloclib>
                   $<TARGET_OBJECTS:spdm_crypt_lib>
                   $<TARGET_OBJECTS:spdm_secured_message_lib>
                   $<TARGET_OBJECTS:spdm_transport_mctp_lib>
                   $<TARGET_OBJECTS:spdm_device_secret_lib_${DEVICE}>
                   $<TARGET_OBJECTS:platform_lib>
    )
else()
    add_executable(spdm_inproc_demo ${src_spdm_inproc_demo})
    target_link_libraries(spdm_inproc_demo ${spdm_inproc_demo_LIBRARY})
endif()

add_subdirectory(test)