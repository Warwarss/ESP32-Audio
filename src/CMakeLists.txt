# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
idf_component_register(SRCS "uezuem_spielraum.c"
                       REQUIRES esp_psram)
littlefs_create_partition_image(littlefs ../data FLASH_IN_PROJECT)

