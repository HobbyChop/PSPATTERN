execute_process(
  COMMAND psp-gcc --version
  OUTPUT_VARIABLE PSP_GCC_VERSION_OUTPUT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "([0-9]+\\.[0-9]+\\.[0-9]+)" PSP_GCC_VERSION "${PSP_GCC_VERSION_OUTPUT}")
string(REGEX MATCH "^[^\n]*" PSP_GCC_VERSION_LINE "${PSP_GCC_VERSION_OUTPUT}")
string(REGEX MATCH "[0-9]{8}" PSP_GCC_DATE "${PSP_GCC_VERSION_OUTPUT}")
if(NOT PSP_GCC_VERSION STREQUAL "15.2.0" OR NOT PSP_GCC_DATE STREQUAL "")
  message(WARNING "This project has been tested and developed with a GCC version that may differ from yours.\n    Reference version: psp-gcc (GCC) 15.2.0\n    Your version:      ${PSP_GCC_VERSION_LINE}")
endif()

cmake_minimum_required(VERSION 3.10)
set(CMAKE_TOOLCHAIN_FILE "$ENV{PSPDEV}/psp/share/pspdev.cmake" CACHE PATH "toolchain file")
project(me-core C ASM)

add_subdirectory(kernel)

add_custom_command(
  OUTPUT kcall.S
  COMMAND psp-build-exports -s ${CMAKE_SOURCE_DIR}/kernel/exports.exp > kcall.S
  DEPENDS kcall_prx
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generating kcall imports"
)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
  COMMAND xxd -i ${CMAKE_CURRENT_BINARY_DIR}/kernel/kcall.prx > ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
  COMMAND sed -i 's/unsigned char [^[]*/unsigned char embedded_kcall/' ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
  COMMAND sed -i 's/unsigned int [^ ]*_len/unsigned int embedded_kcall_len/' ${CMAKE_CURRENT_BINARY_DIR}/embedded.c
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/kernel/kcall.prx
  DEPENDS kcall_prx
  COMMENT "Embedding kcall.prx into C file"
)

add_custom_target(kcall_files DEPENDS kcall.S)

add_library(me-core STATIC
  me-lib.c
  vme-lib.c
  me-core-custom.c
  me-core-mapping.c
  context.S
  embedded.c
  ${CMAKE_CURRENT_BINARY_DIR}/kcall.S
)

add_library(me-core-mapper STATIC
  me-core-mapping.c
)

add_library(me-core-lib STATIC
  me-lib.c
  vme-lib.c
  embedded.c
  ${CMAKE_CURRENT_BINARY_DIR}/kcall.S
)

add_dependencies(me-core kcall_files)

foreach(target me-core me-core-lib me-core-mapper)
  target_compile_options(${target} PRIVATE -Os -G0 -Wall -fno-pic)
  target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

install(TARGETS me-core me-core-lib me-core-mapper
  ARCHIVE DESTINATION "$ENV{PSPDEV}/psp/lib"
)

file(GLOB HEADER_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(FILTER HEADER_FILES EXCLUDE REGEX ".*me-core-mapping\\.def\\.h$")

install(FILES ${HEADER_FILES}
  DESTINATION "$ENV{PSPDEV}/psp/include/me-core-mapper"
)

file(GLOB KERNEL_HEADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/kernel/*.h")

install(FILES ${KERNEL_HEADER_FILES}
  DESTINATION "$ENV{PSPDEV}/psp/include/me-core-mapper/kernel"
)
