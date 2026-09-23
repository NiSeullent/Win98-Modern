# Threadpool fixture follow-up

The isolated fixture and direct Windows 98 smoke now pass. Before counting
these five APIs as production guest support, link `src/m98_threadpool.c` into
`M98WRAP.DLL`, add the sorted KernelEx routes and DllMain calls described in
`docs/THREADPOOL_WORK_PORT.md`, then run the static `KERNEL32.DLL` import probe
and the pinned Notepad++ loader probe after a normal guest restart.

Further stability work remains focused on actual gaps: test concurrent
submission against `WaitForThreadpoolWorkCallbacks(..., TRUE)` and Close;
keep the API library loaded while workers exist, since manual DLL unload during
callbacks is not supported; and design separate implementations before
accepting callback environments that request custom pools, cleanup groups,
activation contexts, or a race DLL. The fixture currently rejects those
environments explicitly.
