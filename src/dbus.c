/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "dbus.h"
#include "input_manager.h"

#define BUS_NAME    "io.furios.InputRedirector"
#define OBJ_PATH    "/io/furios/InputRedirector"
#define IFACE_NAME  "io.furios.InputRedirector"

static GDBusConnection *g_conn = NULL;
static guint g_name_owner_id = 0;
static guint g_obj_reg_id = 0;
static GDBusNodeInfo *g_node_info = NULL;

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='io.furios.InputRedirector'>"
    "    <method name='KeyEvent'>"
    "      <arg name='code' type='u' direction='in'/>"
    "      <arg name='value' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='MouseButton'>"
    "      <arg name='code' type='u' direction='in'/>"
    "      <arg name='value' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='MouseMotion'>"
    "      <arg name='dx' type='i' direction='in'/>"
    "      <arg name='dy' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='Scroll'>"
    "      <arg name='code' type='u' direction='in'/>"
    "      <arg name='value' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='TouchDown'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='TouchMove'>"
    "      <arg name='x' type='i' direction='in'/>"
    "      <arg name='y' type='i' direction='in'/>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='TouchUp'>"
    "      <arg name='source' type='s' direction='in'/>"
    "    </method>"
    "    <method name='GetScreenSize'>"
    "      <arg name='width' type='u' direction='out'/>"
    "      <arg name='height' type='u' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void
handle_method_call(GDBusConnection       *connection,
                   const gchar           *sender,
                   const gchar           *object_path,
                   const gchar           *interface_name,
                   const gchar           *method_name,
                   GVariant              *parameters,
                   GDBusMethodInvocation *invocation,
                   gpointer               user_data)
{
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    (void) user_data;

    if (g_strcmp0(method_name, "KeyEvent") == 0) {
        guint32 code = 0;
        gint32 value = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(uis)", &code, &value, &source);

        /* Treat repeat (2) as press (1) for now */
        if (value == 2)
            value = 1;

        input_manager_inject_key_event(code, value, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "MouseButton") == 0) {
        guint32 code = 0;
        gint32 value = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(uis)", &code, &value, &source);
        input_manager_inject_mouse_button(code, value, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "MouseMotion") == 0) {
        gint32 dx = 0;
        gint32 dy = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(iis)", &dx, &dy, &source);
        input_manager_inject_mouse_motion(dx, dy, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "Scroll") == 0) {
        guint32 code = 0;
        gint32 value = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(uis)", &code, &value, &source);
        input_manager_inject_scroll(code, value, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "TouchDown") == 0) {
        gint32 x = 0;
        gint32 y = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(iis)", &x, &y, &source);
        input_manager_inject_touch_down(x, y, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "TouchMove") == 0) {
        gint32 x = 0;
        gint32 y = 0;
        const gchar *source = NULL;

        g_variant_get(parameters, "(iis)", &x, &y, &source);
        input_manager_inject_touch_move(x, y, source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "TouchUp") == 0) {
        const gchar *source = NULL;

        g_variant_get(parameters, "(s)", &source);
        input_manager_inject_touch_up(source);
        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    if (g_strcmp0(method_name, "GetScreenSize") == 0) {
        guint width = 0;
        guint height = 0;

        input_manager_get_screen_size(&width, &height);

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(uu)", (guint32) width, (guint32) height)
        );
        return;
    }

    g_dbus_method_invocation_return_error(
        invocation,
        G_IO_ERROR,
        G_IO_ERROR_NOT_SUPPORTED,
        "Unknown method: %s",
        method_name
    );
}

static const GDBusInterfaceVTable vtable = {
    .method_call = handle_method_call,
    .get_property = NULL,
    .set_property = NULL,
};

static void
on_bus_acquired(GDBusConnection *connection,
                const gchar     *name,
                gpointer         user_data)
{
    (void) name;
    (void) user_data;

    g_conn = g_object_ref(connection);

    if (!g_node_info) {
        GError *error = NULL;
        g_node_info = g_dbus_node_info_new_for_xml(introspection_xml, &error);
        if (!g_node_info) {
            g_debug("dbus: failed to parse introspection XML: %s",
                    error ? error->message : "unknown error");
            g_clear_error(&error);
            return;
        }
    }

    const GDBusInterfaceInfo *iface_info = g_node_info->interfaces[0];

    GError *error = NULL;
    g_obj_reg_id = g_dbus_connection_register_object(
        g_conn,
        OBJ_PATH,
        (GDBusInterfaceInfo *) iface_info,
        &vtable,
        NULL,
        NULL,
        &error
    );

    if (g_obj_reg_id == 0) {
        g_debug("dbus: failed to register object: %s",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    g_debug("dbus: exported %s at %s (%s)", BUS_NAME, OBJ_PATH, IFACE_NAME);
}

static void
on_name_acquired(GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
    (void) connection;
    (void) user_data;

    g_debug("dbus: acquired name %s", name);
}

static void
on_name_lost(GDBusConnection *connection,
             const gchar     *name,
             gpointer         user_data)
{
    (void) connection;
    (void) user_data;

    g_debug("dbus: lost name %s", name);
}

void
dbus_init(void)
{
    if (g_name_owner_id != 0)
        return;

    g_name_owner_id = g_bus_own_name(
        G_BUS_TYPE_SESSION,
        BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        on_bus_acquired,
        on_name_acquired,
        on_name_lost,
        NULL,
        NULL
    );
}

void
dbus_cleanup(void)
{
    if (g_conn && g_obj_reg_id != 0) {
        g_dbus_connection_unregister_object(g_conn, g_obj_reg_id);
        g_obj_reg_id = 0;
    }

    if (g_name_owner_id != 0) {
        g_bus_unown_name(g_name_owner_id);
        g_name_owner_id = 0;
    }

    if (g_node_info) {
        g_dbus_node_info_unref(g_node_info);
        g_node_info = NULL;
    }

    if (g_conn) {
        g_clear_object(&g_conn);
        g_conn = NULL;
    }
}
