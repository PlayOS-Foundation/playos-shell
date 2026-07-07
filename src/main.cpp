// PlayOS Shell — reference console UI (vertical slice).
//
// Proves the console loop from playos-spec:
//   Shell -> select game -> launch -> play -> return to Shell
//
// Rendering: Raylib (the reference engine).
// Platform services: the PlayOS Platform API (input, lifecycle).
// Launching: the PlayOS Runtime (LaunchAndWait).
//
// This is the Windows SDK-target slice. On a runtime device the shell is a
// Wayland client of the PlayOS compositor; here it is a plain window.

#include "raylib.h"

#include "playos/playos.h"
#include "playos/runtime/process.h"

#include <string>
#include <vector>

namespace {

struct GameEntry {
    std::string title;
    std::string executable;
    std::vector<std::string> args;
};

// Demo library for the slice. These are real Windows programs so we can prove
// launch + return-to-shell end to end. On a device these become PlayOS
// packages resolved by the runtime.
std::vector<GameEntry> DemoLibrary() {
    return {
        {"Demo App (Notepad)", "C:\\Windows\\System32\\notepad.exe", {}},
        {"Terminal Echo", "C:\\Windows\\System32\\cmd.exe",
         {"/c", "echo PlayOS launched me && pause"}},
    };
}

// Returns true if the user pressed "up" this frame (controller or keyboard).
bool PressedUp() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadUp) ||
           IsKeyPressed(KEY_UP);
}

bool PressedDown() {
    return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) ||
           IsKeyPressed(KEY_DOWN);
}

// Confirm = A button or Enter.
bool PressedConfirm() {
    return PlayOS::Input::Pressed(PlayOS::Button::A) ||
           IsKeyPressed(KEY_ENTER);
}

// Back = B button or Escape.
bool PressedBack() {
    return PlayOS::Input::Pressed(PlayOS::Button::B) ||
           IsKeyPressed(KEY_ESCAPE);
}

} // namespace

int main() {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "PlayOS Shell");
    SetTargetFPS(60);

    PlayOS::Lifecycle::Init();

    const auto library = DemoLibrary();
    int selected = 0;
    std::string status = "Ready. A = launch, B = quit.";

    while (!WindowShouldClose()) {
        // --- Update platform + input ---
        PlayOS::Lifecycle::Update();

        if (PressedUp()) {
            selected = (selected - 1 + (int)library.size()) % (int)library.size();
        }
        if (PressedDown()) {
            selected = (selected + 1) % (int)library.size();
        }

        bool launchRequested = PressedConfirm();
        bool quitRequested = PressedBack();

        if (quitRequested) {
            break;
        }

        // --- Draw the shell ---
        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});

        DrawText("PlayOS", 60, 48, 48, RAYWHITE);
        DrawText("Library", 60, 110, 24, Color{140, 140, 160, 255});

        for (int i = 0; i < (int)library.size(); ++i) {
            const int y = 170 + i * 56;
            const bool isSelected = (i == selected);
            if (isSelected) {
                DrawRectangle(50, y - 8, 700, 48, Color{40, 90, 200, 255});
            }
            DrawText(library[i].title.c_str(), 70, y,
                     28, isSelected ? RAYWHITE : Color{200, 200, 210, 255});
        }

        DrawText(status.c_str(), 60, screenHeight - 60, 20,
                 Color{150, 150, 170, 255});
        DrawText("Controller: D-Pad move, A launch, B quit  (Arrows/Enter/Esc also work)",
                 60, screenHeight - 34, 18, Color{90, 90, 110, 255});

        EndDrawing();

        // --- Launch (blocking): shell is hidden while the game runs, then we
        //     return to the shell when it exits. This mirrors the console loop.
        if (launchRequested) {
            const auto& game = library[selected];
            status = "Launching: " + game.title + " ...";

            // Draw one frame showing the launching status.
            BeginDrawing();
            ClearBackground(Color{10, 10, 14, 255});
            DrawText(status.c_str(), 60, screenHeight / 2 - 20, 28, RAYWHITE);
            EndDrawing();

            const auto result =
                PlayOS::Runtime::LaunchAndWait(game.executable, game.args);

            if (!result.launched) {
                status = "Failed to launch: " + game.title;
            } else {
                status = game.title + " exited with code " +
                         std::to_string(result.exitCode) + ". Back in shell.";
            }
        }
    }

    PlayOS::Lifecycle::Shutdown();
    CloseWindow();
    return 0;
}
