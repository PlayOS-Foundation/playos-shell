// PlayOS Shell — StatusBar.
// Persistent top/bottom bar drawn every frame above all screens.
// Polls Platform API indicators periodically (not every frame).
#pragma once

#include "icons.h"
#include "theme.h"
#include <string>

class StatusBar {
public:
    explicit StatusBar(const Icons& icons) : m_icons(icons) {}

    // Call once per frame. Polls indicators every kPollIntervalSec seconds.
    void Update(float dt);

    // Set the device name (from DeviceProfile) to show in the top bar.
    void SetDeviceName(const std::string& name) { m_deviceName = name; }

    // Draw the status bar. Call after the active screen's Draw().
    void Draw(int W, int H, const Theme& theme) const;

private:
    static constexpr float kPollIntervalSec = 5.0f;

    const Icons& m_icons;
    float m_pollTimer = 0.0f;
    std::string m_deviceName;

    // Cached indicator state (updated every kPollIntervalSec)
    bool        m_hasBattery    = false;
    float       m_battLevel     = -1.0f;
    bool        m_battCharging  = false;
    bool        m_hasBluetooth  = false;
    int         m_wifiState     = 0;  // 0=absent,1=connecting,2=connected
    std::string m_ip;
    bool        m_controllerOn  = false;

    void Poll();
};
