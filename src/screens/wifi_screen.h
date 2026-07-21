// PlayOS Shell — WiFiScreen.
// Browse available WiFi networks and connect with optional password entry.
#pragma once

#include "../screen.h"
#include "../screen_stack.h"
#include "playos/network.h"
#include <string>
#include <vector>

class WiFiScreen : public IScreen {
public:
    explicit WiFiScreen(ScreenStack& stack);

    void OnEnter() override;
    void Update(float dt) override;
    void Draw(int W, int H) override;

private:
    enum class State {
        Scanning,    // waiting for scan results
        List,        // browsing networks
        EnterPass,   // typing password
        Connecting,  // connect in progress
        Result,      // success / failure message
    };

    ScreenStack& m_stack;
    State m_state = State::Scanning;

    // Network list
    std::vector<PlayOS::Network::WiFiNetwork> m_networks;
    int m_selected = 0;
    int m_scrollOffset = 0;

    // Password entry
    std::string m_password;
    bool m_passVisible = false;

    // Result
    std::string m_resultMsg;
    float m_resultTimer = 0.0f;

    // Helpers
    void DoScan();
    void DoConnect();
    void AppendTypedChar();
    void DrawSignalBars(int x, int y, int signal, bool active) const;
};
