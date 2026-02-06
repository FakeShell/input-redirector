/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <gio/gio.h>

/**
 * Update the set of monitored input devices.
 *
 * @param paths  Comma-separated list of /dev/input/event* device paths.
 */
void
input_manager_update(const gchar *paths);

/**
 * Stop all device threads and release their grabs.
 */
void
input_manager_stop(void);

/**
 * Enable/disable Wayland backend selection.
 *
 * When enabled, input_manager will route input simulation calls to wayland_vinput.
 * When disabled, it will use the X11 backend (libxdo).
 */
void
input_manager_set_wayland_enabled(gboolean enabled);

/**
 * Set Wayland display name (WAYLAND_DISPLAY).
 *
 * Example: "wayland-0", "wayland-1".
 */
void
input_manager_set_wayland_display(const gchar *wayland_display);

#endif // INPUT_MANAGER_H
