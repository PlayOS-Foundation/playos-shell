// PlayOS Shell — ShellApp.
// Owns the window, screen stack, status bar, and main loop.
#pragma once

#include "core/screen_stack.h"
#include "core/app_context.h"
#include "ui/status_bar.h"
#include "ui/icons.h"
#include <filesystem>

class ShellApp {
public:
    // Run until the window is closed. Returns 0 on clean exit.
    int Run(int argc, char** argv);

private:
    Icons       m_icons;
    StatusBar   m_statusBar{m_icons};
    ScreenStack m_stack;
    AppContext  m_ctx{m_stack};
};
