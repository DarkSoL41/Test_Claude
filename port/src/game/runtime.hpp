// Runtime glue between the translated game code and the platform.
#pragma once
#include <functional>
#include <string>

#include "amiga/gamefiles.hpp"
#include "amiga/amiga.hpp"

namespace game {

struct Environment {
    const amiga::GameFiles* files = nullptr;  // files of the original disk (data folder or ADF)
    std::string savePath;              // hiscore file written instead of PHIL_03 on disk
    // called when the game reads or writes the disk (instant here, a second or
    // more on the Amiga): lets the front end treat buttons held since then as
    // released until they are let go
    std::function<void()> onDiskAccess;
};

// Thrown by the front end to leave the game loop (the original never exits).
struct QuitGame {};

// Prepare the machine: `hw` becomes the bus of the translated code.
void attach(amiga::Amiga& hw, const Environment& env);
// Boot like the disk does: PHIL_00 (intro) at $54000, which then loads and
// starts the main program PHIL_01 at $7E00. Never returns (unless QuitGame).
void run();
// Interrupt request from the hardware layer (called by the host).
void requestInterrupt(int level);

}  // namespace game
