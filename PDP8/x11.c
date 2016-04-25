/*
 * x_interface.c: X window system display for Atari Vector game simulator
 *
 * Copyright 1991, 1992, 1993, 1996, 2003 Eric Smith and Hedley Rainnie
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define XHEIGHT 1024
#define XWIDTH 1024

static int dirty = 0; /* flag to avoid erasing & drawing if no vectors */
static Display *d;
static Window w;
#define d8_lpApt 5 // XXX make into d8 settable value
static unsigned long lastPixel = (unsigned long) -1l;

static GC mygc;
static int windowSizeScale;

/* pixel values */
static unsigned long black, white;

static int lineWidth;
static int windowWidth, windowHeight;
static int lightPenX = 0, lightPenY = 0, lightPenZ = 0;
static int needRedraw = 0;

static void (* btnPress) (int n);
static void (* lpHit) (void);

void handleInput (void)
  {
    XEvent event;
    int tmp;
    int ret;

#define MAX_MAPPED_STRING_LENGTH 10
    char buffer[MAX_MAPPED_STRING_LENGTH];
    int bufsize=MAX_MAPPED_STRING_LENGTH;

    while (XEventsQueued (d, QueuedAfterReading) != 0)
      {
        XNextEvent (d, & event);
//printf ("xev %d\r\n", event.type);
        switch (event.type)
          {
            //case KeyRelease:
            case KeyPress:
              {
                KeySym keysym;
                XComposeStatus compose;
                XLookupString ((XKeyEvent *) & event, buffer, bufsize,
                    &keysym, &compose);
//printf ("xev kc %d 0x%x ks %d 0x%x\r\n", event.xkey.keycode, event.xkey.keycode, keysym, keysym);
                switch (keysym)
                  {
                    case XK_1:
                      if (btnPress)
                        btnPress (0);
                      break;

                    case XK_2:
                      if (btnPress)
                        btnPress (1);
                      break;

                    case XK_3:
                      if (btnPress)
                        btnPress (2);
                      break;

                    case XK_4:
                      if (btnPress)
                        btnPress (3);
                      break;

                    case XK_5:
                      if (btnPress)
                        btnPress (4);
                      break;

                    case XK_6:
                      if (btnPress)
                        btnPress (5);
                      break;

                    case XK_7:
                      if (btnPress)
                        btnPress (6);
                     break;

                    case XK_8:
                      if (btnPress)
                        btnPress (7);
                      break;

                    case XK_9:
                      if (btnPress)
                        btnPress (8);
                      break;

                    case XK_0:
                      if (btnPress)
                        btnPress (9);
                      break;

                    case XK_minus:
                      if (btnPress)
                        btnPress (10);
                      break;

                    case XK_equal:
                      if (btnPress)
                        btnPress (11);
                      break;
                  } // switch (keysym)
              } // case keypress
              break;

            case MotionNotify:
//printf ("%d %d\r\n", event.xmotion.x, event.xmotion.y);
              lightPenX = event.xmotion.x;
              lightPenY = event.xmotion.y;
              break;

            case ButtonPress:
//printf ("press %d\r\n", event.xbutton.button);
              lightPenZ = 1;
              break;

            case ButtonRelease:
//printf ("release %d\r\n", event.xbutton.button);
              lightPenZ = 0;
              break;

            case ConfigureNotify:
              needRedraw = 1;
              break;

            default:
              //printf ("event %d\r\n", event.type);
              break;
          } // switch (event.type)
      }
  }

void initGraphics (char * dispname, int argc, char * argv [], int windowSizeScale_,
                   int lineWidth_, char * windowName, 
                   void (* lpHitp) (void), void (* btnPressp) (int n))
  {
    lpHit = lpHitp;
    btnPress = btnPressp;
    XSizeHints myhint;
    int myscreen;

    windowSizeScale = windowSizeScale_;
    lineWidth = lineWidth;

    d = XOpenDisplay (dispname);
    if (!d)
      {
        fprintf (stderr, "Can't init X\n"); 
        exit (1);
      }
    myscreen = DefaultScreen (d);
    white = WhitePixel (d, myscreen);
    black = BlackPixel (d, myscreen);
  
    windowHeight = XHEIGHT >> windowSizeScale;
    windowWidth  = XWIDTH >> windowSizeScale;

    myhint.x = 50;
    myhint.y = 50;
    myhint.width = windowWidth;
    myhint.height = windowHeight;
    myhint.flags = PPosition | PSize;
    
    w = XCreateSimpleWindow
      (d,
       DefaultRootWindow (d),
       myhint.x, myhint.y, myhint.width, myhint.height,
       5,
       white, black);

    XSetStandardProperties (d, w, windowName, 
                            windowName, None, argv, argc, & myhint);
    mygc = XCreateGC (d, w, 0, 0);
    XSetBackground (d, mygc, black);
    XSetForeground (d, mygc, white);

    XSetLineAttributes (d, mygc, lineWidth, LineSolid, CapButt, JoinMiter);

    XSelectInput (d, w,
                  ButtonPressMask | ButtonReleaseMask |
                  KeyPressMask | KeyReleaseMask |
                  PointerMotionMask |
                  StructureNotifyMask);

    XMapRaised (d, w);

    dirty = 0;
    needRedraw = 1;
    //lastPixel = white;
  }

// http://csharphelper.com/blog/2014/08/find-the-shortest-distance-between-a-point-and-a-line-segment-in-c/
// Calculate the distance between
// point pt and the segment p1 --> p2.
static int dist (int p1X, int p1Y, int p2X, int p2Y, int ptX, int ptY)
  {
    float dx = p2X - p1X;
    float dy = p2Y - p1Y;
    if ((dx == 0) && (dy == 0))
      {
        // It's a point not a line segment.
        //closest = p1;
        dx = ptX - p1X;
        dy = ptY - p1Y;
        return sqrtf (dx * dx + dy * dy);
      }

    // Calculate the t that minimizes the distance.
    float t = ((ptX - p1X) * dx + (ptY - p1Y) * dy) /
              (dx * dx + dy * dy);

    // See if this represents one of the segment's
    // end points or a point in the middle.
    if (t < 0)
      {
        //closest = new PointF(p1.X, p1.Y);
        dx = ptX - p1X;
        dy = ptY - p1Y;
      }
    else if (t > 1)
      {
        //closest = new PointF(p2.X, p2.Y);
        dx = ptX - p2X;
        dy = ptY - p2Y;
      }
    else
      {
        //closest = new PointF(p1.X + t * dx, p1.Y + t * dy);
        float closestX = p1X + t * dx;
        float closestY = p1Y + t * dy;
        dx = ptX - closestX;
        dy = ptY - closestY;
      }

    return sqrtf (dx * dx + dy * dy);
  }

void drawLine (int x0, int y0, int x1, int y1, unsigned long intensity)
  {
#if 0
    if (lightPenZ)
      {
        int d = dist (x0, y0, x1, y1, lightPenX, lightPenY);
        if (d < d8_lpApt)
          lpHit ();
      }
#endif
    unsigned long  pixel;

    //if (z != 0)
      {
        if ((x0 == x1) && (y0 == y1))
          x1++;
        pixel = (intensity << 16) | (intensity << 8) | (intensity << 0);
        if (pixel != lastPixel)
          {
            XSetForeground (d, mygc, pixel);
            pixel = lastPixel;
          }
        XDrawLine (d, w, mygc,
                   x0 >> windowSizeScale, y0 >> windowSizeScale,
                   x1 >> windowSizeScale, y1 >> windowSizeScale);
        dirty = 1;
      }
  }

static void clearDisplay (void)
  {
    XClearWindow (d, w);
  }

void flushDisplay (void)
  {
    XFlush (d);
    //XSync (d, false);
  }

//
// segments: Maintain a display list.
//
// A list of segments is kept. Each segment has:
//    x0,y0,x1,y1   Endpoints of the segment
//    intensity
//    time          The time that segment was added to the display list.
//
// There is a parameter 'persistence', which is how long segments stay
// on the list.

// When a segment is added to the list, the list is scanned to see if it
// is already on the list; it so, the 'time' field is updated to the current
// time. If it wasn't on the list, it is added, with the 'time' field set to 
// the current time, and the segment is drawn on the display.

// Periodically, the display is refreshed. First the display list is scanned,
// looking for segments older than (now - persistence); each one found is
// deleted from the list. If any were deleted, the display is erased and 
// every segment on the list is redrawn.

// DEC 338 Display Brochure (1967): "... the information displayed must
// be refreshed about 30 times a second..."


// get the current time in 100's of a second
static long t (void)
  {
    struct timespec res;
    clock_gettime (CLOCK_MONOTONIC_COARSE, & res);
    return res.tv_sec * 100 + res.tv_nsec / 10000000;
  }

typedef struct entry_
  {
    struct entry_ * next;
    uint16_t x0, y0, x1, y1;
    uint16_t intensity;
    long time;
  } entry;

static entry * head = NULL;
static entry * freelist = NULL;

static void insert (entry * seg)
  {
    seg -> next = head;
    head = seg;
  }

static void addLine (int x0, int y0, int x1, int y1, unsigned long intensity)
  {
    entry * p;
    if (freelist)
      {
        p = freelist;
        freelist = freelist -> next;
      }
    else
      {
        p = malloc (sizeof (entry));
      }
    if (!p)
      {
        fprintf (stderr, "std malloc fail in addLine\n");
        exit (1);
      }
    p -> x0 = x0;
    p -> y0 = y0;
    p -> x1 = x1;
    p -> y1 = y1;
    p -> time = t ();
    p -> intensity = intensity;
    p -> next = head;
    head = p;
  }

static void delete (entry * * parent)
  {
    entry * p = * parent;
    * parent = (* parent) -> next;
    p -> next = freelist;
    freelist = p;
  }

void refreshDisplay (void)
  {
//printf ("%ld %ld\r\n", t (), time (NULL));
    entry * * p =  & head;
    bool change = false;
    long tt = t () - 3; // 3/100ths is about 1/30th.
    while (* p)
      {
        if ((* p) -> time < tt)
          {
            delete (p);
            change = true;
          }
        else
          p = & ((* p) -> next);
      }
// XXX Enabling this check causes test1 to have a blank display until the
// XXX first light pen hit.
    if (needRedraw || change)
      {
        clearDisplay ();
        //int cnt = 0;
        entry * q = head;
        while (q)
          {
            drawLine (q->x0, q->y0, q->x1, q->y1, q->intensity);
            //cnt ++;
            q = q -> next;
          }
//printf ("scnt %d\r\n", cnt);
      }
    flushDisplay ();
    needRedraw = 0;
  }

void drawSegment (uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t intensity)
  {
//printf ("%d %d %d %d %d\r\n", x0, y0, x1, y1, intensity);
    y0 = (XHEIGHT >> windowSizeScale) - y0;
    y1 = (XHEIGHT >> windowSizeScale) - y1;
    if (lightPenZ)
      {
        int d = dist (x0, y0, x1, y1, lightPenX, lightPenY);
        if (d < d8_lpApt)
          if (lpHit)
            lpHit ();
      }
    long tnow = t ();
    entry * p = head;
    while (p)
      {
        if (x0 == p->x0 && y0 == p->y0 && x1 == p->x1 && y1 == p-> y1 &&
            intensity == p -> intensity)
          {
            p -> time = tnow;
            return;
          }
        p = p -> next;
      }
    addLine (x0, y0, x1, y1, intensity);
    drawLine (x0, y0, x1, y1, intensity);
    //flushDisplay ();
  }



    
