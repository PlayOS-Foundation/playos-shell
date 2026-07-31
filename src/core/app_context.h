// PlayOS Shell — AppContext.
// Flat struct bundling all services a screen needs.
// Passed to every IScreen via constructor; the single handle prevents
// constructor signature explosion as features are added.
#pragma once

#include "screen_stack.h"
#include "../ui/theme.h"
#include "../ui/toast_manager.h"
#include "../ui/text_helpers.h"
#include "../audio/audio_manager.h"

struct AppContext {
    ScreenStack&   stack;
    ToastManager&  toasts;
    AudioManager&  audio;
    Font           textFont = {};
    Theme          theme{Theme::Dark()};
};
