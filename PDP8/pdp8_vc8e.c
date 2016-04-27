/* pdp8_vc8e.c: DEC VC8/E Display

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

   vc8e          VC8/E Display controller

*/

// http://homepage.cs.uiowa.edu/~jones/pdp8/man/vc8e.html
//
// 6050 - DILC Display Logic Clear 
// 6051 - DICD Display Clear Done flag 
// 6052 - DISD Display Skip on Done 
// 6053 - DILX Display Load X 
// 6054 - DILY Display Load Y 
// 6055 - DIXY Display Intensify at (X,Y) 
// 6056 - DILE Display Load Enable status register 
// 6057 - DIRE Display Read Enable status register
//
// Enable/Status Register
//
//            00 01 02 03 04 05 06 07 08 09 10 11
//            ___________________________________
//           |  |  |  |  |  |  |  |  |  |  |  |  |
//           |__|__|__|__|__|__|__|__|__|__|__|__|
//           |  |              |  |  |  |  |  |  |
//           |DN|   Ignored    |WT ST ER CO CH IE|
//
// Note that bits 1 to 5 were not stored in the enable status register, and
// that bits 6 to 9 (WT through CO) were well documented but only implemented
// on VC8E systems based on M869 Rev D or higher and M885 Rev F or higher.
//
// IE -- Interrupt Enable
// CH -- Channel
// CO -- Color
// ER -- Erase (write only)
// ST -- Store
// WT -- Write Through
// DN -- Done (read only)
//
// The IE and DN bits are used with all displays. DN indicates that the
// interface has finished its most recent command. If interrupts are enabled,
// the interface will request an interrupt when DN is asserted.
//
// The CH bit is intended for use with addressable displays such as the VR14
// and VR20. These displays decode this one-bit address line and ignore
// intensify commands if the CH output does not equal the selected display.
// This allows one VC8E interface to control two displays. When used with pen
// plotters, the CH bit is the recommended control for pen-up/pen-down; this is
// driven by a TTL line driver.
//
// The CO bit is intended for use with two-color displays such as the VR20. On
// that display, a one in this bit causes a red display, while a zero causes a
// green display. If any VR20 displays were ever sold, they were discontinued
// by August of 1973, and this bit is only supported on VC8E interfaces using
// M869 Rev D or higher and M885 Rev F or higher.
//
// The ER, ST and WT bits are intended for use with storage scopes such as the
// Tektronix 611 or 613. Outputting a one to ER will cause an erase pulse, If
// ST is off, the storage scope will not be in store mode. If ST is on, the
// flood beam will be turned on, causing a faint green background glow and
// causing plotted points to be stored. The WT bit causes the writing beam to
// be defocused during the intensify pulse, displaying a non-storing dim
// ellipse instead of a stored point. These bits are only supported on VC8E
// interfaces using M869 Rev D or higher and M885 Rev F or higher.
//

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

extern uint16 M[];

extern int32 int_req, int_enable, dev_done, stop_inst;
extern int32 tmxr_poll;

static char * x11DisplayName = NULL;
static int refresh_rate = 1;
DEVICE vc8e_dev;
static word12 esreg;

// Interrupt pending
static word1  displayInterruptFlag;

// General registers
static word12 xPosition;
static word12 yPosition;
static word3  intensity; 

int32 vc8e_05 (int32 IR, int32 AC);
t_stat vc8e_svc (UNIT * uptr);
static t_stat vc8e_reset (DEVICE * dptr);
static t_stat set_display (UNIT * uptr, int32 val, char * cptr, void * desc);
static t_stat show_display (FILE * st, UNIT * uptr, int32 val, void * desc);

/* VC8E data structures

   vc8e_dev      VC8E device descriptor
   vc8e_unit     VC8E unit descriptor
   vc8e_reg      VC8E register list
*/

DIB vc8e_dib = { DEV_VC8E, 06, {
  NULL,   /* 00 */
  NULL,   /* 01 */
  NULL,   /* 02 */
  NULL,   /* 03 */
  NULL,   /* 04 */
  &vc8e_05  /* 05 */
  } };

UNIT vc8e_unit = { UDATA (&vc8e_svc, 0, 0), 0 };

REG vc8e_reg[] = {
    { NULL }
    };

MTAB vc8e_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_QUOTE, 0, "DISPLAY", "DISPLAY",
      & set_display, & show_display, NULL },
    { 0 }
    };

#define DBG_IOT         (1U << 0)    // IOT trace
#define DBG_DRAW        (3U << 0)    // graphics trace

static DEBTAB vc8e_dt [] =
  {
    { "IOT",    DBG_IOT },   // Trace IOTs
    { "DRAW",   DBG_DRAW },  // Trace graphics
    NULL
  };

DEVICE vc8e_dev =
  {
    "VC8E",
    & vc8e_unit,
    vc8e_reg,
    vc8e_mod,
    1,
    10,
    31,
    1,
    8,
    8,
    NULL,
    NULL,
    & vc8e_reset,
    NULL,
    NULL,
    NULL,
    & vc8e_dib,
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,
    0,
    vc8e_dt
  };

/* Unit service */

t_stat vc8e_svc (UNIT *uptr)
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

int32 vc8e_05 (int32 IR, int32 AC)
  {
    switch (IR & 07)                                    /* decode IR<9:11> */
      {
        case 0:                                         /* DILC */
          {
	  // DILC 6050 Display Logic Clear Clear all display flags, disable
	  // interrupts and reset done. This is equivalent to a console
	  // reset. Not supported by the display interfaces on PDP-8 models
	  // prior to the PDP-8/E, and thus should be avoided in portable
	  // code.

            sim_debug (DBG_IOT, & vc8e_dev, "DILC\n");
            xPosition = 0;
            yPosition = 0;
            intensity = 3;
            esreg = 0;
            return AC;
          }

        case 1:                                         /* DICD */
          {
            // DICD 6051 Display Clear Done
            // Resets DN (the done flag) in the command/status register.
            sim_debug (DBG_IOT, & vc8e_dev, "DICD\n");
            esreg &= 04000;
            return AC;
          }

        case 2:                                         /* DISD */
          {
            // DISD 6052 Display Skip if Done
            // Skips the next instruction if DN (the done flag) is set; does not clear DN.
            sim_debug (DBG_IOT, & vc8e_dev, "DISD\n");
            return (esreg & 04000) ? IOT_SKP | AC : AC;
          }

        case 3:                                         /* DILX */
          {
            // DILX 6053 Display Load X
	  // The contents of the accumulator is loaded into the X register;
	  // this does not clear the accumulator. The command sets the done
	  // flag after an appropriate interval determined by the display
	  // type.
            sim_debug (DBG_IOT, & vc8e_dev, "DILX\n");
            xPosition = AC;
            esreg |= 04000;
            return AC;
          }

        case 4:                                         /* DILY */
          {
            // DILY 6054 DILY Display Load Y
	  // The contents of the accumulator is loaded into the X register;
	  // this does not clear the accumulator. The command sets the done
	  // flag after an appropriate interval determined by the display
	  // type.
            sim_debug (DBG_IOT, & vc8e_dev, "DILY\n");
            yPosition = AC;
            esreg |= 04000;
            return AC;
          }

        case 5:                                         /* DIXY */
          {
            // DIXY 6055 DIXY Display Intensify at (X,Y)
	  // An 1 microsecond intensify pulse is output to the display,
	  // causing a spot to be displayed at the current X and Y locations.
	  // This should be done only when DN (the done flag) has been set
	  // indicating that the X and Y digital to analog converters have
	  // settled after the most recent DILX or DILY; if not, the
	  // displayed point may not be at the intended location.
            sim_debug (DBG_IOT, & vc8e_dev, "DIXY\n");
            drawPoint (xPosition, yPosition, 1);
            return AC;
          }

        case 6:                                         /* DILE */
          {
	  // DILE 6056 Display Clear Done
	  // The enable/status register is loaded from the accumulator, and
	  // the accumulator is cleared. This does not load DN (the done
	  // flag).
            sim_debug (DBG_IOT, & vc8e_dev, "DILE\n");
            esreg = AC & 00077;
            return 0;
          }

        case 7:                                         /* DIRE */
          {
            // DIRE 6057 Display Clear Done
	  // The contents of the enable/status register are read into the
	  // accumulator. ER (erase) and any unimplemented bits are always
	  // read as zero.
            sim_debug (DBG_IOT, & vc8e_dev, "DIRE\n");
            return (esreg & 04067);
          }
      }
    return AC;
  }



/* Reset routine */

static int graphicsInited = 0;

static t_stat vc8e_reset (DEVICE * dptr)
  {

    dev_done = dev_done & ~INT_D8;                     /* clear done, int */
    int_req = int_req & ~INT_D8;
    int_enable = int_enable | INT_D8;                      /* set enable */

    xPosition = 0;
    yPosition = 0;
    intensity = 3;
    esreg = 0;
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
        static char * windowName = "VC8/E";
        initGraphics (x11DisplayName, 0, NULL, windowSize, lineWidth, windowName, NULL, btnPress);
        drawSegment (0, 0, 0, 0, 0);
        flushDisplay ();
        graphicsInited = 1;
        //vc8e_unit.wait=1;
// XXX This is in the wrong place
        sim_activate (& vc8e_unit, tmxr_poll / refresh_rate);
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
// on the VC8E, the coordinates are signed 10 bit values...

#if 0
    int sx = x & 01777; 
    int sy = y & 01777; 
    if (x & 01000)
      sx = -sx + 1;
    if (y & 01000)
      sy = -sy + 1;
#endif

    word10 sx = (x + 01000) & 01777;
    word10 sy = (y + 01000) & 01777;

    sim_debug (DBG_DRAW, & vc8e_dev, "draw point at x %d y %d on %d int %d\n", x, y, beamOn, intensity);
    if (beamOn)
      drawSegment (sx, sy, sx, sy, intensity * 64 + 63);
    xPosition = x;
    yPosition = y;
  }






// Step the display processor

void sim_instr_vc8e (void)
  {
    if (graphicsInited == 0)
      setupGraphics ();
  } // sim_instr_vc8e


extern int32 OSR;
static void btnPress (int n)
  {
    if (n < 0 || n > 11)
      return;
    OSR ^= 1 << (11 - n);
    sim_printf ("SR: %04o\n", OSR);
  }




