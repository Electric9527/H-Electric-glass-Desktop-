#include "shell/drop_update.h"

#include "log.h"

#include <windows.h>

#include <map>
#include <string>
#include <utility>

namespace atrium {
namespace {

// A path inside a single-quoted PowerShell string.
std::wstring Quoted(const std::wstring& s)
{
    std::wstring out = L"'";
    for (wchar_t c : s) {
        if (c == L'\'')
            out += L'\'';
        out += c;
    }
    return out + L"'";
}

bool WriteUtf8WithBom(const std::wstring& path, const std::wstring& text)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes = "\xEF\xBB\xBF"; // Windows PowerShell 5 reads a script without a BOM in the ANSI code page
    const size_t at = bytes.size();
    bytes.resize(at + size_t(n));
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), bytes.data() + at, n, nullptr, nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(f);
    return ok;
}

} // namespace

std::wstring PendingDropUpdate(const std::wstring& exeDir)
{
    // A zip still being written grows, or is held open: wait until it has been the same size for 4 s.
    static std::map<std::wstring, std::pair<ULONGLONG, ULONGLONG>> seen; // path -> size, first tick at that size
    const std::wstring dir = exeDir + L"\\update\\";
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"*.zip").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) {
        seen.clear();
        return {};
    }
    std::wstring newest;
    FILETIME newestTime{};
    ULONGLONG newestSize = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (newest.empty() || CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0) {
            newest = dir + fd.cFileName;
            newestTime = fd.ftLastWriteTime;
            newestSize = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    if (newest.empty() || newestSize == 0)
        return {};
    const ULONGLONG now = GetTickCount64();
    auto it = seen.find(newest);
    if (it == seen.end() || it->second.first != newestSize) {
        seen[newest] = {newestSize, now};
        return {};
    }
    if (now - it->second.second < 4000)
        return {};
    HANDLE f = CreateFileW(newest.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {}; // someone still writes it
    CloseHandle(f);
    return newest;
}

bool StartDropUpdate(const std::wstring& exeDir, const std::wstring& zip)
{
    wchar_t temp[MAX_PATH + 1]{};
    if (!GetTempPathW(MAX_PATH, temp))
        return false;
    const std::wstring script = std::wstring(temp) + L"atrium-drop-update.ps1";
    std::wstring text;
    text += L"$ErrorActionPreference = 'Continue'\r\n";
    text += L"$dir = " + Quoted(exeDir) + L"\r\n";
    text += L"$zip = " + Quoted(zip) + L"\r\n";
    text += L"$corePid = " + std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    text += LR"PS(
New-Item -ItemType Directory -Force (Join-Path $dir 'logs') | Out-Null
Start-Transcript -Path (Join-Path $dir 'logs\update.log') -Append | Out-Null
"update: $zip"
Wait-Process -Id $corePid -Timeout 30 -ErrorAction SilentlyContinue
# Atrium's guard and settings app run from the same folder: they go too (the guard ends with Atrium).
$prefix = $dir.TrimEnd('\') + '\'
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) {
    $left = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
    if ($left.Count -eq 0) { break }
    if ((Get-Date) -gt $deadline.AddSeconds(-12)) { $left | Stop-Process -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}
$tmp = Join-Path $env:TEMP ('atrium-update-' + [Guid]::NewGuid().ToString('N'))
$ok = $false
try {
    Expand-Archive -LiteralPath $zip -DestinationPath $tmp -Force -ErrorAction Stop
    $src = if (Test-Path (Join-Path $tmp 'Atrium\Atrium.exe')) { Join-Path $tmp 'Atrium' } else { $tmp }
    robocopy $src $dir /E /R:10 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    "robocopy exit $LASTEXITCODE"
    $ok = $LASTEXITCODE -lt 8
} catch {
    "failed: $_"
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    $done = Join-Path $dir ('update\' + $(if ($ok) { 'installed' } else { 'failed' }))
    New-Item -ItemType Directory -Force $done | Out-Null
    Move-Item -LiteralPath $zip -Destination $done -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue }
}
"installed: $ok"
Start-Process -FilePath (Join-Path $dir 'Atrium.exe') -WorkingDirectory $dir
Stop-Transcript | Out-Null
)PS";
    if (!WriteUtf8WithBom(script, text)) {
        Log(L"update: could not write %s", script.c_str());
        return false;
    }
    std::wstring command = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                        exeDir.c_str(), &si, &pi) &&
        !CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, exeDir.c_str(), &si, &pi)) {
        Log(L"update: could not start PowerShell (%lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log(L"update: installing %s; Atrium exits and comes back", zip.c_str());
    return true;
}

} // namespace atrium
