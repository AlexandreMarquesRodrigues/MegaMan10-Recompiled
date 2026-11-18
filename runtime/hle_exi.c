#include "runtime.h"

#include <stdio.h>

/* The boot milestone has no attached EXI devices. Keep these calls
   synchronous and report success for control/transfer operations; probes and
   ID queries report an absent device so callers take their no-device path. */

static void exi_return(uint32_t value) {
    g_cpu.gpr[3] = value;
}

static void exi_log_once(unsigned bit, const char *name) {
    static uint32_t seen;
    if (!(seen & (1u << bit))) {
        seen |= 1u << bit;
        fprintf(stderr, "HLE_CALL %s\n", name);
    }
}

void hle_EXIInit(void) { exi_log_once(0, "EXIInit"); exi_return(1); }

void hle_EXIProbe(void) {
    /* __EXIProbe's caller treats zero as no device (EXIGetID checks this
       before starting a transaction). This build intentionally has none. */
    exi_log_once(1, "EXIProbe");
    exi_return(0);
}

void hle_EXIGetID(void) {
    /* EXIGetID returns a success flag and stores the ID through r5 only when
       a device was found. Leave the output untouched and report no device. */
    exi_log_once(2, "EXIGetID");
    exi_return(0);
}

void hle_EXILock(void) { exi_log_once(3, "EXILock"); exi_return(1); }
void hle_EXIUnlock(void) { exi_log_once(4, "EXIUnlock"); exi_return(1); }
void hle_EXISelect(void) { exi_log_once(5, "EXISelect"); exi_return(1); }
void hle_EXIDeselect(void) { exi_log_once(6, "EXIDeselect"); exi_return(1); }
void hle_EXIImm(void) { exi_log_once(7, "EXIImm"); exi_return(1); }
void hle_EXIImmEx(void) { exi_log_once(8, "EXIImmEx"); exi_return(1); }
void hle_EXISync(void) { exi_log_once(9, "EXISync"); exi_return(1); }
void hle_EXIDma(void) { exi_log_once(10, "EXIDma"); exi_return(1); }
void hle_EXIAttach(void) { exi_log_once(11, "EXIAttach"); exi_return(1); }
void hle_EXIDetach(void) { exi_log_once(12, "EXIDetach"); exi_return(1); }

/* The SDK's internal handler only services interrupt state. With no EXI
   device or interrupt source, there is nothing to dispatch. */
void hle_EXIIntrruptHandler(void) {
    exi_log_once(13, "EXIIntrruptHandler");
    exi_return(0);
}
