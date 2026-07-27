# StageLink RxQ auto upload-protocol selection
# Makes the default environment upload over USB when the board is
# plugged in, and fall back to OTA when it isn't - so VS Code's Upload
# button (which always runs the default environment, and can't pick an
# environment itself) works either way without editing platformio.ini.
#
# Only ever *adds* the fallback: if a USB serial port is present this
# script does nothing at all and the normal esptool path runs untouched.
# The explicit [env:rxq_ota] environment is also left alone, so
# `pio run -e rxq_ota -t upload` still forces OTA regardless of USB.
#
# OTA needs RXQ_OTA_IP (and RXQ_OTA_PASSWORD if the firmware sets an OTA
# password - it does, see src/secrets.h). Both come from the environment
# rather than platformio.ini, which is committed to git.
# Belongs to: StageLink_Rx.

import glob
import os
import re

Import("env")  # noqa: F821 - injected by PlatformIO

# mDNS name ArduinoOTA advertises in Update Mode - must match the
# ArduinoOTA.setHostname() call in src/UpdateMode.cpp. Used so an upload
# doesn't need the OLED's IP typed in: the address is DHCP-assigned and
# changes, the hostname doesn't. RXQ_OTA_IP still overrides it, for a
# network where mDNS doesn't resolve.
DEFAULT_OTA_HOST = "stagelink-rx.local"

# Same file the firmware compiles its OTA password from, so the two can
# never disagree. Gitignored (see .gitignore), which is exactly why the
# password is read from here rather than written into platformio.ini.
SECRETS_PATH = os.path.join("src", "secrets.h")


def password_from_secrets():
    try:
        with open(SECRETS_PATH) as handle:
            match = re.search(r'#define\s+OTA_PASSWORD\s+"([^"]*)"', handle.read())
            return match.group(1) if match else None
    except OSError:
        return None

# Serial-port patterns macOS uses for the USB-to-serial bridges found on
# ESP32 dev boards: CH340/CH341 (wch), CP210x/FTDI (usbserial), Silicon
# Labs' own naming, and native-USB boards (usbmodem).
USB_PORT_PATTERNS = (
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.usbmodem*",
)


def find_usb_port():
    for pattern in USB_PORT_PATTERNS:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


# Guard: this fallback is only for the default USB environment. Without
# it the script would also run for [env:rxq_ota] (which inherits
# extra_scripts via `extends`) and second-guess an explicit choice.
if env["PIOENV"] == "esp32dev":
    usb_port = find_usb_port()

    if usb_port:
        print("AUTO-UPLOAD: USB board found at %s - uploading over USB." % usb_port)
        env.Replace(UPLOAD_PORT=usb_port)
    else:
        ota_host = os.environ.get("RXQ_OTA_IP") or DEFAULT_OTA_HOST
        ota_password = os.environ.get("RXQ_OTA_PASSWORD") or password_from_secrets()

        print(
            "AUTO-UPLOAD: no USB board found - falling back to OTA at %s. "
            "The RxQ must be in Update Mode (Setup > Update Mode)." % ota_host
        )
        env.Replace(UPLOAD_PROTOCOL="espota", UPLOAD_PORT=ota_host)

        if ota_password:
            env.Append(UPLOAD_FLAGS=["--auth=%s" % ota_password])
        else:
            print(
                "AUTO-UPLOAD: no OTA password found in RXQ_OTA_PASSWORD or "
                "src/secrets.h - the upload will be rejected, since the "
                "firmware sets an OTA password."
            )
