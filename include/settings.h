/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <gio/gio.h>

/**
 * Initialize GSettings and start monitoring "input-paths".
 */
void
settings_init(void);

/**
 * Cleanup GSettings resources.
 */
void
settings_cleanup(void);

#endif // SETTINGS_H
