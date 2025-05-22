/**
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2025 Bardia Moshiri <bardia@furilabs.com>
 */

#include "settings.h"
#include "xdo_simulate.h"

int
main(void)
{
    g_debug("Starting input redirector daemon");

    settings_init();
    xdo_init();

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

    settings_cleanup();
    xdo_cleanup();

    g_debug("Exiting input-redirector daemon");
    return 0;
}
