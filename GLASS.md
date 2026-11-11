# H-Electric-glass Desktop

An independent line of [Atrium Desktop](https://github.com/wynter-yaxley/atrium-desktop) (MIT), started from its
version 2.5.0, with its own source, builds and releases. The upstream licence and copyright notice stay in `LICENSE`.

What this line adds: a countdown widget (colours, hours:minutes:seconds, a message, lunar dates, reminders, pinning),
a microphone entry for WeChat Input Method's voice input, notifications at the top centre of the screen, QQ and
bilibili messages as cards, window previews that pop per app, animations that start from rest without a jump,
reduced motion following Windows' "animation effects" setting, a switch that turns off Windows' own
minimise/maximise animation, a copy of the log beside the program, and packages dropped in `update\` that install
themselves. The upstream in-app updater is off.

Builds: every push to `main` is built by `.github/workflows/build.yml` on a Windows runner; the zip is
published as release `glass-<run>` and kept on the `latest-build` branch; the log is `latest.log` on `build-logs`.

Taking something from upstream later is a choice, not a need: `git fetch upstream && git merge <tag>`.
