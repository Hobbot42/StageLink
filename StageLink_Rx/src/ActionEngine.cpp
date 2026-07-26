#include "ActionEngine.h"

void ActionEngine::executeActions(
    const Action *actions,
    uint8_t actionCount,
    StageLink::OutputManager &outputManager
)
{
    for (uint8_t i = 0; i < actionCount; ++i)
    {
        const Action &action = actions[i];

        switch (action.command)
        {
            case ActionCommand::Level:
                outputManager.update(action.outputId, action.value);
                break;

            case ActionCommand::Color:
            case ActionCommand::State:
                break; // recognized, not implemented yet - see Action.h
        }
    }
}
