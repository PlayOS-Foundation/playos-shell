// PlayOS Shell — IScreen interface.
// Every UI view implements this. The ScreenStack owns and drives screens.
#pragma once

class IScreen {
public:
    virtual ~IScreen() = default;

    // Called once when the screen is pushed onto the stack.
    virtual void OnEnter() {}

    // Called once just before the screen is popped off the stack.
    virtual void OnExit() {}

    // Called every frame while this screen is on top.
    // dt is the frame delta in seconds.
    virtual void Update(float dt) = 0;

    // Called every frame to render this screen's content.
    // W and H are the current framebuffer dimensions.
    virtual void Draw(int W, int H) = 0;
};
