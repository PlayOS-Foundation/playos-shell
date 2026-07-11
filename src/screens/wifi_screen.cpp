#include "wifi_screen.h"

#include "raylib.h"
#include "playos/playos.h"

#include <algorithm>
#include <string>

// ── Input helpers ─────────────────────────────────────────────────────────

static bool NavUp()      { return PlayOS::Input::Pressed(PlayOS::Button::DPadUp)   || IsKeyPressed(KEY_UP);     }
static bool NavDown()    { return PlayOS::Input::Pressed(PlayOS::Button::DPadDown) || IsKeyPressed(KEY_DOWN);   }
static bool NavConfirm() { return PlayOS::Input::Pressed(PlayOS::Button::A)        || IsKeyPressed(KEY_ENTER);  }
static bool NavBack()    { return PlayOS::Input::Pressed(PlayOS::Button::B)        || IsKeyPressed(KEY_ESCAPE); }
static bool NavX()       { return PlayOS::Input::Pressed(PlayOS::Button::X)        || IsKeyPressed(KEY_R);      }

// ── Typed-character helper (belt-and-suspenders for Wayland modifier handling) ──

void WiFiScreen::AppendTypedChar() {
    if (m_password.size() >= 63) return;

    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    // Map key → char (lowercase) → shifted char (US keyboard layout)
    struct Mapping { int key; char lo; char hi; };
    static const Mapping map[] = {
        // Letters
        {KEY_A, 'a', 'A'}, {KEY_B, 'b', 'B'}, {KEY_C, 'c', 'C'},
        {KEY_D, 'd', 'D'}, {KEY_E, 'e', 'E'}, {KEY_F, 'f', 'F'},
        {KEY_G, 'g', 'G'}, {KEY_H, 'h', 'H'}, {KEY_I, 'i', 'I'},
        {KEY_J, 'j', 'J'}, {KEY_K, 'k', 'K'}, {KEY_L, 'l', 'L'},
        {KEY_M, 'm', 'M'}, {KEY_N, 'n', 'N'}, {KEY_O, 'o', 'O'},
        {KEY_P, 'p', 'P'}, {KEY_Q, 'q', 'Q'}, {KEY_R, 'r', 'R'},
        {KEY_S, 's', 'S'}, {KEY_T, 't', 'T'}, {KEY_U, 'u', 'U'},
        {KEY_V, 'v', 'V'}, {KEY_W, 'w', 'W'}, {KEY_X, 'x', 'X'},
        {KEY_Y, 'y', 'Y'}, {KEY_Z, 'z', 'Z'},
        // Numbers
        {KEY_ZERO,  '0', ')'}, {KEY_ONE,   '1', '!'},
        {KEY_TWO,   '2', '@'}, {KEY_THREE, '3', '#'},
        {KEY_FOUR,  '4', '$'}, {KEY_FIVE,  '5', '%'},
        {KEY_SIX,   '6', '^'}, {KEY_SEVEN, '7', '&'},
        {KEY_EIGHT, '8', '*'}, {KEY_NINE,  '9', '('},
        // Punctuation
        {KEY_SPACE,        ' ',  ' '},
        {KEY_PERIOD,       '.',  '>'},
        {KEY_COMMA,        ',',  '<'},
        {KEY_SLASH,        '/',  '?'},
        {KEY_SEMICOLON,    ';',  ':'},
        {KEY_EQUAL,        '=',  '+'},
        {KEY_MINUS,        '-',  '_'},
        {KEY_APOSTROPHE,   '\'', '"'},
        {KEY_LEFT_BRACKET, '[',  '{'},
        {KEY_RIGHT_BRACKET,']',  '}'},
        {KEY_BACKSLASH,    '\\', '|'},
        {KEY_GRAVE,        '`',  '~'},
    };

    for (const auto& m : map) {
        if (IsKeyPressed(m.key)) {
            char c = shift ? m.hi : m.lo;
            m_password += c;
            return;  // one char per frame
        }
    }
}

// ── WiFiScreen ────────────────────────────────────────────────────────────

WiFiScreen::WiFiScreen(ScreenStack& stack) : m_stack(stack) {}

void WiFiScreen::OnEnter() {
    m_state   = State::Scanning;
    m_password.clear();
    m_resultMsg.clear();
}

void WiFiScreen::Update(float dt) {
    switch (m_state) {
    // ── Scanning: execute on first update so "Scanning..." renders first ──
    case State::Scanning:
        DoScan();
        m_state = State::List;
        break;

    // ── List: navigate, connect, rescan, back ──────────────────────────────
    case State::List:
        if (NavBack()) { m_stack.Pop(); return; }
        if (!m_networks.empty()) {
            if (NavUp())
                m_selected = std::max(0, m_selected - 1);
            if (NavDown())
                m_selected = std::min((int)m_networks.size() - 1, m_selected + 1);
            if (NavConfirm()) {
                m_password.clear();
                if (m_networks[m_selected].secured)
                    m_state = State::EnterPass;
                else
                    DoConnect();
            }
        }
        if (NavX()) {
            m_state = State::Scanning;
        }
        break;

    // ── Password entry: keyboard input, Enter = connect, Esc = back ───────
    case State::EnterPass: {
        if (NavBack()) { m_state = State::List; return; }

        // Read typed characters (GLFW char callback — works when compositor
        // properly forwards xkb modifiers to the Wayland client).
        int ch;
        bool gotChar = false;
        while ((ch = GetCharPressed()) > 0) {
            if (ch >= 32 && ch < 127 && m_password.size() < 63) {
                m_password += (char)ch;
                gotChar = true;
            }
        }

        // Fallback: raw key detection with Shift awareness for US layout.
        // Only fires when GLFW didn't produce a character (e.g. compositor
        // modifier handling is broken).
        if (!gotChar)
            AppendTypedChar();

        // Backspace
        if (IsKeyPressed(KEY_BACKSPACE) && !m_password.empty())
            m_password.pop_back();

        if (IsKeyPressed(KEY_ENTER) && !m_password.empty())
            DoConnect();
        break;
    }

    // ── Connecting: wait (blocking DoConnect handles this state) ──────────
    case State::Connecting:
        break;

    // ── Result: auto-dismiss after 3 seconds or on any button ─────────────
    case State::Result:
        m_resultTimer -= dt;
        if (m_resultTimer <= 0.0f || NavConfirm() || NavBack())
            m_state = State::List;
        break;
    }
}

void WiFiScreen::Draw(int W, int H) {
    ClearBackground(Color{12, 12, 18, 255});

    // Header
    DrawText("WiFi", 160, 100, 52, Color{180, 180, 220, 255});
    DrawText("[B] Back    [X] Rescan", W - 500, 108, 28, Color{80, 80, 100, 255});
    DrawRectangle(0, 160, W, 2, Color{40, 40, 60, 255});

    switch (m_state) {
    // ── Scanning ──────────────────────────────────────────────────────────
    case State::Scanning: {
        const char* msg = "Scanning for networks...";
        DrawText(msg, (W - MeasureText(msg, 40)) / 2, H / 2 - 20, 40,
                 Color{140, 140, 180, 255});
        break;
    }

    // ── List ──────────────────────────────────────────────────────────────
    case State::List: {
        if (m_networks.empty()) {
            const char* msg = "No networks found.  Press X to rescan.";
            DrawText(msg, (W - MeasureText(msg, 36)) / 2, H / 2 - 18, 36,
                     Color{120, 120, 140, 255});
            break;
        }

        constexpr int kRowH    = 100;
        constexpr int kTop     = 200;
        constexpr int kLeft    = 80;


        for (int i = 0; i < (int)m_networks.size(); ++i) {
            const int y = kTop + i * kRowH;
            if (y + kRowH > H - 100) break; // don't draw off-screen
            const bool sel = (i == m_selected);
            const auto& n = m_networks[i];

            if (sel)
                DrawRectangleRounded({(float)kLeft, (float)y + 4,
                                      (float)(W - 2 * kLeft), (float)kRowH - 8},
                                     0.2f, 8, Color{44, 52, 68, 255});

            // SSID
            Color nameCol = sel ? RAYWHITE : Color{200, 200, 210, 255};
            if (n.active) nameCol = Color{80, 200, 80, 255};
            DrawText(n.ssid.c_str(), kLeft + 20, y + 24, 36, nameCol);

            // Lock icon or "open"
            const char* secLabel = n.secured ? "secured" : "open";
            DrawText(secLabel, kLeft + 20, y + 66, 22, Color{100, 100, 120, 255});

            // Signal bars
            DrawSignalBars(W - kLeft - 100, y + 20, n.signal, n.active);
        }

        // Help
        DrawText("[A] Connect    [X] Rescan",
                 160, H - 70, 28, Color{80, 80, 100, 255});
        break;
    }

    // ── Password entry ────────────────────────────────────────────────────
    case State::EnterPass: {
        const auto& n = m_networks[m_selected];
        DrawText(TextFormat("Password for \"%s\":", n.ssid.c_str()),
                 160, H / 2 - 120, 36, Color{180, 180, 220, 255});

        // Password field
        DrawRectangleRounded({(float)(W / 2 - 400), (float)(H / 2 - 40),
                               800.0f, 80.0f},
                             0.2f, 8, Color{30, 30, 50, 255});
        DrawRectangleRoundedLinesEx({(float)(W / 2 - 400), (float)(H / 2 - 40),
                                     800.0f, 80.0f},
                                    0.2f, 8, 2.0f, Color{80, 80, 140, 255});

        // Show masked or clear text
        std::string display = m_passVisible ? m_password
                            : std::string(m_password.size(), '*');
        display += (int)(GetTime() * 2) % 2 ? "|" : " "; // cursor blink
        DrawText(display.c_str(), W / 2 - 380, H / 2 - 20, 36,
                 Color{220, 220, 240, 255});

        DrawText("[Enter] Connect    [Esc] Cancel    [Tab] Show/hide",
                 160, H / 2 + 80, 26, Color{80, 80, 100, 255});

        if (IsKeyPressed(KEY_TAB)) m_passVisible = !m_passVisible;
        break;
    }

    // ── Connecting ────────────────────────────────────────────────────────
    case State::Connecting: {
        const auto& n = m_networks[m_selected];
        const char* msg = TextFormat("Connecting to \"%s\"...", n.ssid.c_str());
        DrawText(msg, (W - MeasureText(msg, 36)) / 2, H / 2 - 18, 36,
                 Color{140, 140, 200, 255});
        break;
    }

    // ── Result ────────────────────────────────────────────────────────────
    case State::Result: {
        Color col = (m_resultMsg.find("Connected") != std::string::npos)
                  ? Color{80, 200, 80, 255} : Color{220, 80, 80, 255};
        DrawText(m_resultMsg.c_str(),
                 (W - MeasureText(m_resultMsg.c_str(), 40)) / 2,
                 H / 2 - 20, 40, col);
        break;
    }
    }
}

void WiFiScreen::DrawSignalBars(int x, int y, int signal, bool active) const {
    // 4 bars, each taller than the last
    const int barW = 14, gap = 6;
    const int bars = signal >= 75 ? 4 : signal >= 50 ? 3
                   : signal >= 25 ? 2 : signal > 0 ? 1 : 0;
    Color onCol  = active ? Color{80, 200, 80, 255} : Color{100, 140, 220, 255};
    Color offCol = Color{40, 40, 60, 255};
    for (int b = 0; b < 4; ++b) {
        const int bh = 10 + b * 10;
        Color c = (b < bars) ? onCol : offCol;
        DrawRectangle(x + b * (barW + gap), y + 40 - bh, barW, bh, c);
    }
}

void WiFiScreen::DoScan() {
    m_networks = PlayOS::Network::ScanNetworks();
    m_selected = 0;
    // Move connected network to top
    for (int i = 0; i < (int)m_networks.size(); ++i) {
        if (m_networks[i].active) { m_selected = i; break; }
    }
}

void WiFiScreen::DoConnect() {
    if (m_networks.empty()) return;
    m_state = State::Connecting;

    // Force a draw frame so "Connecting..." shows
    const int W = GetScreenWidth(), H = GetScreenHeight();
    BeginDrawing();
    Draw(W, H);
    EndDrawing();

    auto res = PlayOS::Network::Connect(m_networks[m_selected].ssid, m_password);

    switch (res) {
    case PlayOS::Network::ConnectResult::Success:
        m_resultMsg = TextFormat("Connected to \"%s\"!",
                                  m_networks[m_selected].ssid.c_str());
        // Refresh list so the active flag updates
        DoScan();
        break;
    case PlayOS::Network::ConnectResult::WrongPassword:
        m_resultMsg = "Wrong password. Try again.";
        break;
    case PlayOS::Network::ConnectResult::Timeout:
        m_resultMsg = "Connection timed out.";
        break;
    case PlayOS::Network::ConnectResult::Error:
        m_resultMsg = "Connection failed.";
        break;
    }

    m_state = State::Result;
    m_resultTimer = 3.0f;
}
