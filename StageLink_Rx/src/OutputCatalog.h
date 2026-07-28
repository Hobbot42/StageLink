// StageLink RxQ OutputCatalog
// What outputs this controller has, what they are, and what the operator
// calls them. One entry per *logical* output - the thing a cue programs -
// which may cover several OutputManager channels: an addressable LED is
// one output over four channels (red/green/blue/brightness), a servo is
// one output over one.
//
// The wiring is declared once, by main.cpp, right where the devices are
// registered with OutputManager (see addOutput()). Before this existed,
// the GUI carried its own hardcoded copy of the same table and the two
// were kept in step by hand - which is exactly the sort of thing that
// silently breaks the first time hardware is added.
//
// Names are the operator's and persist: "Dragon Head" rather than
// "SERVO". They are stored per output in ConfigManager, not in the show
// file, so renaming an output never disturbs saved shows and adding this
// needed no show storage version bump.
//
// Types are reported, not chosen. A type reflects the device main.cpp
// actually registered on those channels - typing a servo output as a
// relay would just describe the hardware wrongly. Making type editable
// means creating output devices at runtime, which is a separate job.
// Belongs to: StageLink_Rx - the TxQ has no outputs.

#pragma once

#include <cstdint>

namespace StageLink
{
    class OutputCatalog
    {
    public:
        static constexpr uint8_t MAX_OUTPUTS = 8;
        static constexpr uint8_t MAX_CHANNELS_PER_OUTPUT = 4;

        // Long enough for a descriptive name at the display's 23-character
        // row width, once a value has been appended.
        static constexpr uint8_t NAME_SIZE = 20;

        enum class Type : uint8_t
        {
            Servo,
            Led,
            Stepper,
            Relay
        };

        // Loads saved names. Call after ConfigManager::begin() and before
        // any addOutput(), so a declared output picks up its stored name.
        void begin();

        // Declares one logical output. channels are the OutputManager
        // channels it drives, in the order that output type's value fields
        // expect them (an LED is red, green, blue, brightness). defaultName
        // is used only when nothing has been saved for this slot.
        // Returns false if MAX_OUTPUTS is reached or the arguments don't
        // describe a usable output.
        bool addOutput(
            Type type,
            const uint8_t *channels,
            uint8_t channelCount,
            const char *defaultName
        );

        uint8_t getCount() const;

        const char *getName(uint8_t index) const;
        Type getType(uint8_t index) const;

        // Display name for a type - "SERVO", "LED". Uppercase because a
        // type is a system-level name, unlike the operator's own.
        static const char *typeName(Type type);

        // Channels this output drives, or nullptr for an invalid index.
        const uint8_t *getChannels(uint8_t index, uint8_t &countOut) const;

        // Which output owns an OutputManager channel, or NOT_FOUND. Used
        // to group a cue's stored actions back into one row per output.
        static constexpr uint8_t NOT_FOUND = 0xFF;
        uint8_t indexForChannel(uint8_t channel) const;

        // Renames an output and saves it immediately - renaming is rare
        // and one write per rename is not worth deferring.
        bool rename(uint8_t index, const char *name);

    private:
        struct Output
        {
            Type type;
            char name[NAME_SIZE];
            uint8_t channels[MAX_CHANNELS_PER_OUTPUT];
            uint8_t channelCount;
        };

        // ConfigManager key for an output's name. NVS caps keys at 15
        // characters; "out7name" is well under.
        static void buildNameKey(uint8_t index, char *buffer, uint8_t bufferSize);

        Output outputs_[MAX_OUTPUTS] = {};
        uint8_t count_ = 0;
    };
}
