/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Bardia Moshiri <bardia@furilabs.com>
 */

#include "dbus.h"
#include "input_manager.h"

#include <sys/socket.h>
#include <fcntl.h>

#define BUS_NAME    "io.furios.InputRedirector"
#define OBJ_PATH    "/io/furios/InputRedirector"
#define IFACE_NAME  "io.furios.InputRedirector"

#define MAX_INPUT_CHANNELS 16
#define IR_EVENT_VERSION 1

typedef enum {
    IR_EV_KEY = 1,
    IR_EV_MOUSE_BUTTON = 2,
    IR_EV_MOUSE_MOTION = 3,
    IR_EV_SCROLL = 4,
    IR_EV_TOUCH_DOWN = 5,
    IR_EV_TOUCH_MOVE = 6,
    IR_EV_TOUCH_UP = 7,
} IrEventType;

typedef struct __attribute__((packed)) {
    guint16 version;
    guint16 type;
    gint32 a;
    gint32 b;
    guint32 code;
    gint32 value;
} IrSocketEvent;

typedef struct {
    pthread_t thread;
    int fd;
    gboolean active;
    gchar *source;
} InputChannelEntry;

static GDBusConnection *g_conn = NULL;
static guint g_name_owner_id = 0;
static guint g_obj_reg_id = 0;
static GDBusNodeInfo *g_node_info = NULL;

static InputChannelEntry g_channels[MAX_INPUT_CHANNELS];
static GMutex g_channels_mutex;
static gboolean g_channels_mutex_initialized = FALSE;

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
    "    <method name='OpenInputChannel'>"
    "      <arg name='source' type='s' direction='in'/>"
    "      <arg name='fd' type='h' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static void
channels_mutex_ensure_initialized(void)
{
    if (!g_channels_mutex_initialized) {
        g_mutex_init(&g_channels_mutex);
        g_channels_mutex_initialized = TRUE;
    }
}

static int
find_free_channel_slot(void)
{
    int i;

    for (i = 0; i < MAX_INPUT_CHANNELS; i++) {
        if (!g_channels[i].active)
            return i;
    }

    return -1;
}

static void
close_channel_fd(InputChannelEntry *entry)
{
    if (entry->fd >= 0) {
        close(entry->fd);
        entry->fd = -1;
    }
}

static void
channel_cleanup_handler(void *arg)
{
    InputChannelEntry *entry = arg;

    channels_mutex_ensure_initialized();

    g_mutex_lock(&g_channels_mutex);
    close_channel_fd(entry);
    entry->active = FALSE;
    g_clear_pointer(&entry->source, g_free);
    g_mutex_unlock(&g_channels_mutex);
}

static void
dispatch_socket_event(const InputChannelEntry *entry, const IrSocketEvent *ev)
{
    const gchar *source = (entry->source != NULL) ? entry->source : "socket";

    switch ((IrEventType) ev->type) {
    case IR_EV_KEY: {
        gint32 value = ev->value;

        /* Treat repeat (2) as press (1) for now */
        if (value == 2)
            value = 1;

        input_manager_inject_key_event(ev->code, value, source);
        break;
    }

    case IR_EV_MOUSE_BUTTON:
        input_manager_inject_mouse_button(ev->code, ev->value, source);
        break;

    case IR_EV_MOUSE_MOTION:
        input_manager_inject_mouse_motion(ev->a, ev->b, source);
        break;

    case IR_EV_SCROLL:
        input_manager_inject_scroll(ev->code, ev->value, source);
        break;

    case IR_EV_TOUCH_DOWN:
        input_manager_inject_touch_down(ev->a, ev->b, source);
        break;

    case IR_EV_TOUCH_MOVE:
        input_manager_inject_touch_move(ev->a, ev->b, source);
        break;

    case IR_EV_TOUCH_UP:
        input_manager_inject_touch_up(source);
        break;

    default:
        g_debug("dbus: unknown socket event type %u from %s",
                (guint) ev->type, source);
        break;
    }
}

static void *
input_channel_thread(void *data)
{
    InputChannelEntry *entry = data;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    pthread_cleanup_push(channel_cleanup_handler, entry);

    while (1) {
        IrSocketEvent ev;
        ssize_t n;

        n = recv(entry->fd, &ev, sizeof(ev), 0);
        if (n == (ssize_t) sizeof(ev)) {
            if (ev.version != IR_EVENT_VERSION) {
                g_debug("dbus: ignoring socket event with bad version %u from %s",
                        (guint) ev.version,
                        entry->source ? entry->source : "socket");
                continue;
            }

            dispatch_socket_event(entry, &ev);
            continue;
        }

        if (n == 0) {
            g_debug("dbus: input channel closed by peer (%s)",
                    entry->source ? entry->source : "socket");
            break;
        }

        if (n < 0) {
            if (errno == EINTR)
                continue;

            g_debug("dbus: recv() failed on input channel (%s): %s",
                    entry->source ? entry->source : "socket",
                    g_strerror(errno));
            break;
        }

        g_debug("dbus: short packet on input channel (%s): %zd bytes",
                entry->source ? entry->source : "socket", n);
        break;
    }

    pthread_cleanup_pop(1);
    return NULL;
}

static gboolean
open_input_channel_for_source(const gchar *source, gint *out_fd)
{
    int sv[2] = { -1, -1 };
    int idx;
    gboolean ok = FALSE;

    if (out_fd == NULL)
        return FALSE;

    *out_fd = -1;

    channels_mutex_ensure_initialized();

    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) < 0) {
        if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0) {
            g_debug("dbus: socketpair() failed: %s", g_strerror(errno));
            return FALSE;
        }
    }

    fcntl(sv[0], F_SETFD, FD_CLOEXEC);
    fcntl(sv[1], F_SETFD, FD_CLOEXEC);

    g_mutex_lock(&g_channels_mutex);

    idx = find_free_channel_slot();
    if (idx < 0) {
        g_mutex_unlock(&g_channels_mutex);
        g_debug("dbus: no free input channel slots");
        close(sv[0]);
        close(sv[1]);
        return FALSE;
    }

    g_channels[idx].fd = sv[0];
    g_channels[idx].active = TRUE;
    g_channels[idx].source = g_strdup((source && *source) ? source : "socket");

    if (pthread_create(&g_channels[idx].thread, NULL, input_channel_thread, &g_channels[idx]) != 0) {
        g_debug("dbus: failed to create input channel thread: %s", g_strerror(errno));
        close_channel_fd(&g_channels[idx]);
        g_channels[idx].active = FALSE;
        g_clear_pointer(&g_channels[idx].source, g_free);
        g_mutex_unlock(&g_channels_mutex);
        close(sv[1]);
        return FALSE;
    }

    *out_fd = sv[1];
    ok = TRUE;

    g_mutex_unlock(&g_channels_mutex);

    if (ok) {
        g_debug("dbus: opened input channel for source %s", source ? source : "socket");
        return TRUE;
    }

    close(sv[1]);
    return FALSE;
}

static void
handle_open_input_channel(GDBusMethodInvocation *invocation,
                          const gchar           *source)
{
    gint client_fd = -1;
    GUnixFDList *fd_list = NULL;
    gint fd_index;
    GError *error = NULL;

    if (!open_input_channel_for_source(source, &client_fd)) {
        g_dbus_method_invocation_return_error(
            invocation,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Failed to open input channel"
        );
        return;
    }

    fd_list = g_unix_fd_list_new();
    fd_index = g_unix_fd_list_append(fd_list, client_fd, &error);
    if (fd_index < 0) {
        g_debug("dbus: failed to append fd to UnixFDList: %s",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(fd_list);
        close(client_fd);

        g_dbus_method_invocation_return_error(
            invocation,
            G_IO_ERROR,
            G_IO_ERROR_FAILED,
            "Failed to export input channel fd"
        );
        return;
    }

    g_dbus_method_invocation_return_value_with_unix_fd_list(
        invocation,
        g_variant_new("(h)", fd_index),
        fd_list
    );

    g_object_unref(fd_list);
    close(client_fd);
}

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

    if (g_strcmp0(method_name, "OpenInputChannel") == 0) {
        const gchar *source = NULL;

        g_variant_get(parameters, "(&s)", &source);
        handle_open_input_channel(invocation, source);
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
    int i;

    if (g_name_owner_id != 0)
        return;

    channels_mutex_ensure_initialized();

    for (i = 0; i < MAX_INPUT_CHANNELS; i++) {
        g_channels[i].fd = -1;
        g_channels[i].active = FALSE;
        g_channels[i].source = NULL;
    }

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
    int i;

    channels_mutex_ensure_initialized();

    for (i = 0; i < MAX_INPUT_CHANNELS; i++) {
        if (g_channels[i].active) {
            pthread_cancel(g_channels[i].thread);
            pthread_join(g_channels[i].thread, NULL);
        }

        close_channel_fd(&g_channels[i]);
        g_channels[i].active = FALSE;
        g_clear_pointer(&g_channels[i].source, g_free);
    }

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
