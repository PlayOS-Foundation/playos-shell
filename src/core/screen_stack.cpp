#include "screen_stack.h"

void ScreenStack::Push(std::unique_ptr<IScreen> screen) {
    screen->OnEnter();
    m_stack.push_back(std::move(screen));
}

void ScreenStack::Pop() {
    if (m_stack.empty()) return;
    m_stack.back()->OnExit();
    m_stack.pop_back();
}

bool ScreenStack::Empty() const {
    return m_stack.empty();
}

void ScreenStack::Update(float dt) {
    if (!m_stack.empty())
        m_stack.back()->Update(dt);
}

void ScreenStack::Draw(int W, int H) {
    if (!m_stack.empty())
        m_stack.back()->Draw(W, H);
}
