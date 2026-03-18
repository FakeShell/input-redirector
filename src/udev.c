/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "udev.h"

#include <libudev.h>
#include <poll.h>

typedef struct {
    pthread_t thread;
    gboolean running;
    gboolean started;
    int wake_pipe[2];
    GMutex mutex;
    gboolean mutex_initialized;
    UdevDeviceEventCallback callback;
    gpointer user_data;
} UdevMonitorState;

static UdevMonitorState g_udev = {
    .running = FALSE,
    .started = FALSE,
    .wake_pipe = { -1, -1 },
    .mutex_initialized = FALSE,
    .callback = NULL,
    .user_data = NULL,
};

static void
udev_mutex_ensure_initialized(void)
{
    if (!g_udev.mutex_initialized) {
        g_mutex_init(&g_udev.mutex);
        g_udev.mutex_initialized = TRUE;
    }
}

static void
udev_close_pipe(void)
{
    if (g_udev.wake_pipe[0] >= 0) {
        close(g_udev.wake_pipe[0]);
        g_udev.wake_pipe[0] = -1;
    }

    if (g_udev.wake_pipe[1] >= 0) {
        close(g_udev.wake_pipe[1]);
        g_udev.wake_pipe[1] = -1;
    }
}

static void *
udev_monitor_thread(void *data)
{
    (void) data;

    struct udev *udev_ctx = NULL;
    struct udev_monitor *mon = NULL;
    int mon_fd = -1;
    struct pollfd fds[2];

    udev_ctx = udev_new();
    if (udev_ctx == NULL) {
        g_debug("udev: udev_new() failed");
        return NULL;
    }

    mon = udev_monitor_new_from_netlink(udev_ctx, "udev");
    if (mon == NULL) {
        g_debug("udev: udev_monitor_new_from_netlink() failed");
        udev_unref(udev_ctx);
        return NULL;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(mon, "input", NULL) < 0) {
        g_debug("udev: udev_monitor_filter_add_match_subsystem_devtype() failed");
        udev_monitor_unref(mon);
        udev_unref(udev_ctx);
        return NULL;
    }

    if (udev_monitor_enable_receiving(mon) < 0) {
        g_debug("udev: udev_monitor_enable_receiving() failed");
        udev_monitor_unref(mon);
        udev_unref(udev_ctx);
        return NULL;
    }

    mon_fd = udev_monitor_get_fd(mon);
    if (mon_fd < 0) {
        g_debug("udev: udev_monitor_get_fd() failed");
        udev_monitor_unref(mon);
        udev_unref(udev_ctx);
        return NULL;
    }

    fds[0].fd = mon_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = g_udev.wake_pipe[0];
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    g_debug("udev: monitor thread started");

    while (TRUE) {
        int ret;

        ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR)
                continue;

            g_debug("udev: poll() failed: %s", g_strerror(errno));
            break;
        }

        if (fds[1].revents & POLLIN)
            break;

        if (fds[0].revents & POLLIN) {
            struct udev_device *dev;
            const gchar *action;
            const gchar *devnode;
            const gchar *subsystem;

            dev = udev_monitor_receive_device(mon);
            if (dev == NULL)
                continue;

            action = udev_device_get_action(dev);
            devnode = udev_device_get_devnode(dev);
            subsystem = udev_device_get_subsystem(dev);

            if (g_udev.callback != NULL && devnode != NULL && subsystem != NULL)
                g_udev.callback(action, devnode, subsystem, g_udev.user_data);

            udev_device_unref(dev);
        }
    }

    g_debug("udev: monitor thread stopped");

    udev_monitor_unref(mon);
    udev_unref(udev_ctx);
    return NULL;
}

gboolean
udev_start(UdevDeviceEventCallback callback,
           gpointer                user_data)
{
    udev_mutex_ensure_initialized();

    g_mutex_lock(&g_udev.mutex);

    if (g_udev.started) {
        g_mutex_unlock(&g_udev.mutex);
        return TRUE;
    }

    g_udev.callback = callback;
    g_udev.user_data = user_data;

    if (pipe(g_udev.wake_pipe) < 0) {
        g_debug("udev: pipe() failed: %s", g_strerror(errno));
        g_mutex_unlock(&g_udev.mutex);
        return FALSE;
    }

    g_udev.running = TRUE;

    if (pthread_create(&g_udev.thread, NULL, udev_monitor_thread, NULL) != 0) {
        g_debug("udev: pthread_create() failed: %s", g_strerror(errno));
        g_udev.running = FALSE;
        udev_close_pipe();
        g_mutex_unlock(&g_udev.mutex);
        return FALSE;
    }

    g_udev.started = TRUE;

    g_mutex_unlock(&g_udev.mutex);
    return TRUE;
}

void
udev_stop(void)
{
    udev_mutex_ensure_initialized();

    g_mutex_lock(&g_udev.mutex);

    if (!g_udev.started) {
        g_mutex_unlock(&g_udev.mutex);
        return;
    }

    g_udev.running = FALSE;

    if (g_udev.wake_pipe[1] >= 0) {
        char c = 'x';
        if (write(g_udev.wake_pipe[1], &c, 1) < 0)
            g_debug("udev: wake write failed: %s", g_strerror(errno));
    }

    pthread_join(g_udev.thread, NULL);
    udev_close_pipe();

    g_udev.callback = NULL;
    g_udev.user_data = NULL;
    g_udev.started = FALSE;

    g_mutex_unlock(&g_udev.mutex);
}
