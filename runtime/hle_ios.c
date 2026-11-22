/* HLE do IOS: /dev/* minimos, ES, NAND virtual (SYSCONF e setting.txt),
   callbacks adiados e estado de interrupcoes. */
#include "runtime.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IOS_MAX_FDS 32
#define IOS_ENOENT  (-106)   /* ISFS_ENOENT */

static char     g_ios_path[IOS_MAX_FDS][96];
static int      g_ios_used[IOS_MAX_FDS];
static int      g_ios_vfile[IOS_MAX_FDS];   /* 0 = dispositivo; n>0 = arquivo virtual n-1 */
static uint32_t g_ios_pos[IOS_MAX_FDS];

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

/* ---- NAND virtual: SYSCONF e setting.txt gerados em memoria ------------- */
#define SYSCONF_SIZE 0x4000
static uint8_t  g_sysconf[SYSCONF_SIZE];
static uint8_t  g_setting[256];
static int      g_vfiles_ready;

typedef struct { const char *name; int type; uint32_t value; uint32_t len; } ScItem;
/* tipos: 1 bigarray, 2 smallarray, 3 byte, 4 short, 5 long, 7 bool */
static const ScItem k_sc_items[] = {
    {"IPL.CB",  5, 0, 0},   {"IPL.AR",  3, 0, 0},   /* 0 = 4:3 */
    {"IPL.SSV", 3, 1, 0},   {"IPL.LNG", 3, 1, 0},   /* 1 = ingles */
    {"IPL.E60", 3, 0, 0},   {"IPL.PGS", 3, 0, 0},
    {"IPL.SND", 3, 1, 0},   {"IPL.DH",  3, 0, 0},
    {"IPL.IDL", 2, 0, 2},   {"IPL.CD",  7, 1, 0},
    {"IPL.CD2", 7, 1, 0},   {"BT.BAR",  3, 0, 0},
    {"BT.SPKV", 3, 88, 0},  {"BT.MOT",  3, 1, 0},
    {"BT.SENS", 5, 3, 0},   {"BT.DINF", 1, 0, 0x460},
};

static void sc_put16(uint32_t off, uint16_t v) { g_sysconf[off] = (uint8_t)(v >> 8); g_sysconf[off + 1] = (uint8_t)v; }
static void sc_put32(uint32_t off, uint32_t v) { sc_put16(off, (uint16_t)(v >> 16)); sc_put16(off + 2, (uint16_t)v); }

static void build_sysconf(void) {
    const unsigned n = (unsigned)(sizeof k_sc_items / sizeof k_sc_items[0]);
    uint32_t off = 6 + 2 * n + 2;
    memset(g_sysconf, 0, sizeof g_sysconf);
    memcpy(g_sysconf, "SCv0", 4);
    sc_put16(4, (uint16_t)n);
    for (unsigned i = 0; i < n; ++i) {
        const ScItem *it = &k_sc_items[i];
        size_t nl = strlen(it->name);
        sc_put16(6 + 2 * i, (uint16_t)off);
        g_sysconf[off++] = (uint8_t)((it->type << 5) | (nl - 1));
        memcpy(&g_sysconf[off], it->name, nl); off += (uint32_t)nl;
        switch (it->type) {
        case 3: case 7: g_sysconf[off++] = (uint8_t)it->value; break;
        case 4: sc_put16(off, (uint16_t)it->value); off += 2; break;
        case 5: sc_put32(off, it->value); off += 4; break;
        case 2: g_sysconf[off++] = (uint8_t)(it->len - 1); off += it->len; break;
        case 1: sc_put16(off, (uint16_t)(it->len - 1)); off += 2 + it->len; break;
        }
    }
    sc_put16(6 + 2 * n, (uint16_t)off);          /* fim da ultima entrada */
    memcpy(&g_sysconf[0x3FFC], "SCed", 4);
}

static void build_setting(void) {
    static const char txt[] =
        "AREA=USA\r\nMODEL=RVL-001(USA)\r\nDVD=0\r\nMPCH=0x7FFE\r\nCODE=LU\r\n"
        "SERNO=000000000\r\nVIDEO=NTSC\r\nGAME=US\r\n";
    uint32_t key = 0x73B5DBFAu;                  /* cifra do setting.txt (hipotese) */
    memset(g_setting, 0, sizeof g_setting);
    memcpy(g_setting, txt, sizeof txt - 1);
    for (unsigned i = 0; i < sizeof g_setting; ++i) {
        g_setting[i] ^= (uint8_t)(key & 0xff);
        key = (key << 1) | (key >> 31);
    }
}

typedef struct { const char *path; const uint8_t *data; uint32_t size; } VFile;
static VFile g_vfiles[2];

static void vfiles_init(void) {
    if (g_vfiles_ready) return;
    build_sysconf();
    build_setting();
    g_vfiles[0].path = "/shared2/sys/SYSCONF";
    g_vfiles[0].data = g_sysconf;  g_vfiles[0].size = SYSCONF_SIZE;
    g_vfiles[1].path = "/title/00000001/00000002/data/setting.txt";
    g_vfiles[1].data = g_setting;  g_vfiles[1].size = (uint32_t)sizeof g_setting;
    g_vfiles_ready = 1;
}

static int ios_vf_index(int32_t fd) {
    int idx = fd - 0x100;
    return (idx >= 0 && idx < IOS_MAX_FDS && g_ios_used[idx]) ? g_ios_vfile[idx] - 1 : -1;
}

static int32_t vf_read(int32_t fd, uint32_t buf, uint32_t len) {
    int idx = fd - 0x100, vf = ios_vf_index(fd);
    if (vf < 0) return 0;                        /* dispositivo: EOF */
    const VFile *f = &g_vfiles[vf];
    uint32_t pos = g_ios_pos[idx];
    uint32_t n = pos < f->size ? f->size - pos : 0;
    if (n > len) n = len;
    for (uint32_t i = 0; i < n; ++i) MEM_W8(buf + i, f->data[pos + i]);
    g_ios_pos[idx] = pos + n;
    return (int32_t)n;
}

static int32_t vf_seek(int32_t fd, int32_t off, int32_t whence) {
    int idx = fd - 0x100, vf = ios_vf_index(fd);
    if (vf < 0) return 0;
    int64_t size = g_vfiles[vf].size, cur = g_ios_pos[idx];
    int64_t np = whence == 0 ? off : whence == 1 ? cur + off : size + off;
    if (np < 0) np = 0;
    if (np > size) np = size;
    g_ios_pos[idx] = (uint32_t)np;
    return (int32_t)np;
}

static int32_t ios_do_open(const char *path) {
    int idx = -1, vf = -1;
    vfiles_init();
    if (strncmp(path, "/dev/", 5) != 0) {
        for (int i = 0; i < 2; ++i) if (strcmp(path, g_vfiles[i].path) == 0) vf = i;
        if (vf < 0) return IOS_ENOENT;
    }
    for (int i = 0; i < IOS_MAX_FDS; ++i) if (!g_ios_used[i]) { idx = i; break; }
    if (idx < 0) return -5;
    g_ios_used[idx] = 1;
    g_ios_vfile[idx] = vf + 1;
    g_ios_pos[idx] = 0;
    snprintf(g_ios_path[idx], sizeof g_ios_path[idx], "%s", path);
    return 0x100 + idx;
}

static void ios_do_close(int32_t fd) {
    int idx = fd - 0x100;
    if (idx >= 0 && idx < IOS_MAX_FDS) { g_ios_used[idx] = 0; g_ios_vfile[idx] = 0; }
}

/* ISFS_IOCTL_GETFILESTATS (0xB): out = { tamanho, posicao } */
static int vf_ioctl(int32_t fd, uint32_t cmd, uint32_t out) {
    int vf = ios_vf_index(fd);
    if (vf < 0 || cmd != 0xb || !out) return 0;
    MEM_W32(out, g_vfiles[vf].size);
    MEM_W32(out + 4, g_ios_pos[fd - 0x100]);
    return 1;
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

static void ios_ioctlv_common(int32_t fd, uint32_t cmd, uint32_t inc, uint32_t outc, uint32_t vec) {
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlv fd=%d (%s) cmd=0x%x in=%u out=%u\n", fd, path,
           (unsigned)cmd, (unsigned)inc, (unsigned)outc);
    fflush(stdout);
    if (strcmp(path, "/dev/es") == 0 && ios_es_ioctlv(cmd, inc, outc, vec)) return;
    for (uint32_t i = inc; i < inc + outc && i < 16; ++i)
        ios_zero(MEM_R32(vec + 8u * i), MEM_R32(vec + 8u * i + 4u));
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

void hle_IOS_Read(void) {   /* (fd, buf, len) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    int32_t n = vf_read(fd, g_cpu.gpr[4], g_cpu.gpr[5]);
    printf("[IOS] read fd=%d (%s) len=%u -> %d\n", fd, ios_fd_path(fd), (unsigned)g_cpu.gpr[5], (int)n);
    fflush(stdout);
    ios_ret(n);
}

void hle_IOS_Write(void) {  /* (fd, buf, len): aceita tudo */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    uint32_t len = g_cpu.gpr[5];
    printf("[IOS] write fd=%d (%s) len=%u\n", fd, ios_fd_path(fd), (unsigned)len);
    fflush(stdout);
    ios_ret((int32_t)len);
}

void hle_IOS_Seek(void) {   /* (fd, offset, whence) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    int32_t pos = vf_seek(fd, (int32_t)g_cpu.gpr[4], (int32_t)g_cpu.gpr[5]);
    printf("[IOS] seek fd=%d (%s) off=%d whence=%d -> %d\n", fd, ios_fd_path(fd),
           (int)g_cpu.gpr[4], (int)g_cpu.gpr[5], (int)pos);
    fflush(stdout);
    ios_ret(pos);
}

void hle_IOS_Ioctl(void) {  /* (fd, cmd, in, inLen, out, outLen) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    printf("[IOS] ioctl fd=%d (%s) cmd=0x%x in=%u out=%u\n", fd, ios_fd_path(fd),
           (unsigned)g_cpu.gpr[4], (unsigned)g_cpu.gpr[6], (unsigned)g_cpu.gpr[8]);
    fflush(stdout);
    if (!vf_ioctl(fd, g_cpu.gpr[4], g_cpu.gpr[7])) ios_zero(g_cpu.gpr[7], g_cpu.gpr[8]);
    ios_ret(0);
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

void hle_IOS_ReadAsync(void) {          /* (fd, buf, len, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    int32_t n = vf_read(fd, g_cpu.gpr[4], g_cpu.gpr[5]);
    printf("[IOS] readAsync fd=%d (%s) len=%u -> %d\n", fd, ios_fd_path(fd), (unsigned)g_cpu.gpr[5], (int)n);
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[6], n, g_cpu.gpr[7]);
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
    int32_t pos = vf_seek(fd, (int32_t)g_cpu.gpr[4], (int32_t)g_cpu.gpr[5]);
    printf("[IOS] seekAsync fd=%d (%s) -> %d\n", fd, ios_fd_path(fd), (int)pos);
    fflush(stdout);
    ios_queue_cb(g_cpu.gpr[6], pos, g_cpu.gpr[7]);
    ios_ret(0);
}

void hle_IOS_IoctlAsync(void) {         /* (fd, cmd, in, inLen, out, outLen, cb, data) */
    int32_t fd = (int32_t)g_cpu.gpr[3];
    const char *path = ios_fd_path(fd);
    printf("[IOS] ioctlAsync fd=%d (%s) cmd=0x%x in=%u out=%u cb=0x%08x\n", fd, path,
           (unsigned)g_cpu.gpr[4], (unsigned)g_cpu.gpr[6], (unsigned)g_cpu.gpr[8],
           (unsigned)g_cpu.gpr[9]);
    fflush(stdout);
    if (strcmp(path, "/dev/stm/eventhook") == 0) {   /* so responde quando o usuario aperta power/reset */
        ios_ret(0);
        return;
    }
    if (!vf_ioctl(fd, g_cpu.gpr[4], g_cpu.gpr[7])) ios_zero(g_cpu.gpr[7], g_cpu.gpr[8]);
    ios_queue_cb(g_cpu.gpr[9], 0, g_cpu.gpr[10]);
    ios_ret(0);
}

void hle_IOS_IoctlvAsync(void) {        /* (fd, cmd, inCount, outCount, vec, cb, data) */
    ios_ioctlv_common((int32_t)g_cpu.gpr[3], g_cpu.gpr[4], g_cpu.gpr[5], g_cpu.gpr[6], g_cpu.gpr[7]);
    ios_queue_cb(g_cpu.gpr[8], 0, g_cpu.gpr[9]);
    ios_ret(0);
}


/* ---- diagnostico: o jogo/SDK desistiu (OSFatal) ou vai encerrar ---------- */
static void ios_dump_text(const char *tag, uint32_t va, int n) {
    char buf[130];
    int i;
    for (i = 0; i < n && i < 128; ++i) {
        unsigned c = MEM_R8(va + (uint32_t)i);
        buf[i] = (c >= 32 && c < 127) ? (char)c : '.';
    }
    buf[i] = 0;
    printf("[IOS]   %s @0x%08x: %s\n", tag, (unsigned)va, buf);
}

static uint32_t ios_find(const uint8_t *pat, size_t n) {
    for (uint32_t a = 0x80000000u; a < 0x81800000u; ++a) {
        size_t i = 0;
        while (i < n && MEM_R8(a + (uint32_t)i) == pat[i]) ++i;
        if (i == n) return a;
    }
    return 0;
}

void hle_OSFatal(void) {                /* (fg, bg, msg) */
    uint32_t r[4] = { g_cpu.gpr[3], g_cpu.gpr[4], g_cpu.gpr[5], g_cpu.gpr[6] };
    char tag[16];
    printf("[IOS] OSFatal LR=0x%08x r3=%08x r4=%08x r5=%08x r6=%08x\n",
           (unsigned)g_cpu.lr, (unsigned)r[0], (unsigned)r[1], (unsigned)r[2], (unsigned)r[3]);
    for (int i = 0; i < 4; ++i) {
        if ((r[i] >= 0x80000000u && r[i] < 0x81800000u) || (r[i] >= 0x90000000u && r[i] < 0x94000000u)) {
            snprintf(tag, sizeof tag, "texto em r%d", i + 3);
            ios_dump_text(tag, r[i], 100);
        }
    }
    static const uint8_t plain[] = { 'A', 'R', 'E', 'A', '=' };
    uint32_t a = ios_find(plain, sizeof plain);
    if (a) ios_dump_text("setting.txt DECIFRADO", a, 120);
    else printf("[IOS]   'AREA=' nao existe na RAM\n");
    a = ios_find(g_setting, 8);
    if (a) ios_dump_text("setting.txt AINDA CIFRADO", a, 40);
    fflush(stdout);
    ppc_unimplemented("OSFatal", g_cpu.lr);
}

void hle_OSShutdownDevices(void) {      /* (evento) */
    printf("[IOS] __OSShutdownDevices(evento=%u) LR=0x%08x\n",
           (unsigned)g_cpu.gpr[3], (unsigned)g_cpu.lr);
    fflush(stdout);
    ppc_unimplemented("__OSShutdownDevices", g_cpu.lr);
}
