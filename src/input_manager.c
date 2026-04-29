/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "input_manager.h"
#include "udev.h"
#include "xdo_simulate.h"
#include "wayland_vinput.h"

#include <fcntl.h>

#define MAX_DEVICES 16

typedef struct {
    pthread_t thread;
    gchar *path;
    int fd;
    gboolean active;
} DeviceEntry;

typedef enum {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_TOUCHPAD,
    DEVICE_TYPE_TOUCHSCREEN,
} DeviceType;

typedef struct {
    int max_x, max_y;

    int rel_x, rel_y;
    gboolean has_motion;

    int abs_x, abs_y;
    gboolean has_abs;

    int last_abs_x, last_abs_y;
    gboolean have_last_abs;

    gboolean touch_active;
    gboolean moved_during_touch;
    gboolean first;

    DeviceType device_type;
} DeviceThreadState;

static DeviceEntry devices[MAX_DEVICES];
static int device_count = 0;
static volatile gboolean running = FALSE;
static GMutex fds_mutex;
static gboolean fds_mutex_initialized = FALSE;

static gchar *configured_paths[MAX_DEVICES + 1];
static int configured_path_count = 0;

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
static gboolean udev_started = FALSE;

static GMutex mouse_settings_mutex;
static gboolean mouse_settings_mutex_initialized = FALSE;
static gdouble g_mouse_speed = 0.0;
static gboolean g_mouse_natural_scroll = FALSE;

static gboolean
test_bit(const unsigned long *bits, int bit)
{
    return !!(bits[bit / (8 * sizeof(unsigned long))] &
              (1UL << (bit % (8 * sizeof(unsigned long)))));
}

static DeviceType
detect_device_type(int fd)
{
    unsigned long props[(INPUT_PROP_MAX / (8 * sizeof(unsigned long))) + 1] = {0};

    if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0)
        return DEVICE_TYPE_UNKNOWN;

    if (test_bit(props, INPUT_PROP_DIRECT))
        return DEVICE_TYPE_TOUCHSCREEN;

    if (test_bit(props, INPUT_PROP_POINTER) || test_bit(props, INPUT_PROP_BUTTONPAD))
        return DEVICE_TYPE_TOUCHPAD;

    return DEVICE_TYPE_UNKNOWN;
}

static void
fds_mutex_ensure_initialized(void)
{
    if (!fds_mutex_initialized) {
        g_mutex_init(&fds_mutex);
        fds_mutex_initialized = TRUE;
    }
}

static void
backend_mutex_ensure_initialized(void)
{
    if (!backend_mutex_initialized) {
        g_mutex_init(&backend_mutex);
        backend_mutex_initialized = TRUE;
    }
}

static void
mouse_settings_mutex_ensure_initialized(void)
{
    if (!mouse_settings_mutex_initialized) {
        g_mutex_init(&mouse_settings_mutex);
        mouse_settings_mutex_initialized = TRUE;
    }
}

void
input_manager_set_mouse_speed(gdouble speed)
{
    mouse_settings_mutex_ensure_initialized();

    if (speed < -1.0)
        speed = -1.0;
    else if (speed > 1.0)
        speed = 1.0;

    g_mutex_lock(&mouse_settings_mutex);
    g_mouse_speed = speed;
    g_mutex_unlock(&mouse_settings_mutex);
}

void
input_manager_set_mouse_natural_scroll(gboolean enabled)
{
    mouse_settings_mutex_ensure_initialized();

    g_mutex_lock(&mouse_settings_mutex);
    g_mouse_natural_scroll = enabled ? TRUE : FALSE;
    g_mutex_unlock(&mouse_settings_mutex);
}

static gdouble
get_mouse_speed_multiplier(void)
{
    gdouble speed;

    mouse_settings_mutex_ensure_initialized();

    g_mutex_lock(&mouse_settings_mutex);
    speed = g_mouse_speed;
    g_mutex_unlock(&mouse_settings_mutex);

    if (speed == 0.0)
        return 1.0;

    if (speed > 0.0)
        return 1.0 + speed * 2.0;

    return 1.0 + speed * 0.75;
}

static gboolean
get_mouse_natural_scroll(void)
{
    gboolean enabled;

    mouse_settings_mutex_ensure_initialized();

    g_mutex_lock(&mouse_settings_mutex);
    enabled = g_mouse_natural_scroll;
    g_mutex_unlock(&mouse_settings_mutex);

    return enabled;
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

static gboolean
path_is_configured(const gchar *path)
{
    int i;

    if (path == NULL)
        return FALSE;

    for (i = 0; i < configured_path_count; i++) {
        if (g_strcmp0(configured_paths[i], path) == 0)
            return TRUE;
    }

    return FALSE;
}

static int
find_device_index_by_path(const gchar *path)
{
    int i;

    if (path == NULL)
        return -1;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].path != NULL && g_strcmp0(devices[i].path, path) == 0)
            return i;
    }

    return -1;
}

static int
find_free_device_slot(void)
{
    int i;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].path == NULL)
            return i;
    }

    return -1;
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

    fds_mutex_ensure_initialized();

    ungrab_device(entry);

    g_mutex_lock(&fds_mutex);
    entry->active = FALSE;
    g_mutex_unlock(&fds_mutex);
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

    st->last_abs_x = 0;
    st->last_abs_y = 0;
    st->have_last_abs = FALSE;

    st->touch_active = FALSE;
    st->moved_during_touch = FALSE;
    st->first = TRUE;

    st->device_type = DEVICE_TYPE_UNKNOWN;
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
            st->have_last_abs = FALSE;
        } else { /* Touch up */
            st->touch_active = FALSE;
            st->have_last_abs = FALSE;
            /* no movement: treat as click */
            if (!st->moved_during_touch) {
                /* inject a left-button click */
                input_simulate_mouse_button(BTN_LEFT, 1, entry->path);
                input_simulate_mouse_button(BTN_LEFT, 0, entry->path);
            } else if (st->device_type != DEVICE_TYPE_TOUCHPAD) {
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

    if (ev->code == REL_WHEEL || ev->code == REL_HWHEEL) {
        int value = ev->value;

        if (get_mouse_natural_scroll())
            value = -value;

        input_simulate_scroll(ev->code, value, entry->path);
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
        if (st->touch_active && st->device_type != DEVICE_TYPE_TOUCHPAD)
            st->moved_during_touch = TRUE;
        return;
    }

    if (ev->code == ABS_Y) {
        st->abs_y = ev->value;
        st->has_abs = TRUE;
        if (st->touch_active && st->device_type != DEVICE_TYPE_TOUCHPAD)
            st->moved_during_touch = TRUE;
        return;
    }
}

static void
device_ev_syn(DeviceEntry *entry, DeviceThreadState *st)
{
    if (st->has_motion) {
        gdouble multiplier;
        int dx;
        int dy;

        multiplier = get_mouse_speed_multiplier();

        dx = (int) ((gdouble) st->rel_x * multiplier);
        dy = (int) ((gdouble) st->rel_y * multiplier);

        input_simulate_mouse_motion(dx, dy, entry->path);

        st->rel_x = 0;
        st->rel_y = 0;
        st->has_motion = FALSE;
    }

    if (st->has_abs && st->touch_active && st->device_type == DEVICE_TYPE_TOUCHPAD) {
        if (st->have_last_abs) {
            gdouble multiplier;
            int dx;
            int dy;

            multiplier = get_mouse_speed_multiplier();

            dx = st->abs_x - st->last_abs_x;
            dy = st->abs_y - st->last_abs_y;

            if (dx != 0 || dy != 0)
                st->moved_during_touch = TRUE;

            dx = (int) ((gdouble) dx * multiplier);
            dy = (int) ((gdouble) dy * multiplier);

            input_simulate_mouse_motion(dx, dy, entry->path);
        }

        st->last_abs_x = st->abs_x;
        st->last_abs_y = st->abs_y;
        st->have_last_abs = TRUE;
        st->has_abs = FALSE;
        return;
    }

    if (st->has_abs && st->max_x > 0 && st->max_y > 0 && st->touch_active) {
        unsigned int w = 0, h = 0;
        int sx, sy;

        input_get_screen_size(&w, &h);
        sx = (w > 0) ? (st->abs_x * (int) w) / st->max_x : 0;
        sy = (h > 0) ? (st->abs_y * (int) h) / st->max_y : 0;

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
    if (entry->fd < 0) {
        g_mutex_lock(&fds_mutex);
        entry->active = FALSE;
        g_mutex_unlock(&fds_mutex);
        return NULL;
    }

    st.device_type = detect_device_type(entry->fd);

    /* allow read() to be a cancellation point */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* register cleanup */
    pthread_cleanup_push(cleanup_handler, entry);

    /* get ABS ranges */
    device_read_abs_ranges(entry, &st);

    while (running) {
        ssize_t n;

        /* block here until event or pthread_cancel() */
        n = read(entry->fd, &ev, sizeof(ev));
        if (n == (ssize_t) sizeof(ev)) {
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
            continue;
        }

        if (!running)
            break;

        if (n < 0) {
            if (errno == EINTR)
                continue;

            g_debug("device_thread: read(%s) failed: %s",
                    entry->path, g_strerror(errno));
            break;
        }

        if (n == 0) {
            g_debug("device_thread: read(%s) returned EOF", entry->path);
            break;
        }

        g_debug("device_thread: short read on %s: %zd bytes", entry->path, n);
        break;
    }

    /* invoke cleanup handler */
    pthread_cleanup_pop(1);
    return NULL;
}

static gboolean
start_device_thread_for_path(const gchar *path)
{
    int idx;

    fds_mutex_ensure_initialized();

    if (path == NULL || *path == '\0')
        return FALSE;

    g_mutex_lock(&fds_mutex);

    idx = find_device_index_by_path(path);
    if (idx >= 0) {
        if (devices[idx].active) {
            g_mutex_unlock(&fds_mutex);
            return TRUE;
        }

        g_free(devices[idx].path);
        devices[idx].path = g_strdup(path);
        devices[idx].fd = -1;
        devices[idx].active = TRUE;

        if (pthread_create(&devices[idx].thread, NULL, device_thread, &devices[idx]) != 0) {
            g_debug("input_manager: failed to create thread for %s: %s",
                    path, g_strerror(errno));
            devices[idx].active = FALSE;
            g_mutex_unlock(&fds_mutex);
            return FALSE;
        }

        g_mutex_unlock(&fds_mutex);
        return TRUE;
    }

    idx = find_free_device_slot();
    if (idx < 0) {
        g_debug("input_manager: no free device slot for %s", path);
        g_mutex_unlock(&fds_mutex);
        return FALSE;
    }

    devices[idx].path = g_strdup(path);
    devices[idx].fd = -1;
    devices[idx].active = TRUE;

    if (pthread_create(&devices[idx].thread, NULL, device_thread, &devices[idx]) != 0) {
        g_debug("input_manager: failed to create thread for %s: %s",
                path, g_strerror(errno));
        g_free(devices[idx].path);
        devices[idx].path = NULL;
        devices[idx].fd = -1;
        devices[idx].active = FALSE;
        g_mutex_unlock(&fds_mutex);
        return FALSE;
    }

    device_count++;
    g_mutex_unlock(&fds_mutex);
    return TRUE;
}

static void
clear_configured_paths(void)
{
    int i;

    for (i = 0; i < MAX_DEVICES; i++) {
        g_free(configured_paths[i]);
        configured_paths[i] = NULL;
    }

    configured_path_count = 0;
}

static void
store_configured_paths(const gchar *paths)
{
    gchar **list;
    int i;

    clear_configured_paths();

    if (paths == NULL || *paths == '\0')
        return;

    list = g_strsplit(paths, ",", -1);
    for (i = 0; list[i] != NULL && configured_path_count < MAX_DEVICES; i++) {
        gchar *p = g_strstrip(list[i]);

        if (*p == '\0')
            continue;

        configured_paths[configured_path_count++] = g_strdup(p);
    }
    g_strfreev(list);
}

static void
on_udev_device_event(const gchar *action,
                     const gchar *devnode,
                     const gchar *subsystem,
                     gpointer     user_data)
{
    (void) user_data;

    if (subsystem == NULL || g_strcmp0(subsystem, "input") != 0)
        return;

    if (action == NULL || g_strcmp0(action, "add") != 0)
        return;

    fds_mutex_ensure_initialized();

    g_mutex_lock(&fds_mutex);

    if (!running) {
        g_mutex_unlock(&fds_mutex);
        return;
    }

    if (!path_is_configured(devnode)) {
        g_mutex_unlock(&fds_mutex);
        return;
    }

    g_mutex_unlock(&fds_mutex);

    g_debug("input_manager: udev add for configured device %s", devnode);
    start_device_thread_for_path(devnode);
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
    int i;

    fds_mutex_ensure_initialized();

    input_manager_stop();
    backend_ensure_ready();

    if (!udev_started) {
        if (udev_start(on_udev_device_event, NULL))
            udev_started = TRUE;
        else
            g_debug("input_manager: failed to start udev monitor");
    }

    g_mutex_lock(&fds_mutex);

    /* reset state */
    for (i = 0; i < MAX_DEVICES; i++) {
        g_free(devices[i].path);
        devices[i].path = NULL;
        devices[i].fd   = -1;
        devices[i].active = FALSE;
    }
    device_count = 0;

    store_configured_paths(paths);

    running = TRUE;

    g_mutex_unlock(&fds_mutex);

    for (i = 0; i < configured_path_count; i++)
        start_device_thread_for_path(configured_paths[i]);
}

void
input_manager_stop(void)
{
    int i;

    if (!running)
        return;

    g_debug("Stopping all threads");
    running = FALSE;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active) {
            pthread_cancel(devices[i].thread);
            pthread_join(devices[i].thread, NULL);
        }

        g_free(devices[i].path);
        devices[i].path = NULL;
        devices[i].fd = -1;
        devices[i].active = FALSE;
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
    gdouble multiplier;
    int scaled_dx;
    int scaled_dy;

    multiplier = get_mouse_speed_multiplier();

    scaled_dx = (int) ((gdouble) dx * multiplier);
    scaled_dy = (int) ((gdouble) dy * multiplier);

    input_simulate_mouse_motion(scaled_dx, scaled_dy, source ? source : "dbus");
}

void
input_manager_inject_scroll(guint32      code,
                            gint32       value,
                            const gchar *source)
{
    gint32 adjusted_value = value;

    if ((code == REL_WHEEL || code == REL_HWHEEL) && get_mouse_natural_scroll())
        adjusted_value = -adjusted_value;

    input_simulate_scroll((int) code, (int) adjusted_value, source ? source : "dbus");
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
