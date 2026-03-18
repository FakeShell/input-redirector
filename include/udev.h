/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef UDEV_H
#define UDEV_H

#include <gio/gio.h>

typedef void (*UdevDeviceEventCallback)(const gchar *action,
                                        const gchar *devnode,
                                        const gchar *subsystem,
                                        gpointer     user_data);

/**
 * Start udev monitor thread.
 *
 * Watches the "input" subsystem and invokes the callback for matching events.
 *
 * @param callback   Called for each udev event.
 * @param user_data  Opaque pointer passed to callback.
 *
 * @return TRUE on success, FALSE on failure.
 */
gboolean
udev_start(UdevDeviceEventCallback callback,
           gpointer                user_data);

/**
 * Stop udev monitor thread.
 */
void
udev_stop(void);

#endif // UDEV_H
