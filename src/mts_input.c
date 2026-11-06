#include "mts_internal.h"
#include <xinput.h>

static volatile LONG64 next_probe_ms[XUSER_MAX_COUNT];

int mts_bypass_held(void)
{
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) return 1;

    const ULONGLONG now = GetTickCount64();
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if (now < (ULONGLONG)InterlockedCompareExchange64(&next_probe_ms[slot], 0, 0))
            continue;

        XINPUT_STATE state;
        const DWORD status = XInputGetState(slot, &state);
        if (status == ERROR_DEVICE_NOT_CONNECTED) {
            // Skip empty slots briefly while checking connected controllers on every call.
            InterlockedExchange64(&next_probe_ms[slot], (LONG64)(now + 1000));
        }
        if (status == ERROR_SUCCESS && (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER))
            return 1;
    }
    return 0;
}
