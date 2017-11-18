/* 3b2_cpu.h: AT&T 3B2 Model 400 System Devices implementation

   Copyright (c) 2017, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
   This file contains system-specific registers and devices for the
   following 3B2 devices:

   - timer       8253 interval timer
   - nvram       Non-Volatile RAM
   - csr         Control Status Registers
   - tod         MM58174A Real-Time-Clock
*/

#include "3b2_sysdev.h"
#include "3b2_iu.h"

DEBTAB sys_deb_tab[] = {
    { "INIT",       INIT_MSG,       "Init"              },
    { "READ",       READ_MSG,       "Read activity"     },
    { "WRITE",      WRITE_MSG,      "Write activity"    },
    { "EXECUTE",    EXECUTE_MSG,    "Execute activity"  },
    { "IRQ",        IRQ_MSG,        "Interrupt activity"},
    { "TRACE",      TRACE_MSG,      "Detailed activity" },
    { NULL,         0                                   }
};

uint32 *NVRAM = NULL;

extern DEVICE cpu_dev;

/* CSR */

uint16 csr_data;

BITFIELD csr_bits[] = {
    BIT(IOF),
    BIT(DMA),
    BIT(DISK),
    BIT(UART),
    BIT(PIR9),
    BIT(PIR8),
    BIT(CLK),
    BIT(IFLT),
    BIT(ITIM),
    BIT(FLOP),
    BIT(NA),
    BIT(LED),
    BIT(ALGN),
    BIT(RRST),
    BIT(PARE),
    BIT(TIMO),
    ENDBITS
};

UNIT csr_unit = {
    UDATA(&csr_svc, UNIT_FIX, CSRSIZE)
};

REG csr_reg[] = {
    { HRDATADF(DATA, csr_data, 16, "CSR Data", csr_bits) }
};

DEVICE csr_dev = {
    "CSR", &csr_unit, csr_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &csr_ex, &csr_dep, &csr_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

t_stat csr_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    return SCPE_OK;
}

t_stat csr_reset(DEVICE *dptr)
{
    csr_data = 0;
    return SCPE_OK;
}

uint32 csr_read(uint32 pa, size_t size)
{
    uint32 reg = pa - CSRBASE;

    sim_debug(READ_MSG, &csr_dev,
              "[%08x] CSR=%04x\n",
              R[NUM_PC], csr_data);

    switch (reg) {
    case 0x2:
        if (size == 8) {
            return (csr_data >> 8) & 0xff;
        } else {
            return csr_data;
        }
    case 0x3:
        return csr_data & 0xff;
    default:
        return 0;
    }
}

/* TODO: Remove once we confirm we don't need it */
t_stat csr_svc(UNIT *uptr)
{
    return SCPE_OK;
}

void csr_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg = pa - CSRBASE;

    switch (reg) {
    case 0x03:    /* Clear Bus Timeout Error */
        csr_data &= ~CSRTIMO;
        break;
    case 0x07:    /* Clear Memory Parity Error */
        csr_data &= ~CSRPARE;
        break;
    case 0x0b:    /* Set System Reset Request */
        iu_reset(&iu_dev);
        cpu_reset(&cpu_dev);
        cpu_boot(0, &cpu_dev);
        break;
    case 0x0f:    /* Clear Memory Alignment Fault */
        csr_data &= ~CSRALGN;
        break;
    case 0x13:    /* Set Failure LED */
        csr_data |= CSRLED;
        break;
    case 0x17:    /* Clear Failure LED */
        csr_data &= ~CSRLED;
        break;
    case 0x1b:    /* Set Floppy Motor On */
        csr_data |= CSRFLOP;
        break;
    case 0x1f:    /* Clear Floppy Motor On */
        csr_data &= ~CSRFLOP;
        break;
    case 0x23:    /* Set Inhibit Timers */
        csr_data |= CSRITIM;
        break;
    case 0x27:    /* Clear Inhibit Timers */
        csr_data &= ~CSRITIM;
        break;
    case 0x2b:    /* Set Inhibit Faults */
        csr_data |= CSRIFLT;
        break;
    case 0x2f:    /* Clear Inhibit Faults */
        csr_data &= ~CSRIFLT;
        break;
    case 0x33:    /* Set PIR9 */
        csr_data |= CSRPIR9;
        break;
    case 0x37:    /* Clear PIR9 */
        csr_data &= ~CSRPIR9;
        break;
    case 0x3b:    /* Set PIR8 */
        csr_data |= CSRPIR8;
        break;
    case 0x3f:    /* Clear PIR8 */
        csr_data &= ~CSRPIR8;
        break;
    default:
        break;
    }
}

/* NVRAM */

UNIT nvram_unit = {
    UDATA(NULL, UNIT_FIX+UNIT_BINK, NVRAMSIZE)
};

REG nvram_reg[] = {
    { NULL }
};

DEVICE nvram_dev = {
    "NVRAM", &nvram_unit, nvram_reg, NULL,
    1, 16, 8, 4, 16, 32,
    &nvram_ex, &nvram_dep, &nvram_reset,
    NULL, &nvram_attach, &nvram_detach,
    NULL, DEV_DEBUG, 0, sys_deb_tab, NULL, NULL,
    NULL, NULL, NULL,
    &nvram_description
};

t_stat nvram_ex(t_value *vptr, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if ((vptr == NULL) || (addr & 03)) {
        return SCPE_ARG;
    }

    if (addr >= NVRAMSIZE) {
        return SCPE_NXM;
    }

    *vptr = NVRAM[addr >> 2];

    return SCPE_OK;
}

t_stat nvram_dep(t_value val, t_addr exta, UNIT *uptr, int32 sw)
{
    uint32 addr = (uint32) exta;

    if (addr & 03) {
        return SCPE_ARG;
    }

    if (addr >= NVRAMSIZE) {
        return SCPE_NXM;
    }

    NVRAM[addr >> 2] = (uint32) val;

    return SCPE_OK;
}

t_stat nvram_reset(DEVICE *dptr)
{
    if (NVRAM == NULL) {
        NVRAM = (uint32 *)calloc(NVRAMSIZE >> 2, sizeof(uint32));
        memset(NVRAM, 0, sizeof(uint32) * NVRAMSIZE >> 2);
        nvram_unit.filebuf = NVRAM;
    }

    if (NVRAM == NULL) {
        return SCPE_MEM;
    }

    return SCPE_OK;
}

const char *nvram_description(DEVICE *dptr)
{
    return "Non-volatile memory";
}

t_stat nvram_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat r;

    /* If we've been asked to attach, make sure the ATTABLE
       and BUFABLE flags are set on the unit */
    uptr->flags = uptr->flags | (UNIT_ATTABLE | UNIT_BUFABLE);

    r = attach_unit(uptr, cptr);

    if (r != SCPE_OK) {
        /* Unset the ATTABLE and BUFABLE flags if we failed. */
        uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
    } else {
        uptr->hwmark = (uint32) uptr->capac;
    }

    return r;
}

t_stat nvram_detach(UNIT *uptr)
{
    t_stat r;

    r = detach_unit(uptr);

    if ((uptr->flags & UNIT_ATT) == 0) {
        uptr->flags = uptr->flags & ~(UNIT_ATTABLE | UNIT_BUFABLE);
    }

    return r;
}


uint32 nvram_read(uint32 pa, size_t size)
{
    uint32 offset = pa - NVRAMBASE;
    uint32 data;
    uint32 sc = (~(offset & 3) << 3) & 0x1f;

    switch(size) {
    case 8:
        data = (NVRAM[offset >> 2] >> sc) & BYTE_MASK;
        break;
    case 16:
        if (offset & 2) {
            data = NVRAM[offset >> 2] & HALF_MASK;
        } else {
            data = (NVRAM[offset >> 2] >> 16) & HALF_MASK;
        }
        break;
    case 32:
        data = NVRAM[offset >> 2];
        break;
    }

    return data;
}

void nvram_write(uint32 pa, uint32 val, size_t size)
{
    uint32 offset = pa - NVRAMBASE;
    uint32 index = offset >> 2;
    uint32 sc, mask;

    switch(size) {
    case 8:
        sc = (~(pa & 3) << 3) & 0x1f;
        mask = 0xff << sc;
        NVRAM[index] = (NVRAM[index] & ~mask) | (val << sc);
        break;
    case 16:
        if (offset & 2) {
            NVRAM[index] = (NVRAM[index] & ~HALF_MASK) | val;
        } else {
            NVRAM[index] = (NVRAM[index] & HALF_MASK) | (val << 16);
        }
        break;
    case 32:
        NVRAM[index] = val;
        break;
    }
}

/*
 * 8253 Timer.
 *
 * The 8253 Timer IC has three interval timers, which we treat here as
 * three units.
 *
 * Note that this simulation is very specific to the 3B2, and not
 * usable as a general purpose 8253 simulator.
 *
 */

struct timer_ctr TIMERS[3];

/*
 * The three timers, (A, B, C) run at different
 * programmatially controlled frequencies, so each must be
 * handled through a different service routine.
 */

UNIT timer_unit[] = {
    { UDATA(NULL, 0, 0), TMR_WAIT },
    { UDATA(&timer1_svc, 0, 0), TMR_WAIT },
    { UDATA(NULL, 0, 0), TMR_WAIT },
    { NULL }
};

REG timer_reg[] = {
    { HRDATAD(DIVA,  TIMERS[0].divider, 16, "Divider A") },
    { HRDATAD(STA,   TIMERS[0].mode,    16, "Mode A")   },
    { HRDATAD(DIVB,  TIMERS[1].divider, 16, "Divider B") },
    { HRDATAD(STB,   TIMERS[1].mode,    16, "Mode B")   },
    { HRDATAD(DIVC,  TIMERS[2].divider, 16, "Divider C") },
    { HRDATAD(STC,   TIMERS[2].mode,    16, "Mode C")   },
    { NULL }
};

DEVICE timer_dev = {
    "TIMER", timer_unit, timer_reg, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &timer_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

#define TIMER_STP_US  10           /* 10 us delay per timer step */
#define SYSTEM_CLK    1            /* Clock output 1 is the system clock */

#define tmrnum        u3
#define tmr           up7
#define TIMER_START_TIME (sim_gtime() + (DELAY_US(3 * TIMER_STP_US)))

t_stat start_system_clock() {
    int32 ticks, t;
    struct timer_ctr *ctr;
    UNIT *uptr;

    ctr = &TIMERS[SYSTEM_CLK];
    uptr = &timer_unit[SYSTEM_CLK];

    ticks = ctr->divider / TIMER_STP_US;

    if (ticks > 0) {
        if (sim_is_active(uptr)) {
            sim_cancel(uptr);
        }
        t = sim_rtcn_calb(ticks, CLK_TMR);
        sim_activate_after_abs(uptr, 1000000 / ticks);
    }
}

int32 timer_decr(struct timer_ctr *ctr)
{
    double diff;
    int32 decr;

    diff = sim_gtime() - ctr->stime;

    if (diff < 0) {
        return 0;
    }

    return (int32)(diff / DELAY_US(TIMER_STP_US));
}

t_stat timer_reset(DEVICE *dptr) {
    int32 i, t;

    memset(&TIMERS, 0, sizeof(struct timer_ctr) * 3);

    for (i = 0; i < 3; i++) {
        timer_unit[i].tmrnum = i;
        timer_unit[i].tmr = &TIMERS[i];
    }

    /* Timer 1 gate is always active */
    TIMERS[1].gate = 1;

    t = sim_rtcn_init_unit(&timer_unit[1], timer_unit[1].wait, CLK_TMR);

    /* We do not activate the timer yet. Wait for software to do
       that. */

    return SCPE_OK;
}

t_stat timer1_svc(UNIT *uptr)
{
    struct timer_ctr *ctr;
    int32 t;
    int32 ticks;

    ctr = (struct timer_ctr *)uptr->tmr;

    if (ctr->enabled && ctr->gate) {

        /* Fire the IPL 15 clock interrupt */
        csr_data |= CSRCLK;

        ticks = ctr->divider / TIMER_STP_US;
        t = sim_rtcn_calb(ticks, CLK_TMR);
        sim_activate_after(uptr, 1000000 / ticks);
        ctr->stime = TIMER_START_TIME;
    }

    return SCPE_OK;
}

uint32 timer_read(uint32 pa, size_t size)
{
    uint32 reg;
    int32 ctr_val, decr;
    uint8 ctrnum;
    struct timer_ctr *ctr;

    reg = pa - TIMERBASE;
    ctrnum = (reg >> 2) & 0x3;
    ctr = &TIMERS[ctrnum];

    switch (reg) {
    case TIMER_REG_DIVA:
    case TIMER_REG_DIVB:
    case TIMER_REG_DIVC:
        /* TODO: Fix this hacky mess */
        if (ctr->gate && ctr->enabled) {
            decr = timer_decr(ctr);

            if (ctrnum == 1) {
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] Counter 1 initial val = %04x decrementing by %d steps\n",
                          R[NUM_PC], ctr->divider, decr);
            }

            ctr_val = ctr->divider - decr;

            if (ctrnum == 1) {
                sim_debug(READ_MSG, &timer_dev,
                          "[%08x] Counter 1 now = %04x\n",
                          R[NUM_PC], ctr_val);
            }

            if (ctr_val < 0) {
                ctr_val = 0;
            }
        } else {
            ctr_val = ctr->divider;
        }

        switch (ctr->mode & CLK_RW) {
        case CLK_LSB:
            return ctr_val & 0xff;
        case CLK_MSB:
            return (ctr_val & 0xff00) >> 8;
        case CLK_LMB:
            if (ctr->lmb) {
                ctr->lmb = FALSE;
                return (ctr_val & 0xff00) >> 8;
            } else {
                ctr->lmb = TRUE;
                return ctr_val & 0xff;
            }
        default:
            return 0;
        }
        break;
    case TIMER_REG_CTRL:
        return ctr->mode;
    case TIMER_CLR_LATCH:
        csr_data &= ~CSRCLK;
        return 0;
    default:
        /* Unhandled */
        sim_debug(READ_MSG, &timer_dev,
                  "[%08x] UNHANDLED TIMER READ. ADDR=%08x\n",
                  R[NUM_PC], pa);
        return 0;
    }
}

void handle_timer_write(uint8 ctrnum, uint32 val)
{
    struct timer_ctr *ctr;

    ctr = &TIMERS[ctrnum];
    switch(ctr->mode & 0x30) {
    case 0x10:
        ctr->divider &= 0xff00;
        ctr->divider |= val & 0xff;
        ctr->enabled = TRUE;
        ctr->stime = TIMER_START_TIME;
        start_system_clock();
        break;
    case 0x20:
        ctr->divider &= 0x00ff;
        ctr->divider |= (val & 0xff) << 8;
        ctr->enabled = TRUE;
        ctr->stime = TIMER_START_TIME;
        start_system_clock();
        break;
    case 0x30:
        if (ctr->lmb) {
            ctr->lmb = FALSE;
            ctr->divider = (ctr->divider & 0x00ff) | ((val & 0xff) << 8);
            ctr->enabled = TRUE;
            ctr->stime = TIMER_START_TIME;
            start_system_clock();
        } else {
            ctr->lmb = TRUE;
            ctr->divider = (ctr->divider & 0xff00) | (val & 0xff);
        }
        break;
    default:
        break;

    }
}

void timer_write(uint32 pa, uint32 val, size_t size)
{
    uint8 reg, ctrnum;
    int32 time;
    struct timer_ctr *ctr;

    reg = pa - TIMERBASE;

    switch(reg) {
    case TIMER_REG_DIVA:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [TIMER_REG_DIVA] write\n", R[NUM_PC]);
        handle_timer_write(0, val);
        break;
    case TIMER_REG_DIVB:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [TIMER_REG_DIVB] write\n", R[NUM_PC]);
        handle_timer_write(1, val);
        break;
    case TIMER_REG_DIVC:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [TIMER_REG_DIVC] write\n", R[NUM_PC]);
        handle_timer_write(2, val);
        break;
    case TIMER_REG_CTRL:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [TIMER_REG_CTRL] write\n", R[NUM_PC]);
        /* The counter number is in bits 6 and 7 */
        ctrnum = (val >> 6) & 3;
        if (ctrnum > 2) {
            sim_debug(WRITE_MSG, &timer_dev,
                      "[%08x] WARNING: Write to invalid counter: %d\n",
                      R[NUM_PC], ctrnum);
            return;
        }
        ctr = &TIMERS[ctrnum];
        ctr->mode = val;
        ctr->enabled = FALSE;
        ctr->lmb = FALSE;
        sim_cancel(&timer_unit[ctrnum]);
        break;
    case TIMER_CLR_LATCH:
        sim_debug(WRITE_MSG, &timer_dev,
                  "[%08x] [TIMER_CLR_LATCH] unexpected write to timer latch\n", R[NUM_PC]);
        break;
    }
}

/*
 * MM58174A Real-Time-Clock
 */

UNIT tod_unit = { UDATA(&tod_svc, UNIT_IDLE+UNIT_FIX, 0), DELAY_MS(100) };

uint32 tod_reg = 0;

DEVICE tod_dev = {
    "TOD", &tod_unit, NULL, NULL,
    1, 16, 8, 4, 16, 32,
    NULL, NULL, &tod_reset,
    NULL, NULL, NULL, NULL,
    DEV_DEBUG, 0, sys_deb_tab
};

#define CLK_TICKS 10  /* Ten clicks per second */

t_stat tod_reset(DEVICE *dptr)
{
    int32 t;

    /* December 1, 1988 (testing) */
    tod_reg = 596966400;

    if (!sim_is_running) {
        t = sim_rtcn_init_unit(&tod_unit, tod_unit.wait, CLK_TOD);
        sim_activate_after(&tod_unit, 1000000 / CLK_TICKS);
    }

    return SCPE_OK;
}

t_stat tod_svc(UNIT *uptr)
{
    int32 t;

    t = sim_rtcn_calb(CLK_TICKS, CLK_TOD);
    sim_activate_after(&tod_unit, 1000000 / CLK_TICKS);

    tod_reg++;
    return SCPE_OK;
}

uint32 tod_read(uint32 pa, size_t size)
{
    uint32 reg;

    reg = pa - TODBASE;

    switch(reg) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 9:
    case 12:
    default:
        break;
    }

    sim_debug(READ_MSG, &tod_dev,
              "[%08x] READ TOD: reg=%02x\n",
              R[NUM_PC], reg);

    return 0;
}

void tod_write(uint32 pa, uint32 val, size_t size)
{
    uint32 reg;

    reg = pa - TODBASE;

    sim_debug(WRITE_MSG, &tod_dev,
              "[%08x] WRITE TOD: reg=%02x val=%d\n",
              R[NUM_PC], reg, val);
}
