/*
 * Key and mouse bindings.
 * Binding format: { modifiers, key, function, argument }
 * Note: for keys, the first matching entry in keys[] wins and stops the search.
 *
 * Layout ported from the author's Hyprland config (keybinding.lua):
 * Super+Return/C = terminal, Super+Q/X = close, Super+arrows = focus,
 * Super+S = scratchpad tag, middle-drag moves windows, grim/slurp screenshots.
 */

/* Main modifier: Super (Logo) key. */
#define MODKEY WLR_MODIFIER_LOGO
#define ALT_MODKEY WLR_MODIFIER_ALT  /* secondary modifier: Alt */
#define SHIFT WLR_MODIFIER_SHIFT
#define CTRL  WLR_MODIFIER_CTRL

/* Step for keyboard move/resize (Hyprland used 20 px). */
#define WINMOVE_STEP_PX   20
#define WINRESIZE_STEP_PX 20

/* Generates the tag bindings for one number key:
 *   MODKEY + KEY                  view tag
 *   MODKEY + Ctrl + KEY           toggle tag visibility in the current view
 *   MODKEY + Shift + SKEY         move the focused window to the tag
 *   MODKEY + Ctrl + Shift + SKEY  toggle the tag on the focused window
 * KEY is the number key, SKEY its shifted symbol (US layout), TAG is 0-based. */
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|CTRL,               KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|SHIFT,              SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|CTRL|SHIFT,         SKEY,           toggletag,       {.ui = 1 << TAG} }

/* Helper: run a shell command string via /bin/sh -c, e.g. SHCMD("cmd | cmd"). */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* Tag used as a Hyprland-style special workspace (Super+S). Tag 9 = index 8. */
#define SCRATCHTAG (1 << 8)

/* Programs (from Hyprland programs.lua). footclient reuses one `foot --server`
 * process for every window; falls back to plain foot if no server runs. */
static const char *termcmd[]  = { "/bin/sh", "-c", "footclient 2>/dev/null || foot", NULL };
static const char *filecmd[]  = { "thunar", NULL };
static const char *browser[]  = { "zen-browser", NULL };
static const char *wlaunch[]  = { "wmenu-run", NULL };  /* application launcher */
static const char *btop[]     = { "foot", "btop", NULL };

/* Audio (wpctl, PipeWire). Volume capped at 150% like in Hyprland. */
static const char *volupcmd[]   = { "wpctl", "set-volume", "-l", "1.5", "@DEFAULT_AUDIO_SINK@", "2%+", NULL };
static const char *voldowncmd[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "2%-", NULL };
static const char *volmutecmd[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL };
static const char *micmutecmd[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle", NULL };

/* Keyboard bindings. */
static const Key keys[] = {
	/* --- Apps --- */
	{ MODKEY,                    XKB_KEY_Return,      spawn,            {.v = termcmd} },  /* Super+Return: terminal */
	{ MODKEY,                    XKB_KEY_c,           spawn,            {.v = termcmd} },  /* Super+C: terminal */
	{ CTRL|ALT_MODKEY,           XKB_KEY_t,           spawn,            {.v = termcmd} },  /* Ctrl+Alt+T: terminal */
	{ MODKEY,                    XKB_KEY_e,           spawn,            {.v = filecmd} },  /* Super+E: file manager */
	{ MODKEY,                    XKB_KEY_w,           spawn,            {.v = browser} },  /* Super+W: browser */
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = wlaunch} },  /* Super+R: launcher */
	{ MODKEY|SHIFT,              XKB_KEY_B,           spawn,            {.v = btop} },  /* Super+Shift+B: btop */
	{ 0,                         XKB_KEY_F1,          spawn,            SHCMD("python3 ~/.config/hypr/scripts/autoclicker.py start") },  /* F1: autoclicker start */
	{ 0,                         XKB_KEY_F2,          spawn,            SHCMD("python3 ~/.config/hypr/scripts/autoclicker.py stop") },  /* F2: autoclicker stop */

	/* --- Windows --- */
	{ MODKEY,                    XKB_KEY_q,           killclient,       {0} },  /* Super+Q: close */
	{ MODKEY,                    XKB_KEY_x,           killclient,       {0} },  /* Super+X: close */
	{ MODKEY,                    XKB_KEY_f,           togglefullscreen, {0} },  /* Super+F: fullscreen */
	{ MODKEY,                    XKB_KEY_d,           centerwindow,     {0} },  /* Super+D: center window (no maximize on a canvas) */
	{ MODKEY,                    XKB_KEY_p,           tag,              {.ui = ~0} },  /* Super+P: pin = show window on all tags */
	{ MODKEY,                    XKB_KEY_Tab,         focusstackcenter, {.i = +1} },  /* Super+Tab: next window, centered */

	/* Focus by direction (0 = up, 1 = left, 2 = down, 3 = right) */
	{ MODKEY,                    XKB_KEY_Up,          focusdir,         {.ui = 0} },
	{ MODKEY,                    XKB_KEY_Left,        focusdir,         {.ui = 1} },
	{ MODKEY,                    XKB_KEY_Down,        focusdir,         {.ui = 2} },
	{ MODKEY,                    XKB_KEY_Right,       focusdir,         {.ui = 3} },

	/* Move window (0 = left, 1 = right, 2 = up, 3 = down) */
	{ MODKEY|SHIFT,              XKB_KEY_Left,        movewinbykeys,    {.i = 0} },
	{ MODKEY|SHIFT,              XKB_KEY_Right,       movewinbykeys,    {.i = 1} },
	{ MODKEY|SHIFT,              XKB_KEY_Up,          movewinbykeys,    {.i = 2} },
	{ MODKEY|SHIFT,              XKB_KEY_Down,        movewinbykeys,    {.i = 3} },

	/* Resize window (0 = narrower, 1 = wider, 2 = shorter, 3 = taller) */
	{ MODKEY|ALT_MODKEY,         XKB_KEY_Left,        resizewinbykeys,  {.i = 0} },
	{ MODKEY|ALT_MODKEY,         XKB_KEY_Right,       resizewinbykeys,  {.i = 1} },
	{ MODKEY|ALT_MODKEY,         XKB_KEY_Up,          resizewinbykeys,  {.i = 2} },
	{ MODKEY|ALT_MODKEY,         XKB_KEY_Down,        resizewinbykeys,  {.i = 3} },

	/* --- Special workspace (scratchpad) on tag 9 --- */
	{ MODKEY,                    XKB_KEY_s,           toggleview,       {.ui = SCRATCHTAG} },  /* Super+S: show/hide scratchpad over the current tag */
	{ MODKEY|ALT_MODKEY,         XKB_KEY_s,           tag,              {.ui = SCRATCHTAG} },  /* Super+Alt+S: send window to scratchpad */

	/* --- Screenshots (grim + slurp + wl-clipboard) --- */
	{ MODKEY|SHIFT,              XKB_KEY_S,           spawn,            SHCMD("grim -g \"$(slurp)\" - | wl-copy") },  /* Super+Shift+S: region to clipboard */
	{ ALT_MODKEY,                XKB_KEY_c,           spawn,            SHCMD("mkdir -p ~/Pictures/Screenshots && grim -g \"$(slurp)\" ~/Pictures/Screenshots/$(date +%F_%H-%M-%S).png") },  /* Alt+C: region to file */

	/* --- Audio --- */
	{ MODKEY|SHIFT,              XKB_KEY_M,           barspawn,         {.v = volmutecmd} },  /* Super+Shift+M: mute output */
	{ MODKEY|ALT_MODKEY,         XKB_KEY_m,           barspawn,         {.v = micmutecmd} },  /* Super+Alt+M: mute mic */
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  barspawn, {.v = volupcmd} },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  barspawn, {.v = voldowncmd} },
	{ 0, XKB_KEY_XF86AudioMute,         barspawn, {.v = volmutecmd} },
	{ 0, XKB_KEY_XF86AudioMicMute,      barspawn, {.v = micmutecmd} },

	/* --- Session --- */
	{ MODKEY|SHIFT,              XKB_KEY_L,           spawn,            SHCMD("loginctl suspend || doas zzz") },  /* Super+Shift+L: sleep */
	{ CTRL|SHIFT|ALT_MODKEY|MODKEY, XKB_KEY_Delete,   spawn,            SHCMD("loginctl poweroff || doas poweroff") },  /* power off */
	{ MODKEY|SHIFT|ALT_MODKEY,   XKB_KEY_Q,           quit,             {0} },  /* Super+Shift+Alt+Q: quit lilith */
	{ CTRL|ALT_MODKEY,           XKB_KEY_Terminate_Server, quit,        {0} },  /* Ctrl+Alt+Backspace: quit lilith */

	/* --- Tags (workspaces) Super+1..9, Super+0 = all --- */
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                        0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                            1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                    2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                        3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                       4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                   5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                     6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                      7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                     8),
	{ MODKEY,                    XKB_KEY_0,           view,             {.ui = ~0} },  /* Super+0: show all tags */
	{ MODKEY|SHIFT,              XKB_KEY_parenright,  tag,              {.ui = ~0} },  /* Super+Shift+0: window on all tags */

	/* --- Monitors --- */
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFT,              XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFT,              XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },

	/* --- Canvas (lilith-specific) --- */
	{ MODKEY,                    XKB_KEY_z,           zoomfit,          {0} },  /* Super+Z: zoom out to fit all windows */
	{ MODKEY,                    XKB_KEY_equal,       zoom,             {.f = +0.25f} },  /* Super+=: zoom in */
	{ MODKEY,                    XKB_KEY_minus,       zoom,             {.f = -0.25f} },  /* Super+-: zoom out */
	{ MODKEY,                    XKB_KEY_Home,        homecanvas,       {0} },  /* Super+Home: canvas back to start */
	{ MODKEY|CTRL|SHIFT,         XKB_KEY_Left,        movecanvas,       {.i = 0} },  /* pan canvas */
	{ MODKEY|CTRL|SHIFT,         XKB_KEY_Right,       movecanvas,       {.i = 1} },
	{ MODKEY|CTRL|SHIFT,         XKB_KEY_Up,          movecanvas,       {.i = 2} },
	{ MODKEY|CTRL|SHIFT,         XKB_KEY_Down,        movecanvas,       {.i = 3} },

	/* --- Bar --- */
	{ MODKEY,                    XKB_KEY_b,           togglebar,        {0} },  /* Super+B: show/hide bar */
	{ MODKEY|CTRL,               XKB_KEY_b,           barcyclepos,      {.i = +1} },

/* Ctrl+Alt+F1..F12: switch to the virtual terminal n. */
#define CHVT(n) { CTRL|ALT_MODKEY,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

/* Mouse bindings. Format: { click target, modifiers, button, function, argument }
 * Targets: ClkClient = window, ClkRoot = empty canvas, ClkTagBar = tag buttons,
 * ClkStatus = status area of the bar. SCROLL_UP/SCROLL_DOWN are the scroll wheel. */
static const Button buttons[] = {
	{ ClkClient, 0,      BTN_MIDDLE, moveresize,     {.ui = CurMove} },  /* middle drag on a window: move it (Hyprland mouse:274) */
	{ ClkClient, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },  /* Super+left drag: move */
	{ ClkClient, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },  /* Super+right drag: resize */
	{ ClkClient, MODKEY|SHIFT, BTN_LEFT, movecanvasmouse, {.f = 1} },  /* Super+Shift+left drag: pan canvas */
	{ ClkRoot,   0,      BTN_LEFT,   movecanvasmouse, {.f = 1} },  /* left drag on empty canvas: pan */
	{ ClkRoot,   0,      BTN_MIDDLE, movecanvasmouse, {.f = 1} },  /* middle drag on empty canvas: pan */
	{ ClkRoot,   0,      BTN_RIGHT,  spawn,          {.v = termcmd} },  /* right click on empty canvas: terminal */
	{ ClkTagBar, 0,      BTN_LEFT,   view,           {0} },
	{ ClkTagBar, 0,      BTN_RIGHT,  toggleview,     {0} },
	{ ClkTagBar, MODKEY, BTN_LEFT,   tag,            {0} },
	{ ClkTagBar, MODKEY, BTN_RIGHT,  toggletag,      {0} },
	{ ClkStatus, 0,      BTN_MIDDLE, spawn,          {.v = termcmd} },
	{ ClkClient, MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },  /* Super+scroll: zoom */
	{ ClkClient, MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },
	{ ClkRoot,   MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },
	{ ClkRoot,   MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },
	{ ClkRoot,   0,      SCROLL_UP,   zoom, {.f = +0.25f} },  /* scroll on empty canvas: zoom */
	{ ClkRoot,   0,      SCROLL_DOWN, zoom, {.f = -0.25f} },
};
/* Graphics tablet pen buttons. Format: { button, state, function, argument }.
 * The single placeholder entry below binds nothing. */
static const TabletButton tabletbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};

/* Graphics tablet pad buttons, same format. Placeholder entry only; binds nothing. */
static const TabletPadButton tabletpadbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};
