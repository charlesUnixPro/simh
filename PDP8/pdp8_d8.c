//#define sdbg sim_printf
#define sdbg if (0) sim_printf

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

   d8          338 Graphics display

*/

#include "pdp8_defs.h"

void init_graphics (int argc, char *argv[], int p_smallwindow,
                   int p_use_pixmap, int p_line_width, char *window_name);
void open_page (int step);
void close_page (void);
void handle_input (void);

typedef uint16 word1;
typedef uint16 word2;
typedef uint16 word3;
typedef uint16 word6;
typedef uint16 word7;
typedef uint16 word10;
typedef uint16 word12;
typedef uint16 word13;

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;
static int refresh_rate = 1;

DEVICE d8_dev;

int32 d8_err = 0;                                      /* error flag */
int32 d8_stopioe = 0;                                  /* stop on error */

/* 338 state engine */

/* Status registers */

// 2.3.1.5 Read Status 1: "Display interrupt flag. If the interrupt
// system is turned on and bit is a 1, the computer will interrupt.
// It is set by one of the six display flags being on and gated onto
// the interrupt line.
// 2.3.2.2 Set Initial Conditions:
//   Enable edge flag interrupt
//   Enable light pen flag interrupt
//   Enable interrupt on push button hit
//   Enable interrupt on internal stop flag
//
// That's 4 (or 5 if you count 'edge' has both H and V).
//
// Looking at the example interrupt handler in App. 2:
//
//  SPLP light pen flag
//  SPSF internal stop flag
//  SPMI manual interrupt
//  SPEF edge flag
//  SPES external stop flag
//  SPSP slave light pen
//  RS1 and then test for pushbutton flag.
//
// That's 7. Sigh.
//
// 1.1.9 Flags
//
//  Internal stop
//  External stop
//  Edge
//  Light pen
//  Pushbutton
//  Manual interrupt
//
// Okay, that's 6; the optional slave light pen is the 7th
// Manual interrupt and push button do not stop the display; the
// others do.
//

// The interrupt causing flags
static word1  internalStopFlag;
static word1  externalStopFlag;
static word1  verticalEdgeFlag;
static word1  horizontalEdgeFlag;
static word1  lightPenHitFlag;
static word1  pushButtonHitFlag;
static word1  manualInterruptFlag;

// Interrupt pending
static word1  displayInterruptFlag;

// Interrupt enables
static word1  enableEdgeFlagInterrupt;
static word1  enableLightPenFlagInterrupt;
static word1  enablePushButtonHitInterrupt;
static word1  enableInternalStopInterrupt;

// Non-interrupt causing flags
static word1  sector0Flag;

// General registers
static word1  lightPenEnable;
static word12 pushDownPtr;
static word13 xPosition;
static word13 yPosition;
static word12 displayAddressCounter; // Plus breakField for 15 bits 
static word1  controlStateFlag; /* 0: data state, 1: control state */
static word3  breakField;
static word1  byteFlipFlop;
static word2  scale;
static word3  mode;
static word3  intensity;
static word1  blink;
static word12 pushButtons;
static word12 slaveGroup1;
static word12 slaveGroup2;

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
#define SR2_SCL    00300  // Scale
#define SR2_SCL_SHIFT 6
#define SR2_MODE   00070  // Mode
#define SR2_MODE_SHIFT 3
#define SR2_INT    00007  // Intensity

/* Character generator status */
static word1 cgActive;
static word1 cgCharacterByte;
static word1 cgCase;
static word1 cgCodeSize;
static word6 cgStartingAddressRegister;

#define CG_ACT  04000
#define CG_CB   02000
// spare        01000
#define CG_CASE 00400
#define CG_SZ   00200
// spare        00100
#define CG_SAR  00077

/* State registers */

static word1  disableLightPenOnResume;
static word1  lightPenBehavior;
static word2  xDimension;
static word2  yDimension;
static word1  intensifyAllPoints;
static word1  inhibitEdgeFlags;
static word1  breakRequestFlag; /* This is the run/stop bit */

#define SIC_EFI  04000 /* Enable edge flag interrupt */
#define SIC_ELPI 02000 /* Enable light pen interrupt */
#define SIC_DLPR 01000 /* Disable light pen on resume */
#define SIC_LPB  00400 /* Light pen behavior */
#define SIC_YDIM 00300 /* Y dimension */
#define SIC_YDIM_SHIFT 6
#define SIC_XDIM 00060 /* X dimension */
#define SIC_XDIM_SHIFT 4
#define SIC_IAP  00010 /* Intensify all points */
#define SIC_IEF  00004 /* Inhibit edge flags */
#define SIC_EPBI 00002 /* Enable interrupt on push button hit */
#define SIC_EISI 00001 /* Enable interrupt on internal stop */

#define LBF_BFE  04000 /* Break field change enable */
#define LBF_BF   03400 /* Break field */
#define LBF_BF_SHIFT 8
#define LBF_PBE  00200 /* Push button change enable */
#define LBF_WPB  00100 /* Which push buttons */
#define LBF_PBS  00077 /* Push buttons */

#define SCG_CASE 00400 /* Case select */
#define SCG_CHSZ 00200 /* Code size */
#define SCG_SAR  00077 /* Starting address register */

/* D8 State engine */

// G Bell: Computer Structures: Readings and Examples, Chap. 25

static word1 signalExternalStop;

enum d8_state {
  // Control state
  instructionState,
  midFetch,
  instructionFetchComplete,
  midPush,
  midPop, // This isn't in the documented state diagram, but is
          // need to account for cycle stealing.
  internalStop,
  // externalStopControl, // implemented as breakRequestFlag.
  // Data state
  dataState,
  midDataFetch,
  dataFetchComplete,
  dataExecutionWait,
  //externalStopData // implemented as breakRequestFlag.
};

static char * states [] = {
  "instructionState",
  "midFetch",
  "instructionFetchComplete",
  "midPush",
  "midPop", 
  "internalStop",
  "dataState",
  "midDataFetch",
  "dataFetchComplete",
  "dataExecutionWait",
};

static enum d8_state state;
/* sub-state flags */
static word12 instrBuf;
static word12 instrBuf2;
static word12 popbuf;

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
//sim_printf ("svc %d %d\n", time(NULL), tmxr_poll);
#if 1
static time_t last = 0;
static int cnt = 0; 
time_t now = time(NULL);
if (now == last) cnt ++;
else { printf ("cnt %d tmxr_poll %d\r\n", cnt, tmxr_poll); cnt = 0; last = now;}
#endif
//sim_clock_coschedule (uptr, refresh_rate);                 /* continue poll */
sim_activate (uptr, tmxr_poll/refresh_rate/**tmxr_poll*/);                 /* continue poll */

close_page ();
open_page (1);
handle_input ();
return SCPE_OK;
}

static void updateInterrupt (void)
  {
    if ((internalStopFlag   && enableInternalStopInterrupt)  ||
        externalStopFlag                                     ||
        (verticalEdgeFlag   && enableEdgeFlagInterrupt)      ||
        (horizontalEdgeFlag && enableEdgeFlagInterrupt)      ||
        (lightPenHitFlag    && enableLightPenFlagInterrupt)  ||
        (pushButtonHitFlag  && enablePushButtonHitInterrupt) ||
        manualInterruptFlag)
      {
        displayInterruptFlag = 1;
        dev_done = dev_done | INT_LPT;                          /* set done */
        int_req = INT_UPDATE;                                   /* update interrupts */

      }
    else
      {
        displayInterruptFlag = 0;
        int_req = int_req & ~INT_LPT;
      }
  }

/* IOT routines */

int32 d8_05 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* RDPD */
        // RDPD 6051 Read Push Down Pointer
        sdbg ("IOT RDPD %04o\n", AC | pushDownPtr);
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
        AC = displayAddressCounter;
        break;

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
        //        bit is a 1, the computer will interrupt. It is set by one
        //        of the six display flags being on and gated onto the
        //        interrupt line.
        //  9, 10, 11
        //        Contents of break field register. These three bits and the
        //        12 bits from the RDAC instruction gice the full 15-bit 
        //        memory address.
        AC =
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
        pushButtonHitFlag = 0;
        updateInterrupt ();
        break;

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
        AC =
          (byteFlipFlop         ? SR2_BFF  : 0) |
          (lightPenEnable       ? SR2_LPE  : 0) |
          ((scale << SR2_SCL_SHIFT) & SR2_SCL) |
          ((mode << SR2_MODE_SHIFT) & SR2_MODE) |
          (intensity & SR2_INT);
        break;
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
//printf ("rpb %04o\r\n", pushButtons & 07777);
        return pushButtons & 07777;

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

int32 d8_13 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 2:                                             /* SLPP */
        // SLPP 6132 Skip on Light Pen Hit Flag
        return lightPenHitFlag ? IOT_SKP | AC : AC;

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
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 2:                                             /* SLPP */
        // SPSP 6142 Skip on Slave Light Pen Hit Flag
        return ((slaveGroup1 & 0111) | (slaveGroup2 & 0111)) ? IOT_SKP + AC: AC;

    case 5:                                             /* SIC */
        // SIC 6145 Set Initial Conditions
        sdbg ("IOT SIC %04o\n", AC);
        // Set interrupt enable flags, paper size and light pen conditions.
        enableEdgeFlagInterrupt = (AC & SIC_EFI) ? 1 : 0;
        enableLightPenFlagInterrupt = (AC & SIC_ELPI) ? 1 : 0;
        disableLightPenOnResume = (AC & SIC_DLPR) ? 1 : 0;
        lightPenBehavior = (AC & SIC_LPB) ? 1 : 0;
        yDimension = (AC >> SIC_YDIM_SHIFT) & 03;
        xDimension = (AC >> SIC_XDIM_SHIFT) & 03;
        intensifyAllPoints = (AC & SIC_IAP) ? 1 : 0;
        inhibitEdgeFlags = (AC & SIC_IEF) ? 1 : 0;
        enablePushButtonHitInterrupt = (AC & SIC_EPBI) ? 1 : 0;
        enableInternalStopInterrupt = (AC & SIC_EISI) ? 1 : 0;
        updateInterrupt ();
        // XXX Guessing
        xPosition = yPosition = 0;
        return AC;
    }
return AC;
}

int32 d8_15 (int32 IR, int32 AC)
{

// The AC checks in this IOT are a side effect of the slightly incorrect
// implementation of IOTs. On the orignal PDP/8 hardware, the IOT bits
// 9,10,11 controlled the genertation of the IOT1, IOT2 and IOT4 pulses.
// These pulses were generated sequentially rather the in parallel like
// simh tends to treat them. 
//
// So for SPES (6151), the h/w would, on each clock cycle, set IOP to 1, then
// 0 and then 0. 
//
//               IOP at
//     instr     clock  0    1    2
//     -----
//   SPES 6151          1    0    0
//   SPEF 6152          0    1    0
//   STPD 6154          0    0    1
//   LBF  6155          1    0    1
//
//
// The device would count the clock cycles and gate the IOP to the
// appropriate logic; some devices had a very minimal set of IOP
// gating logic, and used bits in the AC to do the additional decoding.
//
// IOT 15 is setup up so that the IOP timing logic can be simplifed. If
// IOP1 is zero, then the operation is either SPES, SPPD or LBF. The LBF
// has two enable flags in the AC, at bits 0 and 4 (04200). If either or
// both are set, the IOT is an LBF. If neither are set, then IOP2 selects
// the SPES or SPPD instruction.
//
// The simplification of the logic proably saved a couple of Flip Chips, but
// allowed the possibility of instructions with non-canonical forms to execute
// predictibly; but without knowing the details of the hardware, it is 
// difficult to model them.
//
// The following code executes canonically; any code that varies from canon
// may behave differently from the h/w.
// 

switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* SPES */
        // SPES 6151 Skip on External Stop Flag
        // This is one of the microprogrammed IOTs and requires bits 0 and 
        // 4 of the AC to be 0 when the IOT is given.
        if (AC & 04200)
          return AC;
        return externalStopFlag ? IOT_SKP + AC: AC;

    case 2:                                             /* SPEF */
        // SPEF 6152 Skip on Edge Flag
        return (verticalEdgeFlag | horizontalEdgeFlag) ? IOT_SKP + AC: AC;

    case 4:                                             /* STPD */
        // STPD 6154 Stop Display (External)
        // STPD stops the display and sets the external stop flag when
        // the display has stopped. This is one of the microprogrammed
        // IOTs and requires bits 0 and 4 of the AC to be 0 when the IOT
        // is given.
        if (AC & 04200)
          return AC;
        signalExternalStop = 1;
        return AC;
        
    case 5:                                             /* LBF */
        // LBF 6155 Load Break field
        sdbg ("IOT LBF %04o\n", AC);
        if (AC & LBF_BFE) { // Is change break field set?
            breakField = (AC >> LBF_BF_SHIFT) & 07;
        }
        if (AC & LBF_PBE) { // Is change push buttons set?
            word6 tmp = AC & LBF_PBS;
            if (AC & LBF_WPB) { // High half or low?
                // Low half
                pushButtons &= 07700; // Clear low half
                pushButtons |= tmp; // Set low half
                } else {
                // High half
                pushButtons &= 00077; // Clear high half
                pushButtons |= tmp << 6; // Set high half
                }
//printf ("lbf %04o\n", pushButtons);
            }
        return AC;
    }

return AC;
}

int32 d8_16 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* CFD */
        // CFD 6161 Clear Display Flags
        // CFD clears the four flags that stop the display. ... (internal
        // and external stop, light pen hit, and edge)
        internalStopFlag = 0;
        externalStopFlag = 0;
        lightPenHitFlag = 0;
        verticalEdgeFlag = 0;
        horizontalEdgeFlag = 0;
        updateInterrupt ();
        // XXX blink reset seems undocumented, but this should be a reasonable
        // place...
        blink = 0;
        return AC;

    case 4:                                             /* RES2 */
        // RES2 6164 Resume After Stop Code
        if (AC)
            return AC; // "The AC must be zero before RES2 is given
        breakRequestFlag = 1; // Start the display
        updateInterrupt ();
        return AC;

    case 5:                                             /* INIT */
        // INIT 6165 Initialize the display
        sdbg ("IOT INIT %04o\n", AC);
        displayAddressCounter = AC & 07777;
        breakRequestFlag = 1; // Start the display
        int_enable = int_enable | INT_LPT;              /* set enable */
        int_req = INT_UPDATE;                           /* update interrupts */
        return AC;
    }

return AC;
}

int32 d8_17 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */

    case 1:                                             /* SPES */
        // SPSF 6171 Skip on Internal Stop Flag
        return internalStopFlag ? IOT_SKP + AC: AC;


    case 2:                                             /* SPMI */
        // SPMI 6172 Skip on Manual Interrupt
        if (manualInterruptFlag)
          AC |= IOT_SKP;
        manualInterruptFlag = 0;
        updateInterrupt ();
        break;

    case 4:                                             /* RES1 */
//printf ("RES1\r\n");
        // RES1 6174 Resume After Light Pen Hit, Edge, or External Stop Flag
        if (lightPenHitFlag || verticalEdgeFlag || horizontalEdgeFlag ||
          externalStopFlag) {
            lightPenHitFlag = 0;
            verticalEdgeFlag = 0;
            horizontalEdgeFlag = 0;
            externalStopFlag= 0;
            breakRequestFlag = 1; // Start the display
            updateInterrupt ();
//printf ("RES1 start\r\n");
        }
        break;
    }
return AC;
}

int32 d8_30 (int32 IR, int32 AC)
{
switch (IR & 07) {                                      /* decode IR<9:11> */


    case 3:                                             /* SCG */
        // SCG 6303 Set Character Generator
        cgCase = (AC & SCG_CASE) ? 1 : 0;
        cgCodeSize = (AC & SCG_CHSZ) ? 1 : 0;
        cgStartingAddressRegister = AC & SCG_SAR;
        return AC;

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

/* Reset routine */

static int graphicsInited = 0;

t_stat d8_reset (DEVICE *dptr)
{
if (graphicsInited == 0) {
  int argc = 0;
  char ** argv = NULL;
  int windowSize = 0; // 0 large, 1 small
  int usePixmap = 0;
  int lineWidth = 0;
  static char * windowName = "338";
  init_graphics (argc, argv, windowSize, usePixmap, lineWidth, windowName);
  open_page (1);
  graphicsInited = 1;
  //d8_unit.wait=1;
  sim_activate (&d8_unit, tmxr_poll/refresh_rate/**tmxr_poll*/);               /* activate */

}

// 2.3.2.8 "The power clear pulse (START key) also clears all display flags.
// All display flags cat be cleared by giving three IOTs: CFD-6161 (internal
// and external stop, light pen high, and edge); RS1-6062 (push button); and 
// SPMI-6172 (manual interrupt).
// CFD
internalStopFlag = 0;
externalStopFlag = 0;
lightPenHitFlag = 0;
verticalEdgeFlag = 0;
horizontalEdgeFlag = 0;
// RS1-6062 makes no sense
pushButtons = 0;
// SPMI-6172 makes no sense
manualInterruptFlag = 0;

state = instructionState;
controlStateFlag = 1;
signalExternalStop = 0;

blink = 0;

dev_done = dev_done & ~INT_D8;                         /* clear done, int */
int_req = int_req & ~INT_D8;
int_enable = int_enable | INT_D8;                      /* set enable */


return SCPE_OK;
}

// Interface to the vector H/W

void draw_line (int x1, int y1, int x2, int y2, unsigned long color);
static void drawPoint (word13 x, word13 y, word1 beamOn)
  {
    sdbg ("draw point at x %d y %d on %d\n", x, y, beamOn);
    draw_line (x, y, x, y, 0xffffff);
    xPosition = x;
    yPosition = y;
  }

static void drawIncrement (word3 dir, word2 n, word1 beamOn)
  {
    if (n == 0)
      n = 1;
    int dx = 0, dy = 0;
    switch (dir)
      {
        case 0: dx = +n;          break;
        case 1: dx = +n; dy =+ n; break;
        case 2:          dy = +n; break;
        case 3: dx = -n; dy = +n; break;
        case 4: dx = -n;          break;
        case 5: dx = -n; dy = -n; break;
        case 6:          dy = -n; break;
        case 7: dx = +n; dy = -n; break;
      }
    sdbg ("draw increment dir %d (%d, %d) n %d on %d\n", dir, dx, dy, n, beamOn);
    //printf ("draw increment dir %d (%d, %d) n %d on %d\r\n", dir, dx, dy, n, beamOn);
#if 0
    draw_line (xPosition, yPosition, (xPosition + dx) & 01777, (yPosition + dy) & 01777, 0xffffff);
#else
    draw_line (xPosition, yPosition, xPosition + dx, yPosition + dy, 0xffffff);
#endif
    xPosition = (xPosition + dx) & 01777;
    yPosition = (yPosition + dy) & 01777;
  }

static void drawVector (word13 x, word13 y, word1 beamOn, int con)
  {
    // XXX deal with con
    sdbg ("draw vector to x %d y %d on %d (con %d)\n", x, y, beamOn, con);
    //printf ("draw vector to x %d y %d on %d (con %d)\r\n", x, y, beamOn, con);
    draw_line (xPosition, yPosition, x, y, 0xffffff);
    xPosition = x;
    yPosition = y;
  }

static void drawChar (word7 ch)
  {
    sdbg ("draw char %d\n", ch);
    // XXX deal with dx/dy
  }

// Step the display processor

extern uint16 M[];

static word12 dacwas; // debugging

void sim_instr_d8 (void)
  {
    if (! breakRequestFlag)
      return;  // Not running.

    sdbg ("d8 state %s\n", states [state]);
    switch (state)
      {
        case instructionState:
          {

            if (signalExternalStop)
              {
                signalExternalStop = 0;
                externalStopFlag = 1;
                breakRequestFlag = 0; // Stop the display
                updateInterrupt ();
                break;
              }

// Get the instruction

// This involves a 'data break'; cycle stealing on the bus.
// Ignore the timing implications of this for the nonce.
// XXX (Decrement sim_interval?) Each access to 'M' should count as
// XXX a break cycle
// 1.1.10 The display can take at most 1 data break every 3 cycles.
// 4.5 usec.

// F-85, pg 4: MAJOR STATE GENERATOR
// Fetch, Defer (indirect address fetch), Execute, Word Count 
// (3-cycle break first step), Current Address (3-cycle break
// second step),  Break (3-cycle break last step or 1-cycle 
// break).

// It seems like the 1.1.10 1 in 3 rule is based on the 338 clock;
// it runs at a speed that effectively a break cycle at half the
// Major State Generator clock, or the MSG is gating the break
// cycle to prevent CPU starvation.

// Since sim_instr doesn't implement the MSG explicitly, it is
// running sim_instr_d8 once per instruction execution, so
// the effective sequence is:
//
//   Fetch, Execute, Break, Fetch, Execute, Break,...
//
// which appears to be a resonable facsimile of the H/W.

            dacwas = displayAddressCounter; // debugging
            instrBuf = M[displayAddressCounter | (breakField << 12)];
            displayAddressCounter = (displayAddressCounter + 1) & 07777;

            word3 opc = (instrBuf >> 9) & 07;
            if (opc == 02) // Jump/Push Jump
                 state = midFetch; 
            else
                 state = instructionFetchComplete; 
            break;
          } // case instructionState

        case midFetch:
          instrBuf2 = M[displayAddressCounter | (breakField << 12)];
          displayAddressCounter = (displayAddressCounter + 1) & 07777;
          state = instructionFetchComplete;
          break;

        case instructionFetchComplete:
          {
//printf ("i %04o %04o\r\n", dacwas, instrBuf); 
            state = instructionState; // Set default state transition

            word3 opc = (instrBuf >> 9) & 07;
            switch (opc)
              {
                case 0: // Parameter

                  if (instrBuf & 00400) // Enable scale change
                    scale = (instrBuf >> 6) & 03;
                  if (instrBuf & 00040) // Enable light pen change
                    lightPenEnable = (instrBuf >> 6) & 03;
                  break;

                case 1: // Mode

// XXX This may be wrong; if both EDS (enter data state) and 
// XXX STOP bits are set, one of them will get ignored.
// XXX The docs are not at all clear about this.
                  if ((instrBuf & 00400) && (instrBuf & 00001))
                    sim_printf ("EDS and STOP at %04o?\n", dacwas);

                  if (instrBuf & 00400) // Stop code
                    {
                      internalStopFlag = 1;
                      updateInterrupt ();
                      state = internalStop;
                    }

                  if (instrBuf & 00200) // Clear push button flag
                    {
                      pushButtonHitFlag = 0;
                      manualInterruptFlag = 0;
                    }

                  if (instrBuf & 00100) // Enable mode change
                    mode = (instrBuf >> 3) & 07;

                  if (instrBuf & 00004) // Clear high order position bits
                    {
                      xPosition &= 001777;
                      yPosition &= 001777;
                    }

                  if (instrBuf & 00002) // Clear low order position bits
                    {
                      xPosition &= 016000;
                      yPosition &= 016000;
                    }

                  if (instrBuf & 00001) // Switch to data state
                    {
                      if (mode == 07)
                        {
                          internalStopFlag = 1;
                          updateInterrupt ();
                          state = internalStop;
                        }
                      else
                        {
                          state = dataState;
                          controlStateFlag = 0;
                        }
                    }
                  break;

                case 2:  // Jump / Push Jump

                  if (instrBuf & 00400) // Enable scale change
                    scale = (instrBuf >> 6) & 03;
                  if (instrBuf & 00040) // Enable light pen change
                    lightPenEnable = (instrBuf >> 6) & 03;
                  if (instrBuf & 00010) // Push Jump
                    {
                      // Save the first word, switch to midPush state
                      word12 w = ((breakField & 03)     << 9) |
                                 ((lightPenEnable & 01) << 8) |
                                 ((scale & 03)          << 6) |
                                 ((mode & 07)           << 3) |
                                 ((intensity & 07)      << 0);
                      // Pushdown list does not use break field
                      M [pushDownPtr & 07777] = w;
                      pushDownPtr = (pushDownPtr + 1) & 07777;
                      state = midPush;
                    }
                  else  // Jump
                    {
                      breakField = instrBuf & 03;
                      displayAddressCounter = instrBuf2 & 07777;
                      state = instructionState;
                    }
                  break; 

                case 3:  // Pop

                  // Recover the first word on the stack, go to midPop state
                  pushDownPtr = (pushDownPtr - 1) & 07777;
                  // Pushdown list does not use break field
                  popbuf = M [pushDownPtr & 07777];
                  state = midPop;

                  break;

                case 4:  // Conditional skip
                case 5:  // Conditional skip 2
                  {
                    // Upper half for case 4, lower for case 5
                    int shift = (opc == 4) ? 6 : 0;
                    // Get the right buttons
                    word6 mask = (instrBuf & 00077) << shift;
                    
                    word6 compl = (instrBuf & 00400) ? 0 : 07777;

                    if ((pushButtons ^ compl) & mask)
                      displayAddressCounter = (displayAddressCounter + 2) & 07777;
//else printf ("didn't skip opc %d shift %d mask %04o compl %04o btns %04o\r\n", opc, shift, mask, compl, pushButtons);
                    if (instrBuf & 00200) // Clear
                      pushButtons = pushButtons & ~ mask;

                    if (instrBuf & 00100) // Complement
                      pushButtons = pushButtons ^ (07777 & mask);
//printf ("sk %04o\r\n", pushButtons);
                    break;
                  } // case 4, 5

                case 6:  // Miscellaneous
                  {
                    word3 uop = (instrBuf >> 6) & 03;
                    switch (uop)
                      {
                        case 0: // Arithmetic compare Push buttons bank 1
                        case 1: // Arithmetic compare Push buttons bank 2
                          {
                            // Upper half for case 4, lower for case 5
                            int shift = opc == 4 ? 6 : 0;
                            // Get the right buttons
                            word6 btns = (pushButtons >> shift) & 077;
                            if (btns == (instrBuf & 00077))
                              displayAddressCounter = (displayAddressCounter + 2) & 07777;
                            break;
                          } // case 0, 1

                        case 2: // Arithmetic compare Push buttons bank 2
                          {
                            int skip = 0;
                            if (instrBuf & 00040) // skip unconditional
                              skip = 1;
                            if (instrBuf & 00020) // skip if beam off screen
                              skip |= (xPosition & 016000) == 0 && (yPosition & 016000);
                            if (instrBuf & 00010) // skip if buttons 0-5
                              skip |= (pushButtons && 07700) != 0;
                            if (instrBuf & 00004) // skip if buttons 6-11
                              skip |= (pushButtons && 00077) != 0;
                            if (skip)
                              displayAddressCounter = (displayAddressCounter + 2) & 07777;
                            break;
                          } // case 2
                        case 3: // Count
                          {
                            if (instrBuf & 00040) // Enable count scale
                              {
                                if (instrBuf & 00020) // count down
                                  {
                                    if (scale > 0)
                                      scale --;
                                  }
                                else // count up
                                  {
                                    if (scale < 3)
                                      scale ++;
                                  }
                              }
                            if (instrBuf & 00010) // Enable count intensity
                              {
                                if (instrBuf & 00004) // count down
                                  {
                                    if (intensity > 0)
                                      intensity --;
                                  }
                                else // count up
                                  {
                                    if (intensity < 3)
                                      intensity ++;
                                  }
                              }
                            if (instrBuf & 00002) // Enable blink set
                              blink = instrBuf & 00001;
                            break;
                          } // case 3

                        case 4: // Slave 0 logic
                        case 5: // Slave 1 logic
                        case 6: // Slave 2 logic
                        case 7: // Slave 3 logic
                          break; // slaves not supported.

                      } // switch (uop)
                    break;
                  } // case misc.

                case 7:  // Spare
                  break;

              } // switch opc

            break;
          } // case instructionFetchComplete;

        case midPush:
          {
            // Save the second word and jump.

            // Pushdown list does not use break field
            M [pushDownPtr & 07777] = displayAddressCounter & 0777;
            pushDownPtr = (pushDownPtr + 1) & 07777;

            breakField = instrBuf & 03;
            displayAddressCounter = instrBuf2 & 07777;
            state = instructionState;

            break;
          } // case (midPush)

        case midPop:
          {
            // The top word in the stack is in popbuf; pop the next word

            // Recover the first word on the stack, go to midPop state
            pushDownPtr = (pushDownPtr - 1) & 07777;
            // Pushdown list does not use break field
            word12 popbuf2 = M [pushDownPtr & 07777];

            // Recover the bits from the popped data.
            // popbuf has the DAC, popbuf2 the status bits.

            if ((instrBuf & 00010) == 0) // Do not inhibit mode restore
              mode = (popbuf2 >> 3) & 03;
            
            if ((instrBuf & 00004) == 0) // Do not inhibit light pen and scale  restore
              {
                lightPenEnable = (popbuf2 >> 8) & 01;
                scale = (popbuf2 >> 6) & 03;
              }

            if ((instrBuf & 00002) == 0) // Do not inhibit instensity restore
              intensity = popbuf2 & 07;
            
            if (instrBuf & 00400) // Enable scale change
              scale = (instrBuf >> 6) & 03;
            if (instrBuf & 00040) // Enable light pen change
              lightPenEnable = (instrBuf >> 6) & 03;

            breakField = popbuf2 & 03;
            displayAddressCounter = popbuf & 07777;

            if (instrBuf & 00001) // Enter data state
              {
                if (mode == 07)
                  {
                    internalStopFlag = 1;
                    updateInterrupt ();
                    state = internalStop;
                  }
                else
                  {
                    state = dataState;
                    controlStateFlag = 0;
                  }
              }
            else
              state = instructionState;
            break;
          } // case (midPop)

        case internalStop:

          if (internalStopFlag == 0)
            state = instructionState;
          break;

        case dataState:
          {
            dacwas = displayAddressCounter; // debugging
            instrBuf = M[displayAddressCounter | (breakField << 12)];
            displayAddressCounter = (displayAddressCounter + 1) & 07777;
            switch (mode)
              {
                case 0: // Point
                case 2: // Vector
                case 3: // Vector Continue
                  state = midDataFetch;
                  break;
                case 1: // Increment
                case 4: // Short Vector
                case 5: // Character
                case 6: // Graphplot
                  state = dataFetchComplete;
                  break;
                case 7: // Unused
                  // Can't happen -- mode is verifed on entry to data state.
                  break;
              }
            break;
          } // case dataState

        case midDataFetch:
          instrBuf2 = M[displayAddressCounter | (breakField << 12)];
          displayAddressCounter = (displayAddressCounter + 1) & 07777;
          state = dataFetchComplete;
          break;

        case dataFetchComplete:
          {
//printf ("d %04o %04o %d\r\n", dacwas, instrBuf, mode); 
            switch (mode)
              {
                case 0: // Point
                  {
                    word13 y = yPosition;
                    word13 x = xPosition;
                    if ((instrBuf & 02000) == 0)
                      y = (yPosition & 016000) | (instrBuf & 01777);

                    if ((instrBuf2 & 02000) == 0)
                      x = (xPosition & 016000) | (instrBuf2 & 01777);
                    word1 beamOn = (instrBuf & 04000) ? 1 : 0;
                    drawPoint (x, y, beamOn);
                    break;
                  } // case (point)

                case 1: // Increment
                  {
                    word1 beamOn1 = (instrBuf & 04000) ? 1 : 0;
                    word2 nMoves1 = (instrBuf >> 9) & 03;
                    word3 dir1 = (instrBuf >> 6) & 07;
                    drawIncrement (dir1, nMoves1, beamOn1);
                    if (nMoves1 != 0)
                      {
                        word1 beamOn2 = (instrBuf & 00040) ? 1 : 0;
                        word2 nMoves2 = (instrBuf >> 3) & 03;
                        word3 dir2 = instrBuf & 07;
                        drawIncrement (dir2, nMoves2, beamOn2);
                      }
                    break;
                  }

                case 2: // Vector
                case 3: // Vector Continue
                  {
                    word1 beamOn = (instrBuf & 04000) ? 1 : 0;
                    word10 dy = instrBuf & 01777;
                    word1 sy = (instrBuf & 02000) ? 1 /* - */ : 0 /* + */;
                    word10 dx = instrBuf2 & 01777;
                    word1 sx = (instrBuf2 & 02000) ? 1 /* - */ : 0 /* + */;
//printf ("sx %d dx %d sy %d dy %d\r\n", sx, dx, sy, dy);
                    word13 x, y;

                    if (sx)
                      x = (xPosition - dx) & 017777;
                    else
                      x = (xPosition + dx) & 017777;

                    if (sy)
                      y = (yPosition - dy) & 017777;
                    else
                      y = (yPosition + dy) & 017777;

                    drawVector (x, y, beamOn, mode == 3);
                    break;
                  } // case (vector, vector continue)

                  break;

                case 4: // Short Vector
                  {
//printf ("svec\r\n");
                    word1 beamOn = (instrBuf & 04000) ? 1 : 0;
                    word10 dy = (instrBuf >> 6) & 017;
                    word1 sy = (instrBuf & 02000) ? 1 /* + */ : 0 /* - */;
                    word10 dx = (instrBuf >> 0) & 017;
                    word1 sx = (instrBuf & 00020) ? 1 /* + */ : 0 /* - */;

                    word13 x, y;

                    if (sx)
                      x = (xPosition + dx) & 017777;
                    else
                      x = (xPosition - dx) & 017777;

                    if (sy)
                      y = (yPosition + dy) & 017777;
                    else
                      y = (yPosition - dy) & 017777;

                    drawVector (x, y, beamOn, 0);
                    break;
                  } // case (short vector)

                case 5: // Character
                  {
                    if (cgCodeSize) // Set is 7 bit
                      {
                        word7 ch = instrBuf & 00177;
                        // XXX draw char (ch)
                      }
                    else
                      {
                        word7 ch1 = (instrBuf >> 6) & 00077;
                        word7 ch2 = (instrBuf >> 0) & 00077;
                        // XXX draw char (ch1)
                        // XXX draw char (ch2)
                      }
                    break;
                  } // case (character)

                case 6: // Graphplot
                  {
                    word13 x = xPosition;
                    word13 y = yPosition;
                    if (instrBuf & 02000) // incr. y, set x
                      {
                        y = (y + 1) & 017777;
                        x = instrBuf & 01777;
                      }
                    else // incr. x, set y
                      {
                        x = (x + 1) & 017777;
                        y = instrBuf & 01777;
                      }
                    drawVector (x, y, 1, 0);
                    break;
                  } // case: graphplot

                case 7: // Unused
                  // Can't happen -- mode is verifed on entry to data state.
                  break;
              } // switch (mode)
            state = dataExecutionWait;
            break;
          } // case dataFetchComplete

        case dataExecutionWait:
          {
            switch (mode)
              {
                case 0: // Point
                case 2: // Vector
                case 3: // Vector Continue
                case 6: // Graphplot
                  {
                    if (instrBuf2 & 04000) // Escape
                      {
                        state = instructionState;
                        controlStateFlag = 1;
                      }
                    else
                      state = dataState;
                    break;
                  } // case point

                case 1: // Increment
                  {
                    if ((instrBuf & 03000) == 0 || // Escape in first half
                        (instrBuf & 00030) == 0) // Escape in second half
                      {
                        state = instructionState;
                        controlStateFlag = 1;
                      }
                    else
                      {
                        state = dataState;
                      }
                    break;
                  }

                case 4: // Short vector
                  {
//printf ("svec esc %04o\r\n", instrBuf & 00040);
                    if (instrBuf & 00040) // Escape
                      {
                        state = instructionState;
                        controlStateFlag = 1;
                      }
                    else
                      state = dataState;
                    break;
                  } // case point

                case 5: // Character
                case 7: // Unused Can't happen -- mode is verifed on entry to data state.
                  state = dataState;
                  break;
              } // switch mode
            break;
          } // case dataExecutionWait
      } // switch (state)
  } // sim_instr_d8


/* x11 callbacks */

void lp_hit (void)
  {
    lightPenHitFlag = 1; // signal the engine
    updateInterrupt ();
    breakRequestFlag = 0; // Stop the display
    //sim_printf ("lp hit\r\n");
  }

void btn_press (int n)
  {
    if (n < 0 || n > 11)
      return;
    word12 mask = 1u << n;
    // Pressing a button complements its state.
    pushButtons ^= mask;
    // Setting a button on unsets the others 
    // in its group
    if (pushButtons & mask)
      {
        word12 cmask;
        if (n < 6)
          cmask = 07700; // if in the low group, keep the high group
        else
          cmask = 00077; // v.v.
        cmask |= mask; // keep the one just set
        pushButtons &= cmask; // clear the others in the group
      }
    pushButtonHitFlag = 1; // signal the engine
    updateInterrupt ();
    //printf ("pb %04o\r\n", pushButtons);
  }


