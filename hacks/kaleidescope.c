/* kaleidescope, Copyright (c) 1997 Ron Tapia <tapia@nmia.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or 
 * implied warranty. 
 */

/*
 * The above, for lack of a better copyright statement in easy reach
 * was just lifted from the xscreensaver source.
 *
 * One of the odd things about this hack is that the radial motion of the
 * segments depends on roundoff error alone. 
 *
 * I tried to make the source easy to add other shapes. So far, I've
 * only messed with elipses and I couldn't do much with them that looked
 * cool. A nice addition would be to add some sort of spline based shapes.
 * Maybe rectangles would look nice.
 *
 */


#include <stdio.h>
#include <math.h>
#include <time.h>
#include <X11/Xlib.h>
#include "spline.h"
#include "screenhack.h"

#define NEWX(x,y) ((x*g.costheta) + (y*g.sintheta))
#define NEWY(x,y) ((y*g.costheta) - (x*g.sintheta))


typedef struct {
  int  xoff, yoff;                    /* offset of origin xmax/2, ymax/2 */
  int  xmax, ymax;                    /* width, height of window */
  float costheta, sintheta;           
  int symmetry;                       
  int ntrails;          
  int nsegments;
  int narcs;
  int nobjects;
  int local_rotation;
  int global_rotation;
  int spring_constant;
  Colormap cmap;
  GC  draw_gc;
  GC  erase_gc;
  unsigned int default_fg_pixel;
  Display *dpy;
  Window window;
  unsigned long delay;
  unsigned short redmin,redrange,greenmin,greenrange,bluemin,bluerange;
  int color_mode;
} GLOBAL;

typedef struct Obj OBJECT;
struct Obj {
  int type;
  int time;
  void (*propogate) (OBJECT *);
  void (*draw) (OBJECT *);
  void (*init) (OBJECT *);
  void *cur;
};

typedef struct KSEGMENT {
  struct KSEGMENT *next;
  XColor color;
  int drawn;
  short int x1,y1,x2,y2;  /* these are in the natural coordinate system */
  XSegment  *xsegments;  /* these are in the X coordinate system */
} Ksegment;

/* BEGIN global variables */

GLOBAL g;
OBJECT *objects;

char *progclass = "Kaleidescope";
char *defaults [] = {
  ".background:	     black",
  ".foreground:	     white",
  "*color_mode:      nice",
  "*symmetry:	       11",
  "*ntrails:	      100",
  "*nsegments:          7",
  "*local_rotation:   -59",
  "*global_rotation:    1",
  "*spring_constant:    5",
  "*delay:          20000",
  "*redmin:         30000",
  "*redrange:       20000",
  "*greenmin:       30000",
  "*greenrange:     20000",
  "*bluemin:        30000",
  "*bluerange:      20000",
  0
};

XrmOptionDescRec options [] = {
  { "-color_mode",       ".color_mode",     XrmoptionSepArg, 0 },
  { "-symmetry",	".symmetry",	    XrmoptionSepArg, 0 },
  { "-nsegments",       ".nsegments",       XrmoptionSepArg, 0 },
  { "-ntrails",		".ntrails",	    XrmoptionSepArg, 0 },
  { "-local_rotation",  ".local_rotation",  XrmoptionSepArg, 0 },
  { "-global_rotation", ".global_rotation", XrmoptionSepArg, 0 },
  { "-delay",           ".delay",           XrmoptionSepArg, 0 },
  { "-spring_constant", ".spring_constant", XrmoptionSepArg, 0 },
  { "-redmin",          ".redmin",          XrmoptionSepArg, 0 },
  { "-redrange",        ".redmin",          XrmoptionSepArg, 0 },
  { "-bluemin",         ".bluemin",         XrmoptionSepArg, 0 },
  { "-bluerange",       ".bluerange",       XrmoptionSepArg, 0 },
  { "-greenmin",        ".greenmin",        XrmoptionSepArg, 0 },
  { "-greenrange",      ".greenrange",      XrmoptionSepArg, 0 },
  { 0, 0, 0, 0 }
};

/* END global variables */

static void
krandom_color(XColor *color)
{
  int r;
  r = random() % 3;

  if((g.color_mode == 0) || (g.color_mode == 1)) {

    color->blue  = ((r = random()) % g.bluerange) + g.bluemin;
    color->green = ((r = random()) % g.greenrange) + g.greenmin;
    color->red   = ((r = random()) % g.redrange) + g.redmin;

    if(!XAllocColor(g.dpy, g.cmap, color)) {
      color->pixel = g.default_fg_pixel;
    } 
    return;
  } else {
    color->pixel = g.default_fg_pixel;
    return;
  }
}


static void
kcopy_color(XColor *to, XColor *from)
{
  to->red   = from->red;
  to->green = from->green;
  to->blue  = from->blue;
  to->pixel = from->pixel;
}

static void
kcycle_color(XColor *color,
	     unsigned short redstep,
	     unsigned short greenstep,
	     unsigned short bluestep) 
{
  unsigned short red,green,blue;

  if (! g.color_mode) {
    XColor copy;
    color->flags = DoRed|DoGreen|DoBlue;
    color->red   = (red = color->red) - redstep;
    color->green = (green = color->green) - greenstep;
    color->blue  = (blue = color->blue)  - bluestep;
    copy = *color;

    if(!XAllocColor(g.dpy, g.cmap, color)) {
      /* printf("couldn't alloc color...\n"); */
      color->pixel = g.default_fg_pixel;
    }
    copy.pixel = color->pixel;
    *color = copy;

    color->red   = red   - redstep;
    color->green = green- greenstep;
    color->blue  = blue  - bluestep;
    return;
  } 
}


static Ksegment *
create_ksegment (void)
{
  Ksegment *seg, *prev;
  XColor new_color;
  int i;
  unsigned short redstep,bluestep,greenstep;

  krandom_color(&new_color);

  redstep = new_color.red/(2 * g.ntrails);
  greenstep = new_color.green/(2 * g.ntrails);
  bluestep = new_color.blue/(2 * g.ntrails);

  seg            = (Ksegment *) malloc(sizeof(Ksegment));
  seg->xsegments = (XSegment  *) malloc(g.symmetry * sizeof(XSegment));

  prev = seg;
  for(i=0; i< (g.ntrails - 1); i++) {

    kcycle_color(&new_color,redstep,greenstep,bluestep);

    kcopy_color(&(prev->color), &new_color);

    prev->next              = (Ksegment*)malloc(sizeof(Ksegment));
    (prev->next)->xsegments = (XSegment*)malloc(g.symmetry * sizeof(XSegment));
    prev->drawn             = 0;
    prev = (prev->next);
  } 

  prev->drawn = 0;
  prev->next = seg;
  kcopy_color(&(prev->color), &new_color);

  return seg;
}

static void
init_ksegment (OBJECT *obj)
{

  /* Give the segment some random values */
  ((Ksegment *)obj->cur)->x1 = random() % g.xoff;
  ((Ksegment *)obj->cur)->y1 = random() % g.yoff;
  ((Ksegment *)obj->cur)->x2 = random() % g.xoff;
  ((Ksegment *)obj->cur)->y2 = random() % g.yoff;
}


static void
draw_ksegment (OBJECT *obj)
{
  register short x1, y1, x2, y2;
  int dx, dy;
  int i;
  static int counter=0;

  counter++;

  x1 = ((Ksegment *)obj->cur)->x1;   /* in the natural coordinate system */
  y1 = ((Ksegment *)obj->cur)->y1;
  x2 = ((Ksegment *)obj->cur)->x2;
  y2 = ((Ksegment *)obj->cur)->y2;

  dx = x2 - x1;
  dy = y2 - y1;

  /* maybe throw away values and start over */
  if( ((dx*dx) + (dy * dy)) < 100) {
    init_ksegment (obj);
    x1 = ((Ksegment *)obj->cur)->x1;   /* in the natural coordinate system */
    y1 = ((Ksegment *)obj->cur)->y1;
    x2 = ((Ksegment *)obj->cur)->x2;
    y2 = ((Ksegment *)obj->cur)->y2;
  }

  for (i=0; i<g.symmetry; i++) {
    (((Ksegment *)obj->cur)->xsegments)[i].x1 = NEWX(x1,y1);
    (((Ksegment *)obj->cur)->xsegments)[i].y1 = NEWY(x1,y1);
    (((Ksegment *)obj->cur)->xsegments)[i].x2 = NEWX(x2,y2);
    (((Ksegment *)obj->cur)->xsegments)[i].y2 = NEWY(x2,y2);

    (((Ksegment *)obj->cur)->xsegments)[i].x1 = (x1 = (((Ksegment *)obj->cur)->xsegments)[i].x1) + g.xoff; 
    (((Ksegment *)obj->cur)->xsegments)[i].y1 = (y1 = (((Ksegment *)obj->cur)->xsegments)[i].y1) + g.yoff;
    (((Ksegment *)obj->cur)->xsegments)[i].x2 = (x2 = (((Ksegment *)obj->cur)->xsegments)[i].x2) + g.xoff;
    (((Ksegment *)obj->cur)->xsegments)[i].y2 = (y2 = (((Ksegment *)obj->cur)->xsegments)[i].y2) + g.yoff;
  }

  XSetForeground(g.dpy, g.draw_gc, (((Ksegment *)obj->cur)->color).pixel);

  XDrawSegments(g.dpy, g.window, g.draw_gc, ((Ksegment *)obj->cur)->xsegments, g.symmetry);
  ((Ksegment *)obj->cur)->drawn = 1;

  if (((((Ksegment *)obj->cur)->next)->drawn) != 0) {
    XDrawSegments(g.dpy, g.window, g.erase_gc, ((Ksegment *)obj->cur)->next->xsegments, g.symmetry);
  }
}

static void
propogate_ksegment(OBJECT *obj)
{
  int t;
  short int x1,y1,x2,y2;
  short int midx,midy,nmidx,nmidy;
  float lsin, lcos, gsin, gcos;

  lsin = sin((2*M_PI/10000)*g.local_rotation);
  lcos = cos((2*M_PI/10000)*g.local_rotation);
  gsin = sin((2*M_PI/10000)*g.global_rotation);
  gcos = cos((2*M_PI/10000)*g.global_rotation);

  t=obj->time;
  obj->time = t + 1;

  x1 = ((Ksegment *) obj->cur)->x1;
  y1 = ((Ksegment *) obj->cur)->y1;
  x2 = ((Ksegment *) obj->cur)->x2;
  y2 = ((Ksegment *) obj->cur)->y2;

  midx = (x1 + x2)/2;
  midy = (y1 + y2)/2;

  nmidx = midx*gcos + midy*gsin;
  nmidy = midy*gcos - midx*gsin;

  x1 = x1 - midx;
  x2 = x2 - midx;
  y1 = y1 - midy;
  y2 = y2 - midy;


  /* This is where we move to the next ksegment... */
  obj->cur = ((Ksegment *)obj->cur)->next;

  ((Ksegment *)obj->cur)->x1 = ((x1*lcos) + (y1*lsin)) + nmidx;
  ((Ksegment *)obj->cur)->y1 = ((y1*lcos) - (x1*lsin)) + nmidy;
  ((Ksegment *)obj->cur)->x2 = ((x2*lcos) + (y2*lsin)) + nmidx;
  ((Ksegment *)obj->cur)->y2 = ((y2*lcos) - (x2*lsin)) + nmidy;

  return ;
}

static void
init_objects (void)
{
  int i;
  for (i=0; i<g.nobjects; i++) {
    (objects[i].init)(objects + i);
  }
}

static void
create_objects (void)
{
  int i;

  objects = (OBJECT *) malloc(g.nobjects * sizeof(OBJECT));

  for (i=0; i< g.nsegments; i++) {
    objects[i].cur = create_ksegment();
    objects[i].type = 1;
    objects[i].time = 0;
    objects[i].propogate = propogate_ksegment;
    objects[i].draw      = draw_ksegment;
    objects[i].init      = init_ksegment;
  }

  /* Here we can add creation functions for other object types. */
}


static void
propogate_objects (void)
{
  int i;

  for(i=0; i<g.nobjects; i++) {
    objects[i].propogate(objects + i);
  }
}

static void
draw_objects (void)
{
  int i;

  for(i=0; i<g.nobjects; i++) {
    objects[i].draw(objects + i);
  }
}

static void
init_g (Display *dpy, Window window)
{
  XWindowAttributes xgwa;
  XGCValues gcv;
  char *color_mode_str;

  g.dpy    = dpy;
  g.window = window;

  g.symmetry        = get_integer_resource("symmetry",         "Integer");
  g.ntrails         = get_integer_resource("ntrails"  ,        "Integer");
  g.nsegments       = get_integer_resource("nsegments",        "Integer");
  g.narcs           = get_integer_resource("narcs",            "Integer");
  g.local_rotation  = get_integer_resource("local_rotation",   "Integer");
  g.global_rotation = get_integer_resource("global_rotation",  "Integer");
  g.spring_constant = get_integer_resource("sprint_constatnt", "Integer");
  g.delay           = get_integer_resource("delay", "Integer");
  g.nobjects        = g.nsegments + g.narcs;

  color_mode_str = get_string_resource("color_mode", "color_mode");

  /* make into an enum... */
  if(!color_mode_str) {
    g.color_mode = 0;
  } else if (!strcmp(color_mode_str, "greedy")) {
    g.color_mode = 0;
  } else if (!strcmp(color_mode_str, "nice")) {
    g.color_mode = 1;
  } else {
    g.color_mode = 2;
  }

  XGetWindowAttributes (dpy, (Drawable) window, &xgwa);
  g.xmax     = xgwa.width;
  g.ymax     = xgwa.height;  
  g.xoff     = g.xmax/2;
  g.yoff     = g.ymax/2;
  g.costheta = cos(2*M_PI/g.symmetry);
  g.sintheta  = sin(2*M_PI/g.symmetry);
  g.cmap     = xgwa.colormap;

  g.redmin     = get_integer_resource("redmin",     "Integer");
  g.redrange   = get_integer_resource("redrange",   "Integer");
  g.greenmin   = get_integer_resource("greenmin",   "Integer");
  g.greenrange = get_integer_resource("greenrange", "Integer");
  g.bluemin    = get_integer_resource("bluemin",    "Integer");
  g.bluerange  = get_integer_resource("bluerange",  "Integer");

  gcv.line_width = 1;
  gcv.cap_style  = CapRound;
  gcv.foreground = g.default_fg_pixel = get_pixel_resource ("foreground", "Foreground", dpy, g.cmap);
  g.draw_gc      = XCreateGC (dpy, (Drawable) window, GCForeground|GCLineWidth|GCCapStyle, &gcv);

  gcv.foreground = get_pixel_resource ("background", "Background", g.dpy, g.cmap);
  g.erase_gc     = XCreateGC (dpy, (Drawable) window, GCForeground|GCLineWidth|GCCapStyle,&gcv);
}

void
screenhack (Display *dpy, Window window)
{
  init_g (dpy, window);
  create_objects();
  init_objects ();

  while (1)
    {
     draw_objects ();
     XSync (dpy, False);
     if(g.delay) {
       screenhack_handle_events (dpy);
       usleep(g.delay);
     }
     propogate_objects(); 
   }
}
