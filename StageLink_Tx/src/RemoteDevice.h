// StageLink RemoteDevice
// Groups everything TX has learned about the connected RX into one
// container, replacing the previously scattered remoteDeviceInfo/
// remoteOutputList variables in main.cpp. Foundation for TX eventually
// building dynamic UI/control mapping from discovered hardware, without
// knowing RX's physical implementation - see isReady()/logSummary().
//
// hasStateSnapshot is reserved for a future protocol direction (RX
// reporting its own state back to TX). Today STATE_SNAPSHOT only flows
// TX -> RX (TX owns control state, RX applies it) - TX never receives
// one, so there is no corresponding data field yet, only the flag, kept
// here so that future addition doesn't require restructuring this class.
//
// Belongs to: StageLink_Tx. Not in StageLink_Common - this is a TX-only
// concept, RX has no "remote device" to track since discovery in this
// protocol is one-directional (TX discovers RX, not the reverse).

#pragma once

#include <Arduino.h>
#include "DeviceInfo.h"
#include "OutputList.h"

class RemoteDevice
{
public:
    // Full manual reset - not currently called automatically anywhere
    // (see setConnected()); available for future use, e.g. handling a
    // reconnect that turns out to be a different physical device.
    void clear()
    {
        connected = false;
        hasDeviceInfo = false;
        hasOutputList = false;
        hasStateSnapshot = false;
        info = StageLink::DeviceInfo();
        outputs = StageLink::OutputList();
    }

    void reset()
    {
        clear();
    }

    // Deliberately has no side effects on hasDeviceInfo/hasOutputList -
    // those latch true once received and are never auto-cleared today,
    // matching the pre-refactor behavior this replaces. Call clear()
    // explicitly if a future caller wants disconnect to invalidate them.
    void setConnected(bool isConnected)
    {
        connected = isConnected;
    }

    void setDeviceInfo(const StageLink::DeviceInfo &newInfo)
    {
        info = newInfo;
        hasDeviceInfo = true;
    }

    void setOutputList(const StageLink::OutputList &newOutputs)
    {
        outputs = newOutputs;
        hasOutputList = true;
    }

    // "Ready" means TX has everything it currently expects to know
    // about RX: link up, DeviceInfo in hand, OutputList in hand.
    // Doesn't require hasStateSnapshot - nothing sets that yet.
    bool isReady() const
    {
        return connected && hasDeviceInfo && hasOutputList;
    }

    void logSummary() const
    {
        Serial.println("Remote Device:");

        if (!hasDeviceInfo)
        {
            Serial.println("  (no device info yet)");
            return;
        }

        Serial.print("  Name: ");
        Serial.println(info.name);
        Serial.print("  FW: ");
        Serial.println(info.firmwareVersion);

        Serial.println("  Outputs:");

        if (!hasOutputList)
        {
            Serial.println("    (no output list yet)");
            return;
        }

        for (uint8_t i = 0; i < outputs.count; ++i)
        {
            Serial.print("    ");
            Serial.print(StageLink::capabilityName(outputs.outputs[i].capability));
            Serial.print(" #");
            Serial.println(outputs.outputs[i].index);
        }
    }

    bool connected = false;
    bool hasDeviceInfo = false;
    bool hasOutputList = false;
    bool hasStateSnapshot = false; // reserved - see class comment

    StageLink::DeviceInfo info = {};
    StageLink::OutputList outputs = {};
};
