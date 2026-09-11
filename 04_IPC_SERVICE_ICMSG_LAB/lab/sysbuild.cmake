# Add the appcpu (core1) image as a second, independent Zephyr application
# built and flashed together with this one via `west build --sysbuild`.
ExternalZephyrProject_Add(
    APPLICATION remote
    SOURCE_DIR ${APP_DIR}/remote
    BOARD esp32s3_devkitc/esp32s3/appcpu
)
