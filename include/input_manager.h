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

#endif // INPUT_MANAGER_H
