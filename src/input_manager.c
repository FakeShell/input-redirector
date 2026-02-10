/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "input_manager.h"
#include "xdo_simulate.h"
#include "wayland_vinput.h"

#include <fcntl.h>

#define MAX_DEVICES 16

typedef struct {
    pthread_t   thread;
    gchar      *path;
    int         fd;
} DeviceEntry;

typedef struct {
    int max_x, max_y;

    int rel_x, rel_y;
    gboolean has_motion;

    int abs_x, abs_y;
    gboolean has_abs;

    gboolean touch_active;
    gboolean moved_during_touch;
    gboolean first;
} DeviceThreadState;

static DeviceEntry devices[MAX_DEVICES];
static int device_count = 0;
static volatile gboolean running = FALSE;
static GMutex fds_mutex;
static gboolean fds_mutex_initialized = FALSE;

typedef enum {
    BACKEND_X11 = 0,
    BACKEND_WAYLAND = 1,
} InputBackend;

static GMutex backend_mutex;
static gboolean backend_mutex_initialized = FALSE;

static gboolean g_wayland_enabled = TRUE;
static gchar *g_wayland_display = NULL;
static InputBackend current_backend = BACKEND_X11;
static gboolean backend_initialized = FALSE;

static void
backend_mutex_ensure_initialized(void)
{
    if (!backend_mutex_initialized) {
        g_mutex_init(&backend_mutex);
        backend_mutex_initialized = TRUE;
    }
}

static InputBackend
get_wanted_backend(void)
{
    return g_wayland_enabled ? BACKEND_WAYLAND : BACKEND_X11;
}

static void
backend_cleanup(void)
{
    if (!backend_initialized)
        return;

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_cleanup();
    else
        xdo_cleanup();

    backend_initialized = FALSE;
}

static void
backend_init(InputBackend backend)
{
    if (backend == BACKEND_WAYLAND)
        wayland_vinput_init(g_wayland_display);
    else
        xdo_init();

    current_backend = backend;
    backend_initialized = TRUE;
}

static void
backend_ensure_ready(void)
{
    backend_mutex_ensure_initialized();

    g_mutex_lock(&backend_mutex);

    InputBackend want = get_wanted_backend();

    if (!backend_initialized) {
        backend_init(want);
    } else if (want != current_backend) {
        g_debug("input_manager: backend changed, reinitializing");
        backend_cleanup();
        backend_init(want);
    }

    g_mutex_unlock(&backend_mutex);
}

static void
input_simulate_key_event(int code, int value, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_key_event(code, value, thread_name);
    else
        xdo_simulate_key_event(code, value, thread_name);
}

static void
input_simulate_mouse_button(int code, int value, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_mouse_button(code, value, thread_name);
    else
        xdo_simulate_mouse_button(code, value, thread_name);
}

static void
input_simulate_mouse_motion(int rel_x, int rel_y, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_mouse_motion(rel_x, rel_y, thread_name);
    else
        xdo_simulate_mouse_motion(rel_x, rel_y, thread_name);
}

static void
input_simulate_scroll(int code, int value, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_scroll(code, value, thread_name);
    else
        xdo_simulate_scroll(code, value, thread_name);
}

static void
input_simulate_touch_move(int x, int y, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_touch_move(x, y, thread_name);
    else
        xdo_simulate_touch_move(x, y, thread_name);
}

static void
input_simulate_touch_down(int x, int y, const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_touch_down(x, y, thread_name);
    else
        xdo_simulate_touch_down(x, y, thread_name);
}

static void
input_simulate_touch_up(const char *thread_name)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_touch_up(thread_name);
    else
        xdo_simulate_touch_up(thread_name);
}

static void
input_get_screen_size(unsigned int *w, unsigned int *h)
{
    backend_ensure_ready();

    if (current_backend == BACKEND_WAYLAND)
        wayland_vinput_get_screen_size(w, h);
    else
        xdo_get_screen_size(w, h);
}

static int
grab_device(const gchar *dev_path)
{
    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        g_debug("grab_device: open(%s) failed: %s", dev_path, g_strerror(errno));
        return -1;
    }
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        g_debug("grab_device: EVIOCGRAB(%s) failed: %s", dev_path, g_strerror(errno));
        close(fd);
        return -1;
    }

    g_mutex_lock(&fds_mutex);
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].fd < 0) {
            devices[i].fd = fd;
            break;
        }
    }
    g_mutex_unlock(&fds_mutex);

    g_debug("grab_device: grabbed %s (fd=%d)", dev_path, fd);
    return fd;
}

static void
ungrab_device(DeviceEntry *entry)
{
    if (entry->fd >= 0) {
        ioctl(entry->fd, EVIOCGRAB, 0);
        close(entry->fd);
        g_debug("ungrab_device: ungrabbed %s (fd=%d)", entry->path, entry->fd);
        entry->fd = -1;
    }
}

static void
cleanup_handler(void *arg)
{
    DeviceEntry *entry = arg;
    ungrab_device(entry);
}

static void
device_state_init(DeviceThreadState *st)
{
    st->max_x = 0;
    st->max_y = 0;

    st->rel_x = 0;
    st->rel_y = 0;
    st->has_motion = FALSE;

    st->abs_x = 0;
    st->abs_y = 0;
    st->has_abs = FALSE;

    st->touch_active = FALSE;
    st->moved_during_touch = FALSE;
    st->first = TRUE;
}

static void
device_read_abs_ranges(DeviceEntry *entry, DeviceThreadState *st)
{
    struct input_absinfo info;

    if (ioctl(entry->fd, EVIOCGABS(ABS_X), &info) == 0)
        st->max_x = info.maximum;
    if (ioctl(entry->fd, EVIOCGABS(ABS_Y), &info) == 0)
        st->max_y = info.maximum;
}

static void
device_ev_key(DeviceEntry *entry, DeviceThreadState *st, const struct input_event *ev)
{
    if (ev->code == BTN_TOUCH) {
        /* Touch down */
        if (ev->value == 1) {
            st->touch_active = TRUE;
            st->moved_during_touch = FALSE;
            st->first = TRUE;
        } else { /* Touch up */
            st->touch_active = FALSE;
            /* no movement: treat as click */
            if (!st->moved_during_touch) {
                /* inject a left-button click */
                input_simulate_mouse_button(BTN_LEFT, 1, entry->path);
                input_simulate_mouse_button(BTN_LEFT, 0, entry->path);
            } else {
                /* end of drag: lift */
                input_simulate_touch_up(entry->path);
            }
        }
        return;
    }

    /* ignore tool events */
    if (ev->code == BTN_TOOL_FINGER)
        return;

    if (ev->code >= BTN_MOUSE && ev->code <= BTN_TASK) {
        input_simulate_mouse_button(ev->code, ev->value, entry->path);
        return;
    }

    input_simulate_key_event(ev->code, ev->value, entry->path);
}

static void
device_ev_rel(DeviceEntry *entry, DeviceThreadState *st, const struct input_event *ev)
{
    if (ev->code == REL_X) {
        st->rel_x += ev->value;
        st->has_motion = TRUE;
        if (st->touch_active)
            st->moved_during_touch = TRUE;
        return;
    }

    if (ev->code == REL_Y) {
        st->rel_y += ev->value;
        st->has_motion = TRUE;
        if (st->touch_active)
            st->moved_during_touch = TRUE;
        return;
    }

    input_simulate_scroll(ev->code, ev->value, entry->path);
}

static void
device_ev_abs(DeviceEntry *entry, DeviceThreadState *st, const struct input_event *ev)
{
    (void) entry;

    if (ev->code == ABS_X) {
        st->abs_x = ev->value;
        st->has_abs = TRUE;
        if (st->touch_active)
            st->moved_during_touch = TRUE;
        return;
    }

    if (ev->code == ABS_Y) {
        st->abs_y = ev->value;
        st->has_abs = TRUE;
        if (st->touch_active)
            st->moved_during_touch = TRUE;
        return;
    }
}

static void
device_ev_syn(DeviceEntry *entry, DeviceThreadState *st)
{
    if (st->has_motion) {
        input_simulate_mouse_motion(st->rel_x, st->rel_y, entry->path);
        st->rel_x = 0;
        st->rel_y = 0;
        st->has_motion = FALSE;
    }

    if (st->has_abs && st->max_x > 0 && st->max_y > 0 && st->touch_active) {
        unsigned int w = 0, h = 0;
        input_get_screen_size(&w, &h);
        int sx = (w > 0) ? (st->abs_x * (int) w) / st->max_x : 0;
        int sy = (h > 0) ? (st->abs_y * (int) h) / st->max_y : 0;

        /* first ABS after touch down: send down at the right pos */
        if (st->first) {
            input_simulate_touch_down(sx, sy, entry->path);
            st->first = FALSE;
        } else {
            input_simulate_touch_move(sx, sy, entry->path);
        }

        st->has_abs = FALSE;
    }
}

static void *
device_thread(void *data)
{
    DeviceEntry *entry = data;
    struct input_event ev;
    DeviceThreadState st;

    device_state_init(&st);

    entry->fd = grab_device(entry->path);
    if (entry->fd < 0)
        return NULL;

    /* allow read() to be a cancellation point */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* register cleanup */
    pthread_cleanup_push(cleanup_handler, entry);

    /* get ABS ranges */
    device_read_abs_ranges(entry, &st);

    while (running) {
        /* block here until event or pthread_cancel() */
        if (read(entry->fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        switch (ev.type) {
        case EV_KEY:
            device_ev_key(entry, &st, &ev);
            break;
        case EV_REL:
            device_ev_rel(entry, &st, &ev);
            break;
        case EV_ABS:
            device_ev_abs(entry, &st, &ev);
            break;
        case EV_SYN:
            device_ev_syn(entry, &st);
            break;
        default:
            break;
        }
    }

    /* invoke cleanup handler */
    pthread_cleanup_pop(1);
    return NULL;
}

void
input_manager_set_wayland_enabled(gboolean enabled)
{
    backend_mutex_ensure_initialized();

    g_mutex_lock(&backend_mutex);
    g_wayland_enabled = enabled ? TRUE : FALSE;
    g_mutex_unlock(&backend_mutex);

    /* next simulation call or update() will reinit backend if needed */
    g_debug("input_manager: enable-wayland set to %s", g_wayland_enabled ? "true" : "false");
}

void
input_manager_set_wayland_display(const gchar *wayland_display)
{
    backend_mutex_ensure_initialized();

    g_mutex_lock(&backend_mutex);
    g_free(g_wayland_display);
    g_wayland_display = (wayland_display && *wayland_display) ? g_strdup(wayland_display) : NULL;
    g_mutex_unlock(&backend_mutex);

    g_debug("input_manager: wayland-display set to %s", g_wayland_display ? g_wayland_display : "(unset)");
}

void
input_manager_update(const gchar *paths)
{
    if (!fds_mutex_initialized) {
        g_mutex_init(&fds_mutex);
        fds_mutex_initialized = TRUE;
    }

    input_manager_stop();
    backend_ensure_ready();

    /* reset state */
    for (int i = 0; i < MAX_DEVICES; i++) {
        devices[i].path = NULL;
        devices[i].fd   = -1;
    }
    device_count = 0;
    running = TRUE;

    gchar **list = g_strsplit(paths, ",", -1);
    for (int i = 0; list[i] && i < MAX_DEVICES; i++) {
        gchar *p = g_strstrip(list[i]);
        devices[i].path = g_strdup(p);
        pthread_create(&devices[i].thread, NULL, device_thread, &devices[i]);
        device_count++;
    }
    g_strfreev(list);
}

void
input_manager_stop(void)
{
    if (!running)
        return;

    g_debug("Stopping all threads");
    running = FALSE;

    for (int i = 0; i < device_count; i++) {
        pthread_cancel(devices[i].thread);
        pthread_join(devices[i].thread, NULL);
        g_free(devices[i].path);
    }

    device_count = 0;
    g_debug("All threads joined and devices released");
}

void
input_manager_inject_key_event(guint32      code,
                               gint32       value,
                               const gchar *source)
{
    input_simulate_key_event((int) code, (int) value, source ? source : "dbus");
}

void
input_manager_inject_mouse_button(guint32      code,
                                  gint32       value,
                                  const gchar *source)
{
    input_simulate_mouse_button((int) code, (int) value, source ? source : "dbus");
}

void
input_manager_inject_mouse_motion(gint32       dx,
                                  gint32       dy,
                                  const gchar *source)
{
    input_simulate_mouse_motion((int) dx, (int) dy, source ? source : "dbus");
}

void
input_manager_inject_scroll(guint32      code,
                            gint32       value,
                            const gchar *source)
{
    input_simulate_scroll((int) code, (int) value, source ? source : "dbus");
}

void
input_manager_inject_touch_down(gint32       x,
                                gint32       y,
                                const gchar *source)
{
    input_simulate_touch_down((int) x, (int) y, source ? source : "dbus");
}

void
input_manager_inject_touch_move(gint32       x,
                                gint32       y,
                                const gchar *source)
{
    input_simulate_touch_move((int) x, (int) y, source ? source : "dbus");
}

void
input_manager_inject_touch_up(const gchar *source)
{
    input_simulate_touch_up(source ? source : "dbus");
}

void
input_manager_get_screen_size(guint *width,
                              guint *height)
{
    unsigned int w = 0, h = 0;
    input_get_screen_size(&w, &h);

    if (width)
        *width = (guint) w;
    if (height)
        *height = (guint) h;
}
