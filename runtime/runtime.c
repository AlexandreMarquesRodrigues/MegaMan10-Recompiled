#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PPCContext g_cpu;
uint8_t *g_mem1;
uint8_t *g_mem2;
uint8_t g_deadzone[PPC_DEADZONE_SIZE];

static uint32_t g_loaded_mem1_top = 0x80800000u;
static uint32_t g_guest_stack[128];
static size_t g_guest_stack_depth;
static uint32_t g_current_guest_function;

static const char *function_name(uint32_t addr) {
    if (addr == 0x80136c30u) return "PPCMfhid2";
    if (addr == 0x80138200u) return "__OSPSInit";
    return NULL;
}

void ppc_runtime_init(void) {
    if (!g_mem1) g_mem1 = (uint8_t *)calloc(1, PPC_MEM1_SIZE);
    if (!g_mem2) g_mem2 = (uint8_t *)calloc(1, PPC_MEM2_SIZE);
    if (!g_mem1 || !g_mem2) { fprintf(stderr, "runtime: guest RAM allocation failed\n"); exit(2); }
    memset(&g_cpu, 0, sizeof(g_cpu));
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
    if (fault_fn) fprintf(stderr, "  function: %s (func_%08x)\n",
                          function_name(fault_fn) ? function_name(fault_fn) : "generated", fault_fn);
    fprintf(stderr, "guest backtrace (oldest to newest):\n");
    if (g_cpu.lr) {
        size_t lo = 0, hi = g_func_table_count;
        while (lo < hi) { size_t mid = lo + (hi-lo)/2; if (g_func_table_addrs[mid] <= g_cpu.lr) lo = mid+1; else hi = mid; }
        fprintf(stderr, "  guest frame #0: LR return=%08x", g_cpu.lr);
        if (lo) fprintf(stderr, " in %s (func_%08x)", function_name(g_func_table_addrs[lo-1]) ? function_name(g_func_table_addrs[lo-1]) : "generated", g_func_table_addrs[lo-1]);
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
uint32_t ppc_spr_read(const char *name) { fprintf(stderr, "unimplemented SPR read: %s\n", name ? name : "?"); fatal_guest(g_current_guest_function, name ? name : "unimplemented SPR read"); return 0; }
void ppc_spr_write(const char *name, uint32_t value) { (void)value; fprintf(stderr, "unimplemented SPR write: %s\n", name ? name : "?"); fatal_guest(g_current_guest_function, name ? name : "unimplemented SPR write"); }
uint32_t ppc_time_base_tb_lower(void) { return 0; }
uint32_t ppc_time_base_tb_upper(void) { return 0; }
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
void ppc_mmio_write(uint32_t va, const void *data, int n) { (void)data;(void)n; fatal_guest(va,"unimplemented MMIO write"); }
bool ppc_mmio_va(uint32_t va) { return va >= 0xcc000000u && va < 0xcd000000u; }

int main(void) {
    ppc_runtime_init();
    uint32_t entry = load_dol("private/main.dol");
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
