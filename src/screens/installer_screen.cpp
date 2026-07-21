#include "installer_screen.h"

#include "raylib.h"
#include "playos/playos.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool PressedUp()      { return PlayOS::Input::Pressed(PlayOS::Button::DPadUp)   || IsKeyPressed(KEY_UP);    }
static bool PressedDown()    { return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) || IsKeyPressed(KEY_DOWN);  }
static bool PressedConfirm() { return PlayOS::Input::Pressed(PlayOS::Button::A)        || IsKeyPressed(KEY_ENTER); }
static bool PressedBack()    { return PlayOS::Input::Pressed(PlayOS::Button::B)        || IsKeyPressed(KEY_ESCAPE)
                                   || PlayOS::Input::Pressed(PlayOS::Button::Home); }

// ── Read install status file ─────────────────────────────────────────────────

static void ReadStatus(std::string& stage, int& percent, std::string& errorMsg) {
    stage = "starting";
    percent = 0;
    errorMsg.clear();

    std::ifstream f("/run/playos/install-status");
    if (!f.is_open()) return;

    std::string line;
    std::getline(f, line);

    auto colon = line.find(':');
    if (colon != std::string::npos) {
        stage = line.substr(0, colon);
        std::string rest = line.substr(colon + 1);
        if (stage == "installing") {
            percent = std::stoi(rest);
        } else if (stage == "failed") {
            errorMsg = rest;
        }
    } else {
        stage = line;
    }
}

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
        ReadStatus(m_statusStage, m_statusPercent, m_statusError);

        // On complete, wait a moment so user sees "Rebooting..." then reboot
        if (m_statusStage == "complete") {
            m_completeTimer += dt;
            if (m_completeTimer > 3.0f) {
                std::system("reboot -f || echo b > /proc/sysrq-trigger &");
            }
            return;
        }
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

    // Initialize status file
    std::ofstream sf("/run/playos/install-status");
    if (sf.is_open()) {
        sf << "starting\n";
        sf.close();
    }

    // Start the installer in background — standalone shell script
    // that writes progress stages to /run/playos/install-status.
    std::system("/usr/bin/playos-installer &");

    m_installing = true;
    m_installTimer = 0.0f;
    m_completeTimer = 0.0f;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void InstallerScreen::Draw(int W, int H) {
    // Dim background
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 200});

    if (m_installing) {
        // ── Installing state ────────────────────────────────────────────────
        const int barX = W / 2 - 300;
        const int barY = H / 2 - 30;
        const int barW = 600;
        const int barH = 40;

        // Stage label
        const char* stageLabel = "Preparing...";
        if (m_statusStage == "starting")         stageLabel = "Starting installer...";
        else if (m_statusStage == "updating_repos") stageLabel = "Updating package repositories...";
        else if (m_statusStage == "partitioning") stageLabel = "Partitioning disk...";
        else if (m_statusStage == "installing")  stageLabel = TextFormat("Installing system... %d%%", m_statusPercent);
        else if (m_statusStage == "configuring") stageLabel = "Configuring PlayOS services...";

        int tw = MeasureText(stageLabel, 32);
        DrawText(stageLabel, (W - tw) / 2, barY - 60, 32, RAYWHITE);

        // Subtitle
        const char* sub;
        if (m_statusStage == "failed") {
            sub = m_statusError.c_str();
        } else if (m_statusStage == "complete") {
            sub = "Rebooting in a moment...";
        } else {
            sub = m_diskPath.c_str();
        }
        tw = MeasureText(sub, 22);
        DrawText(sub, (W - tw) / 2, barY - 20, 22, Color{140, 140, 180, 255});

        // Progress bar background
        DrawRectangleRounded({(float)barX, (float)barY, (float)barW, (float)barH}, 0.3f, 8, Color{30, 30, 50, 255});
        DrawRectangleRoundedLines({(float)barX, (float)barY, (float)barW, (float)barH}, 0.3f, 8, 2.0f, Color{60, 60, 100, 255});

        // Determine progress
        float progress = 0.0f;
        if (m_statusStage == "complete") {
            progress = 1.0f;
        } else if (m_statusStage == "failed") {
            progress = (float)m_statusPercent / 100.0f;  // show partial
        } else if (m_statusStage == "installing") {
            progress = (float)m_statusPercent / 100.0f;
        } else if (m_statusStage == "configuring") {
            progress = 0.98f;
        } else if (m_statusStage == "partitioning") {
            progress = 0.02f;
        } else if (m_statusStage == "updating_repos") {
            progress = 0.01f;
        }

        if (progress > 0.0f) {
            Color barColor;
            if (m_statusStage == "failed") {
                barColor = Color{220, 60, 60, 255};
            } else if (m_statusStage == "complete") {
                barColor = Color{60, 200, 80, 255};
            } else {
                barColor = Color{80, 140, 240, 255};
            }
            DrawRectangleRounded({(float)barX + 2, (float)barY + 2,
                                  (float)barW * progress - 4, (float)barH - 4},
                                 0.3f, 8, barColor);

            // Percentage text inside bar
            int pct = (int)(progress * 100.0f);
            const char* pctText = TextFormat("%d%%", pct);
            tw = MeasureText(pctText, 24);
            DrawText(pctText, barX + barW/2 - tw/2, barY + 8, 24, RAYWHITE);
        }

        // Error dismiss button
        if (m_statusStage == "failed" && PressedConfirm()) {
            m_stack.Pop();
        }
        if (m_statusStage == "failed" && PressedBack()) {
            m_stack.Pop();
        }

        return;
    }

    // ── Panel ────────────────────────────────────────────────────────────────
    const int panW = 760, panH = 520;
    const int px = (W - panW) / 2, py = (H - panH) / 2;

    DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                         0.1f, 12, Color{18, 18, 28, 255});
    DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
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
