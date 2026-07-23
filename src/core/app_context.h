// PlayOS Shell — AppContext.
// Flat struct bundling all services a screen needs.
// Passed to every IScreen via constructor; the single handle prevents
// constructor signature explosion as features are added.
#pragma once

#include "screen_stack.h"
#include "../ui/theme.h"

struct AppContext {
    ScreenStack& stack;
    Theme        theme{Theme::Dark()};
};
