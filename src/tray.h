/*
 * tray.h: system tray (StatusNotifierItem) for the lilith bar.
 *
 * lilith is the StatusNotifierWatcher and the host on the session bus.
 * Apps (Steam, Telegram, Discord, ...) register their items here; the bar
 * draws their icons and sends clicks back. Menus use com.canonical.dbusmenu.
 *
 * Everything is asynchronous on purpose: a blocking D-Bus call inside the
 * compositor would freeze the whole screen if an app hangs.
 *
 * Needs before including: <wayland-server-core.h>, stb_image (wallpaper.h).
 * The includer defines tray_changed() (redraw) and tray_menu_changed().
 */
#include <dbus/dbus.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#define TRAY_WATCHER     "org.kde.StatusNotifierWatcher"
#define TRAY_WATCHER_OBJ "/StatusNotifierWatcher"
#define TRAY_ITEM_IFACE  "org.kde.StatusNotifierItem"
#define TRAY_MENU_IFACE  "com.canonical.dbusmenu"
#define TRAY_PROPS       "org.freedesktop.DBus.Properties"

typedef struct TrayItem {
	struct wl_list link;
	char *service;     /* bus name the item was registered with */
	char *path;        /* object path */
	char *owner;       /* unique bus name (signals come from it) */
	char *title, *icon_name, *theme_path, *menu_path, *status;
	int item_is_menu;
	uint32_t *argb;    /* premultiplied ARGB32 icon, iw x ih */
	int iw, ih;
	int pending, refetch;
} TrayItem;

typedef struct {
	int32_t id;
	char label[96];
	int enabled, separator, toggle, checked, submenu, parent;
} TrayMenuItem;

static DBusConnection *tray_bus;
static struct wl_event_loop *tray_loop;
static struct wl_event_source *tray_idle;
static struct wl_list tray_items;
static int tray_ok, tray_watcher;
static int tray_icon_px = 18;  /* wanted icon size, set by the bar */

/* open menu (flat list, parent = index of the parent entry, -1 = top) */
static TrayMenuItem *tray_menu;
static int tray_menu_n, tray_menu_level = -1, tray_menu_serial;
static char *tray_menu_service, *tray_menu_path;

static void tray_changed(void);
static void tray_menu_changed(void);

/* ---- small helpers ---- */

static char *
tray_strdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

static void
tray_setstr(char **dst, const char *s)
{
	free(*dst);
	*dst = tray_strdup(s && *s ? s : NULL);
}

static TrayItem *
tray_find(const char *service, const char *path)
{
	TrayItem *it;
	wl_list_for_each(it, &tray_items, link)
		if (!strcmp(it->service, service) && !strcmp(it->path, path))
			return it;
	return NULL;
}

static void
tray_item_free(TrayItem *it)
{
	wl_list_remove(&it->link);
	free(it->service); free(it->path); free(it->owner);
	free(it->title); free(it->icon_name); free(it->theme_path);
	free(it->menu_path); free(it->status); free(it->argb);
	free(it);
}

static int
tray_count(void)
{
	return tray_ok ? wl_list_length(&tray_items) : 0;
}

/* ---- event loop integration (libdbus watches/timeouts on wl_event_loop) ---- */

static void
tray_dispatch(void)
{
	while (tray_bus && dbus_connection_dispatch(tray_bus) == DBUS_DISPATCH_DATA_REMAINS)
		;
}

static void
tray_idle_cb(void *data)
{
	tray_idle = NULL;
	tray_dispatch();
}

static void
tray_dispatch_status(DBusConnection *c, DBusDispatchStatus st, void *data)
{
	if (st == DBUS_DISPATCH_DATA_REMAINS && !tray_idle)
		tray_idle = wl_event_loop_add_idle(tray_loop, tray_idle_cb, NULL);
}

static uint32_t
tray_watch_mask(DBusWatch *w)
{
	unsigned int f = dbus_watch_get_flags(w);
	uint32_t m = 0;
	if (!dbus_watch_get_enabled(w))
		return 0;
	if (f & DBUS_WATCH_READABLE)
		m |= WL_EVENT_READABLE;
	if (f & DBUS_WATCH_WRITABLE)
		m |= WL_EVENT_WRITABLE;
	return m;
}

static int
tray_watch_cb(int fd, uint32_t mask, void *data)
{
	unsigned int f = 0;
	if (mask & WL_EVENT_READABLE) f |= DBUS_WATCH_READABLE;
	if (mask & WL_EVENT_WRITABLE) f |= DBUS_WATCH_WRITABLE;
	if (mask & WL_EVENT_HANGUP)   f |= DBUS_WATCH_HANGUP;
	if (mask & WL_EVENT_ERROR)    f |= DBUS_WATCH_ERROR;
	dbus_watch_handle(data, f);
	tray_dispatch();
	return 0;
}

static dbus_bool_t
tray_add_watch(DBusWatch *w, void *data)
{
	struct wl_event_source *src = wl_event_loop_add_fd(tray_loop,
			dbus_watch_get_unix_fd(w), tray_watch_mask(w), tray_watch_cb, w);
	if (!src)
		return FALSE;
	dbus_watch_set_data(w, src, NULL);
	return TRUE;
}

static void
tray_remove_watch(DBusWatch *w, void *data)
{
	struct wl_event_source *src = dbus_watch_get_data(w);
	if (src)
		wl_event_source_remove(src);
	dbus_watch_set_data(w, NULL, NULL);
}

static void
tray_toggle_watch(DBusWatch *w, void *data)
{
	struct wl_event_source *src = dbus_watch_get_data(w);
	if (src)
		wl_event_source_fd_update(src, tray_watch_mask(w));
}

static int
tray_timeout_cb(void *data)
{
	DBusTimeout *t = data;
	struct wl_event_source *src = dbus_timeout_get_data(t);
	/* libdbus timeouts repeat; re-arm first, handling may remove it */
	if (src)
		wl_event_source_timer_update(src, dbus_timeout_get_interval(t));
	dbus_timeout_handle(t);
	tray_dispatch();
	return 0;
}

static dbus_bool_t
tray_add_timeout(DBusTimeout *t, void *data)
{
	struct wl_event_source *src = wl_event_loop_add_timer(tray_loop, tray_timeout_cb, t);
	if (!src)
		return FALSE;
	dbus_timeout_set_data(t, src, NULL);
	if (dbus_timeout_get_enabled(t))
		wl_event_source_timer_update(src, dbus_timeout_get_interval(t));
	return TRUE;
}

static void
tray_remove_timeout(DBusTimeout *t, void *data)
{
	struct wl_event_source *src = dbus_timeout_get_data(t);
	if (src)
		wl_event_source_remove(src);
	dbus_timeout_set_data(t, NULL, NULL);
}

static void
tray_toggle_timeout(DBusTimeout *t, void *data)
{
	struct wl_event_source *src = dbus_timeout_get_data(t);
	if (src)
		wl_event_source_timer_update(src,
				dbus_timeout_get_enabled(t) ? dbus_timeout_get_interval(t) : 0);
}

/* ---- icons ---- */

static uint32_t *
tray_rgba_to_argb(const unsigned char *px, int w, int h)
{
	uint32_t *out = malloc((size_t)w * h * 4);
	int i;
	if (!out)
		return NULL;
	for (i = 0; i < w * h; i++) {
		unsigned int r = px[4*i], g = px[4*i+1], b = px[4*i+2], a = px[4*i+3];
		out[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
	}
	return out;
}

static int
tray_load_png(TrayItem *it, const char *path)
{
	int w, h, n;
	unsigned char *px;
	if (access(path, R_OK) != 0 || !(px = stbi_load(path, &w, &h, &n, 4)))
		return 0;
	free(it->argb);
	it->argb = tray_rgba_to_argb(px, w, h);
	it->iw = w;
	it->ih = h;
	stbi_image_free(px);
	return it->argb != NULL;
}

/* PNG icons only (stb_image): IconThemePath, hicolor, pixmaps */
static void
tray_load_icon_name(TrayItem *it)
{
	static const char *sizes[] = { "22x22", "24x24", "32x32", "16x16", "48x48",
		"64x64", "128x128", "256x256", "scalable" };
	static const char *dirs[] = { "apps", "status" };
	const char *name = it->icon_name, *home = getenv("HOME");
	char p[1024];
	size_t i, j;

	if (!name)
		return;
	if (name[0] == '/') {
		tray_load_png(it, name);
		return;
	}
	if (it->theme_path) {
		snprintf(p, sizeof(p), "%s/%s.png", it->theme_path, name);
		if (tray_load_png(it, p))
			return;
		for (i = 0; i < LENGTH(sizes); i++) for (j = 0; j < LENGTH(dirs); j++) {
			snprintf(p, sizeof(p), "%s/hicolor/%s/%s/%s.png", it->theme_path, sizes[i], dirs[j], name);
			if (tray_load_png(it, p))
				return;
		}
	}
	for (i = 0; i < LENGTH(sizes); i++) for (j = 0; j < LENGTH(dirs); j++) {
		if (home) {
			snprintf(p, sizeof(p), "%s/.local/share/icons/hicolor/%s/%s/%s.png", home, sizes[i], dirs[j], name);
			if (tray_load_png(it, p))
				return;
		}
		snprintf(p, sizeof(p), "/usr/share/icons/hicolor/%s/%s/%s.png", sizes[i], dirs[j], name);
		if (tray_load_png(it, p))
			return;
	}
	snprintf(p, sizeof(p), "/usr/share/pixmaps/%s.png", name);
	tray_load_png(it, p);
}

/* IconPixmap a(iiay): ARGB32 in network byte order; take the best size */
static void
tray_parse_pixmaps(TrayItem *it, DBusMessageIter *arr)
{
	DBusMessageIter s, bytes;
	const unsigned char *best = NULL;
	int bw = 0, bh = 0, i;

	while (dbus_message_iter_get_arg_type(arr) == DBUS_TYPE_STRUCT) {
		int32_t w = 0, h = 0;
		const unsigned char *data = NULL;
		int len = 0;
		dbus_message_iter_recurse(arr, &s);
		if (dbus_message_iter_get_arg_type(&s) == DBUS_TYPE_INT32)
			dbus_message_iter_get_basic(&s, &w);
		dbus_message_iter_next(&s);
		if (dbus_message_iter_get_arg_type(&s) == DBUS_TYPE_INT32)
			dbus_message_iter_get_basic(&s, &h);
		dbus_message_iter_next(&s);
		if (dbus_message_iter_get_arg_type(&s) == DBUS_TYPE_ARRAY) {
			dbus_message_iter_recurse(&s, &bytes);
			if (dbus_message_iter_get_arg_type(&bytes) == DBUS_TYPE_BYTE)
				dbus_message_iter_get_fixed_array(&bytes, &data, &len);
		}
		if (data && w > 0 && h > 0 && len >= w * h * 4) {
			/* smallest one that is at least the bar size, else the largest */
			int better = !best
				|| (bw < tray_icon_px && w > bw)
				|| (w >= tray_icon_px && w < bw);
			if (better) {
				best = data;
				bw = w;
				bh = h;
			}
		}
		dbus_message_iter_next(arr);
	}
	if (!best)
		return;
	free(it->argb);
	if (!(it->argb = malloc((size_t)bw * bh * 4)))
		return;
	for (i = 0; i < bw * bh; i++) {
		unsigned int a = best[4*i], r = best[4*i+1], g = best[4*i+2], b = best[4*i+3];
		it->argb[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
	}
	it->iw = bw;
	it->ih = bh;
}

/* ---- item properties ---- */

static void tray_fetch(TrayItem *it);

static void
tray_fetch_done(DBusPendingCall *pc, void *data)
{
	const char *key = data;  /* "service\npath" */
	const char *nl = strchr(key, '\n');
	char service[256];
	DBusMessage *r = dbus_pending_call_steal_reply(pc);
	DBusMessageIter it0, dict, entry, var;
	TrayItem *it;
	int pix = 0;

	dbus_pending_call_unref(pc);
	if (!nl || (size_t)(nl - key) >= sizeof(service)) {
		if (r) dbus_message_unref(r);
		return;
	}
	memcpy(service, key, (size_t)(nl - key));
	service[nl - key] = '\0';
	if (!(it = tray_find(service, nl + 1))) {
		if (r) dbus_message_unref(r);
		return;
	}
	it->pending = 0;
	if (!r || dbus_message_get_type(r) != DBUS_MESSAGE_TYPE_METHOD_RETURN
			|| !dbus_message_iter_init(r, &it0)
			|| dbus_message_iter_get_arg_type(&it0) != DBUS_TYPE_ARRAY) {
		if (r) dbus_message_unref(r);
		return;
	}
	dbus_message_iter_recurse(&it0, &dict);
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		const char *k = NULL, *sv = NULL;
		int t;
		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &k);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &var);
		t = dbus_message_iter_get_arg_type(&var);
		if (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH)
			dbus_message_iter_get_basic(&var, &sv);
		if (!strcmp(k, "Title"))
			tray_setstr(&it->title, sv);
		else if (!strcmp(k, "Status"))
			tray_setstr(&it->status, sv);
		else if (!strcmp(k, "IconName"))
			tray_setstr(&it->icon_name, sv);
		else if (!strcmp(k, "IconThemePath"))
			tray_setstr(&it->theme_path, sv);
		else if (!strcmp(k, "Menu"))
			tray_setstr(&it->menu_path, sv && strcmp(sv, "/") ? sv : NULL);
		else if (!strcmp(k, "ItemIsMenu") && t == DBUS_TYPE_BOOLEAN) {
			dbus_bool_t b = 0;
			dbus_message_iter_get_basic(&var, &b);
			it->item_is_menu = b;
		} else if (!strcmp(k, "IconPixmap") && t == DBUS_TYPE_ARRAY) {
			DBusMessageIter arr;
			dbus_message_iter_recurse(&var, &arr);
			if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRUCT) {
				tray_parse_pixmaps(it, &arr);
				pix = it->argb != NULL;
			}
		}
		dbus_message_iter_next(&dict);
	}
	dbus_message_unref(r);
	/* a named icon wins when it can be loaded; pixmap otherwise */
	if (it->icon_name) {
		uint32_t *keep = it->argb;
		int kw = it->iw, kh = it->ih;
		it->argb = NULL;
		tray_load_icon_name(it);
		if (it->argb)
			free(keep);
		else {
			it->argb = keep;
			it->iw = kw;
			it->ih = kh;
		}
	} else if (!pix) {
		free(it->argb);
		it->argb = NULL;
	}
	if (it->refetch) {
		it->refetch = 0;
		tray_fetch(it);
	}
	tray_changed();
}

static void
tray_fetch(TrayItem *it)
{
	DBusMessage *m;
	DBusPendingCall *pc = NULL;
	const char *iface = TRAY_ITEM_IFACE;
	char *key;

	if (it->pending) {
		it->refetch = 1;
		return;
	}
	if (!(m = dbus_message_new_method_call(it->service, it->path, TRAY_PROPS, "GetAll")))
		return;
	dbus_message_append_args(m, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID);
	if (dbus_connection_send_with_reply(tray_bus, m, &pc, 5000) && pc
			&& (key = malloc(strlen(it->service) + strlen(it->path) + 2))) {
		sprintf(key, "%s\n%s", it->service, it->path);
		it->pending = 1;
		dbus_pending_call_set_notify(pc, tray_fetch_done, key, free);
	}
	dbus_message_unref(m);
}

static void
tray_owner_done(DBusPendingCall *pc, void *data)
{
	const char *key = data, *nl = strchr(key, '\n'), *owner = NULL;
	DBusMessage *r = dbus_pending_call_steal_reply(pc);
	char service[256];
	TrayItem *it;

	dbus_pending_call_unref(pc);
	if (r && nl && (size_t)(nl - key) < sizeof(service)
			&& dbus_message_get_args(r, NULL, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID)) {
		memcpy(service, key, (size_t)(nl - key));
		service[nl - key] = '\0';
		if ((it = tray_find(service, nl + 1)))
			tray_setstr(&it->owner, owner);
	}
	if (r)
		dbus_message_unref(r);
}

static void
tray_emit_watcher(const char *signal, const char *arg)
{
	DBusMessage *s;
	if (!tray_watcher || !(s = dbus_message_new_signal(TRAY_WATCHER_OBJ, TRAY_WATCHER, signal)))
		return;
	if (arg)
		dbus_message_append_args(s, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID);
	dbus_connection_send(tray_bus, s, NULL);
	dbus_message_unref(s);
}

static void
tray_add(const char *service, const char *path, const char *sender)
{
	TrayItem *it;
	DBusMessage *m;
	DBusPendingCall *pc = NULL;
	char id[512], *key;

	if ((it = tray_find(service, path))) {
		tray_fetch(it);
		return;
	}
	if (!(it = calloc(1, sizeof(*it))))
		return;
	it->service = strdup(service);
	it->path = strdup(path);
	it->owner = tray_strdup(service[0] == ':' ? service : sender);
	wl_list_insert(tray_items.prev, &it->link);
	/* well-known names: learn the unique name the item's signals come from */
	if (service[0] != ':' && (m = dbus_message_new_method_call("org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner"))) {
		dbus_message_append_args(m, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
		if (dbus_connection_send_with_reply(tray_bus, m, &pc, 5000) && pc
				&& (key = malloc(strlen(service) + strlen(path) + 2))) {
			sprintf(key, "%s\n%s", service, path);
			dbus_pending_call_set_notify(pc, tray_owner_done, key, free);
		}
		dbus_message_unref(m);
	}
	snprintf(id, sizeof(id), "%s%s", service, path);
	tray_emit_watcher("StatusNotifierItemRegistered", id);
	tray_fetch(it);
	tray_changed();
}

static void
tray_remove_owner(const char *name)
{
	TrayItem *it, *tmp;
	char id[512];
	int changed = 0;

	wl_list_for_each_safe(it, tmp, &tray_items, link) {
		if (strcmp(it->service, name) && (!it->owner || strcmp(it->owner, name)))
			continue;
		snprintf(id, sizeof(id), "%s%s", it->service, it->path);
		tray_emit_watcher("StatusNotifierItemUnregistered", id);
		if (tray_menu_service && !strcmp(tray_menu_service, it->service)) {
			free(tray_menu); tray_menu = NULL; tray_menu_n = 0;
			tray_menu_changed();
		}
		tray_item_free(it);
		changed = 1;
	}
	if (changed)
		tray_changed();
}

/* ---- watcher object ---- */

static void
tray_append_items(DBusMessageIter *iter)
{
	DBusMessageIter arr;
	TrayItem *it;
	char id[512];
	const char *p = id;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &arr);
	wl_list_for_each(it, &tray_items, link) {
		snprintf(id, sizeof(id), "%s%s", it->service, it->path);
		dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &p);
	}
	dbus_message_iter_close_container(iter, &arr);
}

static void
tray_append_prop(DBusMessageIter *iter, const char *name)
{
	DBusMessageIter v;
	if (!strcmp(name, "RegisteredStatusNotifierItems")) {
		dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as", &v);
		tray_append_items(&v);
	} else if (!strcmp(name, "IsStatusNotifierHostRegistered")) {
		dbus_bool_t b = TRUE;
		dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &v);
		dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
	} else {
		int32_t ver = 0;
		dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "i", &v);
		dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &ver);
	}
	dbus_message_iter_close_container(iter, &v);
}

static const char tray_introspect_xml[] =
	DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE
	"<node><interface name=\"" TRAY_WATCHER "\">"
	"<method name=\"RegisterStatusNotifierItem\"><arg type=\"s\" direction=\"in\"/></method>"
	"<method name=\"RegisterStatusNotifierHost\"><arg type=\"s\" direction=\"in\"/></method>"
	"<property name=\"RegisteredStatusNotifierItems\" type=\"as\" access=\"read\"/>"
	"<property name=\"IsStatusNotifierHostRegistered\" type=\"b\" access=\"read\"/>"
	"<property name=\"ProtocolVersion\" type=\"i\" access=\"read\"/>"
	"<signal name=\"StatusNotifierItemRegistered\"><arg type=\"s\"/></signal>"
	"<signal name=\"StatusNotifierItemUnregistered\"><arg type=\"s\"/></signal>"
	"<signal name=\"StatusNotifierHostRegistered\"/>"
	"</interface>"
	"<interface name=\"org.freedesktop.DBus.Introspectable\">"
	"<method name=\"Introspect\"><arg type=\"s\" direction=\"out\"/></method></interface>"
	"<interface name=\"" TRAY_PROPS "\">"
	"<method name=\"Get\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
	"<arg type=\"v\" direction=\"out\"/></method>"
	"<method name=\"GetAll\"><arg type=\"s\" direction=\"in\"/>"
	"<arg type=\"a{sv}\" direction=\"out\"/></method></interface></node>";

static DBusHandlerResult
tray_watcher_msg(DBusConnection *c, DBusMessage *msg, void *data)
{
	DBusMessage *r = NULL;
	DBusMessageIter it;
	const char *arg = NULL, *arg2 = NULL;

	if (dbus_message_is_method_call(msg, TRAY_WATCHER, "RegisterStatusNotifierItem")) {
		const char *sender = dbus_message_get_sender(msg);
		if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_INVALID) || !sender)
			r = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, "need a service or path");
		else {
			if (arg[0] == '/')
				tray_add(sender, arg, sender);
			else
				tray_add(arg, "/StatusNotifierItem", sender);
			r = dbus_message_new_method_return(msg);
		}
	} else if (dbus_message_is_method_call(msg, TRAY_WATCHER, "RegisterStatusNotifierHost")) {
		r = dbus_message_new_method_return(msg);
	} else if (dbus_message_is_method_call(msg, TRAY_PROPS, "Get")) {
		if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg, DBUS_TYPE_STRING, &arg2, DBUS_TYPE_INVALID)
				&& (r = dbus_message_new_method_return(msg))) {
			dbus_message_iter_init_append(r, &it);
			tray_append_prop(&it, arg2);
		}
	} else if (dbus_message_is_method_call(msg, TRAY_PROPS, "GetAll")) {
		static const char *props[] = { "RegisteredStatusNotifierItems",
			"IsStatusNotifierHostRegistered", "ProtocolVersion" };
		DBusMessageIter arr, e;
		size_t i;
		if ((r = dbus_message_new_method_return(msg))) {
			dbus_message_iter_init_append(r, &it);
			dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
			for (i = 0; i < LENGTH(props); i++) {
				dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &e);
				dbus_message_iter_append_basic(&e, DBUS_TYPE_STRING, &props[i]);
				tray_append_prop(&e, props[i]);
				dbus_message_iter_close_container(&arr, &e);
			}
			dbus_message_iter_close_container(&it, &arr);
		}
	} else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
		const char *xml = tray_introspect_xml;
		if ((r = dbus_message_new_method_return(msg)))
			dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
	} else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (r) {
		dbus_connection_send(c, r, NULL);
		dbus_message_unref(r);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

/* ---- signals ---- */

static DBusHandlerResult
tray_filter(DBusConnection *c, DBusMessage *msg, void *data)
{
	if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged")) {
		const char *name = NULL, *old = NULL, *new = NULL;
		if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
				DBUS_TYPE_STRING, &old, DBUS_TYPE_STRING, &new, DBUS_TYPE_INVALID)
				&& !new[0])
			tray_remove_owner(name);
	} else if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_SIGNAL
			&& dbus_message_has_interface(msg, TRAY_ITEM_IFACE)) {
		const char *sender = dbus_message_get_sender(msg), *path = dbus_message_get_path(msg);
		TrayItem *it;
		wl_list_for_each(it, &tray_items, link)
			if (sender && path && !strcmp(it->path, path)
					&& (!strcmp(it->service, sender) || (it->owner && !strcmp(it->owner, sender))))
				tray_fetch(it);
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* ---- actions ---- */

static TrayItem *
tray_nth(int n)
{
	TrayItem *it;
	int i = 0;
	wl_list_for_each(it, &tray_items, link)
		if (i++ == n)
			return it;
	return NULL;
}

static void
tray_send(const char *service, const char *path, const char *iface,
		const char *method, int first_type, ...)
{
	DBusMessage *m;
	va_list ap;

	if (!tray_bus || !(m = dbus_message_new_method_call(service, path, iface, method)))
		return;
	va_start(ap, first_type);
	if (first_type != DBUS_TYPE_INVALID)
		dbus_message_append_args_valist(m, first_type, ap);
	va_end(ap);
	dbus_message_set_no_reply(m, TRUE);
	dbus_connection_send(tray_bus, m, NULL);
	dbus_message_unref(m);
}

/* x, y: screen position for the app's own popup (if it makes one) */
static void
tray_activate(int n, int secondary, int x, int y)
{
	TrayItem *it = tray_nth(n);
	int32_t px = x, py = y;
	if (it)
		tray_send(it->service, it->path, TRAY_ITEM_IFACE,
				secondary ? "SecondaryActivate" : "Activate",
				DBUS_TYPE_INT32, &px, DBUS_TYPE_INT32, &py, DBUS_TYPE_INVALID);
}

static void
tray_scroll(int n, int delta)
{
	TrayItem *it = tray_nth(n);
	int32_t d = delta;
	const char *orient = "vertical";
	if (it)
		tray_send(it->service, it->path, TRAY_ITEM_IFACE, "Scroll",
				DBUS_TYPE_INT32, &d, DBUS_TYPE_STRING, &orient, DBUS_TYPE_INVALID);
}

/* ---- dbusmenu ---- */

static void
tray_menu_close(void)
{
	free(tray_menu);
	tray_menu = NULL;
	tray_menu_n = 0;
	tray_menu_level = -1;
	tray_menu_changed();
}

/* (ia{sv}av) */
static void
tray_menu_parse(DBusMessageIter *node, int parent, int depth)
{
	DBusMessageIter s, props, e, v, kids, kv;
	TrayMenuItem mi = { .enabled = 1, .parent = parent };
	int visible = 1, self, nkids = 0;

	if (depth > 8 || dbus_message_iter_get_arg_type(node) != DBUS_TYPE_STRUCT)
		return;
	dbus_message_iter_recurse(node, &s);
	if (dbus_message_iter_get_arg_type(&s) != DBUS_TYPE_INT32)
		return;
	dbus_message_iter_get_basic(&s, &mi.id);
	dbus_message_iter_next(&s);
	dbus_message_iter_recurse(&s, &props);
	while (dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
		const char *k = NULL, *sv = NULL;
		int t;
		dbus_message_iter_recurse(&props, &e);
		dbus_message_iter_get_basic(&e, &k);
		dbus_message_iter_next(&e);
		dbus_message_iter_recurse(&e, &v);
		t = dbus_message_iter_get_arg_type(&v);
		if (t == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&v, &sv);
		if (!strcmp(k, "label") && sv) {
			/* drop the "_" mnemonic markers ("__" = literal "_") */
			size_t o = 0;
			for (; *sv && o + 1 < sizeof(mi.label); sv++) {
				if (*sv == '_' && sv[1] != '_')
					continue;
				if (*sv == '_')
					sv++;
				mi.label[o++] = *sv;
			}
			mi.label[o] = '\0';
		} else if (!strcmp(k, "type") && sv && !strcmp(sv, "separator")) {
			mi.separator = 1;
		} else if (!strcmp(k, "toggle-type") && sv && *sv) {
			mi.toggle = 1;
		} else if (!strcmp(k, "children-display") && sv && !strcmp(sv, "submenu")) {
			mi.submenu = 1;
		} else if ((!strcmp(k, "enabled") || !strcmp(k, "visible")) && t == DBUS_TYPE_BOOLEAN) {
			dbus_bool_t b = 1;
			dbus_message_iter_get_basic(&v, &b);
			if (!strcmp(k, "enabled"))
				mi.enabled = b;
			else
				visible = b;
		} else if (!strcmp(k, "toggle-state") && t == DBUS_TYPE_INT32) {
			int32_t st = 0;
			dbus_message_iter_get_basic(&v, &st);
			mi.checked = st == 1;
		}
		dbus_message_iter_next(&props);
	}
	if (!visible)
		return;
	self = parent;
	if (depth > 0) {  /* the root (id 0) itself is not shown */
		TrayMenuItem *n = realloc(tray_menu, (size_t)(tray_menu_n + 1) * sizeof(*n));
		if (!n)
			return;
		tray_menu = n;
		self = tray_menu_n;
		tray_menu[tray_menu_n++] = mi;
	}
	dbus_message_iter_next(&s);
	if (dbus_message_iter_get_arg_type(&s) != DBUS_TYPE_ARRAY)
		return;
	dbus_message_iter_recurse(&s, &kids);
	while (dbus_message_iter_get_arg_type(&kids) == DBUS_TYPE_VARIANT) {
		dbus_message_iter_recurse(&kids, &kv);
		tray_menu_parse(&kv, self, depth + 1);
		nkids++;
		dbus_message_iter_next(&kids);
	}
	if (depth > 0 && nkids)
		tray_menu[self].submenu = 1;
}

static void
tray_menu_done(DBusPendingCall *pc, void *data)
{
	DBusMessage *r = dbus_pending_call_steal_reply(pc);
	DBusMessageIter it;
	int serial = (int)(intptr_t)data;

	dbus_pending_call_unref(pc);
	if (serial != tray_menu_serial || !r
			|| dbus_message_get_type(r) != DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		if (r) dbus_message_unref(r);
		return;
	}
	free(tray_menu);
	tray_menu = NULL;
	tray_menu_n = 0;
	tray_menu_level = -1;
	if (dbus_message_iter_init(r, &it)) {
		dbus_message_iter_next(&it);  /* skip the revision */
		tray_menu_parse(&it, -1, 0);
	}
	dbus_message_unref(r);
	tray_menu_changed();
}

/* returns 0 if the item has no dbusmenu (then the caller may fall back) */
static int
tray_menu_open(int n)
{
	TrayItem *it = tray_nth(n);
	DBusMessage *m;
	DBusPendingCall *pc = NULL;
	int32_t parent = 0, depth = -1;
	const char *none[] = { NULL };
	const char **pnone = none;

	if (!it || !it->menu_path)
		return 0;
	tray_setstr(&tray_menu_service, it->service);
	tray_setstr(&tray_menu_path, it->menu_path);
	tray_send(it->service, it->menu_path, TRAY_MENU_IFACE, "AboutToShow",
			DBUS_TYPE_INT32, &parent, DBUS_TYPE_INVALID);
	if (!(m = dbus_message_new_method_call(it->service, it->menu_path, TRAY_MENU_IFACE, "GetLayout")))
		return 0;
	dbus_message_append_args(m, DBUS_TYPE_INT32, &parent, DBUS_TYPE_INT32, &depth,
			DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &pnone, 0, DBUS_TYPE_INVALID);
	tray_menu_serial++;
	if (dbus_connection_send_with_reply(tray_bus, m, &pc, 5000) && pc)
		dbus_pending_call_set_notify(pc, tray_menu_done,
				(void *)(intptr_t)tray_menu_serial, NULL);
	dbus_message_unref(m);
	return 1;
}

/* the app's own context menu (items without dbusmenu) */
static void
tray_context_menu(int n, int x, int y)
{
	TrayItem *it = tray_nth(n);
	int32_t px = x, py = y;
	if (it)
		tray_send(it->service, it->path, TRAY_ITEM_IFACE, "ContextMenu",
				DBUS_TYPE_INT32, &px, DBUS_TYPE_INT32, &py, DBUS_TYPE_INVALID);
}

/* click on menu entry i (index into tray_menu) */
static void
tray_menu_activate(int i)
{
	DBusMessage *m;
	DBusMessageIter it, v;
	int32_t id, zero = 0;
	uint32_t ts;
	const char *ev = "clicked";

	if (i < 0 || i >= tray_menu_n || tray_menu[i].separator || !tray_menu[i].enabled)
		return;
	if (tray_menu[i].submenu) {
		tray_menu_level = i;
		tray_menu_changed();
		return;
	}
	id = tray_menu[i].id;
	ts = (uint32_t)time(NULL);
	if (tray_menu_service && tray_menu_path
			&& (m = dbus_message_new_method_call(tray_menu_service, tray_menu_path,
					TRAY_MENU_IFACE, "Event"))) {
		dbus_message_iter_init_append(m, &it);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &id);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &ev);
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "i", &v);
		dbus_message_iter_append_basic(&v, DBUS_TYPE_INT32, &zero);
		dbus_message_iter_close_container(&it, &v);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &ts);
		dbus_message_set_no_reply(m, TRUE);
		dbus_connection_send(tray_bus, m, NULL);
		dbus_message_unref(m);
	}
	tray_menu_close();
}

/* ---- setup ---- */

static void
tray_init(struct wl_event_loop *loop)
{
	static const DBusObjectPathVTable vt = { .message_function = tray_watcher_msg };
	DBusError err;
	int ret;

	wl_list_init(&tray_items);
	tray_loop = loop;
	/* never autolaunch a bus from inside the compositor */
	if (!getenv("DBUS_SESSION_BUS_ADDRESS"))
		return;
	dbus_error_init(&err);
	if (!(tray_bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err))) {
		fprintf(stderr, "lilith: tray: no session bus: %s\n", err.message);
		dbus_error_free(&err);
		return;
	}
	dbus_connection_set_exit_on_disconnect(tray_bus, FALSE);
	dbus_connection_set_watch_functions(tray_bus, tray_add_watch, tray_remove_watch,
			tray_toggle_watch, NULL, NULL);
	dbus_connection_set_timeout_functions(tray_bus, tray_add_timeout, tray_remove_timeout,
			tray_toggle_timeout, NULL, NULL);
	dbus_connection_set_dispatch_status_function(tray_bus, tray_dispatch_status, NULL, NULL);
	dbus_connection_add_filter(tray_bus, tray_filter, NULL, NULL);
	dbus_bus_add_match(tray_bus, "type='signal',sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',member='NameOwnerChanged'", NULL);
	dbus_bus_add_match(tray_bus, "type='signal',interface='" TRAY_ITEM_IFACE "'", NULL);
	dbus_connection_register_object_path(tray_bus, TRAY_WATCHER_OBJ, &vt, NULL);
	ret = dbus_bus_request_name(tray_bus, TRAY_WATCHER, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
	if (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		tray_watcher = 1;
		tray_emit_watcher("StatusNotifierHostRegistered", NULL);
	} else
		fprintf(stderr, "lilith: tray: another StatusNotifierWatcher runs, tray stays empty\n");
	dbus_error_free(&err);
	tray_ok = 1;
	tray_dispatch();
}

static void
tray_cleanup(void)
{
	TrayItem *it, *tmp;
	if (tray_ok)
		wl_list_for_each_safe(it, tmp, &tray_items, link)
			tray_item_free(it);
	free(tray_menu);
	tray_menu = NULL;
	if (tray_idle)
		wl_event_source_remove(tray_idle);
	tray_idle = NULL;
	if (tray_bus) {
		dbus_connection_close(tray_bus);
		dbus_connection_unref(tray_bus);
	}
	tray_bus = NULL;
	tray_ok = 0;
}
