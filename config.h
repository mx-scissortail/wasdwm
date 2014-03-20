/* See LICENSE file for copyright and license details. */

/* included for volume control */
#include <X11/XF86keysym.h>
#define SndUp	XF86XK_AudioRaiseVolume
#define SndDown	XF86XK_AudioLowerVolume
#define SndMute	XF86XK_AudioMute

/* appearance */
static const char font[]                 = "-*-terminus-medium-r-*-*-12-*-*-*-*-*-*-*";
static const char normbordercolor[]      = "#444444";
static const char normbgcolor[]          = "#222222";
static const char normfgcolor[]          = "#bbbbbb";
static const char selbordercolor[]       = "#005577";
static const char selbgcolor[]           = "#005577";
static const char selfgcolor[]           = "#eeeeee";
static const char visbordercolor[]       = "#002233";
static const char visbgcolor[]           = "#002233";
static const char visfgcolor[]           = "#bbbbbb";
static const char minimizedbordercolor[] = "#444444"; /* technically useless */
static const char minimizedbgcolor[]     = "#222222";
static const char minimizedfgcolor[]   	 = "#666666";
static const char urgentbordercolor[]    = "#ff0000";
static const char urgentbgcolor[]        = "#ff0000";
static const char urgentfgcolor[]        = "#bbbbbb";
static const unsigned int borderpx       = 0; /* border pixel of windows */
static const unsigned int floatborderpx  = 1; /* border pixel of floating windows */
static const unsigned int snap           = 32; /* snap region */
static const Bool show_tagbar            = True; /* False means no tag bar */
static const Bool tags_on_top            = True; /* False means bottom tag bar */
static const Bool follow_new_windows     = True; /* Switch to a tag if it's not enabled and a new window opens there? */
static const Bool view_tag_toggles       = True; /* True means swtiching to a tagset that's already enabled reverts to previous tagset */
static const Bool hide_inactive_tags     = True; /* Don't display tags with no clients assigned to them (unless they're selected) */
static const Bool resizehints            = False; /* True means respect size hints in tiled resizes */
static const Bool hide_buried_windows    = True; /* True means clients that aren't floating, marked or at the top of the stack are moved off screen - only matters if you care about what's under transparent windows */

/*   Display modes of the client bar: never shown, always shown, shown only when there are offscreen windows */
/*   A mode can be disabled by moving it after the show_clientbar_nmodes end marker */
enum show_clientbar_modes { show_clientbar_never, show_clientbar_auto, show_clientbar_nmodes, show_clientbar_always };
static const int show_clientbar          = show_clientbar_auto; /* Default client bar show mode  */

/* tagging */
static const char *tags[] = { "terminal", "1", "2", "3", "4", "5", "6", "7", "8" };
/* default layout per tags */
/* The first element is for all-tag view, following i-th element corresponds to */
/* tags[i]. Layout is referred using the layouts array index.*/
static int def_layouts[1 + LENGTH(tags)]  = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };


/* special window management rules */
static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class      instance    title       tags mask     isfloating   monitor */
	{ "Gimp",     NULL,       NULL,       0,            True,        -1 },
	{ "Chromium", NULL,       NULL,       1 << 1,       False,       -1 },
	{ "Geany",    NULL,       NULL,       1 << 1,       False,       -1 },
	{ "MPlayer",  NULL,       NULL,       1 << 1,       True,        -1 },
	{ "URxvt",    NULL,       NULL,       1 << 0,       False,       -1 },
	{ "exe",      NULL,       NULL,       0,            True,        -1 }, /* fullscreen flash */
	{ "FTL",	  NULL,		  NULL,		  0,			True,		 -1 },
};

/* layout(s) */
static const float marked_width      = 0.55;  /* default width of marked clients area size [0.05..0.95] */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "D  ",      arrange_deck }, /* first entry is default */    
	{ "[M]",      arrange_monocle },    
	{ "[]=",      arrange_tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      cmd_view_tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      cmd_toggle_tag_view,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      cmd_tag_client,          {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      cmd_toggle_tag,          {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *dmenucmd[] = { "dmenu-launch", "-fn", font, "-nb", normbgcolor, "-nf", normfgcolor, "-sb", selbgcolor, "-sf", selfgcolor, NULL };
static const char *termcmd[]  = { "urxvt", NULL };
static const char *volup[]    = { "volcontrol", "2.5%+", NULL};
static const char *voldown[]  = { "volcontrol", "2.5%-", NULL};
static const char *volmute[]  = { "volcontrol", "toggle", NULL};

static Key keys[] = {
	/* modifier                     key        function                       argument */
	{ MODKEY,                       XK_r,      cmd_spawn,                     {.v = dmenucmd } },
	{ MODKEY|ShiftMask,             XK_Return, cmd_spawn,                     {.v = termcmd } },
	{ MODKEY,                       XK_d,      cmd_cycle_stackarea_selection, {.i = +1 } },
	{ MODKEY,                       XK_a,      cmd_cycle_stackarea_selection, {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_d,      cmd_push_client_right,         {0} },
	{ MODKEY|ShiftMask,             XK_a,      cmd_push_client_left,          {0} },
	{ MODKEY|ControlMask,           XK_d,      cmd_cycle_focus,               {.i = +1 } },
	{ MODKEY|ControlMask,           XK_a,      cmd_cycle_focus,               {.i = -1 } },
	{ MODKEY,                       XK_w,      cmd_cycle_view,		          {.i = +1 } },
	{ MODKEY,                       XK_s,      cmd_cycle_view,		          {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_w,      cmd_shift_tag,		          {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_s,      cmd_shift_tag,		          {.i = -1 } },
	{ MODKEY,                       XK_Tab,    cmd_view_tag,                  {0} }, /* show previous tagset */
	{ MODKEY,                       XK_0,      cmd_view_tag,                  {.ui = ~0 } }, /* show all tags */
	{ MODKEY|ShiftMask,             XK_0,      cmd_tag_client,                {.ui = ~0 } }, /* tag client on all tags */
	{ MODKEY,                       XK_e,      cmd_toggle_mark,               {0} },
	{ MODKEY|ShiftMask,             XK_h,      cmd_hide_window,               {0} },
	{ MODKEY|ShiftMask,             XK_space,  cmd_toggle_floating,           {0} },
	{ MODKEY,                       XK_f,      cmd_toggle_fullscreen,         {0} },
	{ MODKEY,                       XK_Escape, cmd_kill_client,               {0} },
	{ MODKEY,                       XK_Right,  cmd_adjust_marked_width,       {.f = +0.05} },
	{ MODKEY,                       XK_Left,   cmd_adjust_marked_width,       {.f = -0.05} },
	{ MODKEY,                       XK_z,      cmd_set_layout,                {.v = &layouts[0]} },
	{ MODKEY,                       XK_x,      cmd_set_layout,                {.v = &layouts[1]} },
	{ MODKEY,                       XK_c,      cmd_set_layout,                {.v = &layouts[2]} },
	{ MODKEY,                       XK_v,      cmd_set_layout,                {.v = &layouts[3]} },
	{ MODKEY,                       XK_space,  cmd_set_layout,                {0} },
	{ MODKEY,                       XK_comma,  cmd_cycle_focus_monitor,       {.i = -1 } },
	{ MODKEY,                       XK_period, cmd_cycle_focus_monitor,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,  cmd_send_to_monitor,           {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period, cmd_send_to_monitor,           {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_q,      cmd_quit,                      {0} },
	{ MODKEY,                       XK_t,      cmd_toggle_tagbar,             {0} },
	{ MODKEY|ShiftMask,             XK_t,      cmd_set_clientbar_mode,        {.ui = -1 } },
	{ MODKEY,                       XK_F8,     cmd_spawn,                     {.v = voldown } },
	{ MODKEY,                       XK_F9,     cmd_spawn,                     {.v = volup } },
	{ MODKEY,                       XK_F7,     cmd_spawn,                     {.v = volmute } },
	{ 0,                            SndUp,     cmd_spawn,                     {.v = volup } },
	{ 0,                            SndDown,   cmd_spawn,                     {.v = voldown } },
	{ 0,                            SndMute,   cmd_spawn,                     {.v = volmute } },
	TAGKEYS(                        XK_grave,  0)
	TAGKEYS(                        XK_1,      1)
	TAGKEYS(                        XK_2,      2)
	TAGKEYS(                        XK_3,      3)
	TAGKEYS(                        XK_4,      4)
	TAGKEYS(                        XK_5,      5)
	TAGKEYS(                        XK_6,      6)
	TAGKEYS(                        XK_7,      7)
	TAGKEYS(                        XK_8,      8)
};

/* button definitions */
/* click can be ClickLayoutSymbol, ClickStatusText, ClickWinTitle, ClickClientWin, or ClickRootWin */
static Button buttons[] = {
	/* click             event mask      button          function               argument */
	{ ClickLayoutSymbol, 0,              Button1,        cmd_set_layout,        {0} },
	{ ClickLayoutSymbol, 0,              Button3,        cmd_set_layout,        {.v = &layouts[2]} },
	{ ClickWinTitle,     0,              Button2,        cmd_toggle_mark,       {0} },
	{ ClickStatusText,   0,              Button2,        cmd_spawn,             {.v = termcmd } },
	{ ClickClientWin,    MODKEY,         Button1,        cmd_drag_window,       {0} },
	{ ClickClientWin,    MODKEY,         Button2,        cmd_toggle_floating,   {0} },
	{ ClickClientWin,    MODKEY,         Button3,        cmd_resize_with_mouse, {0} },
	{ ClickTagBar,       0,              Button1,        cmd_view_tag,          {0} },
	{ ClickTagBar,       0,              Button3,        cmd_toggle_tag_view,   {0} },
	{ ClickTagBar,       MODKEY,         Button1,        cmd_tag_client,        {0} },
	{ ClickTagBar,       MODKEY,         Button3,        cmd_toggle_tag,        {0} },
	{ ClickClientBar,    0,              Button1,        cmd_focus_client,      {0} },
	{ ClickWinTitle,     0,              Button2,        cmd_toggle_mark,       {0} },
	{ ClickClientBar,    0,              Button3,        cmd_toggle_hidden,     {0} },
};
