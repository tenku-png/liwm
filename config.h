/*
 * Main configuration entry point.
 * Settings are split into topic files under config/ and included below.
 */

/* Converts a 0xRRGGBBAA hex value into a float[4] RGBA initializer
 * (each channel 0.0 - 1.0), the format wlroots expects for colors. */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }

/* Number of tags (workspaces) per monitor. Must match the number of labels
 * in bartags[] (config_bar.h) and the TAGKEYS() bindings (config_keys.h). */
#define TAGCOUNT (9)

/* wlroots log verbosity: WLR_SILENT, WLR_ERROR, WLR_INFO or WLR_DEBUG. */
static int log_level = WLR_SILENT;

/* Split configuration files: */
#include "config/config_appearance.h"  /* borders, colors, wallpaper, spawn region */
#include "config/config_animation.h"  /* tag/open/close/zoom/canvas animations, inertia */
#include "config/config_bar.h"  /* status bar layout, modules, colors */
#include "config/config_input.h"  /* keyboard, touchpad, tablet settings */

/* Per-window rules, applied when a window is mapped. Every matching rule is
 * applied, in order.
 * Fields: { app_id substring (NULL = any), title substring (NULL = any),
 *           tag mask (0 = keep current tags), floating (1/0),
 *           monitor index (-1 = focused monitor) }
 * The single entry below matches every window and keeps the defaults. */
static const Rule rules[] = {
	{ NULL,               NULL,       0,            0,           -1 },
};

/* 1 = place newly opened windows in the center of the monitor's working
 * area; 0 = leave their initial position untouched. */
#define CENTER_NEW_WINDOWS 0

/* 1 = every new window waits until you drag out its rectangle on the canvas
 * (original lilith behaviour); 0 = windows open centered right away. */
#define SPAWN_REGION 0
/* 1 = new windows open centered on the mouse cursor (kept on screen),
 * 0 = centered on the monitor. */
#define SPAWN_AT_CURSOR 1
/* Size used for new windows that do not ask for one (SPAWN_REGION 0). */
#define DEFAULT_WIN_W 1000
#define DEFAULT_WIN_H 640

/* Gap in pixels around tiled (Super+Q) and maximized (Super+D) windows. */
#define TILE_GAP 8

/* 1 = plain left drag on a window border resizes it from that edge/corner. */
#define EDGE_RESIZE 1
/* Minimum grab zone in screen pixels (the border itself is used if wider). */
#define EDGE_RESIZE_MIN 6

/* Per-output rules, matched by output-name substring (e.g. "eDP-1").
 * Fields: { name (NULL = any output), scale, transform,
 *           x, y (-1 = automatic position),
 *           resx, resy (custom resolution, only used when mode is -1),
 *           refresh rate in Hz (0 = automatic, only used when mode is -1),
 *           mode (0 = automatic: listed mode with resx x resy, or the preferred
 *                 resolution when 0, at the refresh closest to the rate, or
 *                 the highest rate when 0; >0 = index into the mode list;
 *                 -1 = custom mode),
 *           adaptive sync (1 = enabled, 0 = disabled) }
 * The last entry applies to all other outputs: scale 1, no rotation,
 * automatic position, highest refresh rate, adaptive sync off.
 * DP-2: 3440x1440@180 ultrawide; DP-1: 1920x1080@170 rotated (portrait) on
 * its right, same layout as configs/swayfx/config in artix-setup. */
static const MonitorRule monrules[] = {
	{ "DP-2", 1, WL_OUTPUT_TRANSFORM_NORMAL,    0, 0, 0, 0, 180.0f, 0, 0 },
	{ "DP-1", 1, WL_OUTPUT_TRANSFORM_270,    3440, 0, 0, 0, 170.0f, 0, 0 },
	{ NULL, 1, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1, 0, 0, 0.0f, 0, 0 },
};

/* Key and mouse bindings. Included last because the tables refer to handler
 * functions declared in lilith.c. */
#include "config/config_keys.h"
