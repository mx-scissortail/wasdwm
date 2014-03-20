/* See LICENSE file for copyright and license details.
 *
 * wasdwm is designed like any other X client. It is driven through handling X
 * events. In contrast to other X clients, a window manager selects for
 * SubstructureRedirectMask on the root window, to receive events about window
 * changes.  Only one X connection at a time is allowed to select for this
 * event mask.
 *
 * The event handlers of wasdwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.  Event handler functions are prefixed with "event_".
 *
 * Each child of the root window is a client, except windows which have set the
 * override_redirect flag.  Clients are organized in a linked client list on
 * each monitor, the focus history is remembered through a stack list on each
 * monitor. Each client contains a bit array to indicate the tags of a client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 * 
 * Commands that can be issued by client input (e.g. shortcut keys, clicking on
 * the bars) are prefixed with "cmd_".
 *
 * To understand everything else, start reading main().
 */

#include "wasdwm.h"

/* global variables */
const char broken[] = "broken";
char stext[256];
int screen;
int sw, sh;           /* X display screen geometry width, height */
int bh, blw = 0;      /* tag bar geometry */
int th = 0;           /* client bar geometry */
int (*xerrorxlib)(Display *, XErrorEvent *);
unsigned int numlockmask = 0;

Atom wmatom[WMLast], netatom[NetLast];
Bool running = True;
Cursor cursor[CursorLast];
ColorScheme scheme[SchemeLast];
Display *dpy;
Graphics *drw;
FontStruct *fnt;
Monitor *mons, *selmon;
Window root;

/* function implementations */

/**
 * Determines whether any custom rules apply to a newly managed client and applies them.
 * 
 * @param	c	The target client.
 */
void
apply_rules (Client *c) {
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = {NULL, NULL};

	/* rule matching */
	c->isfloating = c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
				&& (!r->class || strstr(class, r->class))
				&& (!r->instance || strstr(instance, r->instance)))	{
					
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m) {
				c->mon = m;
			}
		}
	}
	if (ch.res_class) {
		XFree(ch.res_class);
	}
	if (ch.res_name) {
		XFree(ch.res_name);
	}
		
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->selected_tags];
}

/**
 * Applies window size hints.
 * 
 * TODO: improve description, document parameters
 */
Bool
apply_size_hints (Client *c, int *x, int *y, int *w, int *h, Bool interact) {
	Bool baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw) {
			*x = sw - WIDTH(c);
		}
		if (*y > sh) {
			*y = sh - HEIGHT(c);
		}
		if (*x + *w + 2 * c->bw < 0) {
			*x = 0;
		}
		if (*y + *h + 2 * c->bw < 0) {
			*y = 0;
		}
	}
	else {
		if (*x >= m->winarea_x + m->winarea_width) {
			*x = m->winarea_x + m->winarea_width - WIDTH(c);
		}
		if (*y >= m->winarea_y + m->winarea_height) {
			*y = m->winarea_y + m->winarea_height - HEIGHT(c);
		}
		if (*x + *w + 2 * c->bw <= m->winarea_x) {
			*x = m->winarea_x;
		}
		if (*y + *h + 2 * c->bw <= m->winarea_y) {
			*y = m->winarea_y;
		}
	}
	if (*h < bh) {
		*h = bh;
	}
	if (*w < bh) {
		*w = bh;
	}
	if (resizehints || c->isfloating || !c->mon->layout[c->mon->selected_layout]->arrange) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw) {
			*w -= *w % c->incw;
		}
		if (c->inch) {
			*h -= *h % c->inch;
		}
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw) {
			*w = MIN(*w, c->maxw);
		}
		if (c->maxh) {
			*h = MIN(*h, c->maxh);
		}
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

/**
 * Arranges clients on screen using the current layout.
 * 
 * @param	m	The target monitor.  Passing NULL arranges all monitors.
 */
void
arrange (Monitor *m) {
	if (!m) {
		for (m = mons; m; m = m->next) {
			arrange(m);
		}
	} else {
		update_onscreen(m);
		update_visibility(m->stack);
		update_bar_positions(m);
		
		strncpy(m->layout_symbol, m->layout[m->selected_layout]->symbol, sizeof m->layout_symbol);
		if (m->layout[m->selected_layout]->arrange) {
			m->layout[m->selected_layout]->arrange(m);
		}
	}
}

/**
 * Arranges a monitor in the deck layout.
 * 
 * @param	m	The target monitor.
 */
void
arrange_deck (Monitor *m) {
	int dn;
	unsigned int i, n, h, mw, my;
	Client *c;

	for (n = 0, c = next_tiled(m->clients); c; c = next_tiled(c->next), n++);
	if (n == 0) return;
	
	dn = n - m->num_marked_win;
	if (dn > 0) {/* override layout symbol */
		snprintf(m->layout_symbol, sizeof m->layout_symbol, "D %d", dn);
	}
	if (n > m->num_marked_win) {
		mw = m->num_marked_win ? m->winarea_width * m->marked_width : 0;
	} else {
		mw = m->winarea_width;
	}
	for (i = my = 0, c = next_tiled(m->clients); c; c = next_tiled(c->next), i++) {
		if (i < m->num_marked_win) {
			h = (m->winarea_height - my) / (MIN(n, m->num_marked_win) - i);
			resize(c, m->winarea_x, m->winarea_y + my, mw - (2*c->bw), h - (2*c->bw), False);
			my += HEIGHT(c);
		} else {
			resize(c, m->winarea_x + mw, m->winarea_y, m->winarea_width - mw - (2*c->bw), m->winarea_height - (2*c->bw), False);
		}
	}
}

/**
 * Arranges a monitor in the monocle layout.
 * 
 * @param	m	The target monitor.
 */
void
arrange_monocle (Monitor *m) {
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next) {
		if (TAGISVISIBLE(c)) {
			n++;
		}
	}
	if (n > 0) {/* override layout symbol */
		snprintf(m->layout_symbol, sizeof m->layout_symbol, "[%d]", n);
	}
	for (c = next_tiled(m->clients); c; c = next_tiled(c->next)) {
		resize(c, m->winarea_x, m->winarea_y, m->winarea_width - 2 * c->bw, m->winarea_height - 2 * c->bw, False);
	}
}

/**
 * Arranges a monitor in the tile layout.
 * 
 * @param	m	The target monitor.
 */
void
arrange_tile (Monitor *m) {
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = next_tiled(m->clients); c; c = next_tiled(c->next), n++);
	if (n == 0) {
		return;
	}
	if (n > m->num_marked_win) {
		mw = m->num_marked_win ? m->winarea_width * m->marked_width : 0;
	} else {
		mw = m->winarea_width;
	}
	for (i = my = ty = 0, c = next_tiled(m->clients); c; c = next_tiled(c->next), i++) {
		if (i < m->num_marked_win) {
			h = (m->winarea_height - my) / (MIN(n, m->num_marked_win) - i);
			resize(c, m->winarea_x, m->winarea_y + my, mw - (2*c->bw), h - (2*c->bw), False);
			my += HEIGHT(c);
		} else {
			h = (m->winarea_height - ty) / (n - i);
			resize(c, m->winarea_x + mw, m->winarea_y + ty, m->winarea_width - mw - (2*c->bw), h - (2*c->bw), False);
			ty += HEIGHT(c);
		}
	}
}
		
/**
 * Attaches a client to its monitor's list of clients.
 * Windows that aren't marked or floating are placed after marked windows which are placed after floating windows.
 * 
 * @param	c	The target client.
 */
void
attach (Client *c) {
	if (c->isfloating) {
		c->next = c->mon->clients;
		c->mon->clients = c;
	} else {
		c->mon->clients = attach_recursive(c, c->mon->clients);
	}
}

/**
 * A helper function for attaching clients.
 * 
 * @param	c	The target client.
 * @param	pos	The current position in the list of clients.
 */
Client *
attach_recursive (Client *c, Client *pos) {
	if (!pos) {
		return c;
	} else if (pos->isfloating) {
		pos->next = attach_recursive(c, pos->next);
		return pos;
	} else if (c->marked) {
		c->next = pos;
		return c;
	} else if (pos->marked) {
		pos->next = attach_recursive(c, pos->next);
		return pos;
	} else {
		c->next = pos;
		return c;
	}
}

/**
 * Releases resources upon shutdown.
 */
void
cleanup (void) {
	Arg a = { .ui = ~0 };
	Layout foo = { "", NULL };
	Monitor *m;

	cmd_view_tag(&a);
	selmon->layout[selmon->selected_layout] = &foo;
	for (m = mons; m; m = m->next) {
		while (m->stack) {
			unmanage(m->stack, False);
		}
	}
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons) {
		monitor_cleanup(mons);
	}
	XFreeCursor(drw->dpy, cursor[CursorNormal]);
	XFreeCursor(drw->dpy, cursor[CursorResize]);
	XFreeCursor(drw->dpy, cursor[CursorMove]);
	font_free(dpy, fnt);
	color_free(scheme[SchemeNorm].border);
	color_free(scheme[SchemeNorm].bg);
	color_free(scheme[SchemeNorm].fg);
	color_free(scheme[SchemeSel].border);
	color_free(scheme[SchemeSel].bg);
	color_free(scheme[SchemeSel].fg);
	color_free(scheme[SchemeVisible].border);
	color_free(scheme[SchemeVisible].bg);
	color_free(scheme[SchemeVisible].fg);
	color_free(scheme[SchemeMinimized].border);
	color_free(scheme[SchemeMinimized].bg);
	color_free(scheme[SchemeMinimized].fg);
	color_free(scheme[SchemeUrgent].border);
	color_free(scheme[SchemeUrgent].bg);
	color_free(scheme[SchemeUrgent].fg);
	gfx_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

/**
 * Clears the urgent flag on the window associated with a given client.
 * 
 * @param	c	The target client.
 */
void
clear_urgent (Client *c) {
	XWMHints *wmh;

	if (!(wmh = XGetWMHints(dpy, c->win))) return;
	
	c->isurgent = False;
	wmh->flags &= ~XUrgencyHint;
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

/**
 * Command: Adjusts the width of the marked clients area by a given amount.
 * 
 * @param	arg	arg->f determines the amount to adjust by.
 */
void
cmd_adjust_marked_width (const Arg *arg) {
	float f;
	
	if (!arg || !selmon->layout[selmon->selected_layout]->arrange) return;
	f = arg->f + selmon->marked_width;
	if (f < 0.1 || f > 0.9) return;
	selmon->marked_width = selmon->pertag->marked_widths[selmon->pertag->curtag] = f;
	arrange(selmon);
}

/**
 * Command: Cycles the focus to the next (or previous) tiled client, raising if necessary.
 * 
 * @param	arg	Cycles forward if arg->i > 0, else backwards.
 */
void
cmd_cycle_focus (const Arg *arg) {
	Client *c = NULL, *i;

	if (!selmon->sel) { return;	}
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && (!TAGISVISIBLE(c) || c->minimized); c = c->next);
		if (!c) {
			for (c = selmon->clients; c && (!TAGISVISIBLE(c) || c->minimized); c = c->next);
		}
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next) {
			if (TAGISVISIBLE(i) && i && !i->minimized) {
				c = i;
			}
		}
		if (!c) {
			for (; i; i = i->next) {
				if (TAGISVISIBLE(i)&& i && !i->minimized) {
					c = i;
				}
			}
		}
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
}

/**
 * Command: Cycles the focus to the next (or previous) monitor.
 * 
 * @param	arg	Cycles forward if arg->i > 0, else backwards.
 */
void
cmd_cycle_focus_monitor (const Arg *arg) {
	Monitor *m;

	if (!mons->next || (m = direction_to_monitor(arg->i)) == selmon) return;
	
	unfocus(selmon->sel);
	selmon = m;
	focus(NULL);
}


/**
 * Command: Cycles client selection within the stack area.
 * 
 * @param	arg	Cycles forward if arg->i > 0, else backwards.
 */ 
void
cmd_cycle_stackarea_selection (const Arg *arg) {
	Client *c = NULL, *i, *cur;
	
	if (selmon->layout[selmon->selected_layout]->arrange != arrange_deck) {
		cmd_cycle_focus(arg);
		return;
	}
	
	for (cur=selmon->clients; cur && !(cur->onscreen && !cur->marked); cur = cur->next);
	if (!cur) return;
	
	if (arg->i > 0) {
		for (c = cur->next; c && (!TAGISVISIBLE(c) || c->onscreen || c->minimized); c = c->next);
		if (!c) {
			for (c = selmon->clients; c && (!TAGISVISIBLE(c) || c->onscreen || c->minimized); c = c->next);
		}
	}
	else {
		for (i = selmon->clients; i != cur; i = i->next) {
			if (TAGISVISIBLE(i) && i && !i->minimized && !i->onscreen) {
				c = i;
			}
		}
		if (!c) {
			for (; i; i = i->next) {
				if (TAGISVISIBLE(i) && i && !i->minimized && !i->onscreen) {
					c = i;
				}
			}
		}
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
}

/** 
 * Command: Shifts the current tag view to the left/right.
 *
 * @param	arg	"arg->i" stores the number of tags to shift right (positive value) or left (negative value)
 */
void
cmd_cycle_view (const Arg *arg) {
	unsigned int occ = 0;
	int i, curtags;
	int seltag = 0;
	Client* c = NULL;
	Arg a;

	for (c = selmon->clients; c; c = c->next) {
		occ |= c->tags;
	}
		
	if (occ == 0) return;
	
	curtags = selmon->tagset[selmon->selected_tags];
	for (i = 0; i < LENGTH(tags); i++) {
		if (curtags & (1 << i)){
			seltag = i;
			break;
		}
	}
	
	do {
		seltag = (seltag + arg->i) % (int)LENGTH(tags);
		if (seltag < 0) {
			seltag += LENGTH(tags);
		}
	} while (!((1 << seltag) & occ));

	a.i = (1 << seltag);
	cmd_view_tag(&a);
}

/**
 * Command: Activate mouse based window placement.
 * 
 * @param	arg	Unused.
 */
void
cmd_drag_window (const Arg *arg) {
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;

	/* moving fullscreen windows by mouse isn't supported */
	if (!(c = selmon->sel) || c->isfullscreen) return;
		
	restack(selmon);
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor[CursorMove], CurrentTime) != GrabSuccess) return;
						
	if (!get_root_pointer_pos(&x, &y)) return;
	
	ocx = c->x;
	ocy = c->y;

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
			case ConfigureRequest:
			case Expose:
			case MapRequest:
				handler[ev.type](&ev);
				break;
			case MotionNotify:
				nx = ocx + (ev.xmotion.x - x);
				ny = ocy + (ev.xmotion.y - y);
				if (nx >= selmon->winarea_x && nx <= selmon->winarea_x + selmon->winarea_width
				&& ny >= selmon->winarea_y && ny <= selmon->winarea_y + selmon->winarea_height) {
					if (abs(selmon->winarea_x - nx) < snap) {
						nx = selmon->winarea_x;
					} else if (abs((selmon->winarea_x + selmon->winarea_width) - (nx + WIDTH(c))) < snap) {
						nx = selmon->winarea_x + selmon->winarea_width - WIDTH(c);
					}
					if (abs(selmon->winarea_y - ny) < snap) {
						ny = selmon->winarea_y;
					} else if (abs((selmon->winarea_y + selmon->winarea_height) - (ny + HEIGHT(c))) < snap) {
						ny = selmon->winarea_y + selmon->winarea_height - HEIGHT(c);
					}
					if (!c->isfloating && selmon->layout[selmon->selected_layout]->arrange
							&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap)) {
						cmd_toggle_floating(NULL);
					}
				}
				if (!selmon->layout[selmon->selected_layout]->arrange || c->isfloating) {
					resize(c, nx, ny, c->w, c->h, True);
				}
				break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = rect_to_monitor(c->x, c->y, c->w, c->h)) != selmon) {
		send_client_to_monitor(c, m);
		selmon = m;
		focus(NULL);
	}
}

/**
 * Command: Gives focus to a particular client.
 * 
 * @param	arg	The target client is determined by arg->i.
 */
void
cmd_focus_client (const Arg* arg){
  int iwin = arg->i;
  Client* c = NULL;
  
  for (c = selmon->clients; c && (iwin || !TAGISVISIBLE(c)) ; c = c->next) {
	if (TAGISVISIBLE(c)) {
		--iwin;
	}
  }
  if (c) {
	if (c->minimized) {
		c->minimized = False;
		arrange(selmon);
	}
	focus(c);
	restack(selmon);
  }
}

/**
 * Command: Minimizes the currently selected window.
 * 
 * @param	arg	Unused.
 */
void
cmd_hide_window (const Arg *arg) {
	Client* c = selmon->sel;
	if (c) {
		c->minimized = True;
		selmon->sel = 0;
		unfocus(c);
		focus_root();
		arrange(selmon);
	}
}

/**
 * Command: Sends a kill signal to the currently selected client.
 * 
 * @param	arg	Unused.
 */
void
cmd_kill_client (const Arg *arg) {
	if (!selmon->sel) return;
	
	if (!send_event(selmon->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(_xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(_xerror);
		XUngrabServer(dpy);
	}
}

/**
 * Command: Cycles the selected client up (leftward) in its monitor's client list.
 * 
 * @param	arg	Unused.
 */
void
cmd_push_client_left (const Arg *arg) {
	Client *sel = selmon->sel;
	Client *c;

	if (!sel || sel->isfloating) return;
	
	if ((c = prev_tiled(sel))) {
		/* attach before c */
		detach(sel);
		sel->next = c;
		if (selmon->clients == c) {
			selmon->clients = sel;
		} else {
			for (c = selmon->clients; c->next != sel->next; c = c->next);
			c->next = sel;
		}
	} else {
		/* move to the end */
		for (c = sel; c->next; c = c->next);
		detach(sel);
		sel->next = NULL;
		c->next = sel;
	}
	focus(sel);
	arrange(selmon);
}

/**
 * Command: Cycles the selected client down (rightward) in its monitor's client list.
 * 
 * @param	arg	Unused.
 */
void
cmd_push_client_right (const Arg *arg) {
	Client *sel = selmon->sel;
	Client *c;

	if (!sel || sel->isfloating) return;
	
	if ((c = next_tiled(sel->next))) {
		/* attach after c */
		detach(sel);
		sel->next = c->next;
		c->next = sel;
	} else {
		/* move to the front */
		detach(sel);
		attach(sel);
	}
	focus(sel);
	arrange(selmon);
}

/**
 * Command: Shuts down the WM.
 * 
 * @param	arg	Unused.
 */
void
cmd_quit (const Arg *arg) {
	running = False;
}

/**
 * Command: Activates mouse-based window resizing.
 * 
 * @param	arg	Unused.
 */
void
cmd_resize_with_mouse (const Arg *arg) {
	int ocx, ocy;
	int nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;

	/* resizing fullscreen windows by mouse isn't supported */
	if (!(c = selmon->sel) || c->isfullscreen) return;
	
	restack(selmon);
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
			None, cursor[CursorResize], CurrentTime) != GrabSuccess) return;
			
	ocx = c->x;
	ocy = c->y;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->winarea_x + nw >= selmon->winarea_x && c->mon->winarea_x + nw <= selmon->winarea_x + selmon->winarea_width
			&& c->mon->winarea_y + nh >= selmon->winarea_y && c->mon->winarea_y + nh <= selmon->winarea_y + selmon->winarea_height)
			{
				if (!c->isfloating && selmon->layout[selmon->selected_layout]->arrange
						&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap)) {
					cmd_toggle_floating(NULL);
				}
			}
			if (!selmon->layout[selmon->selected_layout]->arrange || c->isfloating) {
				resize(c, c->x, c->y, nw, nh, True);
			}
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = rect_to_monitor(c->x, c->y, c->w, c->h)) != selmon) {
		send_client_to_monitor(c, m);
		selmon = m;
		focus(NULL);
	}
}

/**
 * Command: Sends the currently selected client to the next/previous monitor.
 * 
 * @param	arg	The sign of arg->i determines which direction to send the client.
 */
void
cmd_send_to_monitor (const Arg *arg) {
	if (!selmon->sel || !mons->next) return;
	send_client_to_monitor(selmon->sel, direction_to_monitor(arg->i));
}

/**
 * Command: Sets the client bar display mode.
 * 
 * TODO: document parameters
 */
void
cmd_set_clientbar_mode (const Arg *arg) {
	if (arg && arg->i >= 0) {
		selmon->show_clientbar = arg->ui % show_clientbar_nmodes;
	} else {
		selmon->show_clientbar = (selmon->show_clientbar + 1 ) % show_clientbar_nmodes;
	}
	arrange(selmon);
}

/**
 * Command: Sets the current layout.
 * 
 * @param	arg	arg->v determines the new layout to use.
 */
void
cmd_set_layout (const Arg *arg) {
	if (!arg || !arg->v || arg->v != selmon->layout[selmon->selected_layout]) {
		selmon->pertag->selected_layouts[selmon->pertag->curtag] ^= 1;
		selmon->selected_layout = selmon->pertag->selected_layouts[selmon->pertag->curtag];
	}
	if (arg && arg->v) {
		selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout] = (Layout *)arg->v;
	}
	selmon->layout[selmon->selected_layout] = selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout];
	strncpy(selmon->layout_symbol, selmon->layout[selmon->selected_layout]->symbol, sizeof selmon->layout_symbol);
	
	arrange(selmon);	/* which of these is necessary? */
	draw_tagbar(selmon);
	arrange(selmon);	/* the second call to arrage fixes a mysterious stack issue */
}

/**
 * Command: Sets the width of the marked clients area by a given amount.
 * 
 * @param	arg	arg->f determines the new width of the marked clients area.
 */
void
cmd_set_marked_width (const Arg *arg) {
	if (!arg || !selmon->layout[selmon->selected_layout]->arrange
			|| arg->f < 0.1 || arg->f > 0.9 ) return;
	
	selmon->marked_width = selmon->pertag->marked_widths[selmon->pertag->curtag] = arg->f;
	arrange(selmon);
}

/**
 * Command: Shifts the tag on a client left or right.
 * 
 * @param	arg	arg->i determines the direction to shift.
 */
void
cmd_shift_tag (const Arg *arg) {
	unsigned int occ = 0;
	int i, curtags;
	int seltag = 0;
	Client* c = NULL;
	Arg a;

	for (c = selmon->clients; c; c = c->next) {
		occ |= c->tags;
	}
		
	if (occ == 0) return;
	
	curtags = selmon->tagset[selmon->selected_tags];
	for (i = 0; i < LENGTH(tags); i++) {
		if (curtags & (1 << i)){
			seltag = i;
			break;
		}
	}
	
	seltag = (seltag + arg->i) % (int)LENGTH(tags);
	if (seltag < 0) {
		seltag += LENGTH(tags);
	}

	a.ui = (1 << seltag);
	cmd_tag_client(&a);
}

/**
 * Command: Spawns a child process.
 * 
 * @param	arg	The process to be spawned is determined by arg->v (see config.h).
 */
void
cmd_spawn (const Arg *arg) {
	if (fork() == 0) {
		if (dpy) {
			close(ConnectionNumber(dpy));
		}
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "wasdwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

/**
 * Command: Applies a tag(set) to the selected client.
 * 
 * @param	arg	arg->ui contains the tagset to apply.
 */
void
cmd_tag_client (const Arg *arg) {
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
}

/**
 * Command: Toggles whether or not the current client window is treated as floating.
 * 
 * @param	arg	Unused.
 */
void
cmd_toggle_floating (const Arg *arg) {
	if (!selmon->sel || selmon->sel->isfullscreen) return; /* no support for fullscreen windows */
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating) {
		selmon->sel->bw = floatborderpx;
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			   selmon->sel->w, selmon->sel->h, False);
	} else {
		selmon->sel->bw = borderpx;
	}
	arrange(selmon);
}

/**
 * Command: Toggles fullscreen mode for the current client window.
 * 
 * @param	arg	Unused.
 */
void
cmd_toggle_fullscreen (const Arg *arg) {
	if (selmon->sel) {
		set_fullscreen(selmon->sel, !selmon->sel->isfullscreen);
	}
}

/**
 * Command: Toggles whether or not a given client is minimized.
 * 
 * @param	arg	arg->i determines the target client.
 */
void
cmd_toggle_hidden (const Arg *arg) {
  int iwin = arg->i;
  Client* c = NULL;
  for (c = selmon->clients; c && (iwin || !TAGISVISIBLE(c)) ; c = c->next) {
	if (TAGISVISIBLE(c)) {
		--iwin;
	}
  }
  if (c) {
	  if (c->minimized) {
		  cmd_focus_client(arg);	/* automatically unhides */
	  } else {
		  c->minimized = True;
		  if (c->mon->sel && c == c->mon->sel) {
			c->mon->sel = 0;
			unfocus(c);
			focus_root();
		  }
		  arrange(selmon);
	  }
  }
}

/**
 * Command: Toggles whether or not the currently selected client is marked.
 * 
 * @param	arg	Unused.
 */
void
cmd_toggle_mark (const Arg *arg) {	
	if (!selmon->layout[selmon->selected_layout]->arrange
			|| !selmon->sel || selmon->sel->isfloating) return;
	
	selmon->sel->marked = !selmon->sel->marked;	
	pop(selmon->sel);	/* this will automatically cause a re-arrange */
}

/**
 * Command: Toggle a tag(set) on the currently selected client.
 * 
 * @param	arg	arg->ui contains the tagset to toggle.
 */
void
cmd_toggle_tag (const Arg *arg) {
	unsigned int newtags;

	if (!selmon->sel) return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

/**
 * Command: Toggle the visibility of the tag bar.
 * 
 * @param	arg	Unused.
 */
void
cmd_toggle_tagbar (const Arg *arg) {
	selmon->show_tagbar = selmon->pertag->show_tagbars[selmon->pertag->curtag] = !selmon->show_tagbar;
	update_bar_positions(selmon);
	XMoveResizeWindow(dpy, selmon->tagbar_win, selmon->winarea_x, selmon->tagbar_pos, selmon->winarea_width, bh);
	arrange(selmon);
}

/**
 * Command: Toggles whether a particular tag(set) is visible - does not interfere with the visibility of other tags.
 * 
 * @param	arg	arg->ui contains the target tagset.
 */
void
cmd_toggle_tag_view (const Arg *arg) {
	unsigned int newtagset = selmon->tagset[selmon->selected_tags] ^ (arg->ui & TAGMASK);
	int i;

	if (newtagset) {
		if (newtagset == ~0) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			selmon->pertag->curtag = 0;
		}
		/* test if the user did not select the same tag */
		if (!(newtagset & 1 << (selmon->pertag->curtag - 1))) {
			selmon->pertag->prevtag = selmon->pertag->curtag;
			for (i=0; !(newtagset & 1 << i); i++);
			selmon->pertag->curtag = i + 1;
		}
		selmon->tagset[selmon->selected_tags] = newtagset;

		/* apply settings for this view */
		selmon->marked_width = selmon->pertag->marked_widths[selmon->pertag->curtag];
		selmon->selected_layout = selmon->pertag->selected_layouts[selmon->pertag->curtag];
		selmon->layout[selmon->selected_layout] = selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout];
		selmon->layout[selmon->selected_layout^1] = selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout^1];
		if (selmon->show_tagbar != selmon->pertag->show_tagbars[selmon->pertag->curtag]) {
			cmd_toggle_tagbar(NULL);
		}
		focus(NULL);
		arrange(selmon);
	}
}

/**
 * Command: Views a particular tag(set).
 * 
 * @param	arg	arg->ui holds the tagset to view.
 */
void
cmd_view_tag (const Arg *arg) {
	int i;
	unsigned int tmptag;

	if ((arg->ui & TAGMASK) && (arg->ui & TAGMASK) != selmon->tagset[selmon->selected_tags]) {
		selmon->selected_tags ^= 1; /* toggle sel tagset */
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->tagset[selmon->selected_tags] = arg->ui & TAGMASK;
		if (arg->ui == ~0) {
			selmon->pertag->curtag = 0;
		} else {
			for (i=0; !(arg->ui & 1 << i); i++);
			selmon->pertag->curtag = i + 1;
		}
	} else if (view_tag_toggles) {
		selmon->selected_tags ^= 1; /* toggle sel tagset */
		tmptag = selmon->pertag->prevtag;
		selmon->pertag->prevtag = selmon->pertag->curtag;
		selmon->pertag->curtag = tmptag;
	}
	selmon->marked_width = selmon->pertag->marked_widths[selmon->pertag->curtag];
	selmon->selected_layout = selmon->pertag->selected_layouts[selmon->pertag->curtag];
	selmon->layout[selmon->selected_layout] = selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout];
	selmon->layout[selmon->selected_layout^1] = selmon->pertag->layoutidxs[selmon->pertag->curtag][selmon->selected_layout^1];
	if (selmon->show_tagbar != selmon->pertag->show_tagbars[selmon->pertag->curtag]) {
		cmd_toggle_tagbar(NULL);
	}
	focus(NULL);
	arrange(selmon);
}

/**
 * Creates and initializes a new Color structure.
 * 
 * @param	drw	The relevant Gfx structure.
 * @param	clrname	The name of the color.
 */
Color *
color_create (Graphics *drw, const char *clrname) {
	Color *clr;
	Colormap cmap;
	XColor color;

	if (!drw) {
		return NULL;
	}
	clr = (Color *)calloc(1, sizeof(Color));
	if (!clr) {
		return NULL;
	}
	cmap = DefaultColormap(drw->dpy, drw->screen);
	if (!XAllocNamedColor(drw->dpy, cmap, clrname, &color, &color)) {
		die("error, cannot allocate color '%s'\n", clrname);
	}
	clr->rgb = color.pixel;
	return clr;
}

/**
 * Frees resources associated with a Color structure.
 * 
 * @param	font	The target Color structure..
 */
void
color_free (Color *clr) {
	if (clr) {
		free(clr);
	}
}

/**
 * Updates the geometry of the window associated with a given client.
 * 
 * @param	c	The target client.
 */
void
configure (Client *c) {
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

/**
 * Detaches a client from its monitor's list of clients.
 * 
 * @param	c	The target client.
 */
void
detach (Client *c) {
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

/**
 * Shuts down the WM with an error message.
 * 
 * @param	errstr	The error message to log.
 */
void
die (const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

/**
 * Returns the next or previous monitor in the monitor list (interpreted as a cycle), depending on the sign of the argument.
 * 
 * @param	dir	If dir > 0, the next monitor is returned, otherwise the previous monitor is returned.
 */
Monitor *
direction_to_monitor (int dir) {
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next)) {
			m = mons;
		}
	} else if (selmon == mons) {
		for (m = mons; m->next; m = m->next);
	} else {
		for (m = mons; m->next != selmon; m = m->next);
	}
	return m;
}

/**
 * Draws the tag and client bars on all monitors.
 */
void
draw_bars (void) {
	Monitor *m;

	for (m = mons; m; m = m->next) {
		draw_tagbar(m);
		draw_clientbar(m);
	}
}

/**
 * Draw the client bar on a given monitor.
 * 
 * @param	m	The monitor on which to draw the client bar.
 */
void
draw_clientbar (Monitor *m) {
	Client *c;
	int i, n;
	int view_info_w = 0;
	int sorted_label_widths[MAXTABS];
	int tot_width;
	int maxsize = bh;
	int x = 0;
	int w = 0;
	view_info_w = blw = TEXTW(m->layout_symbol);
	tot_width = view_info_w;

	/* Calculates number of labels and their width */
	m->num_client_tabs = 0;
	for (c = m->clients; c && m->num_client_tabs < MAXTABS; c = c->next) {
		if (!TAGISVISIBLE(c)) continue;
		m->client_tab_widths[m->num_client_tabs] = TEXTW(c->name);
		tot_width += m->client_tab_widths[m->num_client_tabs];
	
		m->num_client_tabs++;
	}

	if (tot_width > m->winarea_width) { /* not enough space to display the labels, they need to be truncated */
		memcpy(sorted_label_widths, m->client_tab_widths, sizeof(int) * m->num_client_tabs);
		qsort(sorted_label_widths, m->num_client_tabs, sizeof(int), _cmpint);
		tot_width = view_info_w;
		for (i = 0; i < m->num_client_tabs; ++i) { // TODO: investigate this loop
			if (tot_width + (m->num_client_tabs - i) * sorted_label_widths[i] > m->winarea_width) break;
			tot_width += sorted_label_widths[i];
		}
		maxsize = (m->winarea_width - tot_width) / (m->num_client_tabs - i);
	} else {
		maxsize = m->winarea_width;
	}
	//update_onscreen(m);	/* is this strictly necessary? we should do it before this is called anyway */
	i = n = 0;
	for (c = m->clients; c && i < m->num_client_tabs; c = c->next) {
		if (!TAGISVISIBLE(c)) continue;
		if (m->client_tab_widths[i] >  maxsize) {
			m->client_tab_widths[i] = maxsize;
		}
		w = m->client_tab_widths[i];
		if (c == m->sel) {
			gfx_set_colorscheme(drw, &scheme[SchemeSel]);
		} else if (c->isurgent) {
			gfx_set_colorscheme(drw, &scheme[SchemeUrgent]);
		} else if (c->minimized) {
			gfx_set_colorscheme(drw, &scheme[SchemeMinimized]);
		} else if (c->onscreen) {
			gfx_set_colorscheme(drw, &scheme[SchemeVisible]);
		} else {
			gfx_set_colorscheme(drw, &scheme[SchemeNorm]);
		}
		gfx_draw_text(drw, x, 0, w, th, c->name);
		if (c->marked) {
			gfx_draw_rect(drw, x, 0, w, th, (c == selmon->sel), True);
		}

		x += w;
		i++;
	}

	gfx_set_colorscheme(drw, &scheme[SchemeNorm]);

	/* cleans interspace between window names and current viewed tag label */
	w = m->winarea_width - view_info_w - x;
	gfx_draw_text(drw, x, 0, w, th, NULL);

	/* layout info */
	x += w;
	w = view_info_w;
	gfx_draw_text(drw, x, 0, w, th, m->layout_symbol);

	gfx_render_to_window(drw, m->clientbar_win, 0, 0, m->winarea_width, th);
}

/**
 * Draws the tag bar for a given monitor.
 * 
 * @param	m	The monitor on which to draw the tag bar.
 */
void
draw_tagbar (Monitor *m) {
	int x, xx, w;
	unsigned int i, occ = 0, urg = 0;
	Client *c;

	for (c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if (c->isurgent) {
			urg |= c->tags;
		}
	}
	x = 0;
	for (i = 0; i < LENGTH(tags); i++) {
		if (!hide_inactive_tags || occ & 1 << i || m->tagset[m->selected_tags] & 1 << i) {
			w = TEXTW(tags[i]);
			if (urg & 1 << i) {
				gfx_set_colorscheme(drw, &scheme[SchemeUrgent]);
			} else if (m->tagset[m->selected_tags] & 1 << i) {
				gfx_set_colorscheme(drw, (m == selmon && selmon->sel && selmon->sel->tags & 1 << i) ? &scheme[SchemeSel] : &scheme[SchemeVisible]);
			} else {
				gfx_set_colorscheme(drw, &scheme[SchemeNorm]);
			}
			gfx_draw_text(drw, x, 0, w, bh, tags[i]);
			gfx_draw_rect(drw, x, 0, w, bh, m == selmon && selmon->sel && selmon->sel->tags & 1 << i, occ & 1 << i);
			x += w;
		}
	}
	gfx_set_colorscheme(drw, &scheme[SchemeNorm]);
	xx = x;
	w = TEXTW(stext);
	x = m->winarea_width - w;
	if (x < xx) {
		x = xx;
		w = m->winarea_width - xx;
	}
	gfx_draw_text(drw, x, 0, w, bh, stext);
	if ((w = x - xx) > bh) {
		x = xx;
		if (m->sel) {
			gfx_set_colorscheme(drw, m == selmon ? &scheme[SchemeSel] : &scheme[SchemeNorm]);
			gfx_draw_text(drw, x, 0, w, bh, m->sel->name);
			gfx_draw_rect(drw, x, 0, w, bh, m->sel->isfixed, m->sel->isfloating);
		} else {
			gfx_set_colorscheme(drw, &scheme[SchemeNorm]);
			gfx_draw_text(drw, x, 0, w, bh, NULL);
		}
	}
	gfx_render_to_window(drw, m->tagbar_win, 0, 0, m->winarea_width, bh);
}

/**
 * Handler for ButtonPress events.
 * Called when the user presses a mouse button.
 * 
 * @param	e	The event.
 */
void
event_button_press (XEvent *e) {
	unsigned int i, x, click, occ = 0;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClickRootWin;
	/* focus monitor if necessary */
	if ((m = window_to_monitor(ev->window)) && m != selmon) {
		unfocus(selmon->sel);
		focus_root();
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->tagbar_win) {
		for (c = m->clients; c; c = c->next) {
			occ |= c->tags;
		}
		i = x = 0;
		do {
			if (!hide_inactive_tags || occ & 1 << i || m->tagset[m->selected_tags] & 1 << i) {
				x += TEXTW(tags[i]);
			}
		} while (ev->x >= x && ++i < LENGTH(tags));
		if (i < LENGTH(tags)) {
			click = ClickTagBar;
			arg.ui = 1 << i;
		} else if (ev->x > selmon->winarea_width - TEXTW(stext)) {
			click = ClickStatusText;
		} else {
			click = ClickWinTitle;
		}
	}
	if (ev->window == selmon->clientbar_win) {
		if (ev->x > selmon->winarea_width - TEXTW(m->layout_symbol)) {
			click = ClickLayoutSymbol;
		} else {
			i = 0; x = 0;
			for (c = selmon->clients; c; c = c->next) {
				if (!TAGISVISIBLE(c)) continue;
				x += selmon->client_tab_widths[i];
				if (ev->x > x) {
					i++;
				} else {
					break;
				}
				if (i >= m->num_client_tabs) break;
			}
			if (c) {
				click = ClickClientBar;
				arg.ui = i;
			}
		}
	} else if ((c = window_to_client(ev->window))) {
		focus(c);
		click = ClickClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++) {
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
				&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
					
			buttons[i].func(((click == ClickTagBar || click == ClickClientBar)
					&& buttons[i].arg.i == 0) ? &arg : &buttons[i].arg);
		}
	}
}

/**
 * Handler for ClientMessage events.
 * Currently handles messages from clients regarding window activation and transitions to/from fullscreen.
 * 
 * @param	e	The event.
 */
void
event_client_message (XEvent *e) {
	XClientMessageEvent *cme = &e->xclient;
	Client *c = window_to_client(cme->window);

	if (!c) return;
	
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen] || cme->data.l[2] == netatom[NetWMFullscreen]) {
			set_fullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
						   || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
		}
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (!TAGISVISIBLE(c)) {
			c->mon->selected_tags ^= 1;
			c->mon->tagset[c->mon->selected_tags] = c->tags;
		}
		pop(c);
	}
}

/**
 * Handler for ConfigureNotify events.
 * Used for making sure the geometry is updated when the root window is resized (e.g. when the user changes resolution).
 * 
 * @param	e	The event.
 */
void
event_configure_notify (XEvent *e) {
	Monitor *m;
	XConfigureEvent *ev = &e->xconfigure;
	Bool dirty;

	/* TODO: update_geometry handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (update_geometry() || dirty) {
			gfx_resize(drw, sw, bh);
			init_bars();
			/* refreshes display of tag bar. The client bar is handled by arrange(), which is called below */
			for (m = mons; m; m = m->next) {
				XMoveResizeWindow(dpy, m->tagbar_win, m->winarea_x, m->tagbar_pos, m->winarea_width, bh);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

/**
 * Handler for ConfigureRequest events.
 * 
 * @param	e	The event.
 */
void
event_configure_request (XEvent *e) {
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = window_to_client(ev->window))) {
		if (ev->value_mask & CWBorderWidth) {
			c->bw = ev->border_width;
		} else if (c->isfloating || !selmon->layout[selmon->selected_layout]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mon_x + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->mon_y + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mon_x + m->mon_width && c->isfloating) {
				c->x = m->mon_x + (m->mon_width / 2 - WIDTH(c) / 2); /* center in x direction */
			}
			if ((c->y + c->h) > m->mon_y + m->mon_height && c->isfloating) {
				c->y = m->mon_y + (m->mon_height / 2 - HEIGHT(c) / 2); /* center in y direction */
			}
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight))) {
				configure(c);
			}
			if (TAGISVISIBLE(c)) {
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
			}
		} else {
			configure(c);
		}
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

/**
 * Handler for DestroyNotify events.
 * Called when a window is destroyed, unmanages it if we have a client for it.
 * 
 * @param	e	The event.
 */
void
event_destroy_notify (XEvent *e) {
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = window_to_client(ev->window))) {
		unmanage(c, True);
	}
}

/**
 * Handler for EnterNotify events.
 * Called when the pointer enters a window area.
 * 
 * @param	e	The event.
 */
void
event_enter_notify (XEvent *e) {
	Client *c;
	Monitor *m;
	XCrossingEvent *ev = &e->xcrossing;

	if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root) return;
	
	c = window_to_client(ev->window);
	m = c ? c->mon : window_to_monitor(ev->window);
	if (m != selmon) {
		unfocus(selmon->sel);
		focus_root();
		selmon = m;
	} else if (!c || c == selmon->sel) {
		return;
	}
	focus(c);
}

/**
 * Handler for Expose events.
 * Called when either of the bars are exposed and need to be redrawn.
 * 
 * @param	e	The event.
 */
void
event_expose (XEvent *e) {
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count == 0 && (m = window_to_monitor(ev->window))) {
		draw_tagbar(m);
		draw_clientbar(m);
	}
}

/**
 * Handler for FocusIn events.
 * Called when the X server detects a focus change.
 * 
 * @param	e	The event.
 */
void
event_focus_in (XEvent *e) {
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win) {
		focus(selmon->sel);
	}
}

/**
 * Handler for KeyPress events.
 * Called when the user presses a key combination that the WM has grabbed.
 * 
 * @param	e	The event.
 */
void
event_key_press (XEvent *e) {
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for (i = 0; i < LENGTH(keys); i++) {
		if (keysym == keys[i].keysym
				&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
				&& keys[i].func) {
			keys[i].func(&(keys[i].arg));
		}
	}
}

/**
 * Handler for MappingNotify events.
 * Called for changes in the keyboard/pointer mapping.
 * 
 * @param	e	The event.
 */
void
event_mapping_notify (XEvent *e) {
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard) {
		grab_shortcut_keys();
	}
}

/**
 * Handler for MapRequest events.
 * Called when clients windows are created.
 * 
 * @param	e	The event.
 */
void
event_map_request (XEvent *e) {
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect) return;
	
	if (!window_to_client(ev->window)) {
		manage(ev->window, &wa);
	}
}

/**
 * Handler for MotionNotify events.
 * Called when the pointer is moved.
 * 
 * @param	e	The event.
 */
void
event_motion_notify (XEvent *e) {
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)	return;
	
	if ((m = rect_to_monitor(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel);
		focus_root();
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

/**
 * Handler for PropertyNotify events.
 * Called when windows change their properties.
 * 
 * @param	e	The event.
 */
void
event_property_notify (XEvent *e) {
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((ev->window == root) && (ev->atom == XA_WM_NAME)) {
		update_statusarea();
	} else if (ev->state == PropertyDelete) {
		return; /* ignore */
	} else if ((c = window_to_client(ev->window))) {
		switch(ev->atom) {
			default:
				break;
			case XA_WM_TRANSIENT_FOR:
				if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				   (c->isfloating = (window_to_client(trans)) != NULL))
					arrange(c->mon);
				break;
			case XA_WM_NORMAL_HINTS:
				update_size_hints(c);
				break;
			case XA_WM_HINTS:
				update_wm_hints(c);
				draw_bars();
				break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			update_title(c);
			if (c == c->mon->sel) {
				draw_tagbar(c->mon);
			}
			draw_clientbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType]) {
			update_window_type(c);
		}
	}
}

/**
 * Handler for UnmapNotify events.
 * Called when a window is unmapped.
 * 
 * @param	e	The event.
 */
void
event_unmap_notify (XEvent *e) {
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = window_to_client(ev->window))) {
		if (ev->send_event) {
			set_client_state(c, WithdrawnState);
		} else {
			unmanage(c, False);
		}
	}
}

/**
 * Gives focus to a given client.  If NULL is passed as an argument, tries to focus on the first visible client in the selected monitor's stack; focus is lost if that fails.
 * 
 * @param	c	The client on which to focus.  
 */
void
focus (Client *c) {
	if (!c || !TAGISVISIBLE(c)) {
		for (c = selmon->stack; c && (!TAGISVISIBLE(c) || c->minimized); c = c->snext);
	}
	if (selmon->sel && selmon->sel != c) {
		unfocus(selmon->sel);
	}
	if (c) {
		if (c->mon != selmon) {
			selmon = c->mon;
		}
		if (c->isurgent) {
			clear_urgent(c);
		}
		stack_detach(c);
		stack_attach(c);
		grab_buttons(c, True);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel].border->rgb);
		if (!c->neverfocus) {
			XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
			XChangeProperty(dpy, root, netatom[NetActiveWindow],
							XA_WINDOW, 32, PropModeReplace,
							(unsigned char *) &(c->win), 1);
		}
		send_event(c, wmatom[WMTakeFocus]);
	} else {
		focus_root();
	}
	selmon->sel = c;
	draw_bars();
	arrange(selmon);
}

/**
 * Gives focus to the root window.
 */
void
focus_root (void) {
	XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

/**
 * Creates and initializes a new Font structure.
 * 
 * @param	dpy	The relevant display.
 * @param	fontname	The name of the font.
 */
FontStruct *
font_create (Display *dpy, const char *fontname) {
	FontStruct *font;
	char *def, **missing;
	int n;

	font = (FontStruct *)calloc(1, sizeof(FontStruct));
	if (!font) {
		return NULL;
	}
	font->set = XCreateFontSet(dpy, fontname, &missing, &n, &def);
	if (missing) {
		while (n--)
			fprintf(stderr, "drw: missing fontset: %s\n", missing[n]);
		XFreeStringList(missing);
	}
	if (font->set) {
		XFontStruct **xfonts;
		char **font_names;
		XExtentsOfFontSet(font->set);
		
		n = XFontsOfFontSet(font->set, &xfonts, &font_names);
		while (n--) {
			font->ascent = MAX(font->ascent, (*xfonts)->ascent);
			font->descent = MAX(font->descent,(*xfonts)->descent);
			xfonts++;
		}
	} else {
		if (!(font->xfont = XLoadQueryFont(dpy, fontname))
				&& !(font->xfont = XLoadQueryFont(dpy, "fixed"))) {
					
			die("error, cannot load font: '%s'\n", fontname);
		}
		font->ascent = font->xfont->ascent;
		font->descent = font->xfont->descent;
	}
	font->h = font->ascent + font->descent;
	return font;
}

/**
 * Frees resources associated with a Font structure.
 * 
 * @param	dpy	The relevant display.
 * @param	font	The target FontStruct structure..
 */
void
font_free (Display *dpy, FontStruct *font) {
	if (!font) return;
	
	if (font->set) {
		XFreeFontSet(dpy, font->set);
	} else {
		XFreeFont(dpy, font->xfont);
	}
	free(font);
}

/**
 * Populate an Extents structure with the dimensions of a string, were it to be rendered with a given font.
 * 
 * @param	font	The font.
 * @param	text	The target text.
 * @param	len		The length of the string text.
 * @param	tex		An Extents structure to populate with the results.
 */
void
font_get_text_extents (FontStruct *font, const char *text, unsigned int len, Extents *tex) {
	XRectangle r;

	if (!font || !text) return;
	
	if (font->set) {
		XmbTextExtents(font->set, text, len, NULL, &r);
		tex->w = r.width;
		tex->h = r.height;
	} else {
		tex->h = font->ascent + font->descent;
		tex->w = XTextWidth(font->xfont, text, len);
	}
}

/**
 * Returns the width of a string, were it to be rendered with a given font.
 * 
 * @param	font	The font.
 * @param	text	The target text.
 * @param	len		The length of the string text.
 */
unsigned int
font_get_text_width (FontStruct *font, const char *text, unsigned int len) {
	Extents tex;

	if (!font) {
		return -1;
	}
	font_get_text_extents(font, text, len, &tex);
	return tex.w;
}

/**
 * Gets a property of a client window from the X server (as an Atom).
 * 
 * @param	c		The target client.
 * @param	prop	An Atom that specifies the desired property.
 */
Atom
get_prop_atom (Client *c, Atom prop) {
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
						  &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

/**
 * Gets a property of a client window from the X server (as text).
 * 
 * @param	c		The target client.
 * @param	prop	An Atom that specifies the desired property.
 * @param	text	A buffer, into which the result is copied.
 * @param	size	The size of the text buffer.
 */
Bool
get_prop_text (Window w, Atom atom, char *text, unsigned int size) {
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0) {
		return False;
	}
	text[0] = '\0';
	XGetTextProperty(dpy, w, &name, atom);
	if (!name.nitems) {
		return False;
	}
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return True;
}

/**
 * Gets the coordinates of the pointer relative to the root window.
 * See http://tronche.com/gui/x/xlib/window-information/XQueryPointer.html for explanation of Boolean return value.
 * 
 * @param	x	A pointer to the location to store the x coordinate.
 * @param	y	A pointer to the location to store the y coordinate.
 */
Bool
get_root_pointer_pos (int *x, int *y) {
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

/**
 * Gets the state of a window (i.e. IconicState, NormalState or WithdrawnState).
 * 
 * @param	w	The target window.
 */
long
get_state (Window w) {
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
						  &real, &format, &n, &extra, (unsigned char **)&p) != Success) {
		return -1;
	}
	if (n != 0) {
		result = *p;
	}
	XFree(p);
	return result;
}

/**
 * Returns a new Graphics structure.
 */
Graphics *
gfx_create (Display *dpy, int screen, Window root, unsigned int w, unsigned int h) {
	Graphics *drw = (Graphics *)calloc(1, sizeof(Graphics));
	
	if (!drw) {
		return NULL;
	}
	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
	drw->gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter);
	return drw;
}

/**
 * Draws a rectangle on the screen.
 * 
 * TODO: document parameters
 */
void
gfx_draw_rect (Graphics *drw, int x, int y, unsigned int w, unsigned int h, int filled, int empty) {
	int dx;

	if (!drw || !drw->font || !drw->scheme) return;
	
	XSetForeground(drw->dpy, drw->gc, drw->scheme->fg->rgb);
	dx = (drw->font->ascent + drw->font->descent + 2) / 4;
	if (filled) {
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x+1, y+1, dx+1, dx+1);
	} else if (empty) {
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x+1, y+1, dx, dx);
	}
}

/**
 * Renders text to the screen.
 * 
 * TODO: document parameters
 */
void
gfx_draw_text (Graphics *drw, int x, int y, unsigned int w, unsigned int h, const char *text) {
	char buf[256];
	int i, tx, ty, th, len, olen;
	Extents tex;

	if (!drw || !drw->scheme) return;
	
	XSetForeground(drw->dpy, drw->gc, drw->scheme->bg->rgb);
	XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	if (!text || !drw->font) return;
	
	olen = strlen(text);
	font_get_text_extents(drw->font, text, olen, &tex);
	th = drw->font->ascent + drw->font->descent;
	ty = y + (h / 2) - (th / 2) + drw->font->ascent;
	tx = x + (h / 2);
	/* shorten text if necessary */
	for (len = MIN(olen, sizeof buf); len && (tex.w > w - tex.h || w < tex.h); len--) {
		font_get_text_extents(drw->font, text, len, &tex);
	}
	if (!len) return;
	memcpy(buf, text, len);
	if (len < olen) {
		for (i = len; i && i > len - 3; buf[--i] = '.');
	}
	XSetForeground(drw->dpy, drw->gc, drw->scheme->fg->rgb);
	if (drw->font->set) {
		XmbDrawString(drw->dpy, drw->drawable, drw->font->set, drw->gc, tx, ty, buf, len);
	} else {
		XDrawString(drw->dpy, drw->drawable, drw->gc, tx, ty, buf, len);
	}
}

/**
 * Frees resources associated with a Graphics structure.
 * 
 * @param	drw	The target Graphics structure.
 */
void
gfx_free (Graphics *drw) {
	XFreePixmap(drw->dpy, drw->drawable);
	XFreeGC(drw->dpy, drw->gc);
	free(drw);
}

/**
 * Renders the provided Graphics to a window at a particular position.
 * 
 * @param	drw	The relevant Graphics.
 * @param	win	The target window.
 * TODO: finish documenting parameters
 */
void
gfx_render_to_window (Graphics *drw, Window win, int x, int y, unsigned int w, unsigned int h) {
	if (!drw) return;
	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, x, y, w, h, x, y);
	XSync(drw->dpy, False);
}

/**
 * Resizes the drawable area for a Graphics structure.
 * 
 * @param	drw	The target Graphics structure.
 * @param	w	Width
 * @param	w	Height
 */
void
gfx_resize (Graphics *drw, unsigned int w, unsigned int h) {
	if (!drw) return;
	drw->w = w;
	drw->h = h;
	if (drw->drawable != 0) {
		XFreePixmap(drw->dpy, drw->drawable);
	}
	drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, DefaultDepth(drw->dpy, drw->screen));
}

/**
 * Sets the ColorScheme to use when drawing.
 * 
 * @param	drw		The relevant Graphics structure.
 * @param	scheme	The ColorScheme to use.
 */
void
gfx_set_colorscheme (Graphics *drw, ColorScheme *scheme) {
	if (drw && scheme) {
		drw->scheme = scheme;
	}
}

/**
 * Sets the FontStruct with which to render text.
 * 
 * @param	drw		The relevant Graphics structure.
 * @param	font	The Font to use.
 */
void
gfx_set_font (Graphics *drw, FontStruct *font) {
	if (drw) {
		drw->font = font;
	}
}

/**
 * Grabs mouse input for a given client.
 * 
 * @param	c	The target client.
 * TODO: explain the boolean
 */
void
grab_buttons (Client *c, Bool focused) {
	unsigned int i, j;
	unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

	update_numlock_mask();
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if (focused) {
		for (i = 0; i < LENGTH(buttons); i++) {
			if (buttons[i].click == ClickClientWin) {
				for (j = 0; j < LENGTH(modifiers); j++) {
					XGrabButton(dpy, buttons[i].button,
								buttons[i].mask | modifiers[j],
								c->win, False, BUTTONMASK,
								GrabModeAsync, GrabModeSync, None, None);
				}
			}
		}
	} else {
		XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
					BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
	}
}

/**
 * Alerts the X server of the shortcut keys the WM uses.
 */
void
grab_shortcut_keys (void) {
	unsigned int i, j;
	unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
	KeyCode code;
	
	update_numlock_mask();

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < LENGTH(keys); i++) {
		if ((code = XKeysymToKeycode(dpy, keys[i].keysym))) {
			for (j = 0; j < LENGTH(modifiers); j++) {
				XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
					 True, GrabModeAsync, GrabModeAsync);
			}
		}
	}
}

/**
 * Initializes (or reinitializes) the windows that represent the tag and client bars.
 */
void
init_bars (void) {
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	
	for (m = mons; m; m = m->next) {
		if (m->tagbar_win) continue;
		
		m->tagbar_win = XCreateWindow(dpy, root, m->winarea_x, m->tagbar_pos, m->winarea_width, bh, 0, DefaultDepth(dpy, screen),
								  CopyFromParent, DefaultVisual(dpy, screen),
								  CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->tagbar_win, cursor[CursorNormal]);
		XMapRaised(dpy, m->tagbar_win);
		m->clientbar_win = XCreateWindow(dpy, root, m->winarea_x, m->clientbar_pos, m->winarea_width, th, 0, DefaultDepth(dpy, screen),
					  CopyFromParent, DefaultVisual(dpy, screen),
					  CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->clientbar_win, cursor[CursorNormal]);
		XMapRaised(dpy, m->clientbar_win);
	}
}


#ifdef XINERAMA
/**
 * Returns False if the geometry of 'info' is duplicated among the elements of the array 'unique'.
 * 
 * @param	unique	An array of XineramaScreenInfo structures to test info against.
 * @param	n		The length of unique.
 * @param	info	The XineramaScreenInfo to test for uniqueness.
 */
static Bool
is_geom_unique (XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info) {
	while (n--) {
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
				&& unique[n].width == info->width && unique[n].height == info->height) {
			return False;
		}
	}
	return True;
}
#endif /* XINERAMA */

/**
 * Begin managing a window.
 * 
 * @param	w	The window to manage.
 * @param	wa	XWindowAttributes for the window to manage.
 */
void
manage (Window w, XWindowAttributes *wa) {
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;
	Arg wintag;

	if (!(c = calloc(1, sizeof(Client)))) {
		die("fatal: could not malloc() %u bytes\n", sizeof(Client));
	}
	c->win = w;
	update_title(c);
	c->minimized = c->marked = False;
	c->onscreen = True;
	if (XGetTransientForHint(dpy, w, &trans) && (t = window_to_client(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		apply_rules(c);
	}
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	if (c->x + WIDTH(c) > c->mon->mon_x + c->mon->mon_width) {
		c->x = c->mon->mon_x + c->mon->mon_width - WIDTH(c);
	}
	if (c->y + HEIGHT(c) > c->mon->mon_y + c->mon->mon_height) {
		c->y = c->mon->mon_y + c->mon->mon_height - HEIGHT(c);
	}
	c->x = MAX(c->x, c->mon->mon_x);
	/* only fix client y-offset, if the client center might cover a bar */
	c->y = MAX(c->y, ((c->mon->tagbar_pos == c->mon->mon_y) && (c->x + (c->w / 2) >= c->mon->winarea_x)
			   && (c->x + (c->w / 2) < c->mon->winarea_x + c->mon->winarea_width)) ? bh : c->mon->mon_y);
	c->bw = c->isfloating || trans != None ? floatborderpx : borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm].border->rgb);
	configure(c); /* propagates border_width, if size doesn't change */
	update_window_type(c);
	update_size_hints(c);
	update_wm_hints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grab_buttons(c, False);
	c->wasfloating = False;
	if (!c->isfloating) {
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	}
	if (c->isfloating) {
		XRaiseWindow(dpy, c->win);
	}
	attach(c);
	stack_attach(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
					(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	set_client_state(c, NormalState);
	if (c->mon == selmon) {
		unfocus(selmon->sel);
	}
	c->mon->sel = c;
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	
	if (follow_new_windows && !(c->tags & c->mon->tagset[c->mon->selected_tags])) {
		wintag.ui = c->tags;
		cmd_view_tag(&wintag);
		
	}
	restack(selmon);
	focus(c);	/* used to be focus(NULL) for unknown reasons, but that prevents certain windows from acquiring focus properly.  Hopefully this doesn't break anything */
}

/**
 * Cleans up WM resources associated with a monitor.
 * 
 * @param	mon	The target monitor.
 */
void
monitor_cleanup (Monitor *mon) {
	Monitor *m;

	if (mon == mons) {
		mons = mons->next;
	} else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->tagbar_win);
	XDestroyWindow(dpy, mon->tagbar_win);
	XUnmapWindow(dpy, mon->clientbar_win);
	XDestroyWindow(dpy, mon->clientbar_win);
	free(mon);
}

/**
 * Creates and initializes a new Monitor structure.
 */
Monitor *
monitor_create (void) {
	Monitor *m;
	int i;

	if (!(m = (Monitor *)calloc(1, sizeof(Monitor)))) {
		die("fatal: could not malloc() %u bytes\n", sizeof(Monitor));
	}
	m->tagset[0] = m->tagset[1] = 1;
	m->marked_width = marked_width;
	m->num_marked_win = 0;
	m->show_tagbar = show_tagbar;
	m->show_clientbar = show_clientbar;
	m->tags_on_top = tags_on_top;
	m->num_client_tabs = 0;
	m->selected_layout = 0;
	m->layout[0] = &layouts[def_layouts[1] % LENGTH(layouts)];
	m->layout[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->layout_symbol, layouts[0].symbol, sizeof m->layout_symbol);
	if (!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag)))) {
		die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));
	}
	m->pertag->curtag = m->pertag->prevtag = 1;
	for (i=0; i <= LENGTH(tags); i++) {
		/* init marked_widths */
		m->pertag->marked_widths[i] = m->marked_width;

		/* init layouts */
		m->pertag->layoutidxs[i][0] = m->layout[0];
		m->pertag->layoutidxs[i][1] = m->layout[1];
		m->pertag->selected_layouts[i] = m->selected_layout;

		/* init show_tagbar */
		m->pertag->show_tagbars[i] = m->show_tagbar;
	}
	return m;
}

/**
 * Steps through the client list (starting with the argument c) looking for a tiled client.
 * 
 * @param	c The first client to test.
 */
Client *
next_tiled (Client *c) {
	for (; c && (c->isfloating || !TAGISVISIBLE(c) || c->minimized); c = c->next);
	return c;
}

/**
 * Brings a client to the top of its monitor's focus stack and gives it focus.
 * 
 * @param	c	The client on which to focus.
 */
void
pop (Client *c) {
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

/**
 * Steps backward through the client list (starting with the argument c) looking for a tiled client.
 * 
 * @param	c The first client to test.
 */
Client *
prev_tiled (Client *c) {
	Client *p, *r;

	for (p = selmon->clients, r = NULL; p && p != c; p = p->next) {
		if (!p->isfloating && TAGISVISIBLE(p)) {
			r = p;
		}
	}
	return r;
}

/**
 * Returns the monitor associated with a rectangular region.
 */
Monitor *
rect_to_monitor (int x, int y, int w, int h) {
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next) {
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	}
	return r;
}
 
/**
 * Resizes a client, applying size hints first.
 *
 * TODO: document parameters.
 */
void
resize (Client *c, int x, int y, int w, int h, Bool interact) {
	if (apply_size_hints(c, &x, &y, &w, &h, interact)) {
		resize_client(c, x, y, w, h);
	}
}

/**
 * Resizes a client (without checking size hints).
 */
void
resize_client (Client *c, int x, int y, int w, int h) {
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

/**
 * Reorders a monitor's visible clients according to its stack list.
 * 
 * @param	m	The target monitor.
 */
void
restack (Monitor *m) {
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	draw_tagbar(m);
	draw_clientbar(m);
	if (!m->sel) return;
	
	if (m->sel->isfloating || !m->layout[m->selected_layout]->arrange) {
		XRaiseWindow(dpy, m->sel->win);
	}
	if (m->layout[m->selected_layout]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->tagbar_win;
		for (c = m->stack; c; c = c->snext) {
			if (!c->isfloating && TAGISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
		}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

/**
 * Scans for preexisting windows to manage.
 */
void
scan (void) {
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
					|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1)) {
				continue;
			}
			if (wa.map_state == IsViewable || get_state(wins[i]) == IconicState) {
				manage(wins[i], &wa);
			}
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa)) continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
					&& (wa.map_state == IsViewable || get_state(wins[i]) == IconicState)) {
				manage(wins[i], &wa);
			}
		}
		if (wins) {
			XFree(wins);
		}
	}
}

/**
 * Sends a client to a given monitor.
 * 
 * @param	c	The target Client.
 * @param	m	The target Monitor.
 */
void
send_client_to_monitor (Client *c, Monitor *m) {
	if (c->mon == m) return;
	unfocus(c);
	focus_root();
	detach(c);
	stack_detach(c);
	c->mon = m;
	c->tags = m->tagset[m->selected_tags]; /* assign tags of target monitor */
	attach(c);
	stack_attach(c);
	focus(NULL);
	arrange(NULL);
}

/**
 * Sends a message to a client (e.g. for focus or window destruction).
 * 
 * @param	c		The target client.
 * @param	proto	An Atom specifying the message to send.
 */
Bool
send_event (Client *c, Atom proto) {
	int n;
	Atom *protocols;
	Bool exists = False;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--) {
			exists = protocols[n] == proto;
		}
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

/**
 * Creates and initializes WM resources.
 */
void
setup (void) {
	XSetWindowAttributes wa;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	fnt = font_create(dpy, font);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	bh = fnt->h + 2;
	th = bh;
	drw = gfx_create(dpy, screen, root, sw, sh);
	gfx_set_font(drw, fnt);
	update_geometry();
	/* init atoms */
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CursorNormal] = XCreateFontCursor(drw->dpy, XC_left_ptr);
	cursor[CursorResize] = XCreateFontCursor(drw->dpy, XC_sizing);
	cursor[CursorMove] = XCreateFontCursor(drw->dpy, XC_fleur);
	/* init appearance */
	scheme[SchemeNorm].border = color_create(drw, normbordercolor);
	scheme[SchemeNorm].bg = color_create(drw, normbgcolor);
	scheme[SchemeNorm].fg = color_create(drw, normfgcolor);
	scheme[SchemeSel].border = color_create(drw, selbordercolor);
	scheme[SchemeSel].bg = color_create(drw, selbgcolor);
	scheme[SchemeSel].fg = color_create(drw, selfgcolor);
	scheme[SchemeVisible].border = color_create(drw, visbordercolor);
	scheme[SchemeVisible].bg = color_create(drw, visbgcolor);
	scheme[SchemeVisible].fg = color_create(drw, visfgcolor);
	scheme[SchemeMinimized].border = color_create(drw, minimizedbordercolor);
	scheme[SchemeMinimized].bg = color_create(drw, minimizedbgcolor);
	scheme[SchemeMinimized].fg = color_create(drw, minimizedfgcolor);
	scheme[SchemeUrgent].border = color_create(drw, urgentbordercolor);
	scheme[SchemeUrgent].bg = color_create(drw, urgentbgcolor);
	scheme[SchemeUrgent].fg = color_create(drw, urgentfgcolor);
	/* init bars */
	init_bars();
	update_statusarea();
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
			PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select for events */
	wa.cursor = cursor[CursorNormal];
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|PointerMotionMask
					|EnterWindowMask|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grab_shortcut_keys();
	focus(NULL);
}

/**
 * Sets the state of a client window (i.e. IconicState, NormalState or WithdrawnState).
 * 
 * @param	c		The target Client.
 * @param	state	The state to set.
 */
void
set_client_state (Client *c, long state) {
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
			PropModeReplace, (unsigned char *)data, 2);
}

/**
 * Sets whether or not a given client is in fullscreen mode or not.
 * 
 * @param	c	The target client.
 * @param	fullscreen	Enable/disable fullscreen?
 */
void
set_fullscreen (Client *c, Bool fullscreen) {
	if (fullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
						PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = True;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = True;
		resize_client(c, c->mon->mon_x, c->mon->mon_y, c->mon->mon_width, c->mon->mon_height);
		XRaiseWindow(dpy, c->win);
	}
	else {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
						PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = False;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resize_client(c, c->x, c->y, c->w, c->h);
		arrange(c->mon);
	}
}

/**
 * Mysterious SIGCHLD thing.
 * TODO: Understand what this does.
 */
void
sigchld (int unused) {
	if (signal(SIGCHLD, sigchld) == SIG_ERR) {
		die("Can't install SIGCHLD handler");
	}
	while (0 < waitpid(-1, NULL, WNOHANG));
}

/**
 * Attaches a client to its monitor's stack of clients.  The stack determines draw order, whereas the list of clients doesn't.
 * 
 * @param	c	The client to attach.
 */
void
stack_attach (Client *c) {
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

/**
 * Detaches a client from its monitor's focus stack.
 * 
 * @param	c	The target client.
 */
void
stack_detach (Client *c) {
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !TAGISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

/**
 * Removes focus from a given client.
 * 
 * @param	c	The target client.
 */
void
unfocus (Client *c) {
	if (!c) return;
	
	grab_buttons(c, False);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm].border->rgb);
}

/**
 * Stops managing a client and frees associate resources.
 * 
 * @param	c	The target client.
 * @param	destroyed	Was the window destroyed?
 */
void
unmanage(Client *c, Bool destroyed) {
	Monitor *m = c->mon;
	XWindowChanges wc;

	/* The server grab construct avoids race conditions. */
	detach(c);
	stack_detach(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy);
		XSetErrorHandler(_xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		set_client_state(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(_xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	update_client_list();
	arrange(m);
}

/**
 * Updates visibility and position of the tag and client bars on a given monitor.
 * 
 * @param	m	The target monitor.
 */
void
update_bar_positions (Monitor *m) {
	Client *c;
	int nvis = 0, nhid = 0;

	m->winarea_y = m->mon_y;
	m->winarea_height = m->mon_height;
	if (m->show_tagbar) {
		m->winarea_height -= bh;
		m->tagbar_pos = m->tags_on_top ? m->winarea_y : m->winarea_y + m->winarea_height;
		if (m->tags_on_top) {
			m->winarea_y += bh;
		}
	} else {
		m->tagbar_pos = -bh;
	}

	for (c = m->clients; c; c = c->next) {
		if (TAGISVISIBLE(c)) {
			nvis++;
			if (c->minimized) {
				nhid++;
			}
		}
	}
	
	if (m->show_clientbar == show_clientbar_always
			|| ((m->show_clientbar == show_clientbar_auto) && (nhid > 0
			|| ((nvis > 1) && (m->layout[m->selected_layout]->arrange == arrange_monocle))
			|| ((nvis > 1 + m->num_marked_win) && m->layout[m->selected_layout]->arrange == arrange_deck)))) {
		m->winarea_height -= th;
		m->clientbar_pos = m->tags_on_top ? m->winarea_y + m->winarea_height : m->winarea_y;
		if (!m->tags_on_top) {
			m->winarea_y += th;
		}
	} else {
		m->clientbar_pos = -th;
	}
	
	XMoveResizeWindow(dpy, m->clientbar_win, m->winarea_x, m->clientbar_pos, m->winarea_width, th);
}

/**
 * Update the root window's client list.
 * See http://standards.freedesktop.org/wm-spec/1.3/ar01s03.html for more information.
 */
void
update_client_list (void) {
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			XChangeProperty(dpy, root, netatom[NetClientList],
							XA_WINDOW, 32, PropModeAppend,
							(unsigned char *) &(c->win), 1);
		}
	}
}

/**
 * Updates screen geometry.
 */
Bool
update_geometry (void) {
	Bool dirty = False;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		if (!(unique = (XineramaScreenInfo *)malloc(sizeof(XineramaScreenInfo) * nn))) {
			die("fatal: could not malloc() %u bytes\n", sizeof(XineramaScreenInfo) * nn);
		}
		for (i = 0, j = 0; i < nn; i++) {
			if (is_geom_unique(unique, j, &info[i])) {
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
			}
		}
		XFree(info);
		nn = j;
		if (n <= nn) {
			for (i = 0; i < (nn - n); i++) { /* new monitors available */
				for (m = mons; m && m->next; m = m->next);
				if (m) {
					m->next = monitor_create();
				} else {
					mons = monitor_create();
				}
			}
			for (i = 0, m = mons; i < nn && m; m = m->next, i++) {
				if (i >= n
						|| (unique[i].x_org != m->mon_x || unique[i].y_org != m->mon_y
						|| unique[i].width != m->mon_width || unique[i].height != m->mon_height)) {
							
					dirty = True;
					m->num = i;
					m->mon_x = m->winarea_x = unique[i].x_org;
					m->mon_y = m->winarea_y = unique[i].y_org;
					m->mon_width = m->winarea_width = unique[i].width;
					m->mon_height = m->winarea_height = unique[i].height;
					update_bar_positions(m);
				}
			}
		} else { /* fewer monitors available nn < n */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next);
				while (m->clients) {
					dirty = True;
					c = m->clients;
					m->clients = c->next;
					stack_detach(c);
					c->mon = mons;
					attach(c);
					stack_attach(c);
				}
				if (m == selmon) {
					selmon = mons;
				}
				monitor_cleanup(m);
			}
		}
		free(unique);
	} else
#endif /* XINERAMA */
	/* default monitor setup */
	{
		if (!mons) {
			mons = monitor_create();
		}
		if (mons->mon_width != sw || mons->mon_height != sh) {
			dirty = True;
			mons->mon_width = mons->winarea_width = sw;
			mons->mon_height = mons->winarea_height = sh;
			update_bar_positions(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = window_to_monitor(root);
	}
	return dirty;
}

/**
 * Updates the numlock mask.
 */
void
update_numlock_mask (void) {
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for (i = 0; i < 8; i++) {
		for (j = 0; j < modmap->max_keypermod; j++) {
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
					== XKeysymToKeycode(dpy, XK_Num_Lock)) {
				numlockmask = (1 << i);
			}
		}
	}
	XFreeModifiermap(modmap);
}

/**
 * Updates which clients are tagged as being on screen for a given monitor.
 * 
 * @param	m	The target monitor.
 */
void
update_onscreen (Monitor *m) {
	Client *c = NULL;
	m->num_marked_win = 0;
	if (!m->layout[m->selected_layout]->arrange || m->layout[m->selected_layout]->arrange == arrange_tile) {
		for (c = m->clients; c; c = c->next) {
			c->onscreen = TAGISVISIBLE(c) && !c->minimized;
			if (TAGISVISIBLE(c) && c->marked) {
				m->num_marked_win++;
			}
		}
	} else if (m->layout[m->selected_layout]->arrange == arrange_monocle) {
		for (c = m->clients; c; c = c->next) {
			c->onscreen = TAGISVISIBLE(c) && !c->minimized && (c->isfloating || c == m->sel);
			if (TAGISVISIBLE(c) && c->marked) {
				m->num_marked_win++;
			}
		}
		if (!m->sel || m->sel->isfloating) {
			for (c = m->stack; c && (c->onscreen || c->minimized || !TAGISVISIBLE(c)); c = c->snext);
			if (c) {
				c->onscreen = True;
			}
		}
	} else if (m->layout[m->selected_layout]->arrange == arrange_deck) {
		for (c = m->clients; c; c = c->next) {
			c->onscreen = TAGISVISIBLE(c) && !c->minimized && (c->isfloating || c->marked || c == m->sel);
			if (TAGISVISIBLE(c) && c->marked) {
				m->num_marked_win++;
			}
		}
		
		if (!m->sel || m->sel->marked || m->sel->isfloating) {
			for (c = m->stack; c && (c->onscreen || c->minimized || !TAGISVISIBLE(c)); c = c->snext);
			if (c) {
				c->onscreen = True;
			}
		}
	}
}

/**
 * Updates size hints for a given client window.
 * 
 * @param	c	The target client.
 */
void
update_size_hints (Client *c) {
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize)) {
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	}
	if (size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	} else if (size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	} else {
		c->basew = c->baseh = 0;
	}
	if (size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	} else {
		c->incw = c->inch = 0;
	}
	if (size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	} else {
		c->maxw = c->maxh = 0;
	}
	if (size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	} else if (size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	} else {
		c->minw = c->minh = 0;
	}
	if (size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else {
		c->maxa = c->mina = 0.0;
	}
	c->isfixed = (c->maxw && c->minw && c->maxh && c->minh
				 && c->maxw == c->minw && c->maxh == c->minh);
}

/**
 * Updates the content of the status area based on the XA_WM_NAME property.
 */
void
update_statusarea (void) {
	Monitor* m;
	if (!get_prop_text(root, XA_WM_NAME, stext, sizeof(stext))) {
		strcpy(stext, "wasdwm-"VERSION);
	}
	for (m = mons; m; m = m->next) {
		draw_tagbar(m);
	}
}

/**
 * Updates a client's title.
 * 
 * @param	c	The target client.
 */
void
update_title (Client *c) {
	if (!get_prop_text(c->win, netatom[NetWMName], c->name, sizeof c->name)) {
		get_prop_text(c->win, XA_WM_NAME, c->name, sizeof c->name);
	}
	if (c->name[0] == '\0') { /* hack to mark broken clients */
		strcpy(c->name, broken);
	}
}

/**
 * Recursively moves down the focus stack, hiding or presenting windows as applicable.
 * 
 * @param	c	The current client.
 */
void
update_visibility (Client *c) {
	if (!c) return;
	if (TAGISVISIBLE(c) && (c->onscreen || (!hide_buried_windows && !c->minimized))) { /* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if ((!c->mon->layout[c->mon->selected_layout]->arrange || c->isfloating) && !c->isfullscreen) {
			resize(c, c->x, c->y, c->w, c->h, False);
		}
		set_client_state(c, NormalState);
		update_visibility(c->snext);
	} else { /* hide clients bottom up */
		update_visibility(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
		set_client_state(c, IconicState);
	}
}

/**
 * Updates the window type of a client based on the properties of its window.
 * 
 * @param	c	The target client.
 */
void
update_window_type (Client *c) {
	Atom state = get_prop_atom(c, netatom[NetWMState]);
	Atom wtype = get_prop_atom(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen]) {
		set_fullscreen(c, True);
	}
	if (wtype == netatom[NetWMWindowTypeDialog]) {
		c->isfloating = True;
	}
}

/**
 * Updates flags for a client based on WM hints associated with its window.
 * 
 * @param	c	The target client.
 */
void
update_wm_hints (Client *c) {
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else {
			c->isurgent = (wmh->flags & XUrgencyHint) ? True : False;
		}
		if (wmh->flags & InputHint) {
			c->neverfocus = !wmh->input;
		} else {
			c->neverfocus = False;
		}
		XFree(wmh);
	}
}

/**
 * Returns the Client structure associated with a given window.
 * 
 * @param	w	The target window.
 */
Client *
window_to_client (Window w) {
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next) {
		for (c = m->clients; c; c = c->next) {
			if (c->win == w) {
				return c;
			}
		}
	}
	return NULL;
}

/**
 * Returns the monitor associated with a given window.
 * 
 * @param	w	The target window.
 */
Monitor *
window_to_monitor (Window w) {
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && get_root_pointer_pos(&x, &y)) {
		return rect_to_monitor(x, y, 1, 1);
	}
	for (m = mons; m; m = m->next) {
		if (w == m->tagbar_win || w == m->clientbar_win) {
			return m;
		}
	}
	if ((c = window_to_client(w))) {
		return c->mon;
	}
	return selmon;
}

/**
 * Key function for the qsort in draw_clientbar.
 */
int
_cmpint (const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
	 pointers to char", but strcmp(3) arguments are "pointers
	 to char", hence the following cast plus dereference */
  return *((int*) p1) > * (int*) p2;
}

/**
 * Default error handler.
 * There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit. 
 */
int
_xerror (Display *dpy, XErrorEvent *ee) {
	if (ee->error_code == BadWindow
			|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
			|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
			|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
			|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
			|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
			|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
			|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
			|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable)) {
		return 0;
	}
	fprintf(stderr, "wasdwm: fatal error: request code=%d, error code=%d\n",
			ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

/**
 * Dummy error handler.
 */
int
_xerrordummy (Display *dpy, XErrorEvent *ee) {
	return 0;
}

/**
 * Startup Error handler to check if another window manager is already running.
 */
int
_xerrorstart (Display *dpy, XErrorEvent *ee) {
	die("wasdwm: another window manager is already running\n");
	return -1;
}


/**
 * Entry point and main event loop.
 */
int
main (int argc, char *argv[]) {
	XEvent ev;
	
	if (argc == 2 && !strcmp("-v", argv[1])) {
		die("wasdwm-"VERSION", see LICENSE for copyright and license details\n");
	} else if (argc != 1) {
		die("usage: wasdwm [-v]\n");
	}
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
		fputs("warning: no locale support\n", stderr);
	}
	if (!(dpy = XOpenDisplay(NULL))) {
		die("wasdwm: cannot open display\n");
	}
	/* Check for another WM */
	xerrorxlib = XSetErrorHandler(_xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(_xerror);
	XSync(dpy, False);
	
	setup();
	scan();
	
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev)) {
		if (handler[ev.type]) {
			handler[ev.type](&ev); /* call handler */
		}
	}

	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
