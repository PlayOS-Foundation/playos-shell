// PlayOS Shell — AudioManager.
// Lightweight singleton that plays short UI sound effects via Raylib.
// Gracefully silent when sound files are absent.
#pragma once

#include "raylib.h"
#include <string>

enum class AudioEvent { MenuMove, Confirm, Back, OverlayOpen,
                        GameLaunch, Notification, Error };

class AudioManager {
public:
    // Load sounds from <sndDir>. Graceful if directory or files are absent.
    bool Load(const std::string& sndDir);

    void Play(AudioEvent event);
    void SetEnabled(bool on) { m_enabled = on; }
    bool IsEnabled() const { return m_enabled; }

    // Free all Sound handles.
    void Unload();

private:
    Sound m_sounds[7] = {};   // indexed by AudioEvent cast
    bool  m_loaded[7] = {};
    bool  m_enabled = true;
    float m_menuMoveLast = 0.0f;  // last Play() for MenuMove (cooldown)
};
