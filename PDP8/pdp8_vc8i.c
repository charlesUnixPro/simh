/* pdp8_vc8i.c: DEC VC8/I Display

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

   vc8i          VC8/I Display controller

*/

/* See http://www.chdickman.com/pdp8/VC8/VC8.txt */
//
//
// 6051	clear X register
// 6052	load X register
// 6054	display point at XY
// 6061	clear Y register
// 6062	load Y register
// 6064	display point at XY
// 6075	load brightness register with intensity 1	
// 6076	load brightness register with intensity 2	
// 6077	load brightness register with intensity 3	
//
//
//   http://www.chdickman.com/pdp8/VC8/
//
// 
// Option	Mnemonic	Code	Operation
// VC8/I
// DCX	6051	Clear X Position Register
// DXL	6052	Load X Position Register
// DIX	6054	Intensify Oscilloscope Beam
// DXS	6057	Microprogrammed Combination of DCX, DXL and DIX
// DCY	6061	Clear Y Position Register
// DYL	6062	Load Y Position Register
// DIY	6064	Intensify Oscilloscope Beam
// DYS	6067	Microprogrammed Combination of DCY, DYL and DIY
// DSK (?)	6071	Skip if Light Pen Flag is Set
// DPC (?)	6072	Clear Light Pen Flag
// DSB n	6074	Set Brightness to n

#include <stdint.h>
#include "pdp8_defs.h"


// Interface to X11

void initGraphics (char * dispname, int argc, char * argv [], int windowSizeScale_,
                   int lineWidth_, char * windowName, 
                   void (* lpHitp) (void), void (* btnPressp) (int n));
void handleInput (void);
void drawSegment (uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t intensity);
void flushDisplay (void);
void refreshDisplay (void);

typedef uint16 word1;
typedef uint16 word2;
typedef uint16 word3;
typedef uint16 word6;
typedef uint16 word7;
typedef uint16 word10;
typedef uint16 word12;
typedef uint16 word13;
typedef uint16 word15;

static void drawPoint (word12 x, word12 y, word1 beamOn);
static void lpHit (void);
static void btnPress (int n);

extern uint16 M[];

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;
extern int32 saved_PC;                                     /* saved IF'PC */

static char * x11DisplayName = NULL;
static int refresh_rate = 1;
DEVICE vc8i_dev;

//static int32 vc8i_err = 0;                                      /* error flag */
//static int32 vc8i_stopioe = 0;                                  /* stop on error */

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
static word1  verticalEdgeFlag;  // XXX unimpl.
static word1  horizontalEdgeFlag;  // XXX unimpl.
static word1  lightPenHitFlag;
static word1  pushButtonHitFlag;
static word1  manualInterruptFlag;  // XXX unimpl.

// Interrupt pending
static word1  displayInterruptFlag;

// Interrupt enables
static word1  enableEdgeFlagInterrupt;
static word1  enableLightPenFlagInterrupt;
static word1  enablePushButtonHitInterrupt;
static word1  enableInternalStopInterrupt;

// Non-interrupt causing flags
static word1  sector0Flag;  // XXX unimpl.

// General registers
static word1  lightPenEnable;
static word12 pushDownPtr;
static word13 xPosition;
static word13 yPosition;
static word12 displayAddressCounter; // Plus breakField for 15 bits 
static word1  controlStateFlag; /* 0: data state, 1: control state */
static word3  breakField;
static word1  byteFlipFlop; // XXX unimpl
static word2  scale; // XXX unimpl
static word3  mode;
static word3  intensity; // XXX unimpl
static word1  blink; // XXX unimpl
static word12 pushButtons;
static word12 slaveGroup1; // XXX unimpl
static word12 slaveGroup2; // XXX unimpl

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
static word6 cgCharacterSave;
static word1 cgMode; // 0 increment, 1 short vector

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
static word1  intensifyAllPoints; // XXX unimpl
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

/* VC8I State engine */

// G Bell: Computer Structures: Readings and Examples, Chap. 25

static word1 signalExternalStop;

enum vc8i_state {
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

static char * opcodes [8] =
  {
    "parameter",
    "mode",
    "jump",
    "pop",
    "skip",
    "skip2",
    "misc",
    "spare"
  };

static char * datacodes [8] =
  {
    "point",
    "increment",
    "vector",
    "vector continue",
    "short vector",
    "character",
    "graphplot",
    "unused"
  };

static enum vc8i_state state;
/* sub-state flags */
static word12 instrBuf;
static word12 instrBuf2;
static word12 popbuf;

int32 vc8i_lpApt = 5;

int32 vc8i_05 (int32 IR, int32 AC);
int32 vc8i_06 (int32 IR, int32 AC);
int32 vc8i_07 (int32 IR, int32 AC);
t_stat vc8i_svc (UNIT * uptr);
static t_stat vc8i_reset (DEVICE * dptr);
static t_stat set_display (UNIT * uptr, int32 val, char * cptr, void * desc);
static t_stat show_display (FILE * st, UNIT * uptr, int32 val, void * desc);

/* VC8I data structures

   vc8i_dev      VC8I device descriptor
   vc8i_unit     VC8I unit descriptor
   vc8i_reg      VC8I register list
*/

DIB vc8i_dib = { DEV_VC8I, 010, {
  NULL,   /* 00 */
  NULL,   /* 01 */
  NULL,   /* 02 */
  NULL,   /* 03 */
  NULL,   /* 04 */
  &vc8i_05, /* 05 */
  &vc8i_06, /* 06 */
  &vc8i_07  /* 07 */
  } };

UNIT vc8i_unit = { UDATA (&vc8i_svc, 0, 0), 0 };

REG vc8i_reg[] = {
    //{ ORDATA (BUF, vc8i_unit.buf, 8) },
    //{ FLDATA (ERR, vc8i_err, 0) },
    //{ FLDATA (DONE, dev_done, INT_V_VC8I) },
    //{ FLDATA (ENABLE, int_enable, INT_V_VC8I) },
    //{ FLDATA (INT, int_req, INT_V_VC8I) },
    //{ DRDATA (POS, vc8i_unit.pos, T_ADDR_W), PV_LEFT },
    //{ DRDATA (TIME, vc8i_unit.wait, 24), PV_LEFT },
    //{ FLDATA (STOP_IOE, vc8i_stopioe, 0) },
    //{ ORDATA (DEVNUM, vc8i_dib.dev, 6), REG_HRO },
    { NULL }
    };

MTAB vc8i_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_QUOTE, 0, "DISPLAY", "DISPLAY",
      & set_display, & show_display, NULL },
    { 0 }
    };

#define DBG_IOT         (1U << 0)    // IOT trace
#define DBG_DRAW        (3U << 0)    // graphics trace

static DEBTAB vc8i_dt [] =
  {
    { "IOT",    DBG_IOT },   // Trace IOTs
    { "DRAW",   DBG_DRAW },  // Trace graphics
    NULL
  };

DEVICE vc8i_dev =
  {
    "VC8I",
    & vc8i_unit,
    vc8i_reg,
    vc8i_mod,
    1,
    10,
    31,
    1,
    8,
    8,
    NULL,
    NULL,
    & vc8i_reset,
    NULL,
    NULL,
    NULL,
    & vc8i_dib,
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0,
    vc8i_dt
  };

/* Unit service */

t_stat vc8i_svc (UNIT *uptr)
{
//sim_printf ("svc %d %d\n", time(NULL), tmxr_poll);
#if 0
static time_t last = 0;
static int cnt = 0; 
time_t now = time(NULL);
if (now == last) cnt ++;
else { printf ("cnt %d tmxr_poll %d\r\n", cnt, tmxr_poll); cnt = 0; last = now;}
#endif
//sim_clock_coschedule (uptr, refresh_rate);                 /* continue poll */
sim_activate (uptr, tmxr_poll/refresh_rate/**tmxr_poll*/);                 /* continue poll */

refreshDisplay ();
handleInput ();
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

int32 vc8i_05 (int32 IR, int32 AC)
  {
    /* decode IR<9:11> */
    if (IR & 01)                                        /* DCX */
      {
        // DCX 6051 Clear X Position Register
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DCX");
        xPosition = 0;
      }

    if (IR & 02)                                        /* DXL */
      {
        // DXL 6052 Load x Position Register
        // A 1s transfer from the x position register to the AC is done.
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DXL");
        xPosition = AC;
      }

    if (IR & 04)                                        /* DIX */
      {
        // DIX 6054 Intensify Oscilloscope Beam
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DIX");
        drawPoint (xPosition, yPosition, 1);
      }
    return AC;
  }

int32 vc8i_06 (int32 IR, int32 AC)
  {
    /* decode IR<9:11> */
    if (IR & 01)                                        /* DCY */
      {
        // DCY 6061 Clear Y Position Register
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DCY");
        yPosition = 0;
      }

    if (IR & 02)                                        /* DYL */
      {
        // DYL 6062 Load x Position Register
        // A 1s transfer from the x position register to the AC is done.
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DYL");
        yPosition = AC;
      }

    if (IR & 04)                                        /* DIY */
      {
        // DIY 6064 Intensify Oscilloscope Beam
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DIY");
        drawPoint (xPosition, yPosition, 1);
      }
    return AC;
  }

int32 vc8i_07 (int32 IR, int32 AC)
  {
    word12 ret = AC;
    /* decode IR<9:11> */
    if ((IR & 07) == 1)                                 /* DSK */
      {
        // DSK 6071 Skip if Light Pen Flag is Set
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DSK");
        ret = lightPenHitFlag ? IOT_SKP | AC : AC;
      }

    if ((IR & 07) == 2)                                 /* DPC */
      {
        // DPC 6072 Clear Light Pen Flag
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DPC");
        lightPenHitFlag = 0;
      }

    if (IR & 04)                                        /* DSB */
      {
        // DSB 6074 Set Brightness to n
        sim_debug (DBG_IOT, & vc8i_dev, "%o %04o %04o i %s\n", (saved_PC >> 12) & 03, saved_PC & 07777, M [saved_PC], "DIY");
        intensity = IR & 03;
      }
    return ret;
  }


/* Reset routine */

static int graphicsInited = 0;

static t_stat vc8i_reset (DEVICE * dptr)
  {
    // 2.3.2.8 "The power clear pulse (START key) also clears all display flags.
    // All display flags cat be cleared by giving three IOTs: CFD-6161 (internal
    // and external stop, light pen high, and edge); RS1-6062 (push button); 
    // and SPMI-6172 (manual interrupt).
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
    //xPosition = 0;
    //yPosition = 0;

    dev_done = dev_done & ~INT_VC8I;                     /* clear done, int */
    int_req = int_req & ~INT_VC8I;
    int_enable = int_enable | INT_VC8I;                      /* set enable */

    return SCPE_OK;
  }

static void setupGraphics (void)
  {
    if (graphicsInited == 0)
      {
        int argc = 0;
        char ** argv = NULL;
        int windowSize = 0; // 0 large, 1 small
        int usePixmap = 0;
        int lineWidth = 0;
        static char * windowName = "338";
        initGraphics (x11DisplayName, 0, NULL, windowSize, lineWidth, windowName, lpHit, btnPress);
        drawSegment (0, 0, 0, 0, 0);
        flushDisplay ();
        graphicsInited = 1;
        //vc8i_unit.wait=1;
// XXX This is in the wrong place
        sim_activate (& vc8i_unit, tmxr_poll / refresh_rate);
      }
  }

t_stat set_display (UNIT * uptr, int32 val, char * cptr, void * desc)
  {
    if (x11DisplayName)
      free (x11DisplayName);
    x11DisplayName = strdup (cptr);
    sim_printf ("Display set to '%s'\n", x11DisplayName);
    return SCPE_OK;
  }

t_stat show_display (FILE * st, UNIT * uptr, int32 val, void * desc)
  {
    sim_printf ("Display set to '%s'\n", x11DisplayName);
    return SCPE_OK;
  }
// Interface to the vector H/W

static void drawPoint (word12 x, word12 y, word1 beamOn)
  {
    sim_debug (DBG_DRAW, & vc8i_dev, "draw point at x %d y %d on %d\n", x, y, beamOn);
    if (beamOn)
      drawSegment (x, y, x, y, intensity * 64 + 63);
    xPosition = x;
    yPosition = y;
  }






// Step the display processor

void sim_instr_vc8i (void)
  {
    if (graphicsInited == 0)
      setupGraphics ();
  } // sim_instr_vc8i


/* x11 callbacks */

static void lpHit (void)
  {
    lightPenHitFlag = 1; // signal the engine
    updateInterrupt ();
    breakRequestFlag = 0; // Stop the display
    //sim_printf ("lp hit\r\n");
  }

static void btnPress (int n)
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


