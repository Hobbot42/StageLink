// StageLink DeviceInfo
// Generic device identity + capability reporting, carried by
// CAPABILITY_REQUEST/CAPABILITY_RESPONSE packets (see StageLinkProtocol.h).
// Lets an RxQ-class device describe itself (name, firmware/hardware
// version, capability list) without TxQ needing to hardcode anything
// device-specific - a new device just fills in a DeviceInfo, it doesn't
// need a new packet type or protocol change.
//
// Capabilities are generic ("Motion", "LED", ...), not tied to a specific
// mechanism - e.g. CAP_MOTION means "this device can produce physical
// motion," not "this device has a servo." Which mechanism actually
// implements a capability is a configuration detail for later, not part
// of the capability ID itself.
// Belongs to: StageLink_Common (shared by TX and RX).

#pragma once

#include <Arduino.h>

namespace StageLink
{
    enum class DeviceCapability : uint8_t
    {
        Motion = 0,
        LedAddressable = 1,
        Relay = 2,
        Input = 3
    };

    // Number of DeviceCapability values - bump this when adding a new one
    // so callers that iterate all capabilities (e.g. TX's device info
    // page) pick it up without a separate hardcoded count.
    constexpr uint8_t DEVICE_CAPABILITY_COUNT = 4;

    using CapabilityMask = uint16_t;

    constexpr CapabilityMask capabilityBit(DeviceCapability capability)
    {
        return static_cast<CapabilityMask>(1u << static_cast<uint8_t>(capability));
    }

    inline bool hasCapability(CapabilityMask mask, DeviceCapability capability)
    {
        return (mask & capabilityBit(capability)) != 0;
    }

    // Single source of truth for capability display text - callers (TX's
    // device info page, Serial logging, ...) should never hardcode these
    // strings themselves.
    inline const char *capabilityName(DeviceCapability capability)
    {
        switch (capability)
        {
            case DeviceCapability::Motion:
                return "Motion";
            case DeviceCapability::LedAddressable:
                return "LED";
            case DeviceCapability::Relay:
                return "Relay";
            case DeviceCapability::Input:
                return "Input";
            default:
                return "Unknown";
        }
    }

    constexpr uint8_t DEVICE_NAME_MAX_LENGTH = 20;
    constexpr uint8_t DEVICE_VERSION_MAX_LENGTH = 12;
    constexpr uint8_t DEVICE_HW_VERSION_MAX_LENGTH = 4; // placeholder for now

    // ESP32 hardware (Wi-Fi station) MAC address, 6 bytes - permanent,
    // factory-assigned, and unique per board, so it's used as-is rather
    // than a manually assigned number. Raw bytes on the wire, not a
    // string - see formatDeviceIdShort() below for a human-readable form.
    constexpr uint8_t DEVICE_ID_LENGTH = 6;

    // User-assigned short label (e.g. "A1", "B2") for telling apart
    // multiple RxQ units in one setup - unlike deviceId, this is not
    // guaranteed unique, just operator-chosen. 2 characters, A-Z/0-9
    // only (see UNIT_NAME_CHARSET/isValidUnitNameChar) - persisted on RX
    // via ConfigManager (see RX main.cpp), defaults to UNIT_NAME_DEFAULT
    // until an operator sets it.
    constexpr uint8_t UNIT_NAME_LENGTH = 3; // 2 chars + null terminator
    constexpr char UNIT_NAME_DEFAULT[UNIT_NAME_LENGTH] = "A1";
    constexpr char UNIT_NAME_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    constexpr uint8_t UNIT_NAME_CHARSET_LENGTH = sizeof(UNIT_NAME_CHARSET) - 1; // exclude the string's own null terminator

    inline bool isValidUnitNameChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    struct DeviceInfo
    {
        char name[DEVICE_NAME_MAX_LENGTH];
        uint8_t deviceId[DEVICE_ID_LENGTH];
        char unitName[UNIT_NAME_LENGTH];
        char firmwareVersion[DEVICE_VERSION_MAX_LENGTH];
        char hardwareVersion[DEVICE_HW_VERSION_MAX_LENGTH];
        CapabilityMask capabilities;
    };

    constexpr uint8_t DEVICE_INFO_WIRE_SIZE =
        DEVICE_NAME_MAX_LENGTH +
        DEVICE_ID_LENGTH +
        UNIT_NAME_LENGTH +
        DEVICE_VERSION_MAX_LENGTH +
        DEVICE_HW_VERSION_MAX_LENGTH +
        sizeof(CapabilityMask);

    // Copies each field explicitly (not a raw struct memcpy) so the wire
    // layout never depends on this compiler's struct padding/alignment -
    // same convention as encodeValueUpdate/serializeStateSnapshot.
    inline uint8_t serializeDeviceInfo(const DeviceInfo &info, uint8_t *payload)
    {
        uint8_t offset = 0;

        memcpy(payload + offset, info.name, DEVICE_NAME_MAX_LENGTH);
        offset += DEVICE_NAME_MAX_LENGTH;

        memcpy(payload + offset, info.deviceId, DEVICE_ID_LENGTH);
        offset += DEVICE_ID_LENGTH;

        memcpy(payload + offset, info.unitName, UNIT_NAME_LENGTH);
        offset += UNIT_NAME_LENGTH;

        memcpy(payload + offset, info.firmwareVersion, DEVICE_VERSION_MAX_LENGTH);
        offset += DEVICE_VERSION_MAX_LENGTH;

        memcpy(payload + offset, info.hardwareVersion, DEVICE_HW_VERSION_MAX_LENGTH);
        offset += DEVICE_HW_VERSION_MAX_LENGTH;

        payload[offset] = info.capabilities & 0xFF;
        payload[offset + 1] = (info.capabilities >> 8) & 0xFF;

        return DEVICE_INFO_WIRE_SIZE;
    }

    inline bool deserializeDeviceInfo(
        const char *payload,
        uint8_t payloadLength,
        DeviceInfo &outInfo
    )
    {
        if (payloadLength != DEVICE_INFO_WIRE_SIZE)
        {
            return false;
        }

        uint8_t offset = 0;

        memcpy(outInfo.name, payload + offset, DEVICE_NAME_MAX_LENGTH);
        outInfo.name[DEVICE_NAME_MAX_LENGTH - 1] = '\0';
        offset += DEVICE_NAME_MAX_LENGTH;

        memcpy(outInfo.deviceId, payload + offset, DEVICE_ID_LENGTH);
        offset += DEVICE_ID_LENGTH;

        memcpy(outInfo.unitName, payload + offset, UNIT_NAME_LENGTH);
        outInfo.unitName[UNIT_NAME_LENGTH - 1] = '\0';
        offset += UNIT_NAME_LENGTH;

        memcpy(outInfo.firmwareVersion, payload + offset, DEVICE_VERSION_MAX_LENGTH);
        outInfo.firmwareVersion[DEVICE_VERSION_MAX_LENGTH - 1] = '\0';
        offset += DEVICE_VERSION_MAX_LENGTH;

        memcpy(outInfo.hardwareVersion, payload + offset, DEVICE_HW_VERSION_MAX_LENGTH);
        outInfo.hardwareVersion[DEVICE_HW_VERSION_MAX_LENGTH - 1] = '\0';
        offset += DEVICE_HW_VERSION_MAX_LENGTH;

        outInfo.capabilities =
            static_cast<uint8_t>(payload[offset]) |
            (static_cast<uint16_t>(static_cast<uint8_t>(payload[offset + 1])) << 8);

        return true;
    }

    constexpr uint8_t DEVICE_ID_SHORT_BYTES = 3; // last 3 bytes - enough to tell a handful of RxQ units apart on one show without printing the full 6-byte MAC
    constexpr uint8_t DEVICE_ID_SHORT_STRING_LENGTH = DEVICE_ID_SHORT_BYTES * 2 + 1; // 2 hex chars/byte + null terminator

    // Formats the last DEVICE_ID_SHORT_BYTES bytes of deviceId as
    // uppercase hex into buffer (must be at least
    // DEVICE_ID_SHORT_STRING_LENGTH). Display-only convenience (TX OLED,
    // Serial logging) - the full 6-byte deviceId is what's actually
    // unique and transmitted; this is just a human-readable short form.
    inline void formatDeviceIdShort(const uint8_t deviceId[DEVICE_ID_LENGTH], char *buffer, uint8_t bufferSize)
    {
        static const char hexDigits[] = "0123456789ABCDEF";

        if (bufferSize < DEVICE_ID_SHORT_STRING_LENGTH)
        {
            buffer[0] = '\0';
            return;
        }

        uint8_t startByte = DEVICE_ID_LENGTH - DEVICE_ID_SHORT_BYTES;

        for (uint8_t i = 0; i < DEVICE_ID_SHORT_BYTES; ++i)
        {
            uint8_t value = deviceId[startByte + i];
            buffer[i * 2] = hexDigits[(value >> 4) & 0x0F];
            buffer[i * 2 + 1] = hexDigits[value & 0x0F];
        }

        buffer[DEVICE_ID_SHORT_BYTES * 2] = '\0';
    }

    // Copies src into a fixed-size DeviceInfo field, always null-
    // terminating even if src is longer than the field (truncates rather
    // than overflowing).
    inline void setDeviceInfoField(char *field, uint8_t fieldSize, const char *src)
    {
        strncpy(field, src, fieldSize - 1);
        field[fieldSize - 1] = '\0';
    }
}
