#include "installer_screen.h"

#include "raylib.h"
#include "playos/playos.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <glob.h>
#include <unistd.h>
#include <sys/wait.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static bool PressedUp()      { return PlayOS::Input::Pressed(PlayOS::Button::DPadUp)   || IsKeyPressed(KEY_UP);    }
static bool PressedDown()    { return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) || IsKeyPressed(KEY_DOWN);  }
static bool PressedConfirm() { return PlayOS::Input::Pressed(PlayOS::Button::A)        || IsKeyPressed(KEY_ENTER); }
static bool PressedBack()    { return PlayOS::Input::Pressed(PlayOS::Button::B)        || IsKeyPressed(KEY_ESCAPE)
                                   || PlayOS::Input::Pressed(PlayOS::Button::Home); }

// ── Detect internal disk ─────────────────────────────────────────────────────

void InstallerScreen::DetectDisk() {
    const char* candidates[] = {"nvme0n1", "sda", "vda", "mmcblk0", nullptr};

    for (int i = 0; candidates[i]; ++i) {
        std::string sysPath = std::string("/sys/block/") + candidates[i];
        std::string removablePath = sysPath + "/removable";
        std::string sizePath = sysPath + "/size";

        std::ifstream szFile(sizePath);
        if (!szFile.is_open()) continue;

        std::ifstream remFile(removablePath);
        if (remFile.is_open()) {
            int removable = 0;
            remFile >> removable;
            if (removable == 1) continue;
        }

        unsigned long long sectors = 0;
        szFile >> sectors;
        if (sectors < 8388608) continue;  // 4 GB minimum

        unsigned long long gigabytes = sectors / (2 * 1024 * 1024);

        m_diskName = candidates[i];
        m_diskPath = std::string("/dev/") + candidates[i];
        m_diskSize = std::to_string(gigabytes) + " GB";
        return;
    }

    m_diskName = "(none found)";
    m_diskPath = "";
    m_diskSize = "N/A";
}

// ── Find disk image ──────────────────────────────────────────────────────────

bool InstallerScreen::FindImage() {
    // 1. Explicit path written by pre-fetch step
    std::ifstream pathFile("/run/playos/disk-image");
    if (pathFile.is_open()) {
        std::getline(pathFile, m_imagePath);
        if (!m_imagePath.empty() && access(m_imagePath.c_str(), R_OK) == 0) {
            return true;
        }
    }

    // 2. Embedded in /usr/share/playos/
    glob_t gl;
    if (glob("/usr/share/playos/playos-gpt-*.img.zst", 0, nullptr, &gl) == 0 && gl.gl_pathc > 0) {
        m_imagePath = gl.gl_pathv[0];
        globfree(&gl);
        return true;
    }
    globfree(&gl);

    // 3. USB drives
    if (glob("/mnt/*/playos-gpt-*.img.zst", 0, nullptr, &gl) == 0 && gl.gl_pathc > 0) {
        m_imagePath = gl.gl_pathv[0];
        globfree(&gl);
        return true;
    }
    globfree(&gl);

    // 4. Media auto-mount
    if (glob("/media/*/playos-gpt-*.img.zst", 0, nullptr, &gl) == 0 && gl.gl_pathc > 0) {
        m_imagePath = gl.gl_pathv[0];
        globfree(&gl);
        return true;
    }
    globfree(&gl);

    return false;
}

// ── Parse dd progress from stderr file ──────────────────────────────────────

void InstallerScreen::ParseDDProgress() {
    std::ifstream f(kProgressFile);
    if (!f.is_open()) return;

    std::string lastLine, line;
    while (std::getline(f, line)) {
        if (!line.empty()) lastLine = line;
    }
    if (lastLine.empty()) return;

    size_t bytesPos = lastLine.find(" bytes");
    if (bytesPos != std::string::npos) {
        try { m_bytesWritten = std::stoll(lastLine.substr(0, bytesPos)); } catch (...) {}
    }
}

// ── Clean up child process ──────────────────────────────────────────────────

void InstallerScreen::CleanupChild() {
    if (m_childPid > 0) {
        int status;
        waitpid(m_childPid, &status, WNOHANG);
        m_childPid = -1;
    }
    unlink(kProgressFile);
}

// ── Run dd pipeline ─────────────────────────────────────────────────────────

void InstallerScreen::RunWriteStep() {
    std::string cmd = "zstd -d < \"" + m_imagePath + "\" | dd of=" + m_diskPath
                    + " bs=4M status=progress 2>" + kProgressFile;

    // Get uncompressed size
    FILE* sizeCheck = popen(("zstd -l \"" + m_imagePath + "\" 2>/dev/null | tail -1 | awk '{print $5}'").c_str(), "r");
    if (sizeCheck) {
        char buf[64] = {};
        if (fgets(buf, sizeof(buf), sizeCheck)) {
            try { m_totalBytes = std::stoll(buf); } catch (...) { m_totalBytes = 0; }
        }
        pclose(sizeCheck);
    }

    m_childPid = fork();
    if (m_childPid == 0) {
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(1);
    }
}

// ── Run partition resize steps ──────────────────────────────────────────────

void InstallerScreen::RunPartitionSteps() {
    switch (m_step) {
    case Step::RelocatingGPT: {
        int rc = std::system(("sgdisk -e " + m_diskPath).c_str());
        if (rc != 0) { m_errorMsg = "Failed to relocate GPT backup"; m_step = Step::Failed; return; }
        m_step = Step::ResizingPartition;
        m_stepProgress = 0.0f;
        return;
    }
    case Step::ResizingPartition: {
        int rc = std::system(("parted -s " + m_diskPath + " resizepart 2 100%").c_str());
        if (rc != 0) { m_errorMsg = "Failed to resize partition"; m_step = Step::Failed; return; }
        m_step = Step::ResizingFS;
        m_stepProgress = 0.5f;
        return;
    }
    case Step::ResizingFS: {
        std::string part2 = m_diskPath;
        if (m_diskPath.find("nvme") != std::string::npos ||
            m_diskPath.find("mmcblk") != std::string::npos) {
            part2 += "p2";
        } else {
            part2 += "2";
        }
        std::system(("resize2fs -p " + part2).c_str());
        m_step = Step::Rebooting;
        m_stepProgress = 1.0f;
        return;
    }
    default: break;
    }
}

// ── Start the install pipeline ──────────────────────────────────────────────

void InstallerScreen::StartInstall() {
    if (!FindImage()) {
        m_step = Step::Failed;
        m_errorMsg = "No PlayOS disk image found.\nPlace playos-gpt-*.img.zst on a USB drive\nor in /usr/share/playos/";
        m_installing = true;
        return;
    }

    m_installing = true;
    m_installTimer = 0.0f;
    m_completeTimer = 0.0f;
    m_step = Step::WritingImage;
    m_stepProgress = 0.0f;
    m_bytesWritten = 0;
    m_totalBytes = 0;

    std::ofstream pf(kProgressFile, std::ios::trunc);
    pf.close();

    RunWriteStep();
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

InstallerScreen::InstallerScreen(ScreenStack& stack) : m_stack(stack) {}

void InstallerScreen::OnEnter() {
    DetectDisk();
    FindImage();  // Check image availability for UI feedback
    m_selected = 1;
    m_installing = false;
    m_installTimer = 0.0f;
    m_step = Step::FindingImage;
    m_childPid = -1;
}

void InstallerScreen::Update(float dt) {
    if (m_installing) {
        m_installTimer += dt;

        if (m_step == Step::WritingImage) {
            int status;
            pid_t result = waitpid(m_childPid, &status, WNOHANG);

            if (result == m_childPid) {
                m_childPid = -1;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    m_step = Step::RelocatingGPT;
                    m_stepProgress = 1.0f;
                } else {
                    m_errorMsg = "Failed to write disk image.\nCheck /run/playos/dd-progress for details.";
                    m_step = Step::Failed;
                }
            } else if (result == -1) {
                m_errorMsg = "Install process terminated unexpectedly.";
                m_step = Step::Failed;
            } else {
                ParseDDProgress();
                if (m_totalBytes > 0) {
                    m_stepProgress = (float)m_bytesWritten / (float)m_totalBytes;
                    if (m_stepProgress > 1.0f) m_stepProgress = 1.0f;
                }
            }
            return;
        }

        if (m_step == Step::RelocatingGPT ||
            m_step == Step::ResizingPartition ||
            m_step == Step::ResizingFS) {
            RunPartitionSteps();
            return;
        }

        if (m_step == Step::Rebooting) {
            if (PressedUp() || PressedDown()) {
                m_rebootSelected = 1 - m_rebootSelected;
            }
            if (PressedConfirm()) {
                if (m_rebootSelected == 0) {
                    std::system("reboot -f || echo b > /proc/sysrq-trigger &");
                    m_step = Step::Done;
                } else {
                    CleanupChild();
                    m_installing = false;
                    m_step = Step::FindingImage;
                }
            }
            if (PressedBack()) {
                m_rebootSelected = 1;
            }
            return;
        }

        if (m_step == Step::Failed || m_step == Step::Done) return;
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

// ── Draw ─────────────────────────────────────────────────────────────────────

void InstallerScreen::Draw(int W, int H) {
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 200});

    // ── Progress states ─────────────────────────────────────────────────────
    if (m_installing && (m_step == Step::WritingImage ||
                         m_step == Step::RelocatingGPT ||
                         m_step == Step::ResizingPartition ||
                         m_step == Step::ResizingFS)) {

        const int barX = W / 2 - 300;
        const int barY = H / 2 - 30;
        const int barW = 600;
        const int barH = 40;

        const char* stageLabel = "Preparing...";
        if (m_step == Step::WritingImage) {
            if (m_totalBytes > 0) {
                int pct = (int)(m_stepProgress * 100.0f);
                stageLabel = TextFormat("Writing to disk... %d%%", pct);
            } else {
                stageLabel = "Writing system image to disk...";
            }
        } else if (m_step == Step::RelocatingGPT) {
            stageLabel = "Updating partition table...";
        } else if (m_step == Step::ResizingPartition) {
            stageLabel = "Expanding root partition...";
        } else if (m_step == Step::ResizingFS) {
            stageLabel = "Growing filesystem...";
        }

        int tw = MeasureText(stageLabel, 32);
        DrawText(stageLabel, (W - tw) / 2, barY - 60, 32, RAYWHITE);

        tw = MeasureText(m_diskPath.c_str(), 22);
        DrawText(m_diskPath.c_str(), (W - tw) / 2, barY - 20, 22, Color{140, 140, 180, 255});

        DrawRectangleRounded({(float)barX, (float)barY, (float)barW, (float)barH}, 0.3f, 8, Color{30, 30, 50, 255});
        DrawRectangleRoundedLines({(float)barX, (float)barY, (float)barW, (float)barH}, 0.3f, 8, 2.0f, Color{60, 60, 100, 255});

        float drawProgress = m_stepProgress;
        if (m_step > Step::WritingImage) {
            drawProgress = 0.5f + ((int)m_step - (int)Step::WritingImage) * 0.15f;
        }
        if (drawProgress > 0.001f) {
            float fillW = (float)barW * drawProgress - 4.0f;
            if (fillW < 0.0f) fillW = 0.0f;
            DrawRectangleRounded({(float)barX + 2, (float)barY + 2, fillW, (float)barH - 4},
                                 0.3f, 8, Color{80, 140, 240, 255});

            int pct = (int)(drawProgress * 100.0f);
            const char* pctText = TextFormat("%d%%", pct);
            tw = MeasureText(pctText, 24);
            DrawText(pctText, barX + barW / 2 - tw / 2, barY + 8, 24, RAYWHITE);
        }
        return;
    }

    // ── Complete state ──────────────────────────────────────────────────────
    if (m_installing && m_step == Step::Rebooting) {
        const int panW = 640, panH = 340;
        const int px = (W - panW) / 2, py = (H - panH) / 2;

        DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                             0.1f, 12, Color{18, 18, 28, 255});
        DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
                                   0.1f, 12, 2.0f, Color{60, 60, 100, 255});

        DrawText("INSTALLATION COMPLETE",
                 W / 2 - MeasureText("INSTALLATION COMPLETE", 40) / 2,
                 py + 40, 40, Color{80, 220, 120, 255});

        DrawText("PlayOS has been written to disk successfully.",
                 W / 2 - MeasureText("PlayOS has been written to disk successfully.", 22) / 2,
                 py + 100, 22, Color{180, 180, 200, 255});
        DrawText("Remove the PXE / installation media before rebooting.",
                 W / 2 - MeasureText("Remove the PXE / installation media before rebooting.", 20) / 2,
                 py + 130, 20, Color{140, 140, 160, 255});

        int by = py + panH - 100;

        Color rebBg = (m_rebootSelected == 0) ? Color{200, 40, 40, 255} : Color{60, 20, 20, 255};
        DrawRectangleRounded({(float)(px + 40), (float)by, 260.0f, 56.0f}, 0.3f, 8, rebBg);
        DrawText("Reboot Now", px + 60, by + 12, 28,
                 (m_rebootSelected == 0) ? RAYWHITE : Color{140, 80, 80, 255});

        Color stayBg = (m_rebootSelected == 1) ? Color{40, 40, 60, 255} : Color{25, 25, 40, 255};
        DrawRectangleRounded({(float)(px + 340), (float)by, 260.0f, 56.0f}, 0.3f, 8, stayBg);
        DrawText("Stay in Shell", px + 360, by + 12, 28,
                 (m_rebootSelected == 1) ? RAYWHITE : Color{100, 100, 130, 255});

        DrawText("[Up/Down] Choose    [Enter] Confirm",
                 W / 2 - MeasureText("[Up/Down] Choose    [Enter] Confirm", 20) / 2,
                 py + panH - 28, 20, Color{80, 80, 100, 255});
        return;
    }

    // ── Failed state ────────────────────────────────────────────────────────
    if (m_installing && m_step == Step::Failed) {
        DrawText("INSTALLATION FAILED",
                 W / 2 - MeasureText("INSTALLATION FAILED", 40) / 2,
                 H / 2 - 80, 40, Color{255, 80, 80, 255});

        int y = H / 2 - 20;
        std::istringstream errs(m_errorMsg);
        std::string errLine;
        while (std::getline(errs, errLine)) {
            int tw = MeasureText(errLine.c_str(), 20);
            DrawText(errLine.c_str(), (W - tw) / 2, y, 20, Color{200, 140, 140, 255});
            y += 26;
        }

        DrawText("Press ENTER or ESC to dismiss",
                 W / 2 - MeasureText("Press ENTER or ESC to dismiss", 22) / 2,
                 H / 2 + 100, 22, Color{180, 180, 200, 255});

        if (PressedConfirm() || PressedBack()) {
            CleanupChild();
            m_stack.Pop();
        }
        return;
    }

    // ── Confirmation panel ──────────────────────────────────────────────────
    const int panW = 760, panH = 560;
    const int px = (W - panW) / 2, py = (H - panH) / 2;

    DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                         0.1f, 12, Color{18, 18, 28, 255});
    DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
                               0.1f, 12, 2.0f, Color{60, 60, 100, 255});

    DrawText("INSTALL TO DISK", px + 40, py + 36, 48, Color{220, 80, 80, 255});
    DrawRectangle(px + 40, py + 100, panW - 80, 2, Color{40, 40, 70, 255});

    DrawText("WARNING: This will ERASE ALL DATA on the target disk.",
             px + 40, py + 130, 24, Color{255, 140, 60, 255});
    DrawText("There is NO undo. Make sure you have backups.",
             px + 40, py + 162, 22, Color{180, 100, 40, 255});

    DrawText("Target disk:", px + 40, py + 210, 28, Color{140, 140, 180, 255});
    DrawText(TextFormat("%s  (%s)", m_diskPath.c_str(), m_diskSize.c_str()),
             px + 40, py + 248, 36, RAYWHITE);

    if (!m_diskPath.empty() && !m_imagePath.empty()) {
        DrawText(TextFormat("Image: %s", m_imagePath.c_str()),
                 px + 40, py + 300, 22, Color{100, 160, 100, 255});
    } else if (!m_diskPath.empty()) {
        DrawText("No disk image found. Place playos-gpt-*.img.zst on USB.",
                 px + 40, py + 300, 22, Color{255, 140, 60, 255});
    }

    if (m_diskPath.empty()) {
        DrawText("No internal disk detected.",
                 px + 40, py + 300, 24, Color{255, 80, 80, 255});
    }

    int by = py + panH - 120;

    Color confBg = (m_selected == 0) ? Color{200, 40, 40, 255} : Color{60, 20, 20, 255};
    DrawRectangleRounded({(float)(px + 40), (float)by, 320.0f, 64.0f}, 0.3f, 8, confBg);
    DrawText("ERASE & INSTALL", px + 60, by + 16, 30,
             (m_selected == 0) ? RAYWHITE : Color{140, 80, 80, 255});

    Color cancBg = (m_selected == 1) ? Color{40, 40, 60, 255} : Color{25, 25, 40, 255};
    DrawRectangleRounded({(float)(px + 400), (float)by, 320.0f, 64.0f}, 0.3f, 8, cancBg);
    DrawText("CANCEL", px + 420, by + 16, 30,
             (m_selected == 1) ? RAYWHITE : Color{100, 100, 130, 255});

    DrawText("[Up/Down] Choose    [Enter] Confirm    [Esc/B] Back",
             px + 40, py + panH - 38, 20, Color{80, 80, 100, 255});
}
