/* See LICENSE file for copyright and license details. */

#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->winarea_x+(m)->winarea_width) - MAX((x),(m)->winarea_x)) \
							   * MAX(0, MIN((y)+(h),(m)->winarea_y+(m)->winarea_height) - MAX((y),(m)->winarea_y)))
#define TAGISVISIBLE(C)         ((C->tags & C->mon->tagset[C->mon->selected_tags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (font_get_text_width(drw->font, X, strlen(X)) + drw->font->h)

/* enums */
enum { CursorNormal, CursorResize, CursorMove, CursorLast }; /* cursor */
enum { SchemeNorm, SchemeSel, SchemeVisible, SchemeMinimized, SchemeUrgent, SchemeLast }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState,
	   NetWMFullscreen, NetActiveWindow, NetWMWindowType,
	   NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClickTagBar, ClickClientBar, ClickLayoutSymbol, ClickStatusText, ClickWinTitle,
	   ClickClientWin, ClickRootWin, ClickLast }; /* clicks */

/* types & structures */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	Bool wasfloating, isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, minimized, onscreen, marked;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct Pertag Pertag;

#define MAXTABS 50

struct Monitor {
	char layout_symbol[16];
	float marked_width;	/* a percentage of the viewable area */
	int num_marked_win;
	int num;
	int tagbar_pos;         /* tag bar geometry */
	int clientbar_pos;               /* client bar geometry */
	int mon_x, mon_y, mon_width, mon_height;   /* screen size */
	int winarea_x, winarea_y, winarea_width, winarea_height;  /* window area  */
	unsigned int selected_tags;
	unsigned int selected_layout;
	unsigned int tagset[2];
	Bool show_tagbar;
	Bool show_clientbar;
	Bool tags_on_top;
	Client *clients;
	Client *sel;
	Client *top;
	Client *stack;
	Monitor *next;
	Window tagbar_win;
	Window clientbar_win;
	int num_client_tabs;
	int client_tab_widths[MAXTABS];
	const Layout *layout[2];
	Pertag *pertag;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	Bool isfloating;
	int monitor;
} Rule;

typedef struct {
	unsigned long rgb;
} Color;

typedef struct {
	int ascent;
	int descent;
	unsigned int h;
	XFontSet set;
	XFontStruct *xfont;
} FontStruct;

typedef struct {
	Color *fg;
	Color *bg;
	Color *border;
} ColorScheme;

typedef struct {
	unsigned int w, h;
	Display *dpy;
	int screen;
	Window root;
	Drawable drawable;
	GC gc;
	ColorScheme *scheme;
	FontStruct *font;
} Graphics;

typedef struct {
	unsigned int w;
	unsigned int h;
} Extents;

/* function declarations */
void apply_rules (Client *c);
Bool apply_size_hints (Client *c, int *x, int *y, int *w, int *h, Bool interact);
void arrange (Monitor *m);
void arrange_deck (Monitor *m);
void arrange_monocle (Monitor *m);
void arrange_tile (Monitor *m);
void attach (Client *c);
Client *attach_recursive (Client *c, Client *pos);
void cleanup (void);
void clear_urgent (Client *c);
void cmd_adjust_marked_width (const Arg *arg);
void cmd_cycle_focus (const Arg *arg);
void cmd_cycle_focus_monitor (const Arg *arg);
void cmd_cycle_stackarea_selection (const Arg *arg);
void cmd_cycle_view (const Arg *arg);
void cmd_drag_window (const Arg *arg);
void cmd_focus_client (const Arg* arg);
void cmd_hide_window (const Arg *arg);
void cmd_kill_client (const Arg *arg);
void cmd_push_client_left (const Arg *arg);
void cmd_push_client_right (const Arg *arg);
void cmd_quit (const Arg *arg);
void cmd_resize_with_mouse (const Arg *arg);
void cmd_send_to_monitor (const Arg *arg);
void cmd_set_clientbar_mode (const Arg *arg);
void cmd_set_layout (const Arg *arg);
void cmd_set_marked_width (const Arg *arg);
void cmd_shift_tag (const Arg *arg);
void cmd_spawn (const Arg *arg);
void cmd_tag_client (const Arg *arg);
void cmd_toggle_floating (const Arg *arg);
void cmd_toggle_fullscreen (const Arg *arg);
void cmd_toggle_hidden (const Arg *arg);
void cmd_toggle_mark (const Arg *arg);
void cmd_toggle_tag (const Arg *arg);
void cmd_toggle_tagbar (const Arg *arg);
void cmd_toggle_tag_view (const Arg *arg);
void cmd_view_tag (const Arg *arg);
Color *color_create (Graphics *drw, const char *clrname);
void color_free (Color *clr);
void configure (Client *c);
void detach (Client *c);
void die (const char *errstr, ...);
Monitor *direction_to_monitor (int dir);
void draw_tagbar (Monitor *m);
void draw_bars (void);
void draw_clientbar (Monitor *m);
void event_button_press (XEvent *e);
void event_client_message (XEvent *e);
void event_configure_notify (XEvent *e);
void event_configure_request (XEvent *e);
void event_destroy_notify (XEvent *e);
void event_enter_notify (XEvent *e);
void event_expose (XEvent *e);
void event_focus_in (XEvent *e);
void event_key_press (XEvent *e);
void event_mapping_notify (XEvent *e);
void event_map_request (XEvent *e);
void event_motion_notify (XEvent *e);
void event_property_notify (XEvent *e);
void event_unmap_notify (XEvent *e);
void focus (Client *c);
void focus_root (void);
FontStruct *font_create (Display *dpy, const char *fontname);
void font_free (Display *dpy, FontStruct *font);
void font_get_text_extents (FontStruct *font, const char *text, unsigned int len, Extents *extnts);
unsigned int font_get_text_width (FontStruct *font, const char *text, unsigned int len);
Bool get_prop_text (Window w, Atom atom, char *text, unsigned int size);
Bool get_root_pointer_pos (int *x, int *y);
long get_state (Window w);
void grab_buttons (Client *c, Bool focused);
void grab_shortcut_keys (void);
Graphics *gfx_create (Display *dpy, int screen, Window win, unsigned int w, unsigned int h);
void gfx_draw_rect (Graphics *drw, int x, int y, unsigned int w, unsigned int h, int filled, int empty);
void gfx_draw_text (Graphics *drw, int x, int y, unsigned int w, unsigned int h, const char *text);
void gfx_free (Graphics *drw);
void gfx_render_to_window (Graphics *drw, Window win, int x, int y, unsigned int w, unsigned int h);
void gfx_resize (Graphics *drw, unsigned int w, unsigned int h);
void gfx_set_font (Graphics *drw, FontStruct *font);
void gfx_set_colorscheme (Graphics *drw, ColorScheme *scheme);
void init_bars (void);
void manage (Window w, XWindowAttributes *wa);
void monitor_cleanup (Monitor *mon);
Monitor *monitor_create (void);
Client *next_tiled (Client *c);
void pop (Client *c);
Client *prev_tiled (Client *c);
Monitor *rect_to_monitor (int x, int y, int w, int h);
void resize (Client *c, int x, int y, int w, int h, Bool interact);
void resize_client (Client *c, int x, int y, int w, int h);
void restack (Monitor *m);
void scan (void);
Bool send_event (Client *c, Atom proto);
void send_client_to_monitor (Client *c, Monitor *m);
void set_client_state (Client *c, long state);
void set_fullscreen (Client *c, Bool fullscreen);
void setup (void);
void sigchld (int unused);
void stack_attach (Client *c);
void stack_detach (Client *c);
void unfocus (Client *c);
void unmanage (Client *c, Bool destroyed);
void update_client_list (void);
Bool update_geometry (void);
void update_bar_positions (Monitor *m);
void update_numlock_mask (void);
void update_onscreen (Monitor *m);
void update_size_hints (Client *c);
void update_statusarea (void);
void update_title (Client *c);
void update_visibility (Client *c);
void update_window_type (Client *c);
void update_wm_hints (Client *c);
Client *window_to_client (Window w);
Monitor *window_to_monitor (Window w);
int _cmpint (const void *p1, const void *p2);
int _xerror (Display *dpy, XErrorEvent *ee);
int _xerrordummy (Display *dpy, XErrorEvent *ee);
int _xerrorstart (Display *dpy, XErrorEvent *ee);

#include "config.h"

struct Pertag {
	unsigned int curtag, prevtag; /* current and previous tag */
	float marked_widths[LENGTH(tags) + 1]; /* marked_widths per tag */
	unsigned int selected_layouts[LENGTH(tags) + 1]; /* selected layouts */
	const Layout *layoutidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
	Bool show_tagbars[LENGTH(tags) + 1]; /* display bar for the current tag */
};

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* a global variable that maps event types to handlers */
void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = event_button_press,
	[ClientMessage] = event_client_message,
	[ConfigureNotify] = event_configure_notify,
	[ConfigureRequest] = event_configure_request,
	[DestroyNotify] = event_destroy_notify,
	[EnterNotify] = event_enter_notify,
	[Expose] = event_expose,
	[FocusIn] = event_focus_in,
	[KeyPress] = event_key_press,
	[MappingNotify] = event_mapping_notify,
	[MapRequest] = event_map_request,
	[MotionNotify] = event_motion_notify,
	[PropertyNotify] = event_property_notify,
	[UnmapNotify] = event_unmap_notify
};
