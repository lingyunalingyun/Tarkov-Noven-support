#include "app/App.h"

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int show_command
) {
    noven::App app;
    return app.Run(instance, show_command);
}
