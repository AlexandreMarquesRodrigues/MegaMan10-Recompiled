# Guest OS thread design for MM10

## Corrected finding from Marco 9

The initial diagnosis used the runtime's initial `r13` value. By the call at
`0x80140ab0`, `OSSetCurrentContext` has restored `r13=0x80680c40`, so the live
slot is `0x8067b798`, inside DOL `data6`. The DOL stores `0x801409f0` there;
that target is a one-instruction `blr` default callback. The loader used to
clear BSS after copying sections and erased this word. Clearing BSS before
section copies preserves it, and startup now passes the call without a
fabricated callback or thread HLE. See the Marco 9 section in `BUILDING.md`.

This fix removes the need for context switching at this boot stop. The design
below remains a proposal for future guest thread API coverage. Do not add a
fake callback to replace the DOL's default or implement switching as part of
this loader correction.

## Reference behavior in `robox-recompiled`

Treat this section as implementation reference only; do not edit that clone.
Its `hle___OSThreadInit` is a no-op. Its `hle_OSCreateThread` records the
guest thread address, entry, parameter, stack, and priority; it initializes
the guest `OSThread` state/stack fields and starts threads suspended. On
resume, selected bounded workers can run inline with a saved/restored PPC
context, while other recognized workers run on host threads under a PPC
execution lock. `OSYieldThread` releases the lock, briefly allows another
worker to run, then reacquires it and restores the saved guest CPU context.
The reference itself notes the shared-CPU limitation and uses special cases
for workers that can safely complete inline. Its approach is useful evidence
for synchronization and stack ownership, but a host thread per guest thread
does not give MM10 deterministic guest scheduling.

## MM10 proposal: cooperative guest coroutines

Represent each guest thread as a coroutine record containing its guest
`OSThread*`, entry and argument, guest stack pointer, saved GPR/FPR state, LR,
CTR, CR, XER, FPSCR, run state, suspend count, priority, queue links, wake
time, and exit/result data. Keep exactly one coroutine running at a time.
`OSCreateThread` initializes the guest-visible structure and stores a runnable
record; `OSResumeThread` makes it ready; scheduler selection saves the current
PPC context and restores the selected context. No host thread concurrently
mutates `g_cpu` or guest memory.

Switch only at explicit guest yield/block points and at interrupt-return
boundaries, after the translated instruction has completed and its memory
effects are visible. Alarm deadlines should be checked at those boundaries
using guest ticks. A due alarm queues its callback as interrupt work; it must
not reenter translated guest code in the middle of a host C function or while
the guest scheduler lock is held. `OSDisableInterrupts` /
`OSRestoreInterrupts` gate delivery, and nested interrupt state must be kept
per coroutine. Queue wakeups and alarm callbacks mark coroutines ready; they
do not perform a nested context switch. When interrupt delivery is re-enabled,
service pending interrupts at the next safe boundary, then run the scheduler.

Priorities select among ready coroutines, with a stable FIFO order for equal
priorities. Sleeping or waiting threads leave the ready queue. Resume,
wakeup, cancellation, and alarm cancellation update both guest-visible state
and the scheduler record. Add tracing for every state transition and selected
thread so a stalled wait can be diagnosed without timing-dependent logs from
host-thread races.

## Named function surface

`games/megaman10/func_list.json` currently names these relevant entry points
that should route to the scheduler implementation when thread support is
added:

- `__OSThreadInit` (`0x80140a00`), `OSInitThreadQueue` (`0x80140c90`),
  `OSDisableScheduler` (`0x80140ce0`), and `OSEnableScheduler` (`0x80140d20`)
- `OSCancelThread` (`0x801415c0`) and `OSSleepThread` (`0x80141d20`)
- `OSDisableInterrupts` (`0x8013de00`), `OSEnableInterrupts` (`0x8013de20`),
  and `OSRestoreInterrupts` (`0x8013de40`)
- `OSCreateAlarm` (`0x80138390`) and `OSSetAlarm` (`0x801385f0`)

The current `func_list.json` does not name `OSCreateThread`, `OSResumeThread`,
`OSSwitchThread`, or `OSYieldThread`; locate their addresses from the DOL and
add verified names before routing them. Context helpers such as
`OSSaveFPUContext`, `OSSetCurrentContext`, `OSLoadContext`, `OSClearContext`,
and `OSSwitchFPUContext` should be reviewed together with the coroutine
context format, but should only be replaced if their guest-visible semantics
are subsumed by the scheduler. Keep alarm callback ordering and interrupt
mask semantics aligned with the actual translated call sites.

## Staged implementation

1. Confirm the callback slot's symbol/contract from a matching SDK reference
   or additional binaries; do not infer its initialization from the BSS word.
2. Add a coroutine context record and a scheduler with tests for create,
   resume, equal-priority ordering, blocking, wakeup, and save/restore.
3. Route the verified thread APIs and make `__OSThreadInit` establish the
   initial guest thread without calling an unset hook.
4. Add safe-boundary alarm and interrupt delivery, then validate startup,
   worker handoffs, sleep/wakeup, and shutdown from guest traces.

