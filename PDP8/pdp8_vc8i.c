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
static void btnPress (int n);

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

extern uint16 M[];

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;

static char * x11DisplayName = NULL;
static int refresh_rate = 1;
DEVICE vc8i_dev;

static word1  lightPenHitFlag;

// General registers
static word12 xPosition;
static word12 yPosition;
static word3  intensity;

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


/* IOT routines */

int32 vc8i_05 (int32 IR, int32 AC)
  {
    /* decode IR<9:11> */
    if (IR & 01)                                        /* DCX */
      {
        // DCX 6051 Clear X Position Register
        sim_debug (DBG_IOT, & vc8i_dev, "DCX\n");
        xPosition = 0;
      }

    if (IR & 02)                                        /* DXL */
      {
        // DXL 6052 Load x Position Register
        // A 1s transfer from the x position register to the AC is done.
        sim_debug (DBG_IOT, & vc8i_dev, "DXL\n");
        xPosition = AC;
      }

    if (IR & 04)                                        /* DIX */
      {
        // DIX 6054 Intensify Oscilloscope Beam
        sim_debug (DBG_IOT, & vc8i_dev, "DIX\n");
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
        sim_debug (DBG_IOT, & vc8i_dev, "DCY\n");
        yPosition = 0;
      }

    if (IR & 02)                                        /* DYL */
      {
        // DYL 6062 Load x Position Register
        // A 1s transfer from the x position register to the AC is done.
        sim_debug (DBG_IOT, & vc8i_dev, "DYL\n");
        yPosition = AC;
      }

    if (IR & 04)                                        /* DIY */
      {
        // DIY 6064 Intensify Oscilloscope Beam
        sim_debug (DBG_IOT, & vc8i_dev, "DIY\n");
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
        sim_debug (DBG_IOT, & vc8i_dev, "DSK\n");
        ret = lightPenHitFlag ? IOT_SKP | AC : AC;
      }

    if ((IR & 07) == 2)                                 /* DPC */
      {
        // DPC 6072 Clear Light Pen Flag
        sim_debug (DBG_IOT, & vc8i_dev, "DPC\n");
        lightPenHitFlag = 0;
      }

    if (IR & 04)                                        /* DSB */
      {
        // DSB 6074 Set Brightness to n
        sim_debug (DBG_IOT, & vc8i_dev, "DSB\n");
        intensity = IR & 03;
      }
    return ret;
  }


/* Reset routine */

static int graphicsInited = 0;

static t_stat vc8i_reset (DEVICE * dptr)
  {
    lightPenHitFlag = 0;
    xPosition = 0;
    yPosition = 0;

    dev_done = dev_done & ~INT_D8;                     /* clear done, int */
    int_req = int_req & ~INT_D8;
    int_enable = int_enable | INT_D8;                      /* set enable */

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
        static char * windowName = "VC8/I";
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
    //sim_printf ("lp hit\r\n");
  }


extern int32 OSR;
static void btnPress (int n)
  {
    if (n < 0 || n > 11)
      return;
    OSR ^= 1 << (11 - n);
    sim_printf ("SR: %04o\n", OSR);
  }


