#include "audio_manager.h"
#include <filesystem>

namespace fs = std::filesystem;

bool AudioManager::Load(const std::string& sndDir) {
    if (!fs::exists(sndDir)) return false;

    struct Mapping { AudioEvent event; const char* file; };
    static const Mapping map[] = {
        {AudioEvent::MenuMove,     "move.wav"},
        {AudioEvent::Confirm,      "confirm.wav"},
        {AudioEvent::Back,         "back.wav"},
        {AudioEvent::OverlayOpen,  "overlay.wav"},
        {AudioEvent::GameLaunch,   "launch.wav"},
        {AudioEvent::Notification, "notify.wav"},
        {AudioEvent::Error,        "error.wav"},
    };

    bool any = false;
    for (const auto& m : map) {
        fs::path p = fs::path(sndDir) / m.file;
        if (!fs::exists(p)) continue;
        Sound s = LoadSound(p.string().c_str());
        if (s.frameCount == 0) continue;
        int idx = static_cast<int>(m.event);
        m_sounds[idx] = s;
        m_loaded[idx] = true;
        any = true;
        TraceLog(LOG_INFO, "PLAYOS-SHELL: Loaded UI sound: %s", p.string().c_str());
    }
    return any;
}

void AudioManager::Play(AudioEvent event) {
    if (!m_enabled) return;
    int idx = static_cast<int>(event);

    // Prevent sound spam on rapid MenuMove scrolling (50 ms cooldown).
    if (event == AudioEvent::MenuMove) {
        float now = static_cast<float>(GetTime());
        if (now - m_menuMoveLast < 0.05f) return;
        m_menuMoveLast = now;
    }

    if (m_loaded[idx] && m_sounds[idx].frameCount > 0)
        PlaySound(m_sounds[idx]);
}

void AudioManager::Unload() {
    for (int i = 0; i < 7; ++i) {
        if (m_loaded[i]) {
            UnloadSound(m_sounds[i]);
            m_loaded[i] = false;
        }
    }
}
