#pragma once

#include "../screen.h"
#include "../screen_stack.h"
#include <string>

class InstallerScreen : public IScreen {
public:
    explicit InstallerScreen(ScreenStack& stack);

    void OnEnter() override;
    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    ScreenStack& m_stack;
    int m_selected = 1;        // 0 = Confirm, 1 = Cancel (default Cancel for safety)
    bool m_installing = false;
    float m_installTimer = 0.0f;
    float m_completeTimer = 0.0f;

    // Polled from /run/playos/install-status
    std::string m_statusStage = "starting";
    int m_statusPercent = 0;
    std::string m_statusError;

    std::string m_diskName;
    std::string m_diskPath;
    std::string m_diskSize;

    void DetectDisk();
    void StartInstall();
    static constexpr const char* kDiskFile = "/run/playos/install-target";
};
