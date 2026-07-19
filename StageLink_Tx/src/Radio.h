#pragma once

class Radio
{
public:

    static bool begin();

    static bool sendMessage(
        const char* message
    );
    
    static bool isReceiverOnline();
};