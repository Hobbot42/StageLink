#include "Action.h"

const char *actionCommandName(ActionCommand command)
{
    switch (command)
    {
        case ActionCommand::Level:
            return "LEVEL";
        default:
            return "UNKNOWN";
    }
}
