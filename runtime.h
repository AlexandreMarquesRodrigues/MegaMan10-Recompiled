// src/runtime.h -- PPC recomp runtime surface consumed by every generated
// function and by sdk/. The generated code is basically a huge pile of
// assignments against g_cpu and MEM_* macros; this header defines both.
//
// Scaffolded by recompile_wii.py from templates/runtime.h.tmpl. Safe to
// edit: the recompiler emits this only when missing. Delete the file to
// regenerate from the template.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>                         // fabs/fabsf/sqrt used by generated code

#if defined(__has_attribute)
#  if __has_attribute(musttail)
#    define PPC_TAIL __attribute__((musttail))
#  endif
#endif
#ifndef PPC_TAIL
#  define PPC_TAIL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Guest memory layout. Broadway VAs; the cached/uncached aliases of MEM1 are
// folded onto the same host buffer in ppc_host_ptr.
// ---------------------------------------------------------------------------

#define PPC_MEM1_BASE   0x80000000u
#define PPC_MEM1_SIZE   0x01800000u       // 24 MB (real Wii)
#define PPC_MEM1_UNCACHED_BASE 0xC0000000u
#define PPC_MEM2_BASE   0x90000000u
#define PPC_MEM2_UNCACHED_BASE 0xD0000000u
// Real Wii MEM2 is 64 MB. The default matches hardware; sdk/os.c's first-fit
// allocator is correct, so games that do bounded MEM2 allocation get the
// same address layout they would on a real Wii. If a specific game does
// truly enormous transient allocations (rare), bump this with
// -DPPC_MEM2_SIZE=0x10000000 to get 256 MB. Reserving more virtual memory
// is free until written, but it can hide real game-side leaks.
#ifndef PPC_MEM2_SIZE
#define PPC_MEM2_SIZE   0x04000000u       // 64 MB — matches hardware
#endif

// Deadzone for unmapped VAs. Reads return 0, writes go to /dev/null. Sized
// to 64 KB so byte-offset probes from a corrupt base don't wrap; see rt.c
// safe_span() for the contract.
#define PPC_DEADZONE_SIZE   0x10000u

extern uint8_t *g_mem1;                   // PPC_MEM1_SIZE bytes
extern uint8_t *g_mem2;                   // PPC_MEM2_SIZE bytes
extern uint8_t  g_deadzone[PPC_DEADZONE_SIZE];

// VA -> host pointer. Returns a pointer into g_mem1/g_mem2 for mapped
// addresses and into g_deadzone for anything else. Never returns NULL;
// sdk code and generated code both rely on that.
void *ppc_host_ptr(uint32_t va);


// ---------------------------------------------------------------------------
// Endian-swapping guest memory access. Generated code funnels every load/
// store through these so we don't need to byte-swap anything at load time.
// ---------------------------------------------------------------------------

static inline uint32_t _ppc_bswap32(uint32_t x) {
    return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8)
         | ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static inline uint16_t _ppc_bswap16(uint16_t x) {
    return (uint16_t)(((x & 0xFFu) << 8) | ((x & 0xFF00u) >> 8));
}
static inline uint64_t _ppc_bswap64(uint64_t x) {
    return ((uint64_t)_ppc_bswap32((uint32_t)x) << 32)
         | (uint64_t)_ppc_bswap32((uint32_t)(x >> 32));
}

bool ppc_mmio_va(uint32_t va);
uint64_t ppc_mmio_read(uint32_t va, int nbytes);

static inline uint8_t  MEM_R8 (uint32_t va) { if (ppc_mmio_va(va)) return (uint8_t)ppc_mmio_read(va, 1); return *(uint8_t *)ppc_host_ptr(va); }
static inline uint16_t MEM_R16(uint32_t va) {
    if (ppc_mmio_va(va)) return (uint16_t)ppc_mmio_read(va, 2);
    uint16_t v; __builtin_memcpy(&v, ppc_host_ptr(va), 2); return _ppc_bswap16(v);
}
static inline uint32_t MEM_R32(uint32_t va) {
    if (ppc_mmio_va(va)) return (uint32_t)ppc_mmio_read(va, 4);
    uint32_t v; __builtin_memcpy(&v, ppc_host_ptr(va), 4); return _ppc_bswap32(v);
}
static inline uint64_t MEM_R64(uint32_t va) {
    if (ppc_mmio_va(va)) return ppc_mmio_read(va, 8);
    uint64_t v; __builtin_memcpy(&v, ppc_host_ptr(va), 8); return _ppc_bswap64(v);
}

// MMIO hook. Writes into CP-FIFO / GX register VAs get forwarded into the
// FIFO interpreter in sdk/peripherals.c instead of touching guest memory.
void ppc_mmio_write(uint32_t va, const void *data, int nbytes);
uint64_t ppc_mmio_read(uint32_t va, int nbytes);
bool ppc_mmio_va(uint32_t va);            // true if the VA is MMIO, not RAM

static inline void MEM_W8(uint32_t va, uint8_t v) {
    if (ppc_mmio_va(va)) { ppc_mmio_write(va, &v, 1); return; }
    *(uint8_t *)ppc_host_ptr(va) = v;
}
static inline void MEM_W16(uint32_t va, uint16_t v) {
    uint16_t be = _ppc_bswap16(v);
    if (ppc_mmio_va(va)) { ppc_mmio_write(va, &be, 2); return; }
    __builtin_memcpy(ppc_host_ptr(va), &be, 2);
}
static inline void MEM_W32(uint32_t va, uint32_t v) {
    uint32_t be = _ppc_bswap32(v);
    if (ppc_mmio_va(va)) { ppc_mmio_write(va, &be, 4); return; }
    __builtin_memcpy(ppc_host_ptr(va), &be, 4);
}
static inline void MEM_W64(uint32_t va, uint64_t v) {
    uint64_t be = _ppc_bswap64(v);
    if (ppc_mmio_va(va)) { ppc_mmio_write(va, &be, 8); return; }
    __builtin_memcpy(ppc_host_ptr(va), &be, 8);
}

// Float variants: guest stores IEEE-754 single/double in big-endian layout.
// Translate through the matching integer swap.
static inline float MEM_RF(uint32_t va) {
    uint32_t u = MEM_R32(va); float f; __builtin_memcpy(&f, &u, 4); return f;
}
static inline void MEM_WF(uint32_t va, float f) {
    uint32_t u; __builtin_memcpy(&u, &f, 4); MEM_W32(va, u);
}
static inline double MEM_RD(uint32_t va) {
    uint64_t u = MEM_R64(va); double d; __builtin_memcpy(&d, &u, 8); return d;
}
static inline void MEM_WD(uint32_t va, double d) {
    uint64_t u; __builtin_memcpy(&u, &d, 8); MEM_W64(va, u);
}


// ---------------------------------------------------------------------------
// PPC register state. Everything the generated code touches lives here.
// ---------------------------------------------------------------------------

typedef union {
    double   f64;
    uint64_t u64;
    float    ps[2];                       // paired singles (Broadway)
} PPCFpr;

typedef struct {
    uint32_t gpr[32];
    PPCFpr   fpr[32];
    uint32_t lr;
    uint32_t ctr;
    uint32_t xer;                         // bit 29 = CA, bit 30 = OV, bit 31 = SO
    uint32_t cr;                          // packed 8x4 fields
    uint32_t msr;
    uint32_t fpscr;
} PPCContext;

// Single-threaded: the game's own "threads" (DvdStreamThreadProc etc.)
// are skipped from OSResumeThread since their wait loops desync without
// the host-level timing they assume. Matches the AC PC-port approach:
// keep the guest deliberately single-threaded, and let main do all work.
extern PPCContext g_cpu;


// ---------------------------------------------------------------------------
// CR / XER helpers. The generated code calls these by name; keep the shapes
// exactly as the recompiler emits.
// ---------------------------------------------------------------------------

// CR0 bit positions (PPC big-endian bit numbering, top-bit first):
//   bit 31 = LT, 30 = GT, 29 = EQ, 28 = SO.
// CR field N occupies bits (31 - 4N) .. (28 - 4N).
#define CR0_FROM_RESULT(signed_result) do {                    \
    int32_t  _r = (int32_t)(signed_result);                    \
    uint32_t _c = (_r < 0) ? 8u : (_r > 0) ? 4u : 2u;          \
    /* SO copies from XER.SO (bit 31 of XER) */                \
    _c |= (g_cpu.xer >> 31) & 1u;                              \
    g_cpu.cr = (g_cpu.cr & 0x0FFFFFFFu) | (_c << 28);          \
} while (0)

#define CR_SET_SIGNED(field, a, b, xer_unused) do {            \
    int32_t  _a = (int32_t)(a), _b = (int32_t)(b);             \
    uint32_t _c = (_a < _b) ? 8u : (_a > _b) ? 4u : 2u;        \
    _c |= (g_cpu.xer >> 31) & 1u;                              \
    uint32_t _shift = 28u - 4u * (uint32_t)(field);            \
    g_cpu.cr = (g_cpu.cr & ~(0xFu << _shift)) | (_c << _shift);\
    (void)(xer_unused);                                        \
} while (0)

#define CR_SET_UNSIGNED(field, a, b, xer_unused) do {          \
    uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b);           \
    uint32_t _c = (_a < _b) ? 8u : (_a > _b) ? 4u : 2u;        \
    _c |= (g_cpu.xer >> 31) & 1u;                              \
    uint32_t _shift = 28u - 4u * (uint32_t)(field);            \
    g_cpu.cr = (g_cpu.cr & ~(0xFu << _shift)) | (_c << _shift);\
    (void)(xer_unused);                                        \
} while (0)

// XER carry (bit 29 in big-endian numbering = bit 2 from MSB) lives at
// bit 29 of the 32-bit xer. Use positional-from-LSB: XER.CA is bit 29 counting
// from MSB = bit 2 from LSB. We pack it as bit 29 in our uint32 to match the
// architectural layout expected by mfxer/mtxer paths.
#define XER_CA_BIT   29u
#define XER_GET_CA() ((g_cpu.xer >> XER_CA_BIT) & 1u)
#define XER_SET_CA(cond) do {                                                 \
    if (cond) g_cpu.xer |=  (1u << XER_CA_BIT);                               \
    else      g_cpu.xer &= ~(1u << XER_CA_BIT);                               \
} while (0)


// ---------------------------------------------------------------------------
// Arithmetic helpers the recompiler inlines by name.
// ---------------------------------------------------------------------------

// ADDC: a + b, set XER.CA from 33rd bit.
static inline uint32_t ADDC(uint32_t a, uint32_t b) {
    uint64_t r = (uint64_t)a + (uint64_t)b;
    XER_SET_CA(r > 0xFFFFFFFFull);
    return (uint32_t)r;
}
// ADDE: a + b + CA, set CA from 33rd bit.
static inline uint32_t ADDE(uint32_t a, uint32_t b) {
    uint64_t r = (uint64_t)a + (uint64_t)b + (XER_GET_CA() ? 1u : 0u);
    XER_SET_CA(r > 0xFFFFFFFFull);
    return (uint32_t)r;
}
// SUBFC: b - a (i.e. ~a + b + 1), set CA from 33rd bit.
static inline uint32_t SUBFC(uint32_t a, uint32_t b) {
    uint64_t r = (uint64_t)(~a) + (uint64_t)b + 1ull;
    XER_SET_CA(r > 0xFFFFFFFFull);
    return (uint32_t)r;
}
// SUBFE: ~a + b + CA, set CA from 33rd bit.
static inline uint32_t SUBFE(uint32_t a, uint32_t b) {
    uint64_t r = (uint64_t)(~a) + (uint64_t)b + (XER_GET_CA() ? 1u : 0u);
    XER_SET_CA(r > 0xFFFFFFFFull);
    return (uint32_t)r;
}

static inline int32_t  MULHW (int32_t  a, int32_t  b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 32);
}
static inline uint32_t MULHWU(uint32_t a, uint32_t b) {
    return (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32);
}
static inline uint32_t ROTL32(uint32_t x, uint32_t n) {
    n &= 31; return (x << n) | (x >> ((32u - n) & 31u));
}
static inline uint32_t CLZ32(uint32_t x) {
    if (!x) return 32u;
#ifdef __GNUC__
    return (uint32_t)__builtin_clz(x);
#else
    uint32_t n = 0; while (!(x & 0x80000000u)) { x <<= 1; ++n; } return n;
#endif
}

// FRSQRTE: Broadway's "reciprocal square root estimate" opcode. The HW is
// a low-precision estimate (~5 bits); we hand back a full IEEE result,
// which is a tighter answer and almost always what the game actually wanted.
static inline double FRSQRTE(double x) {
    if (x <= 0.0) return 0.0;             // HW returns +Inf; 0 is safer for poked memory
    return 1.0 / sqrt(x);
}


// ---------------------------------------------------------------------------
// Paired-singles quantized load/store (psq_l / psq_st). The real opcodes use
// GQR registers (W = which GQR half, I = result type/scale). For first-pass
// correctness we assume "float32, no scale" -- if a game actually relies on
// the quantization we'll see it as broken geometry and extend then.
// ---------------------------------------------------------------------------
static inline void ppc_psq_load(int fpr_idx, uint32_t va, int W, int I) {
    (void)W; (void)I;
    g_cpu.fpr[fpr_idx].ps[0] = MEM_RF(va);
    g_cpu.fpr[fpr_idx].ps[1] = MEM_RF(va + 4);
}
static inline void ppc_psq_store(int fpr_idx, uint32_t va, int W, int I) {
    (void)W; (void)I;
    MEM_WF(va,     g_cpu.fpr[fpr_idx].ps[0]);
    MEM_WF(va + 4, g_cpu.fpr[fpr_idx].ps[1]);
}


// ---------------------------------------------------------------------------
// Control flow: indirect calls, unimplemented stubs, trace.
// ---------------------------------------------------------------------------

typedef void (*PPC_Func)(void);

// Populated by the generated func_table.c constructor.
extern PPC_Func  g_func_table_ptrs [];
extern uint32_t  g_func_table_addrs[];
extern size_t    g_func_table_count;

// Dispatch `addr` through the table. Prints + aborts for unknown addresses
// by default; see PPC_INDIRECT_SOFT_FAIL to downgrade to a warn-and-return.
PPC_Func ppc_lookup_func(uint32_t addr);
void     ppc_call_indirect(uint32_t addr);
void     ppc_jump_indirect(void);

// STATUS:PARTIAL functions emit these for unhandled mnemonics. First hit
// prints; we then continue so the guest can still make progress. Flip
// PPC_ABORT_ON_UNIMPL=1 to turn these back into hard aborts while hunting.
void ppc_unimplemented(const char *mnem, uint32_t pc);

// Trace hook, enabled with -DPPC_TRACE_ALL=1. Release builds leave the
// define at 0 so every generated function's prologue compiles away.
void ppc_trace(uint32_t pc);

// Named checkpoint marker. Cheap printf when called; meant to be sprinkled
// into generated C by hand during debugging. Strips at compile time if
// -DPPC_NO_DBG_MARK is set.
void ppc_dbg_mark(const char *tag);

// SPR catch-all (HID*, GQR*, PMC*, BATs). Unknown reads return 0. Writes
// are logged on first sight then silently accepted.
uint32_t ppc_spr_read (const char *name);
void     ppc_spr_write(const char *name, uint32_t value);

// Time base. Broadway TB ticks at bus/4 (~60.75 MHz). We derive it from
// a monotonic host clock so OSGetTime / __OSGetSystemTime return something
// moving, which keeps polling loops from spinning forever.
uint32_t ppc_time_base_tb_lower(void);
uint32_t ppc_time_base_tb_upper(void);


// ---------------------------------------------------------------------------
// Lifecycle -- wiring called from main().
// ---------------------------------------------------------------------------

// Allocate g_mem1/g_mem2 and zero them. Safe to call multiple times (no-op
// after the first) so tests can reset by calling ppc_reset_memory().
void ppc_runtime_init(void);
void ppc_reset_memory(void);

// Parse a Wii/GC PPC ELF32 file and copy its loadable segments into guest
// memory at their requested VAs. Returns 0 on success, non-zero on error.
// Writes the entry-point VA to *entry_out if non-NULL.
int  ppc_load_elf(const char *path, uint32_t *entry_out);

// Highest end-VA written into MEM1 by the most recent ppc_load_elf().
// main passes this to os_set_arena_bounds() so the SDK arena_lo lands past
// the loaded .bss instead of stomping it. Returns 0x80800000 fallback if
// no ELF has been loaded.
uint32_t ppc_loaded_mem1_top(void);

// Raw load: read `path` and memcpy its bytes into guest memory starting
// at VA `load_base`. Used for .rso / .rel modules where the recompiler
// already chose a load base and the module's code references addresses
// of the form (load_base + file_offset). Returns 0 on success.
int  ppc_load_module_image(const char *path, uint32_t load_base);

// External relocation patch: a single cross-module reference (RSO -> ELF)
// already resolved at build time. The runtime applies the patch against
// guest memory after rso_apply_internal_relocs.
//
//   file_off  -- byte offset within the module file image (matches the
//                relocation entry's r_offset, which is also where the
//                bytes live in guest memory at load_base + file_off).
//   target_va -- the imported symbol's resolved guest VA (in main ELF).
//   type      -- R_PPC_* (1=ADDR32, 4=ADDR16_LO, 6=ADDR16_HA, 10=REL24).
//   addend    -- RELA addend (added to target_va before patching).
typedef struct RecompExtPatch {
    uint32_t file_off;
    uint32_t target_va;
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t addend;
} RecompExtPatch;

// Apply N external-reloc patches against the module loaded at `load_base`.
// Mirrors the apply switch in rso_apply_internal_relocs but the symbol VA
// is supplied directly (no section-table lookup needed).
void ppc_apply_external_relocs(uint32_t load_base,
                               uint32_t n,
                               const RecompExtPatch *patches);

// Convenience: log guest-side register snapshot for crash diagnostics.
void ppc_dump_registers(void);

// For sdk/peripherals.c diagnostics -- prints recent unique PCs from the
// trace ring. Safe no-op if tracing isn't enabled.
void ppc_dump_unique_recent(uint32_t span);

// Legacy diagnostic hooks that sdk/peripherals.c fires on specific VI
// retrace ticks. If you didn't instrument call history / hotspots, these
// are safe no-ops. Kept as weak stubs so the retrace handler compiles.
void ppc_dump_recent_calls(void);
void ppc_dump_hotspots   (void);


// ---------------------------------------------------------------------------
// Include the generated forward-decl header so every caller can reference
// any func_XXXXXXXX symbol without writing extern declarations. Guarded so
// fresh trees (before the first recompile) still build runtime-only pieces.
// ---------------------------------------------------------------------------
#if defined(__has_include)
#  if __has_include("func_decls.h")
#    include "func_decls.h"
#  endif
#endif


#ifdef __cplusplus
}
#endif
