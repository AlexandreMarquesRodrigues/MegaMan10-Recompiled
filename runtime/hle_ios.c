    /* HLE minimo do IOS para o boot: chamadas sincronas completam na hora, com log. */
#include "runtime.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOS_MAX_FDS 32
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

void hle_IOS_Open(void) {
    char path[48];
    int idx = -1;
    ios_read_cstr(g_cpu.gpr[3], path, sizeof path);
    for (int i = 0; i < IOS_MAX_FDS; ++i) if (!g_ios_used[i]) { idx = i; break; }
    if (idx < 0) { printf("[IOS] open \"%s\" -> sem fd livre\n", path); fflush(stdout); ios_ret(-5); return; }
    g_ios_used[idx] = 1;
    snprintf(g_ios_path[idx], sizeof g_ios_path[idx], "%s", path);
    printf("[IOS] open \"%s\" mode=%u -> fd %d\n", path, (unsigned)g_cpu.gpr[4], 0x100 + idx);
    fflush(stdout);
    ios_ret(0x100 + idx);
}

void hle_IOS_Close(void) {
    int32_t fd = (int32_t)g_cpu.gpr[3];
    int idx = fd - 0x100;
    printf("[IOS] close fd=%d (%s)\n", fd, ios_fd_path(fd));
    fflush(stdout);
    if (idx >= 0 && idx < IOS_MAX_FDS) g_ios_used[idx] = 0;
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

/* ES (/dev/es): so o minimo que o boot consulta. Numeros dos ioctlv: WiiBrew. */
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

void hle_IOS_Ioctlv(void) { /* (fd, cmd, inCount, outCount, vec) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    uint32_t cmd = g_cpu.gpr[4], inc = g_cpu.gpr[5], outc = g_cpu.gpr[6], vec = g_cpu.gpr[7];
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlv fd=%d (%s) cmd=0x%x in=%u out=%u\n", fd, path,
           (unsigned)cmd, (unsigned)inc, (unsigned)outc);
    fflush(stdout);
    if (strcmp(path, "/dev/es") == 0 && ios_es_ioctlv(cmd, inc, outc, vec)) { ios_ret(0); return; }
    for (uint32_t i = inc; i < inc + outc && i < 16; ++i)
        ios_zero(MEM_R32(vec + 8u * i), MEM_R32(vec + 8u * i + 4u));
    ios_ret(0);
}

#define IOS_ASYNC_STOP(NAME) \
    void hle_##NAME(void) { \
        printf("[IOS] " #NAME " ainda nao implementada (r3=0x%08x)\n", (unsigned)g_cpu.gpr[3]); \
        fflush(stdout); \
        ppc_unimplemented(#NAME, g_cpu.lr); \
        ios_ret(-1); \
    }
IOS_ASYNC_STOP(IOS_OpenAsync)
IOS_ASYNC_STOP(IOS_CloseAsync)
IOS_ASYNC_STOP(IOS_ReadAsync)
IOS_ASYNC_STOP(IOS_WriteAsync)
IOS_ASYNC_STOP(IOS_SeekAsync)
IOS_ASYNC_STOP(IOS_IoctlvAsync)

/* (fd, cmd, in, inLen, out, outLen, callback, userData) */
void hle_IOS_IoctlAsync(void) {
    int32_t fd = (int32_t)g_cpu.gpr[3];
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlAsync fd=%d (%s) cmd=0x%x in=%u out=%u cb=0x%08x\n", fd, path,
           (unsigned)g_cpu.gpr[4], (unsigned)g_cpu.gpr[6], (unsigned)g_cpu.gpr[8],
           (unsigned)g_cpu.gpr[9]);
    fflush(stdout);
    if (strcmp(path, "/dev/stm/eventhook") == 0) {
        /* O IOS so responde quando o usuario aperta power/reset: fica pendente para sempre. */
        ios_ret(0);
        return;
    }
    printf("[IOS] ioctlAsync neste dispositivo ainda nao implementada\n");
    fflush(stdout);
    ppc_unimplemented("IOS_IoctlAsync", g_cpu.lr);
    ios_ret(-1);
}