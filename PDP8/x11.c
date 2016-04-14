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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#define XHEIGHT 1024
#define XWIDTH 1024

static int blank_display = 1; /* flag to avoid erasing & drawing if no vectors */

static Display *mydisplay;
static Window mywindow;
static Drawable mypixmap;
static GC mygc;

/* pixel values */
static unsigned long black, white;
static unsigned long last_pixel;

static int smallwindow;
static int use_pixmap;
static int line_width;
static int window_width, window_height;


static void setup_keyboard (void)
{
}

void btn_press (int n);
void lp_hit (void);

static int lightPenX = 0, lightPenY = 0, lightPenZ = 0;

void handle_input ()
{
  XEvent event;
  int tmp;
  int ret;

#define MAX_MAPPED_STRING_LENGTH 10
  char buffer[MAX_MAPPED_STRING_LENGTH];
  int bufsize=MAX_MAPPED_STRING_LENGTH;

  while (XEventsQueued (mydisplay, QueuedAfterReading) != 0)
    {
      XNextEvent (mydisplay, & event);
//printf ("xev %d\r\n", event.type);
      switch (event.type)
        {
          //case KeyRelease:
          case KeyPress:
            {
#if 0
              KeySym * keysym = XGetKeyboardMapping (mydisplay, event.xkey.keycode, 1, & ret);
printf ("xev kc %d 0x%x ks %d 0x%x\r\n", event.xkey.keycode, event.xkey.keycode, *keysym, *keysym);
              //switch (event.xkey.keycode)
#endif
              KeySym keysym;
              XComposeStatus compose;
              XLookupString ((XKeyEvent *) & event, buffer, bufsize,
                  &keysym, &compose);
//printf ("xev kc %d 0x%x ks %d 0x%x\r\n", event.xkey.keycode, event.xkey.keycode, keysym, keysym);
              switch (keysym)
                {
                  case XK_1:
//printf ("help\r\n");
                    btn_press (0);
                    break;

                  case XK_2:
                    btn_press (1);
                    break;

                  case XK_3:
                    btn_press (2);
                    break;

                  case XK_4:
                    btn_press (3);
                    break;

                  case XK_5:
                    btn_press (4);
                    break;

                  case XK_6:
                    btn_press (5);
                    break;

                  case XK_7:
                    btn_press (6);
                    break;

                  case XK_8:
                    btn_press (7);
                    break;

                  case XK_9:
                    btn_press (8);
                    break;

                  case XK_0:
                    btn_press (9);
                    break;

                  case XK_minus:
                    btn_press (10);
                    break;

                  case XK_equal:
                    btn_press (11);
                    break;
                } // switch (keysym)
            } // case keypress
            break;

          case MotionNotify:
//printf ("%d %d\r\n", event.xmotion.x, event.xmotion.y);
            //lightPenX = (event.xmotion.x * 256.0) / window_width;
            //lightPenY = (event.xmotion.y * 256.0) / window_height;
            lightPenX = event.xmotion.x;
            //lightPenY = (XHEIGHT >> smallwindow) - event.xmotion.y;
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

        } // switch (event.type)
#if 0
        case ButtonRelease:
//printf ("press %d\n", event.xbutton.button);
          if (event.xbutton.button == 4)
            {
              tmp = encoder_wheel;
              tmp ++;
              tmp &= 0xf;
              encoder_wheel = tmp;
            }
          else if (event.xbutton.button == 5)
            {
              tmp = encoder_wheel;
              tmp --;
              tmp &= 0xf;
              encoder_wheel = tmp;
            }
          break;
//        case ButtonRelease:
//printf ("release %d\n", event.xbutton.button);
//          break; 
          case NoExpose:
            break;
          default:
            printf ("unknown event %d\n", event.type);
            break;
#endif
    }
}

/* create a pixmap to double-buffer the window */
static Drawable create_pixmap (Display *display, Drawable drawable)
  {
    Pixmap apixmap;
    Window root;
    unsigned int width, height, depth, border_width;
    unsigned int act_width, act_height, act_depth;
    int x, y;

    /* get the width, height, and depth of the window */
    if (0 == XGetGeometry (display, drawable, & root, & x, & y,
                               & width, & height, & border_width,
                               & depth))
      {
        fprintf (stderr, "XGeometry() failed for window\n");
        goto fail;
      }

    //printf ("depth: %d\n", depth);

    /* create a pixmap */
    apixmap = XCreatePixmap (display, drawable, width, height, depth);

    /* find out if we are successful */
    if (0 == XGetGeometry (display, apixmap, & root, & x, & y,
                               & act_width, & act_height, & border_width,
                               & act_depth))
      {
        fprintf (stderr, "XGeometry() failed for pixmap\n");
        goto fail;
      }

    if ((act_width == width) && (act_height == height) && (act_depth == depth))
      return (apixmap);

    fprintf (stderr, "pixmap error, requested width %d height %d depth %d\n",
               width, height, depth);
    fprintf (stderr, "                    got width %d height %d depth %d\n",
               act_width, act_height, act_depth);

    /* if unsuccessful, draw directly into the window */
   fail:
    return (drawable);
  }

void init_graphics (int argc, char *argv[], int p_smallwindow,
                   int p_use_pixmap, int p_line_width, char *window_name)
  {
    XSizeHints myhint;
    int myscreen;

    smallwindow = p_smallwindow;
    use_pixmap = p_use_pixmap;
    line_width = p_line_width;

    mydisplay = XOpenDisplay ("");
    if (!mydisplay)
      {
        fprintf (stderr, "Can't init X\n"); 
        exit (1);
      }
    myscreen = DefaultScreen (mydisplay);
    white = WhitePixel (mydisplay, myscreen);
    black = BlackPixel (mydisplay, myscreen);
  
    window_height = XHEIGHT >> smallwindow;
    window_width  = XWIDTH >> smallwindow;

    myhint.x = 50;
    myhint.y = 50;
    myhint.width = window_width;
    myhint.height = window_height;
    myhint.flags = PPosition | PSize;
    
    mywindow = XCreateSimpleWindow
      (mydisplay,
       DefaultRootWindow (mydisplay),
       myhint.x, myhint.y, myhint.width, myhint.height,
       5,
       white, black);

    XSetStandardProperties (mydisplay, mywindow, window_name, 
                            window_name, None, argv, argc, & myhint);
    mygc = XCreateGC (mydisplay, mywindow, 0, 0);
    XSetBackground (mydisplay, mygc, black);
    XSetForeground (mydisplay, mygc, white);

    XSetLineAttributes (mydisplay, mygc, line_width, LineSolid, CapButt, JoinMiter);

    XSelectInput (mydisplay, mywindow,
                      ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | PointerMotionMask);

    XMapRaised (mydisplay, mywindow);

    if (use_pixmap)
      mypixmap = create_pixmap (mydisplay, mywindow);
    else
      mypixmap = mywindow;

    if (mypixmap != mywindow)
      {
        XSetForeground (mydisplay, mygc, black);
        XFillRectangle (mydisplay, mypixmap, mygc, 0, 0, window_width, window_height);
        XSetForeground (mydisplay, mygc, white);
      }

    blank_display = 1;
    last_pixel = white;

    setup_keyboard ();
  }

/**
 * \brief    Fast Square root algorithm
 *
 * Fractional parts of the answer are discarded. That is:
 *      - SquareRoot(3) --> 1
 *      - SquareRoot(4) --> 2
 *      - SquareRoot(5) --> 2
 *      - SquareRoot(8) --> 2
 *      - SquareRoot(9) --> 3
 *
 * \param[in] a_nInput - unsigned integer for which to find the square root
 *
 * \return Integer square root of the input value.
 */
uint32_t SquareRoot(uint32_t a_nInput)
{
    uint32_t op  = a_nInput;
    uint32_t res = 0;
    uint32_t one = 1uL << 30; // The second-to-top bit is set: use 1u << 14 for uint16_t type; use 1uL<<30 for uint32_t type


    // "one" starts at the highest power of four <= than the argument.
    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0)
    {
        if (op >= res + one)
        {
            op = op - (res + one);
            res = res +  2 * one;
        }
        res >>= 1;
        one >>= 2;
    }
    return res;
}
#if 0
// # Given a line with coordinates 'start' and 'end' and the
// # coordinates of a point 'pnt' the proc returns the shortest 
// # distance from pnt to the line and the coordinates of the 
// # nearest point on the line.
// #
// # 1  Convert the line segment to a vector ('line_vec').
// # 2  Create a vector connecting start to pnt ('pnt_vec').
// # 3  Find the length of the line vector ('line_len').
// # 4  Convert line_vec to a unit vector ('line_unitvec').
// # 5  Scale pnt_vec by line_len ('pnt_vec_scaled').
// # 6  Get the dot product of line_unitvec and pnt_vec_scaled ('t').
// # 7  Ensure t is in the range 0 to 1.
// # 8  Use t to get the nearest location on the line to the end
// #    of vector pnt_vec_scaled ('nearest').
// # 9  Calculate the distance from nearest to pnt_vec_scaled.
// # 10 Translate nearest back to the start/end line. 
// # Malcolm Kesson 16 Dec 2012
//   
// def pnt2line(pnt, start, end):
//     line_vec = vector(start, end)
//     pnt_vec = vector(start, pnt)
//     line_len = length(line_vec)
//     line_unitvec = unit(line_vec)
//     pnt_vec_scaled = scale(pnt_vec, 1.0/line_len)
//     t = dot(line_unitvec, pnt_vec_scaled)    
//     if t < 0.0:
//         t = 0.0
//     elif t > 1.0:
//         t = 1.0
//     nearest = scale(line_vec, t)
//     dist = distance(nearest, pnt_vec)
//     nearest = add(nearest, start)
//     return (dist, nearest)

static int dist (int x1, int y1, int x2, int y2, int xp, int yp)
  {
    // line vec
    int lvx = x2 - x1;
    int lvy = y2 - y1;
    //  point vec
    int pvx = xp - x1;
    int pvy = yp - y1;
    // line_len
    int line_len = SquareRoot (lvx * lvx + lvy * lvy);
    // line_unit_vec
    int luvx = 0, luvy = 0;
    if (line_len)
      {
        luvx = lvx / line_len;
        luvy = lvy / line_len;
       }
    // pnt_vec_scaled
    int pvsx = 0, pvsy = 0;
    if (line_len)
      {
        pvsx = pvx / line_len;
        pvsy = pvy / line_len;
       }
    // t
    int t = luvx * pvsx * luvy * pvsy;
    if (t < 0)
      t = 0;
    if (t > 1)
      t = 1;
    // nearest
    int nx = lvx * t;
    int ny = lvy * t;
    // dist
    int dist = SquareRoot ((nx - pvx) * (nx - pvx) + (ny - pvy) * (ny - pvy));
    return dist;
  }
#endif

#if 0
// http://geomalgorithms.com/a02-_lines.html
// dist_Point_to_Segment(): get the distance of a point to a segment
//     Input:  a Point P and a Segment S (in any dimension)
//     Return: the shortest distance from P to S
static int dist (int x1, int y1, int x2, int y2, int xp, int yp)
{
     //Vector v = S.P1 - S.P0;
     int vx = x2 - x1;
     int vy = y2 - y1;

     //Vector w = P - S.P0;
     int wx = xp - x1;
     int wy = yp - y1;

     //double c1 = dot(w,v);
     int c1 = wx * vx + wy * vy;

     //if ( c1 <= 0 )
          //return d(P, S.P0);
     if (c1 <= 0)
       return SquareRoot ((xp - x1) * (xp - x1) + (yp - y1) * (yp - y1));

     //double c2 = dot(v,v);
     int c2 = vx * vx + vy * vy;

     //if ( c2 <= c1 )
          //return d(P, S.P1);
     if (c2 <= c1)
       return SquareRoot ((xp - x2) * (xp - x2) + (yp - y2) * (yp - y2));

     //double b = c1 / c2;
     //Point Pb = S.P0 + b * v;
     int Pbx = x1 + vx * c1 / c2;
     int Pby = y1 + vy * c1 / c2;

     //return d(P, Pb);
     return SquareRoot ((xp - Pbx) * (xp - Pbx) + (yp - Pby) * (yp - Pby));
}
#endif


#if 0
#include <math.h>

// http://geomalgorithms.com/a02-_lines.html
// dist_Point_to_Segment(): get the distance of a point to a segment
//     Input:  a Point P and a Segment S (in any dimension)
//     Return: the shortest distance from P to S
static int dist (int SP0x, int SP0y, int SP1x, int SP1y, int Px, int Py)
{
     //Vector v = S.P1 - S.P0;
     float vx = SP1x - SP0x;
     float vy = SP1y - SP0y;

     //Vector w = P - S.P0;
     float wx = Px - SP0x;
     float wy = Py - SP0y;

     //double c1 = dot(w,v);
     float c1 = wx * vx + wy * vy;

     //if ( c1 <= 0 )
          //return d(P, S.P0);
     if (c1 <= 0)
       return sqrtf ((Px - SP0x) * (Px - SP0x) + (Py - SP0y) * (Py - SP0y));

     //double c2 = dot(v,v);
     float c2 = vx * vx + vy * vy;

     //if ( c2 <= c1 )
          //return d(P, S.P1);
     if (c2 <= c1)
       return sqrtf ((Px - SP1x) * (Px - SP1x) + (Py - SP1y) * (Py - SP1y));

     //double b = c1 / c2;
     float b = c1 / c2;

     //Point Pb = S.P0 + b * v;
     float Pbx = SP0x + b * vx;
     float Pby = SP0y + b * vy;

     //return d(P, Pb);
     return sqrtf ((Px - Pbx) * (Px - Pbx) + (Py - Pby) * (Py - Pby));
}
#endif

#if 0
static int dist (int SP0x, int SP0y, int SP1x, int SP1y, int Px, int Py)
{
     //Vector v = S.P1 - S.P0;
     float vx = SP1x - SP0x;
     float vy = SP1y - SP0y;

     //Vector w = P - S.P0;
     float wx = Px - SP0x;
     float wy = Py - SP0y;

     //double c1 = dot(w,v);
     float c1 = wx * vx + wy * vy;

     //if ( c1 <= 0 )
          //return d(P, S.P0);
     if (c1 <= 0)
       return sqrtf ((Px - SP0x) * (Px - SP0x) + (Py - SP0y) * (Py - SP0y));

     //double c2 = dot(v,v);
     float c2 = vx * vx + vy * vy;

     //if ( c2 <= c1 )
          //return d(P, S.P1);
     if (c2 <= c1)
       return sqrtf ((Px - SP1x) * (Px - SP1x) + (Py - SP1y) * (Py - SP1y));

     //double b = c1 / c2;
     float b = c1 / c2;

     //Point Pb = S.P0 + b * v;
     float Pbx = SP0x + b * vx;
     float Pby = SP0y + b * vy;

     //return d(P, Pb);
     return sqrtf ((Px - Pbx) * (Px - Pbx) + (Py - Pby) * (Py - Pby));
}
#endif

#if 1
#include <math.h>

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
#endif

void draw_line (int x1, int y1, int x2, int y2, unsigned long color)
  {
    if (lightPenZ)
      {
        int d = dist (x1, y1, x2, y2, lightPenX, lightPenY);
        if (d < 5)
          lp_hit ();
      }
    unsigned long  pixel;

    //if (z != 0)
      {
        if ((x1 == x2) && (y1 == y2))
          x2++;
        pixel = color;
        if (pixel != last_pixel)
          {
            XSetForeground (mydisplay, mygc, pixel);
            pixel = last_pixel;
          }
        XDrawLine (mydisplay, mypixmap, mygc,
                     x1 >> smallwindow, y1 >> smallwindow,
                     x2 >> smallwindow, y2 >> smallwindow);
        blank_display = 0;
      }
  }

void open_page (int step)
{
  XSynchronize (mydisplay, step);
  
  if (! blank_display)
    {
      if (mypixmap == mywindow)
          XClearWindow (mydisplay, mywindow);
      else
          {
            XSetForeground (mydisplay, mygc, black);
            XFillRectangle (mydisplay, mypixmap, mygc, 0, 0, window_width, window_height);  
            XSetForeground (mydisplay, mygc, white);
          }
    }
  
  blank_display = 1;
}

void close_page (void)
{
  if (! blank_display)
    {
      if (mywindow != mypixmap)
          {
            XCopyArea (mydisplay, mypixmap, mywindow, mygc,
                         0, 0, /* source upper left X, Y */
                         window_width, window_height, /* width, height */
                         0, 0); /* dest upper left X, Y */
          }

      XFlush (mydisplay);
    }
}

