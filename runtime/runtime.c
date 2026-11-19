#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

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
static uint32_t g_trace_ring[512];
static size_t g_trace_next, g_trace_count;
static int g_boot_active;
static clock_t g_boot_clock_start;
static uint64_t g_boot_wall_start;
static uint32_t g_last_mmio_read;
static uint64_t g_same_mmio_read_count;
static uint32_t g_mmio_stop_blocks;
static int g_mmio_stop_initialized;
#define DVD_CB_SLOT 0x8067b7e8u
static int g_watch_dvd_init_seen;

#define MMIO_CC_BASE 0xcc000000u
#define MMIO_CC_SIZE 0x00010000u
#define MMIO_CD_BASE 0xcd000000u
#define MMIO_CD_SIZE 0x00900000u
#define MMIO_RECORDS 16384u
static uint8_t g_mmio_cc[MMIO_CC_SIZE];
static uint8_t g_mmio_cd[MMIO_CD_SIZE];
typedef struct {
    uint32_t addr, first_lr, first_function, lr, function;
    uint64_t first_value, reads, writes;
    uint8_t first_is_write, used;
} MMIORecord;
static MMIORecord g_mmio_records[MMIO_RECORDS];
typedef struct { uint32_t addr, lr, function; uint64_t value; uint8_t write; } MMIOEvent;
static MMIOEvent g_mmio_recent[20];
static size_t g_mmio_recent_next, g_mmio_recent_count;

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

#ifdef MM10_WATCH
static void watch_dump_words(const char *tag, uint32_t first, uint32_t end) {
    fprintf(stderr, "MM10_WATCH_WORDS %s %08x..%08x\n", tag, first, end);
    for (uint32_t a = first; a < end; a += 4)
        fprintf(stderr, "  %08x=%08x\n", a, MEM_R32(a));
}
static void watch_dump_boot_state(const char *phase) {
    fprintf(stderr, "MM10_WATCH_STATE %s r13=%08x loaded_mem1_top=%08x\n",
            phase, g_cpu.gpr[13], g_loaded_mem1_top);
    watch_dump_words("lowmem_00000028_40", 0x80000028u, 0x80000044u);
    watch_dump_words("lowmem_000000f0_100", 0x800000f0u, 0x80000100u);
    watch_dump_words("lowmem_00003100_140", 0x80003100u, 0x80003140u);
    watch_dump_words("sda_mem2_lo_neighbors", g_cpu.gpr[13] - 0x54e0u, g_cpu.gpr[13] - 0x54c8u);
    watch_dump_words("sda_mem2_hi_neighbors", g_cpu.gpr[13] - 0x3fc0u, g_cpu.gpr[13] - 0x3fa0u);
}
static void watch_snapshot(const char *phase) {
    uint8_t *p = (uint8_t *)ppc_host_ptr(DVD_CB_SLOT - 8u);
    fprintf(stderr, "MM10_WATCH_SNAPSHOT %s addr=%08x bytes=", phase, DVD_CB_SLOT - 8u);
    for (unsigned i = 0; i < 16; ++i) fprintf(stderr, "%02x", p[i]);
    fprintf(stderr, " slot=%08x\n", MEM_R32(DVD_CB_SLOT));
}
void ppc_watch_write(uint32_t va, const void *bytes, uint32_t nbytes) {
    uint64_t end = (uint64_t)va + nbytes;
    uint64_t slot_end = (uint64_t)DVD_CB_SLOT + 4u;
    if (!nbytes || (uint64_t)va >= slot_end || end <= DVD_CB_SLOT) return;
    const uint8_t *src = (const uint8_t *)bytes;
    uint32_t lo = va > DVD_CB_SLOT ? va : DVD_CB_SLOT;
    uint32_t hi = end < slot_end ? (uint32_t)end : (uint32_t)slot_end;
    fprintf(stderr, "MM10_WATCH_WRITE va=%08x size=%u overlap=%08x bytes=", va, nbytes, lo);
    for (uint32_t a = lo; a < hi; ++a) fprintf(stderr, "%02x", src[a - va]);
    fprintf(stderr, " LR=%08x function=%s@%08x r3=%08x r4=%08x\n", g_cpu.lr,
            function_name(g_current_guest_function), g_current_guest_function,
            g_cpu.gpr[3], g_cpu.gpr[4]);
}
#endif

static uint8_t *mmio_shadow_ptr(uint32_t va) {
    if (va >= MMIO_CC_BASE && va < MMIO_CC_BASE + MMIO_CC_SIZE) return g_mmio_cc + (va - MMIO_CC_BASE);
    if (va >= MMIO_CD_BASE && va < MMIO_CD_BASE + MMIO_CD_SIZE) return g_mmio_cd + (va - MMIO_CD_BASE);
    return NULL;
}

static unsigned mmio_block(uint32_t va) {
    /* Wii maps the Flipper-style offsets in the CD Hollywood window too;
       e.g. CD00680C is EXI channel-0 control, not an IPC register. */
    if ((va >= MMIO_CD_BASE + 0x6000u && va < MMIO_CD_BASE + 0x7000u))
        va = 0xcc000000u | (va & 0xffffu);
    if (va >= 0xcc002000u && va < 0xcc003000u) return 0; /* VI */
    if (va >= 0xcc003000u && va < 0xcc004000u) return 1; /* PI */
    if (va >= 0xcc004000u && va < 0xcc005000u) return 2; /* MI */
    if (va >= 0xcc005000u && va < 0xcc006000u) return 3; /* DSP */
    if (va >= 0xcc006000u && va < 0xcc006400u) return 4; /* DI */
    if (va >= 0xcc006400u && va < 0xcc006800u) return 5; /* SI */
    if (va >= 0xcc006800u && va < 0xcc006c00u) return 6; /* EXI */
    if (va >= 0xcc006c00u && va < 0xcc007000u) return 7; /* AI */
    if (va >= MMIO_CD_BASE && va < MMIO_CD_BASE + MMIO_CD_SIZE) return 8; /* IOS/Hollywood IPC window */
    return 9;
}

static const char *const g_mmio_block_names[] = {"VI","PI","MI","DSP","DI","SI","EXI","AI","IPC","other"};

static MMIORecord *mmio_record(uint32_t va) {
    uint32_t slot = (va * 2654435761u) & (MMIO_RECORDS - 1u);
    for (uint32_t n = 0; n < MMIO_RECORDS; ++n) {
        MMIORecord *r = &g_mmio_records[(slot + n) & (MMIO_RECORDS - 1u)];
        if (!r->used) { r->used = 1; r->addr = va; return r; }
        if (r->addr == va) return r;
    }
    return NULL;
}

static void check_watchdog(void) {
    if (!g_boot_active) return;
#ifdef _WIN32
    if (GetTickCount64() >= g_boot_wall_start + 10000ull)
#else
    if ((uint64_t)(clock() - g_boot_clock_start) * 1000ull >= (uint64_t)CLOCKS_PER_SEC * 10000ull)
#endif
        fatal_guest(g_current_guest_function, "boot watchdog: execution exceeded 10 seconds");
}

static void record_mmio(uint32_t va, int write, uint64_t value) {
    MMIOEvent *event = &g_mmio_recent[g_mmio_recent_next];
    event->addr = va; event->lr = g_cpu.lr; event->function = g_current_guest_function;
    event->value = value; event->write = (uint8_t)write;
    g_mmio_recent_next = (g_mmio_recent_next + 1u) % 20u;
    if (g_mmio_recent_count < 20u) ++g_mmio_recent_count;
    MMIORecord *r = mmio_record(va);
    if (!r) { fatal_guest(va, "MMIO telemetry table full"); return; }
    if (r->reads + r->writes == 0) {
        r->first_value = value;
        r->first_is_write = (uint8_t)write;
        r->first_lr = g_cpu.lr;
        r->first_function = g_current_guest_function;
    }
    if (write) { ++r->writes; g_same_mmio_read_count = 0; }
    else {
        ++r->reads;
        if (g_same_mmio_read_count && g_last_mmio_read == va) ++g_same_mmio_read_count;
        else { g_last_mmio_read = va; g_same_mmio_read_count = 1; }
    }
    r->lr = g_cpu.lr;
    r->function = g_current_guest_function;
    if (r->reads + r->writes == 1)
        fprintf(stderr, "MM10_WATCH addr=%08x %c value=%016llx LR=%08x function=%s@%08x\n",
                va, write ? 'W' : 'R', (unsigned long long)value, g_cpu.lr,
                function_name(g_current_guest_function), g_current_guest_function);
    if (!g_mmio_stop_initialized) {
        const char *stop = getenv("MM10_STOP_ON_MMIO");
        if (stop && strcmp(stop, "VI") == 0) g_mmio_stop_blocks = 1u << 0;
        if (stop && strcmp(stop, "GX") == 0) g_mmio_stop_blocks = 1u << 10;
        g_mmio_stop_initialized = 1;
    }
    int is_gx = (va >= 0xcc000000u && va < 0xcc002000u) ||
                (va >= 0xcc008000u && va < 0xcc009000u);
    if (g_watch_dvd_init_seen && ((is_gx && (g_mmio_stop_blocks & (1u << 10))) ||
                                  (!is_gx && (g_mmio_stop_blocks & (1u << mmio_block(va))))))
        fatal_guest(va, "MM10_STOP_ON_MMIO requested stop");
    if (g_same_mmio_read_count > 100000ull)
        fatal_guest(va, "MMIO wait loop: same address read more than 100000 times consecutively");
}

static void dump_mmio_telemetry(void) {
    uint64_t reads[10] = {0}, writes[10] = {0};
    unsigned distinct[10] = {0};
    fprintf(stderr, "MMIO shadow telemetry (no device behavior):\n");
    fprintf(stderr, "last %zu MMIO accesses (oldest to newest):\n", g_mmio_recent_count);
    for (size_t j = 0; j < g_mmio_recent_count; ++j) {
        MMIOEvent *e = &g_mmio_recent[(g_mmio_recent_next + 20u - g_mmio_recent_count + j) % 20u];
        fprintf(stderr, "  %08x %c %016llx LR=%08x function~%s@%08x\n", e->addr,
                e->write ? 'W' : 'R', (unsigned long long)e->value, e->lr,
                function_name(e->function), e->function);
    }
    for (unsigned i = 0; i < MMIO_RECORDS; ++i) if (g_mmio_records[i].used) {
        MMIORecord *r = &g_mmio_records[i];
        unsigned b = mmio_block(r->addr);
        reads[b] += r->reads; writes[b] += r->writes; ++distinct[b];
    }
    fprintf(stderr, "  block  distinct   reads   writes\n");
    for (unsigned b = 0; b < 9; ++b)
        fprintf(stderr, "  %-5s  %8u %7llu %8llu\n", g_mmio_block_names[b], distinct[b],
                (unsigned long long)reads[b], (unsigned long long)writes[b]);
    fprintf(stderr, "  distinct MMIO addresses:\n");
    for (unsigned i = 0; i < MMIO_RECORDS; ++i) if (g_mmio_records[i].used) {
        MMIORecord *r = &g_mmio_records[i];
        fprintf(stderr, "    %-3s %08x first=%c:%016llx count=%llu (R=%llu W=%llu) first_LR=%08x function~%s@%08x last_LR=%08x last_function~%s@%08x\n",
                g_mmio_block_names[mmio_block(r->addr)], r->addr,
                r->first_is_write ? 'W' : 'R', (unsigned long long)r->first_value,
                (unsigned long long)(r->reads+r->writes), (unsigned long long)r->reads,
                (unsigned long long)r->writes, r->first_lr, function_name(r->first_function),
                r->first_function, r->lr, function_name(r->function), r->function);
    }
}

static void dump_recent_functions(void) {
    uint32_t unique[16];
    size_t n = 0;
    for (size_t back = 0; back < g_trace_count && n < 16; ++back) {
        size_t idx = (g_trace_next + 512u - 1u - back) % 512u;
        uint32_t addr = g_trace_ring[idx];
        int found = 0;
        for (size_t j = 0; j < n; ++j) if (unique[j] == addr) { found = 1; break; }
        if (!found) unique[n++] = addr;
    }
    fprintf(stderr, "last %zu distinct guest functions (oldest to newest):\n", n);
    while (n) { uint32_t addr = unique[--n]; fprintf(stderr, "  %s @ %08x\n", function_name(addr), addr); }
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
    /* Wii IOS low-memory layout from Dolphin's IOS.cpp address constants. */
    MEM_W32(0x80003100u, PPC_MEM1_SIZE); /* MEM1 physical size */
    MEM_W32(0x80003104u, PPC_MEM1_SIZE); /* MEM1 simulated size */
    MEM_W32(0x80003108u, PPC_MEM1_BASE + PPC_MEM1_SIZE); /* MEM1 end */
    MEM_W32(0x8000310cu, 0u); /* MEM1 arena begin */
    MEM_W32(0x80003110u, PPC_MEM1_BASE + PPC_MEM1_SIZE); /* MEM1 arena end */
    MEM_W32(0x80003114u, 0xdeadbeefu); /* Dolphin IOS placeholder PH1 */
    MEM_W32(0x80003118u, PPC_MEM2_SIZE); /* MEM2 physical size */
    MEM_W32(0x8000311cu, PPC_MEM2_SIZE); /* MEM2 simulated size */
    MEM_W32(0x80003120u, PPC_MEM2_BASE + PPC_MEM2_SIZE); /* MEM2 end */
    MEM_W32(0x80003124u, PPC_MEM2_BASE + 0x800u); /* MEM2 arena begin */
    MEM_W32(0x80003128u, PPC_MEM2_BASE + PPC_MEM2_SIZE); /* MEM2 arena end */
    MEM_W32(0x8000312cu, 0xdeadbeefu); /* Dolphin IOS placeholder PH2 */
    MEM_W32(0x80003130u, 0u); /* no IOS IPC buffer in direct-DOL runtime */
    MEM_W32(0x80003134u, 0u);
    MEM_W32(0x80003138u, 0u);
    MEM_W32(0x8000313cu, 0xdeadbeefu); /* Dolphin IOS placeholder PH3 */
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
    if (ppc_mmio_va(va)) return mmio_shadow_ptr(va);
    return g_deadzone + (va & (PPC_DEADZONE_SIZE - 1));
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t dol_crc32(const uint8_t *data, uint32_t size) {
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static uint32_t dol_file_crc32(FILE *f, uint32_t file_off, uint32_t size) {
    uint8_t block[4096];
    uint32_t crc = 0xffffffffu;
    if (fseek(f, (long)file_off, SEEK_SET)) return 0;
    while (size) {
        uint32_t n = size < sizeof(block) ? size : (uint32_t)sizeof(block);
        if (fread(block, 1, n, f) != n) return 0;
        for (uint32_t i = 0; i < n; ++i) {
            crc ^= block[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
        size -= n;
    }
    return ~crc;
}

static void audit_dol_data(FILE *f, const uint8_t *h, const char *phase) {
    fprintf(stderr, "DOL_AUDIT phase=%s slot[803daa28]=%08x observed_context_slot[8067b798]=%08x\n",
            phase, MEM_R32(0x803daa28u), MEM_R32(0x8067b798u));
    for (unsigned i = 0; i < 11; ++i) {
        uint32_t off = read_be32(h + 0x1c + i * 4);
        uint32_t va = read_be32(h + 0x64 + i * 4);
        uint32_t size = read_be32(h + 0xac + i * 4);
        if (!size) continue;
        uint8_t *ram = (uint8_t *)ppc_host_ptr(va);
        uint32_t file_crc = dol_file_crc32(f, off, size);
        uint32_t ram_crc = dol_crc32(ram, size);
        fprintf(stderr, "DOL_AUDIT phase=%s data%u va=%08x size=%08x file_crc=%08x ram_crc=%08x match=%s\n",
                phase, i, va, size, file_crc, ram_crc, file_crc == ram_crc ? "yes" : "no");
    }
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
#ifdef MM10_WATCH
    ppc_watch_write(va, dst, size);
#endif
}

static uint32_t load_dol(const char *path) {
    uint8_t h[0x100];
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fread(h, 1, sizeof(h), f) != sizeof(h)) { fprintf(stderr, "short DOL header\n"); exit(2); }
    uint32_t max_mem1 = PPC_MEM1_BASE;
    uint32_t bss = read_be32(h + 0xd8), bss_size = read_be32(h + 0xdc);
    const char *audit_env = getenv("MM10_DOL_AUDIT");
    int audit = audit_env && audit_env[0] == '1' && audit_env[1] == '\0';
    if (bss_size) {
        void *dst = ppc_host_ptr(bss);
        if (dst == g_deadzone || (uint64_t)bss + bss_size > 0x100000000ull) { fprintf(stderr, "DOL BSS outside guest RAM\n"); exit(2); }
        memset(dst, 0, bss_size);
#ifdef MM10_WATCH
        ppc_watch_write(bss, dst, bss_size);
#endif
        if (bss >= PPC_MEM1_BASE && bss < PPC_MEM1_BASE + PPC_MEM1_SIZE && bss + bss_size > max_mem1) max_mem1 = bss + bss_size;
    }
    if (audit) audit_dol_data(f, h, "after_bss_before_copy");
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
    if (audit) audit_dol_data(f, h, "after_copy");
    fclose(f);
    g_loaded_mem1_top = max_mem1;
#ifdef MM10_WATCH
    watch_snapshot("after_dol_load");
#endif
    return read_be32(h + 0xe0);
}

static void fatal_guest(uint32_t addr, const char *why) {
#ifdef MM10_WATCH
    watch_snapshot("stop");
#endif
    size_t i;
    uint32_t fault_fn = ppc_lookup_func(addr) ? addr : g_current_guest_function;
    fprintf(stderr, "\nGUEST STOP: %s at %08x\n", why, addr);
    if (fault_fn) fprintf(stderr, "  function: %s (0x%08x)\n", function_name(fault_fn), fault_fn);
    if (g_trace_count) fprintf(stderr, "  note: current function name is inferred from the last ppc_trace entry and may be stale within that function.\n");
    dump_recent_functions();
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
    dump_mmio_telemetry();
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
    if (addr == 0x8016779cu) {
        static int osreport_line_start = 1;
        uint32_t text_va = g_cpu.gpr[4];
        uint32_t text_size = g_cpu.gpr[5];
        const uint8_t *text = (const uint8_t *)ppc_host_ptr(text_va);
        for (uint32_t i = 0; i < text_size; ++i) {
            if (osreport_line_start) { fputs("[OSReport] ", stdout); osreport_line_start = 0; }
            fputc(text[i], stdout);
            if (text[i] == '\n') osreport_line_start = 1;
        }
        fflush(stdout);
        g_cpu.gpr[3] = 1;
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
void ppc_trace(uint32_t pc) {
    g_current_guest_function = pc;
    g_trace_ring[g_trace_next] = pc;
    g_trace_next = (g_trace_next + 1u) % 512u;
    if (g_trace_count < 512u) ++g_trace_count;
    if (pc == 0x801462a0u) g_watch_dvd_init_seen = 1;
#ifdef MM10_WATCH
    if (pc == 0x801462a0u) {
        watch_snapshot("DVDInit_entry");
    }
    if (pc == 0x801373d0u) watch_dump_boot_state("ClearMEM2Arena_entry");
    if (pc == 0x801394d0u) {
        uint32_t r3 = g_cpu.gpr[3], r4 = g_cpu.gpr[4];
        uint64_t lines = r4 ? ((uint64_t)(r4 + (r3 & 31u) + 31u) >> 5) : 0;
        uint32_t first = r3 & ~31u;
        fprintf(stderr, "MM10_WATCH_DCZeroRange_ENTRY r3=%08x r4=%08x lines=%llu zero=[%08x,%08llx)\n",
                r3, r4, (unsigned long long)lines, first,
                (unsigned long long)((uint64_t)first + lines * 32u));
        watch_dump_boot_state("DCZeroRange_entry");
    }
#endif
    check_watchdog();
}
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
void ppc_mmio_write(uint32_t va, const void *data, int n) {
    uint8_t *shadow = mmio_shadow_ptr(va);
    if (!shadow || n < 1 || n > 8 || !mmio_shadow_ptr(va + (uint32_t)n - 1u)) {
        fatal_guest(va, "MMIO write outside shadow ranges"); return;
    }
    memcpy(shadow, data, (size_t)n);
    uint64_t value = 0;
    for (int i = 0; i < n; ++i) value = (value << 8) | shadow[i];
    record_mmio(va, 1, value);
    check_watchdog();
}
uint64_t ppc_mmio_read(uint32_t va, int n) {
    uint8_t *shadow = mmio_shadow_ptr(va);
    if (!shadow || n < 1 || n > 8 || !mmio_shadow_ptr(va + (uint32_t)n - 1u)) {
        fatal_guest(va, "MMIO read outside shadow ranges"); return 0;
    }
    uint64_t value = 0;
    for (int i = 0; i < n; ++i) value = (value << 8) | shadow[i];
    record_mmio(va, 0, value);
    check_watchdog();
    return value;
}
bool ppc_mmio_va(uint32_t va) { return mmio_shadow_ptr(va) != NULL; }

#ifdef _WIN32
static DWORD WINAPI boot_watchdog_thread(LPVOID unused) {
    (void)unused;
    Sleep(10000);
    if (g_boot_active) fatal_guest(g_current_guest_function, "boot watchdog: execution exceeded 10 seconds");
    return 0;
}
#endif

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
    g_boot_active = 1;
    g_boot_clock_start = clock();
#ifdef _WIN32
    g_boot_wall_start = GetTickCount64();
    if (!CreateThread(NULL, 0, boot_watchdog_thread, NULL, 0, NULL))
        fprintf(stderr, "warning: 10-second watchdog thread could not start\n");
#endif
    start();
    fatal_guest(g_cpu.lr, "guest entry returned");
    return 1;
}
