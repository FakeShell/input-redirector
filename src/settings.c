/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "settings.h"
#include "input_manager.h"

static GSettings *settings = NULL;
static GSettings *mouse_settings = NULL;
static GSettings *touchpad_settings = NULL;

static gboolean mouse_has_speed = FALSE;
static gboolean mouse_has_natural_scroll = FALSE;
static gboolean touchpad_has_speed = FALSE;

static GSettingsSchema *
lookup_schema(const gchar *schema_id)
{
    GSettingsSchemaSource *source;

    source = g_settings_schema_source_get_default();
    if (!source)
        return NULL;

    return g_settings_schema_source_lookup(source, schema_id, TRUE);
}

static void
apply_mouse_speed_setting(void)
{
    gdouble speed;

    if (!mouse_settings || !mouse_has_speed)
        return;

    speed = g_settings_get_double(mouse_settings, "speed");
    g_debug("GNOME mouse speed: %.3f", speed);
    input_manager_set_mouse_speed(speed);
}

static void
apply_mouse_natural_scroll_setting(void)
{
    gboolean natural_scroll;

    if (!mouse_settings || !mouse_has_natural_scroll)
        return;

    natural_scroll = g_settings_get_boolean(mouse_settings, "natural-scroll");
    g_debug("GNOME mouse natural-scroll: %s", natural_scroll ? "true" : "false");
    input_manager_set_mouse_natural_scroll(natural_scroll);
}

static void
apply_touchpad_speed_setting(void)
{
    gdouble speed;

    if (!touchpad_settings || !touchpad_has_speed)
        return;

    speed = g_settings_get_double(touchpad_settings, "speed");
    g_debug("GNOME touchpad speed: %.3f", speed);
    input_manager_set_touchpad_speed(speed);
}

static void
on_mouse_settings_changed(GSettings *settings,
                          gchar     *key,
                          gpointer   user_data)
{
    if (g_strcmp0(key, "speed") == 0)
        apply_mouse_speed_setting();
    else if (g_strcmp0(key, "natural-scroll") == 0)
        apply_mouse_natural_scroll_setting();
}

static void
on_touchpad_settings_changed(GSettings *settings,
                             gchar     *key,
                             gpointer   user_data)
{
    if (g_strcmp0(key, "speed") == 0)
        apply_touchpad_speed_setting();
}

static void
setup_gnome_mouse_settings(void)
{
    GSettingsSchema *schema;

    schema = lookup_schema("org.gnome.desktop.peripherals.mouse");
    if (!schema) {
        g_debug("GNOME mouse settings schema not found. using builtin defaults");
        input_manager_set_mouse_speed(0.0);
        input_manager_set_mouse_natural_scroll(FALSE);
        return;
    }

    mouse_has_speed = g_settings_schema_has_key(schema, "speed");
    mouse_has_natural_scroll = g_settings_schema_has_key(schema, "natural-scroll");

    if (!mouse_has_speed && !mouse_has_natural_scroll) {
        g_debug("GNOME mouse schema exists, but wanted keys do not. using builtin defaults");
        input_manager_set_mouse_speed(0.0);
        input_manager_set_mouse_natural_scroll(FALSE);
        g_settings_schema_unref(schema);
        return;
    }

    mouse_settings = g_settings_new("org.gnome.desktop.peripherals.mouse");

    if (mouse_has_speed) {
        apply_mouse_speed_setting();

        g_signal_connect(mouse_settings,
                         "changed::speed",
                         G_CALLBACK(on_mouse_settings_changed),
                         NULL);
    } else {
        input_manager_set_mouse_speed(0.0);
    }

    if (mouse_has_natural_scroll) {
        apply_mouse_natural_scroll_setting();

        g_signal_connect(mouse_settings,
                         "changed::natural-scroll",
                         G_CALLBACK(on_mouse_settings_changed),
                         NULL);
    } else {
        input_manager_set_mouse_natural_scroll(FALSE);
    }

    g_settings_schema_unref(schema);
}

static void
setup_gnome_touchpad_settings(void)
{
    GSettingsSchema *schema;

    schema = lookup_schema("org.gnome.desktop.peripherals.touchpad");
    if (!schema) {
        g_debug("GNOME touchpad settings schema not found. using builtin defaults");
        input_manager_set_touchpad_speed(0.0);
        return;
    }

    touchpad_has_speed = g_settings_schema_has_key(schema, "speed");

    if (!touchpad_has_speed) {
        g_debug("GNOME touchpad schema exists, but speed key does not. using builtin default");
        input_manager_set_touchpad_speed(0.0);
        g_settings_schema_unref(schema);
        return;
    }

    touchpad_settings = g_settings_new("org.gnome.desktop.peripherals.touchpad");

    apply_touchpad_speed_setting();

    g_signal_connect(touchpad_settings,
                     "changed::speed",
                     G_CALLBACK(on_touchpad_settings_changed),
                     NULL);

    g_settings_schema_unref(schema);
}

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
    gchar *disp;
    gchar *wdisp;
    gchar *paths;
    gboolean enable_wayland;

    g_debug("Initializing GSettings for io.furios.input-redirector");
    settings = g_settings_new("io.furios.input-redirector");

    setup_gnome_mouse_settings();
    setup_gnome_touchpad_settings();

    /* apply DISPLAY / WAYLAND_DISPLAY */
    disp = g_settings_get_string(settings, "display");
    apply_display_setting(disp);
    g_free(disp);

    wdisp = g_settings_get_string(settings, "wayland-display");
    apply_wayland_display_setting(wdisp);
    input_manager_set_wayland_display(wdisp);
    g_free(wdisp);

    enable_wayland = g_settings_get_boolean(settings, "enable-wayland");
    input_manager_set_wayland_enabled(enable_wayland);

    /* spawn input threads */
    paths = g_settings_get_string(settings, "input-paths");
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
    if (touchpad_settings) {
        g_object_unref(touchpad_settings);
        touchpad_settings = NULL;
    }

    if (mouse_settings) {
        g_object_unref(mouse_settings);
        mouse_settings = NULL;
    }

    if (settings) {
        g_object_unref(settings);
        settings = NULL;
    }
}
