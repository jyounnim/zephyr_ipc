# sysbuild.cmake - builds core0 (this app, procpu) and core1 (remote/,
# appcpu) as two independent Zephyr images for one ESP32-S3 chip.
#
# Build with:
#   west build -p always --sysbuild -b esp32s3_devkitc/esp32s3/procpu \
#       02_MBOX_DOORBELL_LAB/lab
#
# Pattern follows this project's established ESP32-S3 AMP convention
# (see 01_HELLO_DUALCORE_LAB/lab/sysbuild.cmake).

ExternalZephyrProject_Add(
	APPLICATION remote
	SOURCE_DIR ${APP_DIR}/remote
	BOARD esp32s3_devkitc/esp32s3/appcpu
)
