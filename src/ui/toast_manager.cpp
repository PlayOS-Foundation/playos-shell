#include "toast_manager.h"
#include "text_helpers.h"

#include "raylib.h"
#include <algorithm>
#include <cmath>

void ToastManager::Show(const std::string& msg, ToastType type) {
    m_queue.push_back({msg, type, 0.0f});
}

void ToastManager::Update(float dt) {
    for (auto& t : m_queue)
        t.timer += dt;

    // Remove expired toasts
    m_queue.erase(
        std::remove_if(m_queue.begin(), m_queue.end(),
                       [](const Toast& t) { return t.timer >= kDuration; }),
        m_queue.end());
}

float ToastManager::EaseOutCubic(float t) {
    // t in [0, 1], returns eased value
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

void ToastManager::Draw(int W, int H, const Theme& theme, Font textFont) const {
    (void)H;

    constexpr float toastW   = 420.0f;
    constexpr float toastH   = 56.0f;
    constexpr float margin   = 20.0f;   // from right edge
    constexpr float gap      = 8.0f;    // between stacked toasts
    constexpr float topY     = 84.0f;   // below status bar (72 px)
    constexpr float borderW  = 6.0f;

    // Map ToastType to Theme colour
    auto borderColor = [&](ToastType t) -> Color {
        switch (t) {
        case ToastType::Info:    return theme.info;
        case ToastType::Success: return theme.success;
        case ToastType::Warning: return theme.warning;
        case ToastType::Error:   return theme.danger;
        }
        return theme.info;
    };

    for (size_t i = 0; i < m_queue.size(); ++i) {
        const auto& toast = m_queue[i];

        // Slide-in: first kSlideIn seconds, slide from right edge
        const float t = std::min(toast.timer / kSlideIn, 1.0f);
        const float offsetX = (toastW + margin) * (1.0f - EaseOutCubic(t));

        const float x = W - toastW - margin + offsetX;
        const float y = topY + i * (toastH + gap);

        // Background
        DrawRectangleRounded({x, y, toastW, toastH}, 0.25f, 8, theme.surface);

        // Coloured left border
        DrawRectangle((int)x, (int)y, (int)borderW, (int)toastH,
                      borderColor(toast.type));

        // Text
        const float textX = x + borderW + 16.0f;
        const float textY = y + (toastH - 24.0f) * 0.5f;
        DrawTextF(textFont, toast.msg.c_str(), textX, textY, 24, theme.textPrimary);
    }
}
