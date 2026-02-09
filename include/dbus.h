/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef DBUS_H
#define DBUS_H

#include <gio/gio.h>

/**
 * Initialize the D-Bus service (session bus) for input injection.
 *
 * Exports:
 *  - Bus name:    io.furios.InputRedirector
 *  - Object path: /io/furios/InputRedirector
 *  - Interface:   io.furios.InputRedirector
 */
void
dbus_init(void);

/**
 * Cleanup D-Bus resources.
 */
void
dbus_cleanup(void);

#endif // DBUS_H
