// Interface language of the level editor: English (default) or Russian.
// Every user-visible string is written in both languages at the place it is
// used: tr("English", "Русский").
#pragma once

namespace editor {

inline bool g_russian = false;

inline const char* tr(const char* en, const char* ru) { return g_russian ? ru : en; }

}  // namespace editor
