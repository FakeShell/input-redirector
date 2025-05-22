/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "xdo_simulate.h"
#include <gio/gio.h>

static xdo_t *xdo_instance = NULL;
static GMutex xdo_mutex;

static const char *
_get_keysym(int code)
{
    switch (code) {
      case KEY_A:
           return "a";
      case KEY_B:
           return "b";
      case KEY_C:
           return "c";
      case KEY_D:
           return "d";
      case KEY_E:
           return "e";
      case KEY_F:
           return "f";
      case KEY_G:
           return "g";
      case KEY_H:
           return "h";
      case KEY_I:
           return "i";
      case KEY_J:
           return "j";
      case KEY_K:
           return "k";
      case KEY_L:
           return "l";
      case KEY_M:
           return "m";
      case KEY_N:
           return "n";
      case KEY_O:
           return "o";
      case KEY_P:
           return "p";
      case KEY_Q:
           return "q";
      case KEY_R:
           return "r";
      case KEY_S:
           return "s";
      case KEY_T:
           return "t";
      case KEY_U:
           return "u";
      case KEY_V:
           return "v";
      case KEY_W:
           return "w";
      case KEY_X:
           return "x";
      case KEY_Y:
           return "y";
      case KEY_Z:
           return "z";
      case KEY_1:
           return "1";
      case KEY_2:
           return "2";
      case KEY_3:
           return "3";
      case KEY_4:
           return "4";
      case KEY_5:
           return "5";
      case KEY_6:
           return "6";
      case KEY_7:
           return "7";
      case KEY_8:
           return "8";
      case KEY_9:
           return "9";
      case KEY_0:
           return "0";
      case KEY_SPACE:
           return "space";
      case KEY_ENTER:
           return "Return";
      case KEY_ESC:
           return "Escape";
      case KEY_BACKSPACE:
           return "BackSpace";
      case KEY_TAB:
           return "Tab";
      case KEY_CAPSLOCK:
           return "Caps_Lock";
      case KEY_LEFTSHIFT:
      case KEY_RIGHTSHIFT:
           return "shift";
      case KEY_LEFTCTRL:
      case KEY_RIGHTCTRL:
           return "ctrl";
      case KEY_LEFTALT:
      case KEY_RIGHTALT:
           return "alt";
      case KEY_LEFTMETA:
           return "Super_L";
      case KEY_RIGHTMETA:
           return "Super_R";
      case KEY_COMPOSE:
           return "Menu";
      case KEY_UP:
           return "Up";
      case KEY_DOWN:
           return "Down";
      case KEY_LEFT:
           return "Left";
      case KEY_RIGHT:
           return "Right";
      case KEY_HOME:
           return "Home";
      case KEY_END:
           return "End";
      case KEY_PAGEUP:
           return "Page_Up";
      case KEY_PAGEDOWN:
           return "Page_Down";
      case KEY_INSERT:
           return "Insert";
      case KEY_DELETE:
           return "Delete";
      case KEY_F1:
           return "F1";
      case KEY_F2:
           return "F2";
      case KEY_F3:
           return "F3";
      case KEY_F4:
           return "F4";
      case KEY_F5:
           return "F5";
      case KEY_F6:
           return "F6";
      case KEY_F7:
           return "F7";
      case KEY_F8:
           return "F8";
      case KEY_F9:
           return "F9";
      case KEY_F10:
           return "F10";
      case KEY_F11:
           return "F11";
      case KEY_F12:
           return "F12";
      case KEY_COMMA:
           return "comma";
      case KEY_DOT:
           return "period";
      case KEY_SLASH:
           return "slash";
      case KEY_SEMICOLON:
           return "semicolon";
      case KEY_APOSTROPHE:
           return "apostrophe";
      case KEY_LEFTBRACE:
           return "bracketleft";
      case KEY_RIGHTBRACE:
           return "bracketright";
      case KEY_BACKSLASH:
           return "backslash";
      case KEY_MINUS:
           return "minus";
      case KEY_EQUAL:
           return "equal";
      case KEY_GRAVE:
           return "grave";
      default:
           return NULL;
    }
}

void
xdo_init(void)
{
    g_debug("Initializing libxdo...");
    g_mutex_init(&xdo_mutex);
    xdo_instance = xdo_new(NULL);
    if (!xdo_instance)
        g_error("Failed to initialize libxdo");
    g_debug("libxdo initialized successfully");
}

void
xdo_cleanup(void)
{
    if (xdo_instance) {
        g_debug("Cleaning up libxdo");
        xdo_free(xdo_instance);
        xdo_instance = NULL;
    }
    g_mutex_clear(&xdo_mutex);
}

void
xdo_simulate_key_event(int         code,
                       int         value,
                       const char *thread_name)
{
    const char *key = _get_keysym(code);
    if (!key) {
        g_debug("[%s] Unknown key code: %d",
                thread_name, code);
        return;
    }

    g_mutex_lock(&xdo_mutex);
    if (value == 1) {
        g_debug("[%s] Key down: %s",
                thread_name, key);
        xdo_send_keysequence_window_down(
            xdo_instance,
            CURRENTWINDOW,
            key,
            0
        );
    } else if (value == 0) {
        g_debug("[%s] Key up: %s",
                thread_name, key);
        xdo_send_keysequence_window_up(
            xdo_instance,
            CURRENTWINDOW,
            key,
            0
        );
    }
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_mouse_button(int         code,
                          int         value,
                          const char *thread_name)
{
    int button = 0;

    switch (code) {
      case BTN_LEFT:
           button = 1;
           break;
      case BTN_MIDDLE:
           button = 2;
           break;
      case BTN_RIGHT:
           button = 3;
           break;
      case BTN_SIDE:
           button = 8;
           break;
      case BTN_EXTRA:
           button = 9;
           break;
      default:
           g_debug("[%s] Unknown mouse button code: %d",
                   thread_name, code);
           return;
    }

    g_mutex_lock(&xdo_mutex);
    if (value == 1) {
        g_debug("[%s] Mouse button %d down",
                thread_name, button);
        xdo_mouse_down(
            xdo_instance,
            CURRENTWINDOW,
            button
        );
    } else {
        g_debug("[%s] Mouse button %d up",
                thread_name, button);
        xdo_mouse_up(
            xdo_instance,
            CURRENTWINDOW,
            button
        );
    }
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_mouse_motion(int           rel_x,
                          int           rel_y,
                          const char   *thread_name)
{
    g_mutex_lock(&xdo_mutex);
    g_debug("[%s] Mouse motion: dx=%d, dy=%d",
            thread_name, rel_x, rel_y);
    xdo_move_mouse_relative(
        xdo_instance,
        rel_x,
        rel_y
    );
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_scroll(int         code,
                    int         value,
                    const char *thread_name)
{
    int button = 0;

    if (code == REL_WHEEL)
        button = (value > 0) ? 4 : 5;
    else if (code == REL_HWHEEL)
        button = (value > 0) ? 7 : 6;
    else
        return;

    g_mutex_lock(&xdo_mutex);
    g_debug("[%s] Scroll: code=%d, value=%d, button=%d",
            thread_name, code, value, button);
    xdo_click_window(
        xdo_instance,
        CURRENTWINDOW,
        button
    );
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_touch_move(int         x,
                        int         y,
                        const char *thread_name)
{
    g_mutex_lock(&xdo_mutex);
    g_debug("[%s] Touch move: x=%d, y=%d",
            thread_name, x, y);
    xdo_move_mouse(
        xdo_instance,
        x,
        y,
        0
    );
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_touch_down(int         x,
                        int         y,
                        const char *thread_name)
{
    g_mutex_lock(&xdo_mutex);
    g_debug("[%s] Touch down: x=%d, y=%d",
            thread_name, x, y);
    xdo_move_mouse(
        xdo_instance,
        x,
        y,
        0
    );
    xdo_mouse_down(
        xdo_instance,
        CURRENTWINDOW,
        1
    );
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_simulate_touch_up(const char *thread_name)
{
    g_mutex_lock(&xdo_mutex);
    g_debug("[%s] Touch up", thread_name);
    xdo_mouse_up(
        xdo_instance,
        CURRENTWINDOW,
        1
    );
    g_mutex_unlock(&xdo_mutex);
}

void
xdo_get_screen_size(unsigned int *width,
                    unsigned int *height)
{
    Window root = DefaultRootWindow(xdo_instance->xdpy);
    xdo_get_window_size(
        xdo_instance,
        root,
        width,
        height
    );
}
