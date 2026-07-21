// PlayOS Installer — Standalone raylib GUI app for disk installation.
// Spawned by playos-shell's overlay menu via: playos-installer-gui
//
// Flow:
//   1. Detect internal disk, show confirmation screen
//   2. On confirm: spawn install script, poll progress from status file
//   3. Show animated progress bar with stage labels
//   4. On complete: show "Rebooting...", reboot
//   5. On failure: show error, allow dismiss

#include "raylib.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

// ── Helpers ──────────────────────────────────────────────────────────────────

static std::string g_diskName;
static std::string g_diskPath;
static std::string g_diskSize;

static void DetectDisk() {
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

        g_diskName = candidates[i];
        g_diskPath = std::string("/dev/") + candidates[i];
        g_diskSize = std::to_string(gigabytes) + " GB";
        return;
    }

    g_diskName = "(none found)";
    g_diskPath = "";
    g_diskSize = "N/A";
}

// ── Progress polling ────────────────────────────────────────────────────────

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
            try { percent = std::stoi(rest); } catch (...) { percent = 0; }
        } else if (stage == "failed") {
            errorMsg = rest;
        }
    } else {
        stage = line;
    }
}

static float GetProgress(const std::string& stage, int percent) {
    if (stage == "complete") return 1.0f;
    if (stage == "configuring") return 0.98f;
    if (stage == "installing") return (float)percent / 100.0f;
    if (stage == "partitioning") return 0.02f;
    if (stage == "updating_repos") return 0.01f;
    return 0.0f;
}

static const char* GetStageLabel(const std::string& stage, int percent) {
    if (stage == "starting")         return "Starting installer...";
    if (stage == "updating_repos")   return "Updating package repositories...";
    if (stage == "partitioning")     return "Partitioning disk...";
    if (stage == "installing")       return TextFormat("Installing system... %d%%", percent);
    if (stage == "configuring")      return "Configuring PlayOS services...";
    if (stage == "complete")         return "Installation complete!";
    return "Preparing...";
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    DetectDisk();

    const int screenWidth = 1280;
    const int screenHeight = 800;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "PlayOS Installer");
    SetTargetFPS(60);

    // States
    enum State { CONFIRM, INSTALLING, DONE, FAILED };
    State state = CONFIRM;
    bool hasDisk = !g_diskPath.empty();
    // selected: 0 = Install (only reachable if hasDisk), 1 = Cancel
    int selected = 1;
    bool firstFrame = true;

    // Install state
    std::string statusStage = "starting";
    int statusPercent = 0;
    std::string statusError;
    pid_t scriptPid = -1;
    float completeTimer = 0.0f;

    while (!WindowShouldClose()) {
        int W = GetScreenWidth();
        int H = GetScreenHeight();
        float dt = GetFrameTime();

        BeginDrawing();
        ClearBackground(Color{8, 8, 16, 255});

        // ── CONFIRM STATE ──────────────────────────────────────────────────
        if (state == CONFIRM) {
            if (firstFrame) { selected = 1; firstFrame = false; }

            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
                if (hasDisk) selected = 0;
            }
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) selected = 1;
            if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B)) { CloseWindow(); return 0; }

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
                if (selected == 0 && hasDisk) {
                    // Start the install
                    std::ofstream flag("/run/playos/install-target");
                    if (flag.is_open()) {
                        flag << g_diskPath << "\n";
                        flag.close();
                    }
                    std::ofstream sf("/run/playos/install-status");
                    if (sf.is_open()) { sf << "starting\n"; sf.close(); }

                    // Fork the install script
                    scriptPid = fork();
                    if (scriptPid == 0) {
                        execl("/usr/bin/playos-installer", "playos-installer", nullptr);
                        _exit(1);
                    }
                    state = INSTALLING;
                } else {
                    CloseWindow();
                    return 0;
                }
            }

            // Panel
            const int panW = 760, panH = 500;
            const int px = (W - panW) / 2, py = (H - panH) / 2;

            DrawRectangleRounded({(float)px, (float)py, (float)panW, (float)panH},
                                 0.1f, 12, Color{18, 18, 28, 255});
            DrawRectangleRoundedLines({(float)px, (float)py, (float)panW, (float)panH},
                                       0.1f, 12, Color{60, 60, 100, 255});

            DrawText("INSTALL TO DISK", px + 40, py + 36, 48, Color{220, 80, 80, 255});
            DrawRectangle(px + 40, py + 100, panW - 80, 2, Color{40, 40, 70, 255});

            DrawText("WARNING: This will ERASE ALL DATA on the target disk.",
                     px + 40, py + 130, 24, Color{255, 140, 60, 255});
            DrawText("There is NO undo. Make sure you have backups.",
                     px + 40, py + 162, 22, Color{180, 100, 40, 255});

            DrawText("Target disk:", px + 40, py + 210, 28, Color{140, 140, 180, 255});

            if (hasDisk) {
                DrawText(TextFormat("%s  (%s)", g_diskPath.c_str(), g_diskSize.c_str()),
                         px + 40, py + 248, 36, RAYWHITE);
            } else {
                DrawText("No internal disk detected.",
                         px + 40, py + 248, 24, Color{255, 80, 80, 255});
            }

            // Buttons
            int by = py + panH - 120;

            if (hasDisk) {
                Color confBg = (selected == 0) ? Color{200, 40, 40, 255} : Color{60, 20, 20, 255};
                DrawRectangleRounded({(float)(px + 40), (float)by, 320.0f, 64.0f}, 0.3f, 8, confBg);
                DrawText("ERASE & INSTALL", px + 60, by + 16, 30,
                         (selected == 0) ? RAYWHITE : Color{140, 80, 80, 255});
            } else {
                DrawRectangleRounded({(float)(px + 40), (float)by, 320.0f, 64.0f}, 0.3f, 8,
                                     Color{30, 30, 40, 255});
                DrawText("No disk found", px + 60, by + 16, 30, Color{100, 100, 120, 255});
            }

            Color cancBg = (selected == 1) ? Color{40, 40, 60, 255} : Color{25, 25, 40, 255};
            DrawRectangleRounded({(float)(px + 400), (float)by, 320.0f, 64.0f}, 0.3f, 8, cancBg);
            DrawText("CANCEL", px + 420, by + 16, 30,
                     (selected == 1) ? RAYWHITE : Color{100, 100, 130, 255});

            DrawText("[Up/Down] Choose    [Enter] Confirm    [Esc/B] Quit",
                     px + 40, py + panH - 38, 20, Color{80, 80, 100, 255});
        }

        // ── INSTALLING STATE ───────────────────────────────────────────────
        else if (state == INSTALLING) {
            ReadStatus(statusStage, statusPercent, statusError);

            if (statusStage == "failed") {
                state = FAILED;
                continue;
            }
            if (statusStage == "complete") {
                state = DONE;
                completeTimer = 0.0f;
                continue;
            }

            const int barX = W / 2 - 300;
            const int barY = H / 2 - 30;
            const int barW = 600;

            // Stage label
            const char* label = GetStageLabel(statusStage, statusPercent);
            int tw = MeasureText(label, 32);
            DrawText(label, (W - tw) / 2, barY - 60, 32, RAYWHITE);

            // Disk target
            tw = MeasureText(g_diskPath.c_str(), 22);
            DrawText(g_diskPath.c_str(), (W - tw) / 2, barY - 20, 22, Color{140, 140, 180, 255});

            // Progress bar background
            DrawRectangleRounded({(float)barX, (float)barY, (float)barW, 40.0f},
                                 0.3f, 8, Color{30, 30, 50, 255});
            DrawRectangleRoundedLines({(float)barX, (float)barY, (float)barW, 40.0f},
                                       0.3f, 8, Color{60, 60, 100, 255});

            float progress = GetProgress(statusStage, statusPercent);
            if (progress > 0.001f) {
                float fillW = (float)barW * progress - 4.0f;
                if (fillW < 0.0f) fillW = 0.0f;
                DrawRectangleRounded({(float)barX + 2, (float)barY + 2,
                                      fillW, 36.0f},
                                     0.3f, 8, Color{80, 140, 240, 255});

                int pct = (int)(progress * 100.0f);
                const char* pctText = TextFormat("%d%%", pct);
                tw = MeasureText(pctText, 24);
                DrawText(pctText, barX + barW / 2 - tw / 2, barY + 8, 24, RAYWHITE);
            }

            // Check if script exited unexpectedly
            if (scriptPid > 0) {
                int status;
                pid_t result = waitpid(scriptPid, &status, WNOHANG);
                if (result == scriptPid && WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    // Script exited but status file may already show failed/complete
                }
            }
        }

        // ── DONE STATE ─────────────────────────────────────────────────────
        else if (state == DONE) {
            completeTimer += dt;

            const int barX = W / 2 - 300;
            const int barY = H / 2 - 30;
            const int barW = 600;

            DrawText("Installation complete!", W / 2 - MeasureText("Installation complete!", 36) / 2,
                     barY - 60, 36, Color{80, 220, 120, 255});
            DrawText("Rebooting in a moment...", W / 2 - MeasureText("Rebooting in a moment...", 28) / 2,
                     barY - 10, 28, Color{140, 140, 180, 255});

            // Full green bar
            DrawRectangleRounded({(float)barX, (float)barY, (float)barW, 40.0f},
                                 0.3f, 8, Color{30, 30, 50, 255});
            DrawRectangleRoundedLines({(float)barX, (float)barY, (float)barW, 40.0f},
                                       0.3f, 8, Color{60, 60, 100, 255});
            DrawRectangleRounded({(float)(barX + 2), (float)(barY + 2), (float)(barW - 4), 36.0f},
                                 0.3f, 8, Color{60, 200, 80, 255});
            DrawText("100%", barX + barW / 2 - 40, barY + 8, 24, RAYWHITE);

            if (completeTimer > 3.0f) {
                system("reboot -f || echo b > /proc/sysrq-trigger &");
                completeTimer = 0.0f;  // Don't keep calling reboot
            }
        }

        // ── FAILED STATE ───────────────────────────────────────────────────
        else if (state == FAILED) {
            DrawText("INSTALLATION FAILED", W / 2 - MeasureText("INSTALLATION FAILED", 40) / 2,
                     H / 2 - 80, 40, Color{255, 80, 80, 255});

            if (!statusError.empty()) {
                int tw = MeasureText(statusError.c_str(), 24);
                DrawText(statusError.c_str(), (W - tw) / 2, H / 2 - 20, 24, Color{200, 140, 140, 255});
            }

            DrawText("Check /var/log/playos-install.log for details",
                     W / 2 - MeasureText("Check /var/log/playos-install.log for details", 20) / 2,
                     H / 2 + 20, 20, Color{140, 140, 180, 255});

            DrawText("Press ENTER or ESC to exit",
                     W / 2 - MeasureText("Press ENTER or ESC to exit", 22) / 2,
                     H / 2 + 70, 22, Color{180, 180, 200, 255});

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
                CloseWindow();
                return 1;
            }
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
