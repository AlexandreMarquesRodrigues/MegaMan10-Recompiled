#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

PPCContext g_cpu;
uint8_t *g_mem1;
uint8_t *g_mem2;
uint8_t g_deadzone[PPC_DEADZONE_SIZE];

static uint32_t g_loaded_mem1_top = 0x80800000u;
static uint32_t g_guest_stack[128];
static size_t g_guest_stack_depth;
static uint32_t g_current_guest_function;
static clock_t g_clock_epoch;
static int g_clock_started;

extern const char *ppc_function_name(uint32_t addr);
static void fatal_guest(uint32_t addr, const char *why);

typedef struct { const char *name; uint32_t value; } GuestSpr;
static GuestSpr g_sprs[] = {
    {"HID0",0}, {"HID1",0x80000000u},
    /* Iteration 1: __OSPSInit reads HID2 through PPCMfhid2 before
       configuring the other SPRs. A zero reset value takes the Wii path. */
    {"HID2",0}, {"HID4",0},
    {"GQR0",0}, {"GQR1",0}, {"GQR2",0}, {"GQR3",0},
    {"GQR4",0}, {"GQR5",0}, {"GQR6",0}, {"GQR7",0},
    {"MMCR0",0}, {"MMCR1",0}, {"PMC1",0}, {"PMC2",0},
    {"PMC3",0}, {"PMC4",0}, {"UMMCR0",0}, {"UMMCR1",0},
    {"UPMC1",0}, {"UPMC2",0}, {"UPMC3",0}, {"UPMC4",0},
    {"L2CR",0}, {"WPAR",0}, {"DMAU",0}, {"DMAL",0}, {"DEC",0},
    {"SRR0",0}, {"SRR1",0}, {"SPRG0",0}, {"SPRG1",0}, {"SPRG2",0}, {"SPRG3",0},
    {"DAR",0}, {"DSISR",0}, {"PVR",0x00083214u},
    {"IBAT0U",0},{"IBAT0L",0},{"IBAT1U",0},{"IBAT1L",0},{"IBAT2U",0},{"IBAT2L",0},{"IBAT3U",0},{"IBAT3L",0},
    {"DBAT0U",0},{"DBAT0L",0},{"DBAT1U",0},{"DBAT1L",0},{"DBAT2U",0},{"DBAT2L",0},{"DBAT3U",0},{"DBAT3L",0},
    {"IBAT4U",0},{"IBAT4L",0},{"IBAT5U",0},{"IBAT5L",0},{"IBAT6U",0},{"IBAT6L",0},{"IBAT7U",0},{"IBAT7L",0},
    {"DBAT4U",0},{"DBAT4L",0},{"DBAT5U",0},{"DBAT5L",0},{"DBAT6U",0},{"DBAT6L",0},{"DBAT7U",0},{"DBAT7L",0},
    {"DABR",0},{"EAR",0},{"IABR",0},{"ICTC",0},{"SIA",0},{"USIA",0}
};

static uint64_t host_timebase(void) {
    clock_t now = clock();
    if (!g_clock_started) { g_clock_epoch = now; g_clock_started = 1; }
    uint64_t elapsed = now >= g_clock_epoch ? (uint64_t)(now - g_clock_epoch) : 0;
    return elapsed * 60750000ull / (uint64_t)CLOCKS_PER_SEC;
}

static const char *function_name(uint32_t addr) {
    const char *name = ppc_function_name(addr);
    static char fallback[24];
    if (name) return name;
    snprintf(fallback, sizeof(fallback), "fn_%08X", addr);
    return fallback;
}

static GuestSpr *find_spr(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof(g_sprs)/sizeof(g_sprs[0]); ++i)
        if (strcmp(g_sprs[i].name, name) == 0) return &g_sprs[i];
    return NULL;
}

static void initialize_low_memory(void) {
    /* IPL/IOS values used by this game's startup: boot magic, retail board,
       MEM1 size, arena pointers and clock rates. __start at 0x80004050 first
       checks 0x800000f4 and 0x80000034 for optional apploader callbacks;
       leave both zero so it follows its no-apploader path. */
    MEM_W32(0x80000000u, 0x57525845u); /* WRXE game ID */
    MEM_W32(0x80000020u, 0x0d15ea5eu); /* IOS/BS2 boot marker */
    MEM_W32(0x80000024u, 1u);
    MEM_W32(0x80000028u, PPC_MEM1_SIZE);
    MEM_W32(0x8000002cu, 3u);          /* retail console type */
    MEM_W32(0x80000030u, (g_loaded_mem1_top + 31u) & ~31u); /* arena starts after loaded DOL/BSS */
    MEM_W32(0x80000034u, 0u);          /* no boot callback for direct DOL entry */
    MEM_W32(0x800000f0u, PPC_MEM1_SIZE);
    MEM_W32(0x800000f4u, 0u);          /* no apploader table */
    MEM_W32(0x800000f8u, 0x0e7be2c0u); /* 243 MHz Wii bus */
    MEM_W32(0x800000fcu, 0x2b73a840u); /* 729 MHz Broadway clock */
    MEM_W32(0x80000300u, 0x4c000064u);
    MEM_W32(0x80000800u, 0x4c000064u);
    MEM_W32(0x80000c00u, 0x4c000064u);
    MEM_W32(0x8000311cu, PPC_MEM2_SIZE);
    MEM_W32(0x80003128u, PPC_MEM2_BASE + PPC_MEM2_SIZE);
    MEM_W32(0x80003184u, PPC_MEM1_BASE);
    MEM_W16(0x800030e6u, 0x8201u);
    MEM_W8(0x8000315cu, 0x80u);
    MEM_W16(0x8000315eu, 0x0113u);
}

void ppc_runtime_init(void) {
    if (!g_mem1) g_mem1 = (uint8_t *)calloc(1, PPC_MEM1_SIZE);
    if (!g_mem2) g_mem2 = (uint8_t *)calloc(1, PPC_MEM2_SIZE);
    if (!g_mem1 || !g_mem2) { fprintf(stderr, "runtime: guest RAM allocation failed\n"); exit(2); }
    memset(&g_cpu, 0, sizeof(g_cpu));
    g_clock_started = 0;
    (void)host_timebase();
}

void ppc_reset_memory(void) {
    ppc_runtime_init();
    memset(g_mem1, 0, PPC_MEM1_SIZE);
    memset(g_mem2, 0, PPC_MEM2_SIZE);
    memset(g_deadzone, 0, sizeof(g_deadzone));
}

void *ppc_host_ptr(uint32_t va) {
    if (va >= PPC_MEM1_BASE && (uint64_t)va < (uint64_t)PPC_MEM1_BASE + PPC_MEM1_SIZE)
        return g_mem1 + (va - PPC_MEM1_BASE);
    if (va >= PPC_MEM1_UNCACHED_BASE && (uint64_t)va < (uint64_t)PPC_MEM1_UNCACHED_BASE + PPC_MEM1_SIZE)
        return g_mem1 + (va - PPC_MEM1_UNCACHED_BASE);
    if (va >= PPC_MEM2_BASE && (uint64_t)va < (uint64_t)PPC_MEM2_BASE + PPC_MEM2_SIZE)
        return g_mem2 + (va - PPC_MEM2_BASE);
    if (va >= PPC_MEM2_UNCACHED_BASE && (uint64_t)va < (uint64_t)PPC_MEM2_UNCACHED_BASE + PPC_MEM2_SIZE)
        return g_mem2 + (va - PPC_MEM2_UNCACHED_BASE);
    if (ppc_mmio_va(va)) fatal_guest(va, "MMIO read");
    return g_deadzone + (va & (PPC_DEADZONE_SIZE - 1));
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void copy_dol_span(FILE *f, uint32_t file_off, uint32_t va, uint32_t size) {
    if (!size) return;
    void *dst = ppc_host_ptr(va);
    if (dst == g_deadzone || (uint64_t)va + size > 0x100000000ull ||
        (dst >= (void *)g_mem1 && (uint64_t)va + size > (uint64_t)PPC_MEM1_BASE + PPC_MEM1_SIZE) ||
        (dst >= (void *)g_mem2 && (uint64_t)va + size > (uint64_t)PPC_MEM2_BASE + PPC_MEM2_SIZE)) {
        fprintf(stderr, "DOL section outside guest RAM: VA=%08x size=%08x\n", va, size); exit(2);
    }
    if (fseek(f, (long)file_off, SEEK_SET) || fread(dst, 1, size, f) != size) {
        fprintf(stderr, "DOL read failed at file offset %08x\n", file_off); exit(2);
    }
}

static uint32_t load_dol(const char *path) {
    uint8_t h[0x100];
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fread(h, 1, sizeof(h), f) != sizeof(h)) { fprintf(stderr, "short DOL header\n"); exit(2); }
    uint32_t max_mem1 = PPC_MEM1_BASE;
    for (unsigned i = 0; i < 18; ++i) {
        const unsigned off_idx = i < 7 ? i : 7 + (i - 7);
        uint32_t file_off = read_be32(h + (i < 7 ? i * 4 : 0x1c + (i - 7) * 4));
        uint32_t va = read_be32(h + (i < 7 ? 0x48 + i * 4 : 0x64 + (i - 7) * 4));
        uint32_t size = read_be32(h + (i < 7 ? 0x90 + i * 4 : 0xac + (i - 7) * 4));
        (void)off_idx;
        if (size) {
            copy_dol_span(f, file_off, va, size);
            if (va >= PPC_MEM1_BASE && va < PPC_MEM1_BASE + PPC_MEM1_SIZE && va + size > max_mem1) max_mem1 = va + size;
        }
    }
    uint32_t bss = read_be32(h + 0xd8), bss_size = read_be32(h + 0xdc);
    if (bss_size) {
        void *dst = ppc_host_ptr(bss);
        if (dst == g_deadzone || (uint64_t)bss + bss_size > 0x100000000ull) { fprintf(stderr, "DOL BSS outside guest RAM\n"); exit(2); }
        memset(dst, 0, bss_size);
        if (bss >= PPC_MEM1_BASE && bss < PPC_MEM1_BASE + PPC_MEM1_SIZE && bss + bss_size > max_mem1) max_mem1 = bss + bss_size;
    }
    fclose(f);
    g_loaded_mem1_top = max_mem1;
    return read_be32(h + 0xe0);
}

static void fatal_guest(uint32_t addr, const char *why) {
    size_t i;
    uint32_t fault_fn = ppc_lookup_func(addr) ? addr : g_current_guest_function;
    fprintf(stderr, "\nGUEST STOP: %s at %08x\n", why, addr);
    if (fault_fn) fprintf(stderr, "  function: %s (0x%08x)\n", function_name(fault_fn), fault_fn);
    fprintf(stderr, "guest backtrace (oldest to newest):\n");
    if (g_cpu.lr) {
        size_t lo = 0, hi = g_func_table_count;
        while (lo < hi) { size_t mid = lo + (hi-lo)/2; if (g_func_table_addrs[mid] <= g_cpu.lr) lo = mid+1; else hi = mid; }
        fprintf(stderr, "  guest frame #0: LR return=%08x", g_cpu.lr);
        if (lo) fprintf(stderr, " in %s (0x%08x)", function_name(g_func_table_addrs[lo-1]), g_func_table_addrs[lo-1]);
        fputc('\n', stderr);
    }
    for (i = 0; i < g_guest_stack_depth; ++i) fprintf(stderr, "  #%zu func_%08x\n", i, g_guest_stack[i]);
    fprintf(stderr, "  pc: %08x lr=%08x r1=%08x\n", addr, g_cpu.lr, g_cpu.gpr[1]);
    uint32_t frame = g_cpu.gpr[1];
    for (i = 0; i < 16 && frame >= PPC_MEM1_BASE && frame + 8 < PPC_MEM1_BASE + PPC_MEM1_SIZE; ++i) {
        uint32_t caller = MEM_R32(frame + 4), prev = MEM_R32(frame);
        if (!caller || caller == 0xffffffffu) break;
        fprintf(stderr, "  guest frame #%zu: return=%08x", i, caller);
        size_t lo = 0, hi = g_func_table_count;
        while (lo < hi) { size_t mid = lo + (hi-lo)/2; if (g_func_table_addrs[mid] <= caller) lo = mid+1; else hi = mid; }
        if (lo) fprintf(stderr, " in func_%08x", g_func_table_addrs[lo-1]);
        fputc('\n', stderr);
        if (prev <= frame || prev >= PPC_MEM1_BASE + PPC_MEM1_SIZE) break;
        frame = prev;
    }
    ppc_dump_registers();
    fflush(stderr);
    exit(1);
}

PPC_Func ppc_lookup_func(uint32_t addr) {
    size_t lo = 0, hi = g_func_table_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_func_table_addrs[mid] < addr) lo = mid + 1; else hi = mid;
    }
    return lo < g_func_table_count && g_func_table_addrs[lo] == addr ? g_func_table_ptrs[lo] : NULL;
}

void ppc_call_indirect(uint32_t addr) {
    /* CodeWarrior emits calls into the middle of these SDK save/restore
       thunks to select which callee-saved register suffix is required. The
       first boot run exposed this at OSExceptionInit -> __save_gpr+0x18. */
    /* Iteration 2: OSExceptionInit calls __save_gpr+0x18 at 0x8015fd0c.
       CodeWarrior uses the interior entry point to save only r20-r31. */
    if (addr >= 0x8015fc5cu && addr <= 0x8015fca0u && !(addr & 3u)) {
        unsigned first = 14u + (addr - 0x8015fc5cu) / 4u;
        for (unsigned r = first; r <= 31; ++r) MEM_WD(g_cpu.gpr[11] - 0x90u + (r - 14u) * 8u, g_cpu.fpr[r].f64);
        return;
    }
    if (addr >= 0x8015fca8u && addr <= 0x8015fcecu && !(addr & 3u)) {
        unsigned first = 14u + (addr - 0x8015fca8u) / 4u;
        for (unsigned r = first; r <= 31; ++r) g_cpu.fpr[r].f64 = MEM_RD(g_cpu.gpr[11] - 0x90u + (r - 14u) * 8u);
        return;
    }
    if (addr >= 0x8015fcf4u && addr <= 0x8015fd38u && !(addr & 3u)) {
        unsigned first = 14u + (addr - 0x8015fcf4u) / 4u;
        for (unsigned r = first; r <= 31; ++r) MEM_W32(g_cpu.gpr[11] - 0x48u + (r - 14u) * 4u, g_cpu.gpr[r]);
        return;
    }
    if (addr >= 0x8015fd40u && addr <= 0x8015fd84u && !(addr & 3u)) {
        unsigned first = 14u + (addr - 0x8015fd40u) / 4u;
        for (unsigned r = first; r <= 31; ++r) g_cpu.gpr[r] = MEM_R32(g_cpu.gpr[11] - 0x48u + (r - 14u) * 4u);
        return;
    }
    PPC_Func fn = ppc_lookup_func(addr);
    if (!fn) fatal_guest(addr, "unimplemented indirect function");
    if (g_guest_stack_depth < sizeof(g_guest_stack)/sizeof(g_guest_stack[0])) g_guest_stack[g_guest_stack_depth++] = addr;
    fn();
    if (g_guest_stack_depth) --g_guest_stack_depth;
}
void ppc_jump_indirect(void) {
    PPC_Func fn = ppc_lookup_func(g_cpu.ctr);
    if (!fn) fatal_guest(g_cpu.ctr, "unimplemented indirect jump");
    fn();
}
void ppc_unimplemented(const char *mnem, uint32_t pc) { fatal_guest(pc, mnem ? mnem : "unimplemented instruction"); }
void ppc_trace(uint32_t pc) { g_current_guest_function = pc; }
void ppc_dbg_mark(const char *tag) { fprintf(stderr, "guest mark: %s\n", tag ? tag : "(null)"); }
uint32_t ppc_spr_read(const char *name) {
    if (name && (!strcmp(name, "TB") || !strcmp(name, "TBL"))) return (uint32_t)host_timebase();
    if (name && !strcmp(name, "TBU")) return (uint32_t)(host_timebase() >> 32);
    GuestSpr *spr = find_spr(name);
    if (spr) return spr->value;
    fprintf(stderr, "unknown SPR read: %s\n", name ? name : "(null)");
    fatal_guest(g_current_guest_function, "unknown SPR read");
    return 0;
}
void ppc_spr_write(const char *name, uint32_t value) {
    GuestSpr *spr = find_spr(name);
    if (spr && strcmp(name, "PVR") != 0) { spr->value = value; return; }
    fprintf(stderr, "unsupported SPR write: %s value=%08x\n", name ? name : "(null)", value);
    fatal_guest(g_current_guest_function, "unsupported SPR write");
}
uint32_t ppc_time_base_tb_lower(void) { return (uint32_t)host_timebase(); }
uint32_t ppc_time_base_tb_upper(void) { return (uint32_t)(host_timebase() >> 32); }
int ppc_load_elf(const char *path, uint32_t *entry_out) { (void)path; (void)entry_out; return -1; }
uint32_t ppc_loaded_mem1_top(void) { return g_loaded_mem1_top; }
int ppc_load_module_image(const char *path, uint32_t base) { (void)path; (void)base; return -1; }
void ppc_apply_external_relocs(uint32_t base, uint32_t n, const RecompExtPatch *p) { (void)base; (void)n; (void)p; }
void ppc_dump_registers(void) {
    fprintf(stderr, "  LR=%08x CTR=%08x CR=%08x XER=%08x\n", g_cpu.lr, g_cpu.ctr, g_cpu.cr, g_cpu.xer);
    for (unsigned i=0;i<32;i+=4) fprintf(stderr,"  r%-2u=%08x r%-2u=%08x r%-2u=%08x r%-2u=%08x\n",i,g_cpu.gpr[i],i+1,g_cpu.gpr[i+1],i+2,g_cpu.gpr[i+2],i+3,g_cpu.gpr[i+3]);
}
void ppc_dump_unique_recent(uint32_t span) { (void)span; }
void ppc_dump_recent_calls(void) {}
void ppc_dump_hotspots(void) {}
/* Iteration 3 stops here: the first I/O write observed is 0xCC003004 from
   __fill_mem during __OSInterruptInit. This is the Marco 5 stopping point. */
void ppc_mmio_write(uint32_t va, const void *data, int n) { (void)data;(void)n; fatal_guest(va,"MMIO write (first hardware I/O)"); }
bool ppc_mmio_va(uint32_t va) { return va >= 0xcc000000u && va < 0xce000000u; }

int main(void) {
    ppc_runtime_init();
    uint32_t entry = load_dol("private/main.dol");
    initialize_low_memory();
    if (entry != 0x80004050u) fprintf(stderr, "DOL entry is %08x; requested entry is 80004050\n", entry);
    g_cpu.gpr[1] = 0x817ff000u;
    g_cpu.gpr[2] = 0x803e0000u;
    g_cpu.gpr[13] = 0x803dfed0u;
    PPC_Func start = ppc_lookup_func(0x80004050u);
    if (!start) fatal_guest(0x80004050u, "entry function missing");
    start();
    fatal_guest(g_cpu.lr, "guest entry returned");
    return 1;
}
