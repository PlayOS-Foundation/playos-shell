// PlayOS Shell — ScreenStack.
// Manages a stack of IScreen objects. Only the top screen is updated and
// drawn. Push to go forward, pop to go back.
#pragma once

#include "screen.h"
#include <memory>
#include <vector>

class ScreenStack {
public:
    // Push a new screen on top. Calls OnEnter() on the new screen.
    void Push(std::unique_ptr<IScreen> screen);

    // Pop the top screen. Calls OnExit() on the removed screen.
    // No-op if the stack is empty.
    void Pop();

    // Returns true if there are no screens on the stack.
    bool Empty() const;

    // Update the top screen.
    void Update(float dt);

    // Draw the top screen.
    void Draw(int W, int H);

private:
    std::vector<std::unique_ptr<IScreen>> m_stack;
};
