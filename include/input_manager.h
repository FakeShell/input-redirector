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

/**
 * Inject a key event into the current backend.
 *
 * @param code   evdev key code (KEY_*).
 * @param value  1 press, 0 release (2 repeat may be normalized by caller).
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_key_event(guint32      code,
                               gint32       value,
                               const gchar *source);

/**
 * Inject a mouse button event into the current backend.
 *
 * @param code   evdev button code (BTN_*).
 * @param value  1 press, 0 release.
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_mouse_button(guint32      code,
                                  gint32       value,
                                  const gchar *source);

/**
 * Inject relative mouse motion into the current backend.
 *
 * @param dx     Relative X delta.
 * @param dy     Relative Y delta.
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_mouse_motion(gint32       dx,
                                  gint32       dy,
                                  const gchar *source);

/**
 * Inject scroll into the current backend.
 *
 * @param code   evdev relative axis code (REL_WHEEL / REL_HWHEEL).
 * @param value  Axis delta (kernel semantics).
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_scroll(guint32      code,
                            gint32       value,
                            const gchar *source);

/**
 * Inject a touch down into the current backend.
 *
 * @param x      Absolute X in target screen coordinates.
 * @param y      Absolute Y in target screen coordinates.
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_touch_down(gint32       x,
                                gint32       y,
                                const gchar *source);

/**
 * Inject a touch move into the current backend.
 *
 * @param x      Absolute X in target screen coordinates.
 * @param y      Absolute Y in target screen coordinates.
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_touch_move(gint32       x,
                                gint32       y,
                                const gchar *source);

/**
 * Inject a touch up into the current backend.
 *
 * @param source Used for debug logging to identify the event origin.
 */
void
input_manager_inject_touch_up(const gchar *source);

/**
 * Query the target screen size from the current backend.
 *
 * @param width   Returned width (pixels). If unknown, 0.
 * @param height  Returned height (pixels). If unknown, 0.
 */
void
input_manager_get_screen_size(guint *width,
                              guint *height);

#endif // INPUT_MANAGER_H
