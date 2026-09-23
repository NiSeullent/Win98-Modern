# Native Windows 98 GUI startup diagnostic

`app_probe.exe` runs one explicitly named target process in the installed Windows 98 guest. It observes for 1–30,000 milliseconds, prints the child process ID, records each newly seen visible top-level window owned by that child, and records a final visible-window snapshot. Each window line includes its HWND, ANSI class and title. It reports whether the immediate child remained alive at the end of observation and its eventual exit code. **A window is only window evidence; the tool never labels an application functional.**

Build separately from the project's wrapper build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File remote/build-app-probe.ps1
python remote/check_app_probe_pe.py build/app_probe.exe
```

The PE gate checks x86 PE32, console subsystem and minimum operating-system version 4.10, no CRT/TLS/delay/CLR imports, and every imported name against `benchmarks/win98se-ko-oem-native-exports-v1.json`. Only original Windows 98 `KERNEL32.DLL` and `USER32.DLL` imports are accepted. It is a static host check, not guest execution proof.

Run in the guest, or pass the same command to the remote agent's `EXEC` operation with an approximately 40-second outer timeout:

```text
C:\TOOLS\APP_PROBE.EXE 10000 "C:\NPP\notepad++.exe"
C:\TOOLS\APP_PROBE.EXE 5000 "C:\Program Files\Example\example.exe" /safe-mode
```

The first argument is observation time in milliseconds. The remaining text is passed as the child's command line with its quotes preserved. The first target token must be an absolute `X:\...` path, quoted if it contains spaces; that token is also passed as `CreateProcessA`'s explicit application name. The child inherits the probe's current directory, but not the probe's standard-output handle. The remote `EXEC --cwd` option can set the directory. The tool accepts at most 4,095 target-command bytes and a target path shorter than `MAX_PATH`.

Output labels have distinct meanings:

- `LAUNCH_FAIL` means `CreateProcessA` failed; the Win32 error is printed. Probe exit code is 2.
- `WINDOW_FIRST_SEEN` records a visible top-level child window while observing. `FINAL_WINDOW` is the snapshot at the end. Class and title are escaped ANSI text; empty or possibly truncated text is reported as observed, without inventing a dialog meaning.
- `ALIVE_AFTER_OBSERVATION` reports `0`, `1`, or `unknown` if the process wait itself failed. `TARGET_EXIT_CODE` reports its final status, which can be nonzero even when the probe itself exits zero.
- `WM_CLOSE_POSTED_COUNT` records asynchronous close requests to visible top-level child windows. After two seconds, the probe terminates and waits up to three more seconds for an unclosed immediate child, reporting `TERMINATED_BY_PROBE` and `IMMEDIATE_CHILD_REAPED` separately. A failure to reap makes the probe exit nonzero.
- `DESCENDANTS_NOT_TRACKED=1` states the limit: child processes started by the target are not tracked or terminated.

Window enumeration is sampled every 200 milliseconds and is capped at 16 detailed first-seen windows and 16 detailed final windows so the worst-case escaped text plus summary fits the remote agent's 64 KiB capture; count/limit lines reveal an overflow. The target can exit before the observation deadline. The probe can capture persistent loader-error dialogs if they belong to the child. A synchronous loader failure may return `LAUNCH_FAIL`; a blocking system dialog during `CreateProcessA` can instead exhaust the remote agent's outer `EXEC` timeout before the probe has a child PID. A dialog owned by another process is outside the PID filter. Preserve a guest screenshot and the actual Win32 or transport error in those cases. A successful probe exit means that it launched and reaped the immediate child and wrote the diagnostic stream. It does **not** establish application startup or useful application behavior.

Host checks: the build script and PE gate passed with 12 `KERNEL32.DLL` and 6 `USER32.DLL` OEM imports. The repository's own `remote_fixture.exe` exited normally through the probe with target exit code 0. Its five-second sleep mode observed for 1,000 ms, posted no close messages because it had no GUI window, then terminated and reaped the immediate child with target exit code 1460. An absent target produced `LAUNCH_FAIL win32_error=2`, and 30,001 ms was rejected. A temporary copy of the fixture with spaces in its name launched through a quoted absolute target path. A separate temporary GUI fixture produced `WINDOW_FIRST_SEEN` and `FINAL_WINDOW` with its expected class and title; the probe posted one `WM_CLOSE`, and the fixture exited naturally with code 42. Both temporary fixtures were removed.

Guest control: the probe ran the installed `C:\WINDOWS\NOTEPAD.EXE` in Windows 98 SE. It recorded `WINDOW_FIRST_SEEN` and `FINAL_WINDOW` with class `Notepad` and the observed Korean title, posted one `WM_CLOSE`, and reaped the child with exit code 0. Thus window enumeration and close behavior work in this guest. With Notepad++ 8.9.8 on `D:\NPP`, the same probe returned `LAUNCH_FAIL win32_error=31`; the guest screenshot identified `UXTHEME.DLL!DrawThemeTextEx` as the unresolved loader import. This does not mean the probe itself failed, and it does not establish Notepad++ startup.
