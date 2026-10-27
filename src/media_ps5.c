/* SPDX-License-Identifier: GPL-3.0-only
 * NID and title-directory registration ABI from owendswang/ps5-web-file-manager
 * and kvnhrt/ps5-browser-app; see NOTICE. KeyboardSense adaptation 2026-10-27. */
#include "media.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ps5/kernel.h>

int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
static int (*install_title)(const char *, const char *, void *);
static bool missing_api;

static int initialize(void)
{
    int rc = sceAppInstUtilInitialize();
    if (rc != 0) return rc;
    uint32_t handle;
    intptr_t address = 0;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle) == 0)
        address = kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
    if (address <= 0) {
        missing_api = true;
        sceAppInstUtilTerminate();
        return -ENOSYS;
    }
    _Static_assert(sizeof(install_title) == sizeof(address), "title registration function pointer");
    memcpy(&install_title, &address, sizeof(install_title));
    return 0;
}

static int register_title(const char *title, const char *root)
{
    return install_title(title, root, NULL);
}

int ks_media_install_ps5(char *error, size_t capacity)
{
    /* Resolve just this title's registration API. Do not fall back to an
     * InstallAll call that can register unrelated directories. */
    missing_api = false;
    const KsMediaBackend backend = {initialize, register_title, sceAppInstUtilTerminate};
    int result = ks_media_install("/user/app", &backend, error, capacity);
    if (missing_api)
        snprintf(error, capacity, "Media title registration unavailable on this firmware/HEN");
    return result;
}
