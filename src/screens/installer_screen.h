// PlayOS Shell — InstallerScreen.
// Confirmation UI for installing PlayOS to the internal disk.
// On confirm, starts playos-installer.service which handles the actual
// disk partitioning, filesystem copy, bootloader install, and reboot.
#pragma once

#include "../screen.h"
#include "../screen_stack.h"

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

    std::string m_diskName;
    std::string m_diskPath;
    std::string m_diskSize;

    void DetectDisk();
    void StartInstall();
    static constexpr const char* kDiskFile = "/run/playos/install-target";
};
