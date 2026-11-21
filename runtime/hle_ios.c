/* HLE do IOS: dispositivos /dev/* minimos, ES, entrega adiada de callbacks
   e estado de interrupcoes (OSDisable/Enable/RestoreInterrupts). */
#include "runtime.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOS_MAX_FDS 32
#define IOS_ENOENT  (-106)   /* ISFS_ENOENT */

static char g_ios_path[IOS_MAX_FDS][48];
static int  g_ios_used[IOS_MAX_FDS];

static void ios_ret(int32_t v) { g_cpu.gpr[3] = (uint32_t)v; }

static void ios_read_cstr(uint32_t va, char *dst, size_t n) {
    size_t i = 0;
    while (va && i + 1 < n) {
        char c = (char)MEM_R8(va + (uint32_t)i);
        if (!c) break;
        dst[i++] = c;
    }
    dst[i] = 0;
}

static const char *ios_fd_path(int32_t fd) {
    int idx = fd - 0x100;
    return (idx >= 0 && idx < IOS_MAX_FDS && g_ios_used[idx]) ? g_ios_path[idx] : "?";
}

static void ios_zero(uint32_t va, uint32_t len) {
    if (!va || len > 0x100000u) return;
    for (uint32_t i = 0; i < len; ++i) MEM_W8(va + i, 0);
}

/* So os dispositivos /dev/* existem; arquivos da NAND ainda nao. */
static int32_t ios_do_open(const char *path) {
    int idx = -1;
    if (strncmp(path, "/dev/", 5) != 0) return IOS_ENOENT;
    for (int i = 0; i < IOS_MAX_FDS; ++i) if (!g_ios_used[i]) { idx = i; break; }
    if (idx < 0) return -5;
    g_ios_used[idx] = 1;
    snprintf(g_ios_path[idx], sizeof g_ios_path[idx], "%s", path);
    return 0x100 + idx;
}

static void ios_do_close(int32_t fd) {
    int idx = fd - 0x100;
    if (idx >= 0 && idx < IOS_MAX_FDS) g_ios_used[idx] = 0;
}

/* ---- interrupcoes e callbacks pendentes -------------------------------- */
typedef struct { uint32_t cb; int32_t result; uint32_t data; } PendingCb;
#define PENDING_MAX 64
static PendingCb g_pending[PENDING_MAX];
static unsigned g_pending_head, g_pending_count;
static int g_ee;          /* 1 = interrupcoes habilitadas (MSR[EE]); comeca desligada */
static int g_delivering;

static void ios_queue_cb(uint32_t cb, int32_t result, uint32_t data) {
    if (!cb) return;
    if (g_pending_count >= PENDING_MAX) { printf("[IOS] fila de callbacks cheia\n"); fflush(stdout); return; }
    PendingCb *p = &g_pending[(g_pending_head + g_pending_count++) % PENDING_MAX];
    p->cb = cb; p->result = result; p->data = data;
}

/* Imita uma interrupcao: salva a CPU, roda cada callback como o IOS faria, restaura. */
static void deliver_pending(void) {
    if (g_delivering) return;
    g_delivering = 1;
    while (g_pending_count) {
        PendingCb p = g_pending[g_pending_head];
        g_pending_head = (g_pending_head + 1) % PENDING_MAX;
        --g_pending_count;
        PPCContext saved = g_cpu;
        int saved_ee = g_ee;
        g_ee = 0;
        g_cpu.gpr[1] = (saved.gpr[1] - 0x400u) & ~7u;
        g_cpu.gpr[3] = (uint32_t)p.result;
        g_cpu.gpr[4] = p.data;
        printf("[IOS] callback 0x%08x(result=%d, data=0x%08x)\n",
               (unsigned)p.cb, (int)p.result, (unsigned)p.data);
        fflush(stdout);
        ppc_call_indirect(p.cb);
        g_cpu = saved;
        g_ee = saved_ee;
    }
    g_delivering = 0;
}

void hle_OSDisableInterrupts(void) { g_cpu.gpr[3] = (uint32_t)g_ee; g_ee = 0; }
void hle_OSEnableInterrupts(void)  { g_cpu.gpr[3] = (uint32_t)g_ee; g_ee = 1; deliver_pending(); }
void hle_OSRestoreInterrupts(void) {
    int level = g_cpu.gpr[3] != 0;
    g_cpu.gpr[3] = (uint32_t)g_ee;
    g_ee = level;
    if (level) deliver_pending();
}

/* ---- ES (/dev/es): so o minimo que o boot consulta (ioctlv: WiiBrew) ----- */
#define MM10_TITLE_HI 0x00010001u  /* WiiWare (titulos da loja) */
#define MM10_TITLE_LO 0x57525845u  /* 'WRXE' */

static int ios_es_ioctlv(uint32_t cmd, uint32_t inc, uint32_t outc, uint32_t vec) {
    if (cmd == 0x20 && inc == 0 && outc == 1) {            /* ES_GetTitleId(u64 *out) */
        uint32_t out = MEM_R32(vec);
        MEM_W32(out, MM10_TITLE_HI);
        MEM_W32(out + 4, MM10_TITLE_LO);
        printf("[IOS] ES_GetTitleId -> %08x-%08x\n", MM10_TITLE_HI, MM10_TITLE_LO);
        fflush(stdout);
        return 1;
    }
    if (cmd == 0x1d && inc == 1 && outc == 1) {            /* ES_GetDataDir(u64 titleID, char *out) */
        uint32_t tid = MEM_R32(vec);
        uint32_t hi = MEM_R32(tid), lo = MEM_R32(tid + 4);
        uint32_t out = MEM_R32(vec + 8), len = MEM_R32(vec + 12);
        char path[40];
        snprintf(path, sizeof path, "/title/%08x/%08x/data", (unsigned)hi, (unsigned)lo);
        for (uint32_t i = 0; i < len && i < 30u; ++i)
            MEM_W8(out + i, i < strlen(path) ? (uint8_t)path[i] : 0);
        printf("[IOS] ES_GetDataDir(%08x-%08x) -> %s\n", (unsigned)hi, (unsigned)lo, path);
        fflush(stdout);
        return 1;
    }
    return 0;
}

/* ---- chamadas sincronas ------------------------------------------------- */
void hle_IOS_Open(void) {
    char path[96];
    ios_read_cstr(g_cpu.gpr[3], path, sizeof path);
    int32_t fd = ios_do_open(path);
    printf("[IOS] open \"%s\" mode=%u -> %d\n", path, (unsigned)g_cpu.gpr[4], (int)fd);
    fflush(stdout);
    ios_ret(fd);
}

void hle_IOS_Close(void) {
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] close fd=%d (%s)\n", fd, ios_fd_path(fd));
    fflush(stdout);
    ios_do_close(fd);
    ios_ret(0);
}

void hle_IOS_Read(void) {   /* (fd, buf, len): sem dados (EOF) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] read fd=%d (%s) len=%u -> 0\n", fd, ios_fd_path(fd), (unsigned)g_cpu.gpr[5]);
    fflush(stdout);
    ios_ret(0);
}

void hle_IOS_Write(void) {  /* (fd, buf, len): aceita tudo */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    uint32_t len = g_cpu.gpr[5];
    printf("[IOS] write fd=%d (%s) len=%u\n", fd, ios_fd_path(fd), (unsigned)len);
    fflush(stdout);
    ios_ret((int32_t)len);
}

void hle_IOS_Seek(void) {
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] seek fd=%d (%s) off=%d whence=%d\n", fd, ios_fd_path(fd),
           (int)g_cpu.gpr[4], (int)g_cpu.gpr[5]);
    fflush(stdout);
    ios_ret(0);
}

void hle_IOS_Ioctl(void) {  /* (fd, cmd, in, inLen, out, outLen) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] ioctl fd=%d (%s) cmd=0x%x in=%u out=%u\n", fd, ios_fd_path(fd),
           (unsigned)g_cpu.gpr[4], (unsigned)g_cpu.gpr[6], (unsigned)g_cpu.gpr[8]);
    fflush(stdout);
    ios_zero(g_cpu.gpr[7], g_cpu.gpr[8]);
    ios_ret(0);
}

/* Trata um ioctlv: ES conhecido, o resto zera as saidas. */
static void ios_ioctlv_common(int32_t fd, uint32_t cmd, uint32_t inc, uint32_t outc, uint32_t vec) {
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlv fd=%d (%s) cmd=0x%x in=%u out=%u\n", fd, path,
           (unsigned)cmd, (unsigned)inc, (unsigned)outc);
    fflush(stdout);
    if (strcmp(path, "/dev/es") == 0 && ios_es_ioctlv(cmd, inc, outc, vec)) return;
    for (uint32_t i = inc; i < inc + outc && i < 16; ++i)
        ios_zero(MEM_R32(vec + 8u * i), MEM_R32(vec + 8u * i + 4u));
}

void hle_IOS_Ioctlv(void) { /* (fd, cmd, inCount, outCount, vec) */
    ios_ioctlv_common((int32_t)g_cpu.gpr[3], g_cpu.gpr[4], g_cpu.gpr[5], g_cpu.gpr[6], g_cpu.gpr[7]);
    ios_ret(0);
}

/* ---- chamadas assincronas: devolvem 0 e entregam o callback depois ------- */
void hle_IOS_OpenAsync(void) {          /* (path, mode, cb, data) */
    char path[96];
    ios_read_cstr(g_cpu.gpr[3], path, sizeof path);
    int32_t fd = ios_do_open(path);
    printf("[IOS] openAsync \"%s\" mode=%u -> %d (cb=0x%08x)\n", path,
           (unsigned)g_cpu.gpr[4], (int)fd, (unsigned)g_cpu.gpr[5]);
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[5], fd, g_cpu.gpr[6]);
    ios_ret(0);
}

void hle_IOS_CloseAsync(void) {         /* (fd, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] closeAsync fd=%d (%s)\n", fd, ios_fd_path(fd));
    fflush(stdout);
    ios_do_close(fd);
    ios_queue_cb(g_cpu.gpr[4], 0, g_cpu.gpr[5]);
    ios_ret(0);
}

void hle_IOS_ReadAsync(void) {          /* (fd, buf, len, cb, data): sem dados */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] readAsync fd=%d (%s) len=%u -> 0\n", fd, ios_fd_path(fd), (unsigned)g_cpu.gpr[5]);
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[6], 0, g_cpu.gpr[7]);
    ios_ret(0);
}

void hle_IOS_WriteAsync(void) {         /* (fd, buf, len, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    uint32_t len = g_cpu.gpr[5];
    printf("[IOS] writeAsync fd=%d (%s) len=%u\n", fd, ios_fd_path(fd), (unsigned)len);
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[6], (int32_t)len, g_cpu.gpr[7]);
    ios_ret(0);
}

void hle_IOS_SeekAsync(void) {          /* (fd, offset, whence, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] seekAsync fd=%d (%s)\n", fd, ios_fd_path(fd));
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[6], 0, g_cpu.gpr[7]);
    ios_ret(0);
}

void hle_IOS_IoctlAsync(void) {         /* (fd, cmd, in, inLen, out, outLen, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlAsync fd=%d (%s) cmd=0x%x in=%u out=%u cb=0x%08x\n", fd, path,
           (unsigned)g_cpu.gpr[4], (unsigned)g_cpu.gpr[6], (unsigned)g_cpu.gpr[8],
           (unsigned)g_cpu.gpr[9]);
    fflush(stdout);
    if (strcmp(path, "/dev/stm/eventhook") == 0) {
        /* So o IOS responde quando o usuario aperta power/reset: fica pendente para sempre. */
        ios_ret(0);
        return;
    }
    ios_zero(g_cpu.gpr[7], g_cpu.gpr[8]);
    ios_queue_cb(g_cpu.gpr[9], 0, g_cpu.gpr[10]);
    ios_ret(0);
}

void hle_IOS_IoctlvAsync(void) {        /* (fd, cmd, inCount, outCount, vec, cb, data) */
    ios_ioctlv_common((int32_t)g_cpu.gpr[3], g_cpu.gpr[4], g_cpu.gpr[5], g_cpu.gpr[6], g_cpu.gpr[7]);
    ios_queue_cb(g_cpu.gpr[8], 0, g_cpu.gpr[9]);
    ios_ret(0);
}