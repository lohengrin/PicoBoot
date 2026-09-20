# storage: AppCatalog over pico_toolset::SdCard. Depends only on the SD
# card component -- no boot_core/usb/ui dependency.
add_library(picoboot_storage STATIC
    src/app_catalog.cpp
    src/storage_state.cpp
)

target_include_directories(picoboot_storage PUBLIC include)

target_link_libraries(picoboot_storage PUBLIC
    pico_stdlib
    pico_toolset_sdcard
)
