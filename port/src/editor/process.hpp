// Starting the game from the editor (blocking; call it from a worker thread).
#pragma once
#include <string>
#include <vector>

namespace editor {

// Runs exe with the arguments and waits for it. Returns the exit code, or -1
// if it could not be started.
int runProcess(const std::string& exe, const std::vector<std::string>& args);

}  // namespace editor
