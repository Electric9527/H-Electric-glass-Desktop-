// Updates dropped into the program's own folder, installed without anyone's help: a package zip put in
// <program folder>\update (by whoever builds this copy, e.g. from the private repository's releases) is found
// within seconds once it is complete; Atrium then starts a PowerShell script and exits. The script waits for
// Atrium (and its guard and settings app) to end, unpacks the zip over the program folder, moves the zip to
// update\installed (update\failed when it could not be installed, so it is never tried in a loop) and starts
// Atrium again. Its transcript: <program folder>\logs\update.log.
#pragma once

#include <string>

namespace atrium {

// The newest zip waiting in <exeDir>\update that is complete (its size unchanged for a few seconds and not
// open for writing), or empty. Called every few seconds.
std::wstring PendingDropUpdate(const std::wstring& exeDir);

// Starts the installer for `zip`; true when it runs, and the caller then exits at once.
bool StartDropUpdate(const std::wstring& exeDir, const std::wstring& zip);

} // namespace atrium
