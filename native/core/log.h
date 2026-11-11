// Append-only log at %LOCALAPPDATA%\Atrium\logs\core.log, written by a thread of its own; this build also
// keeps a copy beside the program (<program folder>\logs\core.log), where whoever maintains it can read it.
#pragma once

namespace atrium {

void Log(const wchar_t* format, ...);

} // namespace atrium
