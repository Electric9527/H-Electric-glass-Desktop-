#pragma once

#include <windows.h>

namespace atrium {

// The bar's microphone: starts (or ends) the voice input of WeChat Input Method (微信输入法, WeType), the
// same voice session its floating bar's microphone and its hotkey Ctrl+Win+Shift start and end.
//
// In order: the keyboard goes back to the app the user was typing in (when one of Atrium's own windows took
// it); then the microphone button on the input method's floating bar is clicked, in every case: a hidden bar
// is put on screen for the moment of the click (UI Automation finds the button "语音输入"; it is clicked for
// real, the pointer put back); only when that changes nothing is the hotkey sent. Whether it worked is seen from the voice panel
// (the input method's window "语音输入"). Every step is
// written to core.log ("voice: …"), with the input method's windows, so a machine where neither works can be
// looked at. Returns at once; the work runs on a short-lived thread.
void StartVoiceInput(HWND lastApp);

// True while the input method's voice panel (its window "语音输入") is on screen: a voice session is running,
// started from the bar, the floating bar or the hotkey alike. Cheap enough to poll a few times a second.
bool VoiceSessionOpen();

} // namespace atrium
