/* pdp8_d8.c: DISPLAY-8 / 338 Program Buffered Display

   Copyright (c) 1993-2011, Robert M Supnik

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   lpt          LP8E line printer

   19-Jan-07    RMS     Added UNIT_TEXT
   25-Apr-03    RMS     Revised for extended file support
   04-Oct-02    RMS     Added DIB, enable/disable, device number support
   30-May-02    RMS     Widened POS to 32b
*/

#include "pdp8_defs.h"

extern int32 int_req, int_enable, dev_done, stop_inst;

DEVICE d8_dev;

int32 d8_err = 0;                                      /* error flag */
int32 d8_stopioe = 0;                                  /* stop on error */

/* 338 state engine */

/* Status registers */

static uint16 pushDownPtr; /* 12 bits */
static uint16 xPosition; /* 13 bits */
static uint16 yPosition; /* 13 bits */
static uint16 displayAddressCounter; /* 12 bits */
static uint8  lightPenHitFlag; /* 1 bit */
static uint8  verticalEdgeFlag; /* 1 bit */
static uint8  horizontalEdgeFlag; /* 1 bit */
static uint8  internalStopFlag; /* 1 bit */
static uint8  sector0Flag; /* 1 bit */
static uint8  controlStateFlag; /* 1 bit; 0: data state, 1: control state */
static uint8  manualInterruptFlag; /* 1 bit */
static uint8  pushButtonHitFlag; /* 1 bit */
static uint8  displayInterruptFlag; /* 1 bit */
static uint8  breakField; /* 3 bits */
static uint8  byteFlipFlop; /* 1 bit */
static uint8  lightPenEnable; /* 1 bit */
static uint16 lightPenScale; /* 2 bits */
static uint16 lightPenMode; /* 3 bits */
static uint16 lightPenIntensity; /* 3 bits */
static uint16 pushButtons; /* 12 bits */
static uint16 slaveGroup1; /* 12 bits */
static uint16 slaveGroup2; /* 12 bits */

// Status register 1 bit fields
#define SR1_LPHF   04000  // Light pen hit
#define SR1_VEF    02000  // Vertical edge flag
#define SR1_HEF    01000  // Horizontal edge flag
#define SR1_ISF    00400  // Internal stop flag
#define SR1_S0F    00200  // Sector 0 flag
#define SR1_CSF    00100  // Control state flag
#define SR1_MIF    00040  // Manual interrupt flag
#define SR1_PBHF   00020  // Push-button hit flag
#define SR1_DIF    00010  // Display interrupt flag
#define SR1_BFR    00007  // Break field register

// Status register 2 bit fields
#define SR2_BFF    04000  // Byte flip flop
#define SR2_LPE    02000  // Light pen enable
#define SR2_HOXP   01000  // High order x position register bit
#define SR2_HOYP   00400  // High order y position register bit
#define SR2_LPSCL  00300  // Scale
#define SR2_LPSCL_SHIFT 6
#define SR2_LPMODE 00070  // Mode
#define SR2_LPMODE_SHIFT 3
#define SR2_LPINT  00007  // Light pen intensity

/* Character generator status */
static uint8  cgActive; /* 1 bit */
static uint8  cgCharacterByte; /* 1 bit */
static uint8  cgCase; /* 1 bit */
static uint8  cgCodeSize; /* 1 bit */
static uint16 cgStartingAddressRegister; /* 6 bits */

#define CG_ACT  04000
#define CG_CB   02000
// spare        01000
#define CG_CASE 00400
#define CG_SZ   00200
// spare        00100
#define CG_SAR  00077

/* State registers */

static uint8  enableEdgeFlagInterrupt; /* 1 bit */
static uint8  enableLightPenFlagInterrupt; /* 1 bit */
static uint8  enablePushButtonHitInterrupt; /* 1 bit */
static uint8  enableInternalStopInterrupt; /* 1 bit */

#define SIC_EFI  04000 /* Enable edge flag interrupt */
#define SIC_ELPI 02000 /* Enable light pen interrupt */

int32 d8_05 (int32 IR, int32 AC);
int32 d8_06 (int32 IR, int32 AC);
int32 d8_07 (int32 IR, int32 AC);
int32 d8_13 (int32 IR, int32 AC);
int32 d8_14 (int32 IR, int32 AC);
int32 d8_15 (int32 IR, int32 AC);
int32 d8_16 (int32 IR, int32 AC);
int32 d8_17 (int32 IR, int32 AC);
int32 d8_30 (int32 IR, int32 AC);
t_stat d8_svc (UNIT *uptr);
t_stat d8_reset (DEVICE *dptr);
//t_stat d8_attach (UNIT *uptr, char *cptr);
//t_stat d8_detach (UNIT *uptr);

/* D8 data structures

   d8_dev      D8 device descriptor
   d8_unit     D8 unit descriptor
   d8_reg      D8 register list
*/

DIB d8_dib = { DEV_D8, 031, {
  NULL,   /* 00 */
  NULL,   /* 01 */
  NULL,   /* 02 */
  NULL,   /* 03 */
  NULL,   /* 04 */
  &d8_05, /* 05 */
  &d8_06, /* 06 */
  &d8_07, /* 07 */
  NULL,   /* 10 */
  NULL,   /* 11 */
  NULL,   /* 12 */
  &d8_13, /* 13 */
  &d8_14, /* 14 */
  &d8_15, /* 15 */
  &d8_16, /* 16 */
  &d8_17, /* 17 */
  NULL,   /* 20 */
  NULL,   /* 21 */
  NULL,   /* 22 */
  NULL,   /* 23 */
  NULL,   /* 24 */
  NULL,   /* 25 */
  NULL,   /* 26 */
  NULL,   /* 27 */
  &d8_30, /* 30 */
  } };

UNIT d8_unit = { UDATA (&d8_svc, 0, 0), 0 };

REG d8_reg[] = {
    //{ ORDATA (BUF, d8_unit.buf, 8) },
    //{ FLDATA (ERR, d8_err, 0) },
    //{ FLDATA (DONE, dev_done, INT_V_D8) },
    //{ FLDATA (ENABLE, int_enable, INT_V_D8) },
    //{ FLDATA (INT, int_req, INT_V_D8) },
    //{ DRDATA (POS, d8_unit.pos, T_ADDR_W), PV_LEFT },
    //{ DRDATA (TIME, d8_unit.wait, 24), PV_LEFT },
    //{ FLDATA (STOP_IOE, d8_stopioe, 0) },
    //{ ORDATA (DEVNUM, d8_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB d8_mod[] = {
    //{ MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      //&set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE d8_dev = {
    "D8", &d8_unit, d8_reg, d8_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &d8_reset,
    NULL, NULL/*&d8_attach*/, NULL/*&d8_detach*/,
    &d8_dib, DEV_DISABLE
    };

/* Unit service */

t_stat d8_svc (UNIT *uptr)
{
return SCPE_OK;
}

/* IOT routine */

/* 2.3.1 Group 1. From the display */

int32 d8_05 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* RDPD */
        // RDPD 6051 Read Push Down Pointer
        // A 1s (inclusive OR) transfer from the push down pointer
        // (12 bits) to the AC is done
        return AC | pushDownPtr;

    case 2:                                             /* RXP */
        // RXP 6052 Read x Position Register
        // A 1s transfer from the x position register to the AC is done.
        // Only the low order 12 (of 13) bits are transferred; the high
        // order bit must be obtained from the RS2 instruction
        return xPosition;

    case 4:                                             /* RYP */
        // RYP 6054 Read y Position Register
        // Same as RXP, except the y position register is transferred.
        return yPosition;
    }

return AC;
}

int32 d8_06 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* RDAC */
        // RDAC 6061 Read Display Address Counter
        // The contents of the display address counter are transferred
        // from the display to the AC. The DAC will be set at the next
        // command to be executed by the display XXX ???
        return displayAddressCounter;

    case 2:                                             /* RS1 */
        // RS1 6062 Read Status 1
        // Status 1 consists of all display flags and the contents of
        // the break field register.
        //  0     Light pen hit flag
        //  1     Vertical edge flag. The y position register has overflowed
        //  2     Horizontal edge flag. The x position register has overflowed
        //  3     Internal stop flag.
        //  4     Sector 0 flag. If bit is a 1, the display is in sector 0.
        //  5     Control state flag. If bit is a 1, the display is in control
        //        state, if it is a 0, the display is in data state.
        //  6     Manual interrupt flag.
        //  7     Push-button hit flag.
        //  8     Display interrupt flag. If the intrrupt system is turned on
        //        bit is a 1, the computer will interrupy. It is set by one
        //        of the six display flags being on and gated onto the
        //        interrupt line.
        //  9, 10, 11
        //        Contents of break field register. These three bits and the
        //        12 bits from the RDAC instruction gice the full 15-bit 
        //        memory address.
        return
          (lightPenHitFlag      ? SR1_LPHF : 0) |
          (verticalEdgeFlag     ? SR1_VEF  : 0) |
          (horizontalEdgeFlag   ? SR1_HEF  : 0) |
          (internalStopFlag     ? SR1_ISF  : 0) |
          (sector0Flag          ? SR1_S0F  : 0) |
          (controlStateFlag     ? SR1_CSF  : 0) |
          (manualInterruptFlag  ? SR1_MIF  : 0) |
          (pushButtonHitFlag    ? SR1_PBHF : 0) |
          (displayInterruptFlag ? SR1_DIF  : 0) |
          (breakField & SR1_BFR);

    case 4:                                             /* RS2 */
        // RS2 6064 Read Status 2
        // Status 2 consists of the contents of some of the major registers
        // in the display; e.g., light pen scale, mode, and intensity. It 
        // also contains byte information and the high order bit of the x
        // and y position registers. The byte flip-flop indicates whether
        // the left half of right half byte in increment mode was being
        // executed when the display stopped. It does not tell whether 
        // the right of left hand character is being executed; this information
        // is obtained from the RCG (IOT 304) instruction. The low twelve
        // bits of the 13-bit x and y position register are obtained by giving
        // RXP or RYP.
        return
          (byteFlipFlop         ? SR2_BFF  : 0) |
          (lightPenEnable       ? SR2_LPE  : 0) |
          ((lightPenScale << SR2_LPSCL_SHIFT) & SR2_LPSCL) |
          ((lightPenMode << SR2_LPMODE_SHIFT) & SR2_LPMODE) |
          (lightPenIntensity & SR2_LPINT);

    }
return AC;
}

int32 d8_07 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* RPB */
        // RPB 6071 Read Push Buttons
        // The contents of the twelve push buttons (0-11) are transferred
        // into the corresponding AC bits.
        return pushButtons;

    case 2:                                             /* RSG1 */
        // RSG1 6072 Read Slave Group 1
        // On this instruction, the light pen, light pen hit, and
        // intensity status forces slaves 0, 1, 2, and 3. The control state
        // command "set slaves" sets the light pen and intensity status.
        // If the slave option is not present, the IOT reeads back 0s
        // into the accumulator.
        //  0       Light pen enable slave 0.
        //  1       Intensity staus of slave 0.
        //  2       Light pen hit, status slave 0.
        //  3,4,5   Same format as above for slave 1.
        //  6,7,8   Same format as above for slave 2.
        //  9,10,11 Same format as above for slave 3.
        return slaveGroup1;

    case 4:                                             /* RSG2 */
        // RSG2 6074 Read Slave Group 2
        // RSG2 has the same format as RSG1, except it reads status of
        // slaves 4,5,6, and 7.
        return slaveGroup2;
    }
return AC;
}

int32 d8_30 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 4:                                             /* RCG */
        // RCG 6304 Read Character Generator
        // RCG reads in the five character generator parameters: character
        // generator actce (CHACT), character byte (CB), case, code size
        // (CHSZ), and starting address register (SAR).
        return
          (cgActive ? CG_ACT : 0) |
          (cgCharacterByte ? CG_CB : 0) |
          (cgCase ? CG_CASE : 0) |
          (cgCodeSize ? CG_SZ : 0) |
          (cgStartingAddressRegister & CG_SAR);
    }
return AC;
}

/* 2.3.2 Group 2. To the display */

int32 d8_13 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 5:                                             /* SPDP */
        // SPDP 6135 Set the Push Down Pointer
        // The contents of the AC are transferred into the PDP register.
        // Since the PDP is a 12-bit register, the PDP list must reside
        // in the first 4K of memory.
        pushDownPtr = AC & 07777;
        return AC;
    }
return AC;
}

int32 d8_14 (int32 IR, int32 AC)
{
return AC;
}

int32 d8_15 (int32 IR, int32 AC)
{
return AC;
}

int32 d8_16 (int32 IR, int32 AC)
{
return AC;
}

int32 d8_17 (int32 IR, int32 AC)
{
return AC;
}

/* Reset routine */

t_stat d8_reset (DEVICE *dptr)
{
return SCPE_OK;
}

#if 0 // NEVER
/* IOT routine */

int32 lpt (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* PSKF */
        return (dev_done & INT_LPT)? IOT_SKP + AC: AC;

    case 2:                                             /* PCLF */
        dev_done = dev_done & ~INT_LPT;                 /* clear flag */
        int_req = int_req & ~INT_LPT;                   /* clear int req */
        return AC;

    case 3:                                             /* PSKE */
        return (lpt_err)? IOT_SKP + AC: AC;

    case 6:                                             /* PCLF!PSTB */
        dev_done = dev_done & ~INT_LPT;                 /* clear flag */
        int_req = int_req & ~INT_LPT;                   /* clear int req */

    case 4:                                             /* PSTB */
        lpt_unit.buf = AC & 0177;                       /* load buffer */
        if ((lpt_unit.buf == 015) || (lpt_unit.buf == 014) ||
            (lpt_unit.buf == 012)) {
            sim_activate (&lpt_unit, lpt_unit.wait);
            return AC;
            }
        return (lpt_svc (&lpt_unit) << IOT_V_REASON) + AC;

    case 5:                                             /* PSIE */
        int_enable = int_enable | INT_LPT;              /* set enable */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;

    case 7:                                             /* PCIE */
        int_enable = int_enable & ~INT_LPT;             /* clear enable */
        int_req = int_req & ~INT_LPT;                   /* clear int req */
        return AC;

    default:
        return (stop_inst << IOT_V_REASON) + AC;
        }                                               /* end switch */
}

/* Unit service */

t_stat lpt_svc (UNIT *uptr)
{
dev_done = dev_done | INT_LPT;                          /* set done */
int_req = INT_UPDATE;                                   /* update interrupts */
if ((uptr->flags & UNIT_ATT) == 0) {
    lpt_err = 1;
    return IORETURN (lpt_stopioe, SCPE_UNATT);
    }
fputc (uptr->buf, uptr->fileref);                       /* print char */
uptr->pos = ftell (uptr->fileref);
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("LPT I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
lpt_unit.buf = 0;
dev_done = dev_done & ~INT_LPT;                         /* clear done, int */
int_req = int_req & ~INT_LPT;
int_enable = int_enable | INT_LPT;                      /* set enable */
lpt_err = (lpt_unit.flags & UNIT_ATT) == 0;
sim_cancel (&lpt_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, char *cptr)
{
t_stat reason;

reason = attach_unit (uptr, cptr);
lpt_err = (lpt_unit.flags & UNIT_ATT) == 0;
return reason;
}

/* Detach routine */

t_stat lpt_detach (UNIT *uptr)
{
lpt_err = 1;
return detach_unit (uptr);
}
#endif
