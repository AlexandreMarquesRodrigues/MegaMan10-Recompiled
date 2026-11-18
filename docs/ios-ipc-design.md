# IOS IPC and asynchronous device design

This is a proposal only. The current runtime stops at a DVD asynchronous
dispatcher; it does not emulate IOS replies, DVD completion interrupts, or
callbacks. Do not add a DVD HLE as a workaround for the null callback.

## Safe delivery model

Keep guest execution single threaded. Device models enqueue completion records
containing the request, result, and registered guest callback. Deliver them
only at explicit safe points after a guest function returns to the runtime
dispatcher, and only while the guest interrupt-enable state permits delivery.
Set the guest exception state and enter the registered SDK interrupt handler
through the normal exception path; do not call a guest callback from inside an
MMIO read/write helper or halfway through a translated instruction sequence.
Masking interrupts must defer queued work, and acknowledging a device status
bit must clear only the documented write-one-to-clear bits.

IPC/IOS calls should have a synchronous deterministic model at the API
boundary: complete the request structure, result, and output buffers before
returning from synchronous calls. Async calls should enqueue the same modeled
result and invoke their registered callback only at a safe point. Keep device
completion and IOS IPC request/reply paths separate so each can be tested and
traced independently.

## Candidate named functions

The names below exist in `games/megaman10/func_list.json`; selecting a name
does not mean it should be stubbed. Preserve request structures and callback
ordering when adding a handler.

- IPC plumbing: `IPCInit`, `IPCReadReg`, `IPCWriteReg`, `IPCGetBufferHi`,
  `IPCGetBufferLo`, `IPCSetBufferLo`, `IPCInterruptHandler`, `IPCCltInit`.
- IPC profiling hooks: `IPCiProfInit`, `IPCiProfQueueReq` (only if the game
  reaches the profiling path).
- IOS calls: `IOS_Open`, `IOS_OpenAsync`, `IOS_IoctlAsync`, `IOS_Ioctlv`.
- DVD request and completion path: `DVDInit`, `DVDInquiryAsync`,
  `DVDSetAutoFatalMessaging`, `DVDLowInit`, `DVDLowUnencryptedRead`,
  `DVDLowRequestError`, `DVDLowReportKey`, `DVDLowGetImmBufferReg`,
  `DVDLowUnmaskStatusInterrupts`, `DVDLowMaskCoverInterrupt`.
- NAND calls: `nandOpen`, `NANDPrivateOpenAsync`, `nandOpenCallback`,
  `nandConvertPath`, `nandIsPrivatePath`, `nandIsInitialized`,
  `nandConvertErrorCode`, `nandGenerateAbsPath`, `NANDInit`,
  `NANDLoggingAddMessageAsync`.
- Boot clock/IOS revision entry points: `__AIClockInit`, `__OSGetIOSRev`.

## IOS activity seen during this boot

The Marco 11 run reaches `__AIClockInit` before the DVD request and touches
the Hollywood IPC MMIO window. The trace records `CD800180` as read then
written with `0x00000100`; `CD8001D0` as reads/writes including `0`,
`0x40000000`, and `0xC0000000`; and `CD8001CC` as a read/write timebase pair.
The SDK uses these registers for the AI clock/IPC handshake and tick
accounting. This run does not show a named `IOS_Open*` or `IOS_Ioctl*` call
before the stop. Do not infer an IOS reply from the shadow register echo: the
runtime currently has no IPC device response model.

## Open implementation questions

1. Confirm the IOS IPC command/status bits and interrupt acknowledge semantics
   from a primary hardware/SDK reference before implementing the register
   model.
2. Trace which IOS device and command the boot-time AI clock handshake expects
   and model its reply fields and error path.
3. The slot at `0x8067B7E8` is initialized in data6 to `0x80146290`, then
   cleared by `ClearMEM2Arena` through `DCZeroRange` before `DVDInit`. Determine
   why the MEM2 arena clear range includes loaded data6 before designing DVD
   completion delivery; the slot is not initially empty in the DOL.
