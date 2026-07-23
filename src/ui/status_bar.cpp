#include "status_bar.h"
#include "theme.h"

#include "raylib.h"
#include "playos/playos.h"

void StatusBar::Update(float dt) {
    m_pollTimer += dt;
    if (m_pollTimer >= kPollIntervalSec || m_pollTimer == dt) {
        // == dt means first frame — poll immediately
        m_pollTimer = 0.0f;
        Poll();
    }
    // Controller is polled every frame (instant feedback)
    m_controllerOn = PlayOS::Input::ControllerConnected();
}

void StatusBar::Poll() {
    using namespace PlayOS;

    m_hasBattery = Capabilities::Has(Capability::Battery);
    if (m_hasBattery) {
        m_battLevel    = Battery::Level();
        m_battCharging = Battery::IsCharging();
    }

    m_hasBluetooth = Capabilities::Has(Capability::BluetoothPresent);

    const auto wifi = Network::GetWiFiState();
    m_wifiState = (wifi == Network::WiFiState::Connected)  ? 2
                : (wifi == Network::WiFiState::Connecting) ? 1
                                                           : 0;

    const char* ip = Network::PrimaryIP();
    m_ip = (ip && ip[0] != '\0') ? ip : "";
}

void StatusBar::Draw(int W, int H, const Theme& theme) const {
    // ── top bar ──────────────────────────────────────────────────────────
    DrawRectangle(0, 0, W, 72, theme.statusBarBg);

    // Device name from profile (e.g. "ASUS ROG Ally") next to "PlayOS"
    if (!m_deviceName.empty()) {
        DrawText(TextFormat("PlayOS  ·  %s", m_deviceName.c_str()),
                 32, 16, 32, theme.textPrimary);
    } else {
        DrawText("PlayOS", 32, 16, 40, theme.textPrimary);
    }

    if (m_controllerOn) {
        DrawCircle(W - 200, 36, 10, theme.success);
        DrawText("Controller", W - 176, 16, 32, theme.textSecondary);
    } else {
        DrawText("No controller", W - 260, 16, 32, theme.inactive);
    }

    // ── bottom-right indicators ───────────────────────────────────────────

    // Battery
    if (m_hasBattery && m_battLevel >= 0.0f) {
        const int pct = (int)(m_battLevel * 100.0f + 0.5f);
        Color col = m_battLevel > 0.3f ? theme.success
                  : m_battLevel > 0.1f ? theme.warning
                                       : theme.danger;
        const char* pctStr  = TextFormat("%d%%", pct);
        const char* glyph   = m_battCharging ? Icons::BatteryCharge : Icons::Battery;
        const char* fallback = m_battCharging ? "~" : "";
        const float iw  = m_icons.Measure(glyph, fallback, 44);
        const int   pctW = MeasureText(pctStr, 28);
        float rx = (float)(W) - iw - pctW - 52;
        m_icons.Draw(glyph, fallback, rx, (float)(H - 148), 44, col);
        DrawText(pctStr, W - pctW - 40, H - 142, 28, col);
    }

    // WiFi + Bluetooth row
    {
        const int indSz = 48;
        const int indY  = H - 200;
        int xCursor = W - 40;

        // Bluetooth
        Color btCol = m_hasBluetooth ? theme.info
                                     : theme.inactive;
        float bw = m_icons.Measure(Icons::Bluetooth, "B", (float)indSz);
        xCursor -= (int)bw + 16;
        m_icons.Draw(Icons::Bluetooth, "B", (float)xCursor, (float)indY,
                     (float)indSz, btCol);

        // WiFi
        Color wfCol = m_wifiState == 2 ? theme.success
                    : m_wifiState == 1 ? theme.warning
                                       : theme.inactive;
        float ww = m_icons.Measure(Icons::Wifi, "W", (float)indSz);
        xCursor -= (int)ww + 16;
        m_icons.Draw(Icons::Wifi, "W", (float)xCursor, (float)indY,
                     (float)indSz, wfCol);
    }

    // IP address
    if (!m_ip.empty()) {
        const char* label = TextFormat("IP: %s", m_ip.c_str());
        const int lw = MeasureText(label, 32);
        DrawText(label, W - lw - 40, H - 136, 32, theme.success);
    }
}
