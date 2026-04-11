#!/bin/sh
# Flash a pre-built ScummVM-for-T-Deck merged image to the device.
#
# Usage:  ./flash.sh [serial-port]
# If no port is given, esptool tries to auto-detect.
#
# Prereqs:
#   pip install esptool
#
# Put the T-Deck in download mode first:
#   hold BOOT, tap RESET, release BOOT (screen will stay dark)

set -e

IMAGE="scummvm-tdeck-full.bin"
if [ ! -f "$IMAGE" ]; then
	echo "ERROR: $IMAGE not found in current directory."
	echo "Download it from the GitHub Releases page and place it next to this script."
	exit 1
fi

PORT_ARG=""
if [ -n "$1" ]; then
	PORT_ARG="-p $1"
fi

python -m esptool --chip esp32s3 $PORT_ARG -b 921600 write_flash 0x0 "$IMAGE"

echo
echo "Done. The T-Deck should now reboot into ScummVM."
