// PlayOS Shell - entry point.
#include "shell_app.h"

int main(int argc, char** argv) {
    ShellApp app;
    return app.Run(argc, argv);
}
