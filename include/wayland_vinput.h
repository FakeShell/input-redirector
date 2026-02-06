/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#ifndef WAYLAND_VINPUT_H
#define WAYLAND_VINPUT_H

#include <gio/gio.h>
#include <linux/input.h>

/**
 * Initialize Wayland virtual input backend.
 *
 * @param wayland_display  WAYLAND_DISPLAY value (e.g. "wayland-1"). May be NULL/empty.
 */
void
wayland_vinput_init(const gchar *wayland_display);

/**
 * Cleanup Wayland virtual input backend.
 */
void
wayland_vinput_cleanup(void);

/**
 * Simulate a key press or release event.
 *
 * @param code         Linux evdev key code (e.g. KEY_A).
 * @param value        1 for press, 0 for release.
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_key_event(int         code,
                         int         value,
                         const char *thread_name);

/**
 * Simulate a mouse button press or release.
 *
 * @param code         The button event code (BTN_LEFT, BTN_RIGHT, etc.).
 * @param value        1 for press, 0 for release.
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_mouse_button(int         code,
                            int         value,
                            const char *thread_name);

/**
 * Simulate relative mouse motion.
 *
 * @param rel_x        Delta X movement.
 * @param rel_y        Delta Y movement.
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_mouse_motion(int         rel_x,
                            int         rel_y,
                            const char *thread_name);

/**
 * Simulate scroll wheel events.
 *
 * @param code         REL_WHEEL or REL_HWHEEL.
 * @param value        Wheel delta (positive or negative).
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_scroll(int         code,
                      int         value,
                      const char *thread_name);

/**
 * Simulate a touch move (absolute coordinates).
 *
 * @param x            X coordinate on screen.
 * @param y            Y coordinate on screen.
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_touch_move(int         x,
                          int         y,
                          const char *thread_name);

/**
 * Simulate a touch down (press) at absolute coordinates.
 *
 * @param x            X coordinate on screen.
 * @param y            Y coordinate on screen.
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_touch_down(int         x,
                          int         y,
                          const char *thread_name);

/**
 * Simulate a touch up (release).
 *
 * @param thread_name  Name of the source thread.
 */
void
wayland_vinput_touch_up(const char *thread_name);

/**
 * Get the current screen width and height.
 *
 * @param width   Output: screen width in pixels.
 * @param height  Output: screen height in pixels.
 */
void
wayland_vinput_get_screen_size(unsigned int *width,
                               unsigned int *height);

#endif // WAYLAND_VINPUT_H
