/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "settings.h"
#include "input_manager.h"

static GSettings *settings = NULL;

static void
apply_display_setting(const gchar *display_value)
{
    if (display_value && *display_value) {
        g_debug("Setting DISPLAY from GSettings: %s", display_value);
        g_setenv("DISPLAY", display_value, TRUE);
    } else {
        const gchar *env = g_getenv("DISPLAY");
        g_debug("Using existing DISPLAY: %s", env ? env : "(unset)");
    }
}

static void
apply_wayland_display_setting(const gchar *wayland_display_value)
{
    if (wayland_display_value && *wayland_display_value) {
        g_debug("Setting WAYLAND_DISPLAY from GSettings: %s", wayland_display_value);
        g_setenv("WAYLAND_DISPLAY", wayland_display_value, TRUE);
    } else {
        const gchar *env = g_getenv("WAYLAND_DISPLAY");
        g_debug("Using existing WAYLAND_DISPLAY: %s", env ? env : "(unset)");
    }
}

static void
on_settings_changed(GSettings *settings,
                    gchar     *key,
                    gpointer   user_data)
{
    gchar *paths = g_settings_get_string(settings, "input-paths");

    if (g_strcmp0(key, "input-paths") == 0) {
        g_debug("input-paths changed: %s", paths);
    } else if (g_strcmp0(key, "display") == 0) {
        gchar *disp = g_settings_get_string(settings, "display");
        apply_display_setting(disp);
        g_free(disp);
    } else if (g_strcmp0(key, "enable-wayland") == 0) {
        gboolean enable_wayland = g_settings_get_boolean(settings, "enable-wayland");
        g_debug("enable-wayland changed: %s", enable_wayland ? "true" : "false");
        input_manager_set_wayland_enabled(enable_wayland);
    } else if (g_strcmp0(key, "wayland-display") == 0) {
        gchar *wdisp = g_settings_get_string(settings, "wayland-display");
        g_debug("wayland-display changed: %s", wdisp);
        apply_wayland_display_setting(wdisp);
        input_manager_set_wayland_display(wdisp);
        g_free(wdisp);
    }

    input_manager_update(paths);
    g_free(paths);
}

void
settings_init(void)
{
    g_debug("Initializing GSettings for io.furios.input-redirector");
    settings = g_settings_new("io.furios.input-redirector");

    /* apply DISPLAY / WAYLAND_DISPLAY */
    gchar *disp = g_settings_get_string(settings, "display");
    apply_display_setting(disp);
    g_free(disp);

    gchar *wdisp = g_settings_get_string(settings, "wayland-display");
    apply_wayland_display_setting(wdisp);
    input_manager_set_wayland_display(wdisp);
    g_free(wdisp);

    gboolean enable_wayland = g_settings_get_boolean(settings, "enable-wayland");
    input_manager_set_wayland_enabled(enable_wayland);

    /* spawn input threads */
    gchar *paths = g_settings_get_string(settings, "input-paths");
    input_manager_update(paths);
    g_free(paths);

    g_signal_connect(settings,
                     "changed::input-paths",
                     G_CALLBACK(on_settings_changed),
                     NULL);
    g_signal_connect(settings,
                     "changed::display",
                     G_CALLBACK(on_settings_changed),
                     NULL);
    g_signal_connect(settings,
                     "changed::enable-wayland",
                     G_CALLBACK(on_settings_changed),
                     NULL);
    g_signal_connect(settings,
                     "changed::wayland-display",
                     G_CALLBACK(on_settings_changed),
                     NULL);
}

void
settings_cleanup(void)
{
    if (settings)
        g_object_unref(settings);
}
