// PlayOS Shell — ShellApp.
// Owns the window, screen stack, status bar, and main loop.
#pragma once

#include "core/screen_stack.h"
#include "core/app_context.h"
#include "ui/status_bar.h"
#include "ui/toast_manager.h"
#include "ui/icons.h"
#include "audio/audio_manager.h"
#include <filesystem>

class ShellApp {
public:
    // Run until the window is closed. Returns 0 on clean exit.
    int Run(int argc, char** argv);

private:
    Icons         m_icons;
    StatusBar     m_statusBar{m_icons};
    ScreenStack   m_stack;
    ToastManager  m_toastManager;
    AudioManager  m_audioManager;
    Font          m_textFont = {};
    AppContext    m_ctx{m_stack, m_toastManager, m_audioManager};
};
