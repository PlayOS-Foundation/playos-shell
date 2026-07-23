// PlayOS Shell — LibraryScreen.
// The default home screen: game list, navigation, launch.
#pragma once

#include "../core/screen.h"
#include "../core/app_context.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct GameEntry {
    std::string title;
    std::string subtitle;
    std::string executable;
    std::vector<std::string> args;
};

class LibraryScreen : public IScreen {
public:
    // exeDir is passed so samples can be located relative to the shell binary.
    // onPushScreen is called when the screen wants to push another screen
    // (e.g. overlay on Home press).
    explicit LibraryScreen(const std::filesystem::path& exeDir,
                           AppContext& ctx);

    void OnEnter() override;
    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    AppContext&              m_ctx;
    std::vector<GameEntry>   m_library;
    int                      m_selected = 0;
    std::string              m_status;
};
