// PlayOS Shell — ToastManager.
// Non-blocking toast notifications that slide in from the top-right,
// auto-dismiss after 3 seconds. Queue supports stacking when multiple fire.
#pragma once

#include "theme.h"
#include <string>
#include <vector>

enum class ToastType { Info, Success, Warning, Error };

class ToastManager {
public:
    void Show(const std::string& msg, ToastType type = ToastType::Info);
    void Update(float dt);
    void Draw(int W, int H, const Theme& theme, Font textFont) const;

private:
    struct Toast {
        std::string msg;
        ToastType   type;
        float       timer;  // seconds since creation
    };
    std::vector<Toast> m_queue;

    static constexpr float kDuration = 3.0f;
    static constexpr float kSlideIn  = 0.2f;   // slide-in duration (200 ms)

    // Easing
    static float EaseOutCubic(float t);
};
