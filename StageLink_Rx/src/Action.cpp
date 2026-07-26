#include "Action.h"

const char *actionCommandName(ActionCommand command)
{
    switch (command)
    {
        case ActionCommand::Level:
            return "LEVEL";
        case ActionCommand::Color:
            return "COLOR";
        case ActionCommand::State:
            return "STATE";
        default:
            return "UNKNOWN";
    }
}
