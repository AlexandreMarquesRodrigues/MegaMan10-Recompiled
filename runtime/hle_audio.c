#include "runtime.h"
#include "func_decls.h"

#include <stdio.h>

/* HLE for the boot-time SDK audio-system bring-up. The RAM buffers, AI-clock
   setup, OS tick delay, and cache-range operation remain visible to the rest
   of the guest. Only the DSP command/status protocol is omitted. */
void hle_OSInitAudioSystem(void) {
    static int logged;
    if (!logged) {
        fprintf(stderr, "HLE_CALL __OSInitAudioSystem (DSP MMIO omitted)\n");
        logged = 1;
    }

    ppc_trace(0x80139150u);

    /* Mirror the SDK prologue so nested translated helpers use the same guest
       stack depth and preserve the caller's LR and r31. */
    uint32_t caller_sp = g_cpu.gpr[1];
    g_cpu.gpr[1] -= 0x10u;
    MEM_W32(g_cpu.gpr[1], caller_sp);
    MEM_W32(g_cpu.gpr[1] + 0x14u, g_cpu.lr);
    MEM_W32(g_cpu.gpr[1] + 0x0cu, g_cpu.gpr[31]);

    /* The original invokes __AIClockInit(1) only when its SDA init flag is
       clear. Keep that SDK path, including its IOS IPC clock bookkeeping and
       OS tick waits; these are not DSP registers. */
    if (MEM_R32(g_cpu.gpr[13] - 0x4008u) == 0) {
        g_cpu.gpr[3] = 1;
        g_cpu.lr = 0x80139174u;
        func_80138f30();
    }

    /* Preserve the two guest-visible buffer copies and DCFlushRange. The
       translated cache helper keeps the SDK's range/alignment work; cache
       instructions themselves are coherent no-ops in this single-memory
       host runtime. */
    g_cpu.lr = 0x80139178u;
    func_80138e50();
    g_cpu.gpr[3] -= 0x80u;
    g_cpu.gpr[4] = 0x81000000u;
    g_cpu.gpr[5] = 0x80u;
    g_cpu.lr = 0x80139188u;
    func_80004364();

    g_cpu.gpr[3] = 0x81000000u;
    g_cpu.gpr[4] = 0x80314250u;
    g_cpu.gpr[5] = 0x80u;
    g_cpu.lr = 0x8013919cu;
    func_80004364();

    g_cpu.gpr[3] = 0x81000000u;
    g_cpu.gpr[4] = 0x80u;
    g_cpu.lr = 0x801391a8u;
    func_80139440();

    /* Omitted DSP conversation: writes/reads of 0xCC005000..0xCC005036,
       including DSP reset/start bits, DMA descriptors, status-bit polling,
       and the 0x892-tick DSP hand-off wait. No DSP/ARAM engine is modeled;
       those MMIO effects cannot be represented as ordinary guest RAM. Keep
       the SDK's non-DSP delay so host time still advances by the same ticks. */
    uint32_t tick_start = ppc_time_base_tb_lower();
    while ((uint32_t)(ppc_time_base_tb_lower() - tick_start) < 0x892u) {
        /* OSGetTick is a time-base read; no guest state is changed here. */
    }

    /* The SDK copies its temporary audio buffer back to 0x81000000 after the
       DSP hand-off. Preserve that copy even though the DSP cannot modify the
       temporary buffer in this HLE path. */
    g_cpu.lr = 0x801392f8u;
    func_80138e50();
    g_cpu.gpr[4] = g_cpu.gpr[3] - 0x80u;
    g_cpu.gpr[3] = 0x81000000u;
    g_cpu.gpr[5] = 0x80u;
    g_cpu.lr = 0x8013930cu;
    func_80004364();

    /* Mirror the SDK epilogue. */
    g_cpu.lr = MEM_R32(g_cpu.gpr[1] + 0x14u);
    g_cpu.gpr[31] = MEM_R32(g_cpu.gpr[1] + 0x0cu);
    g_cpu.gpr[1] = MEM_R32(g_cpu.gpr[1]);
}
