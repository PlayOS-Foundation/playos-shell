#include "installer_screen.h"

#include "raylib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool PressedUp()      { return IsKeyPressed(KEY_UP)    || IsKeyPressed(KEY_W);     }
static bool PressedDown()    { return IsKeyPressed(KEY_DOWN)  || IsKeyPressed(KEY_S);     }
static bool PressedConfirm() { return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE); }
static bool PressedBack()    { return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B);    }

// ── Detect internal disk ─────────────────────────────────────────────────────

void InstallerScreen::DetectDisk() {
    // Read /sys/block and find the first non-removable disk >= 4 GB.
    const char* candidates[] = {"nvme0n1", "sda", "vda", "mmcblk0", nullptr};

    for (int i = 0; candidates[i]; ++i) {
        std::string sysPath = std::string("/sys/block/") + candidates[i];
        std::string removablePath = sysPath + "/removable";
        std::string sizePath = sysPath + "/size";

        // Check if device exists
        std::ifstream szFile(sizePath);
        if (!szFile.is_open()) continue;

        // Skip removable
        std::ifstream remFile(removablePath);
        if (remFile.is_open()) {
            int removable = 0;
            remFile >> removable;
            if (removable == 1) continue;
        }

        // Check size (in 512-byte sectors; 4 GB = 8388608)
        unsigned long long sectors = 0;
        szFile >> sectors;
        if (sectors < 8388608) continue;

        unsigned long long gigabytes = sectors / (2 * 1024 * 1024); // approximate

        m_diskName = candidates[i];
        m_diskPath = std::string("/dev/") + candidates[i];
        m_diskSize = std::to_string(gigabytes) + " GB";
        return;
    }

    m_diskName = "(none found)";
    m_diskPath = "";
    m_diskSize = "N/A";
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

InstallerScreen::InstallerScreen(ScreenStack& stack) : m_stack(stack) {}

void InstallerScreen::OnEnter() {
    DetectDisk();
    m_selected = 1;  // Cancel is default
    m_installing = false;
    m_installTimer = 0.0f;
}

void InstallerScreen::Update(float dt) {
    if (m_installing) {
        m_installTimer += dt;
        return;
    }

    if (PressedUp())        m_selected = 0;
    if (PressedDown())      m_selected = 1;
    if (PressedBack())      { m_stack.Pop(); return; }

    if (PressedConfirm()) {
        if (m_selected == 0 && !m_diskPath.empty()) {
            StartInstall();
        } else {
            m_stack.Pop();
        }
    }
}

void InstallerScreen::StartInstall() {
    // Write target disk to flag file
    std::ofstream flag(kDiskFile);
    if (flag.is_open()) {
        flag << m_diskPath << "\n";
        flag.close();
    }

    // Start the installer service (stops compositor, runs install script, reboots)
    std::system("systemctl start playos-installer.service &");

    m_installing = true;
    m_installTimer = 0.0f;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void InstallerScreen::Draw(int W, int H) {
    // Dim background
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 200});

    if (m_installing) {
        // ── Installing state ────────────────────────────────────────────────
        const char* msg = "Installing PlayOS to internal disk...";
        int tw = MeasureText(msg, 36);
        DrawText(msg, (W - tw) / 2, H / 2 - 60, 36, RAYWHITE);

        const char* sub = "The system will reboot when complete.";
        tw = MeasureText(sub, 24);
        DrawText(sub, (W - tw) / 2, H / 2 - 10, 24, Color{140, 140, 180, 255});

        // Animated dots
        int dots = ((int)(m_installTimer * 2.0f)) % 4;
        char dotStr[] = "....";
        dotStr[dots] = '\0';
        tw = MeasureText(dotStr, 48);
        DrawText(dotStr, (W - tw) / 2, H / 2 + 40, 48, Color{100, 200, 100, 255});
        return;
    }

    // ── Panel ────────────────────────────────────────────────────────────────
    const int panW = 760, panH = 520;
    const int px = (W - panW) / 2, py = (H - panH) / 2;

    DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                         0.1f, 12, Color{18, 18, 28, 255});
    DrawRectangleRoundedLinesEx({(float)px, (float)py, (float)panW, (float)panH},
                                 0.1f, 12, 2.0f, Color{60, 60, 100, 255});

    // Title
    DrawText("INSTALL TO DISK", px + 40, py + 36, 48, Color{220, 80, 80, 255});
    DrawRectangle(px + 40, py + 100, panW - 80, 2, Color{40, 40, 70, 255});

    // Warning
    DrawText("WARNING: This will ERASE ALL DATA on the target disk.",
             px + 40, py + 130, 24, Color{255, 140, 60, 255});
    DrawText("There is NO undo. Make sure you have backups.",
             px + 40, py + 162, 22, Color{180, 100, 40, 255});

    // Disk info
    DrawText("Target disk:", px + 40, py + 210, 28, Color{140, 140, 180, 255});
    DrawText(TextFormat("%s  (%s)", m_diskPath.c_str(), m_diskSize.c_str()),
             px + 40, py + 248, 36, RAYWHITE);

    if (m_diskPath.empty()) {
        DrawText("No internal disk detected.",
                 px + 40, py + 300, 24, Color{255, 80, 80, 255});
    }

    // ── Buttons ──────────────────────────────────────────────────────────────
    int by = py + panH - 120;

    // Confirm button
    Color confBg = (m_selected == 0) ? Color{200, 40, 40, 255} : Color{60, 20, 20, 255};
    DrawRectangleRounded({(float)(px + 40), (float)by, 320.0f, 64.0f},
                         0.3f, 8, confBg);
    DrawText("ERASE & INSTALL", px + 60, by + 16, 30,
             (m_selected == 0) ? RAYWHITE : Color{140, 80, 80, 255});

    // Cancel button
    Color cancBg = (m_selected == 1) ? Color{40, 40, 60, 255} : Color{25, 25, 40, 255};
    DrawRectangleRounded({(float)(px + 400), (float)by, 320.0f, 64.0f},
                         0.3f, 8, cancBg);
    DrawText("CANCEL", px + 420, by + 16, 30,
             (m_selected == 1) ? RAYWHITE : Color{100, 100, 130, 255});

    // Help
    DrawText("[Up/Down] Choose    [Enter] Confirm    [Esc/B] Back",
             px + 40, py + panH - 38, 20, Color{80, 80, 100, 255});
}
