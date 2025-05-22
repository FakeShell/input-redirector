/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "input_manager.h"
#include "xdo_simulate.h"

#include <fcntl.h>

#define MAX_DEVICES 16

typedef struct {
    pthread_t   thread;
    gchar      *path;
    int         fd;
} DeviceEntry;

static DeviceEntry devices[MAX_DEVICES];
static int device_count = 0;
static volatile gboolean running = FALSE;
static GMutex fds_mutex;
static gboolean fds_mutex_inited = FALSE;

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

static void *
device_thread(void *data)
{
    DeviceEntry *entry = data;
    struct input_event ev;
    struct input_absinfo info;
    int max_x = 0, max_y = 0;
    int rel_x = 0, rel_y = 0;
    gboolean has_motion = FALSE;
    int abs_x = 0, abs_y = 0;
    gboolean has_abs = FALSE;
    gboolean touch_active = FALSE;
    static gboolean first = TRUE;

    entry->fd = grab_device(entry->path);
    if (entry->fd < 0)
        return NULL;

    /* allow read() to be a cancellation point */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    /* register cleanup */
    pthread_cleanup_push(cleanup_handler, entry);

    /* get ABS ranges */
    if (ioctl(entry->fd, EVIOCGABS(ABS_X), &info) == 0)
        max_x = info.maximum;
    if (ioctl(entry->fd, EVIOCGABS(ABS_Y), &info) == 0)
        max_y = info.maximum;

    while (running) {
        /* block here until event or pthread_cancel() */
        if (read(entry->fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        switch (ev.type) {
            case EV_KEY:
                if (ev.code >= BTN_MOUSE) {
                    xdo_simulate_mouse_button(ev.code, ev.value, entry->path);
                } else if (ev.code == BTN_TOUCH || ev.code == BTN_TOOL_FINGER) {
                    touch_active = (ev.value == 1);
                    if (!touch_active)
                        xdo_simulate_touch_up(entry->path);
                } else {
                    xdo_simulate_key_event(ev.code, ev.value, entry->path);
                }
                break;
            case EV_REL:
                if (ev.code == REL_X) {
                    rel_x += ev.value;
                    has_motion = TRUE;
                } else if (ev.code == REL_Y) {
                    rel_y += ev.value;
                    has_motion = TRUE;
                } else {
                    xdo_simulate_scroll(ev.code, ev.value, entry->path);
                }
                break;
            case EV_ABS:
                if (ev.code == ABS_X) {
                    abs_x = ev.value;
                    has_abs = TRUE;
                } else if (ev.code == ABS_Y) {
                    abs_y = ev.value;
                    has_abs = TRUE;
                }
                break;
            case EV_SYN:
                if (has_motion) {
                    xdo_simulate_mouse_motion(rel_x, rel_y, entry->path);
                    rel_x = rel_y = 0; has_motion = FALSE;
                }

                if (has_abs && max_x > 0 && max_y > 0) {
                    unsigned int w = 0, h = 0;
                    xdo_get_screen_size(&w, &h);
                    int sx = (abs_x * w) / max_x;
                    int sy = (abs_y * h) / max_y;
                    if (touch_active && first) {
                        xdo_simulate_touch_down(sx, sy, entry->path);
                        first = FALSE;
                    } else if (touch_active) {
                        xdo_simulate_touch_move(sx, sy, entry->path);
                    } else {
                        first = TRUE;
                    }
                    has_abs = FALSE;
                }
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
input_manager_update(const gchar *paths)
{
    if (!fds_mutex_inited) {
        g_mutex_init(&fds_mutex);
        fds_mutex_inited = TRUE;
    }

    input_manager_stop();

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
