/* SPDX-License-Identifier: GPL-3.0-only
 * Title-directory registration pattern researched in ps5-web-file-manager
 * (owendswang) and ps5-browser-app (kvnhrt); source commits in NOTICE.
 * KeyboardSense implementation, 2026-10-27: owned assets, atomic writes,
 * idempotent targeted registration, and recoverable failures. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "media.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const unsigned char owner[] = "KeyboardSense Media shortcut v1\n";

static int directory(const char *path)
{
    struct stat st;
    if (mkdir(path, 0755) == 0) return 0;
    if (errno != EEXIST || lstat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return -1; }
    return 0;
}

static bool matches(const char *path, const unsigned char *data, size_t length)
{
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (uintmax_t)st.st_size != length) return false;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    unsigned char buffer[4096];
    size_t offset = 0;
    bool same = true;
    while (offset < length) {
        size_t amount = length - offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        ssize_t n = read(fd, buffer, amount);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || memcmp(buffer, data + offset, (size_t)n)) { same = false; break; }
        offset += (size_t)n;
    }
    if (close(fd) != 0) same = false;
    return same;
}

static int write_asset(const char *path, const unsigned char *data, size_t length)
{
    if (matches(path, data, length)) return 0;
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode)) { errno = EINVAL; return -1; }
    } else if (errno != ENOENT) return -1;
    char temporary[512];
    int n = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) { errno = ENAMETOOLONG; return -1; }
    int fd = mkstemp(temporary);
    if (fd < 0) return -1;
    bool ok = fchmod(fd, 0644) == 0;
    size_t offset = 0;
    while (ok && offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { ok = false; break; }
        offset += (size_t)written;
    }
    if (ok && fsync(fd) != 0) ok = false;
    int saved = errno;
    if (close(fd) != 0 && ok) { ok = false; saved = errno; }
    if (ok && rename(temporary, path) != 0) { ok = false; saved = errno; }
    if (!ok) { unlink(temporary); errno = saved; return -1; }
    return 1;
}

int ks_media_install(const char *root, const KsMediaBackend *backend, char *error, size_t capacity)
{
    char base[384], system[400], marker[420], param[420], icon[420], app_root[384];
    if (capacity) *error = 0;
    if (!backend || !backend->initialize || !backend->register_title || !backend->terminate) {
        snprintf(error, capacity, "Media registration API unavailable"); return -1;
    }
    if (strlen(root) > 340) { snprintf(error, capacity, "Media root too long"); return -1; }
    snprintf(app_root, sizeof(app_root), "%s/", root);
    snprintf(base, sizeof(base), "%s/%s", root, ks_media_title_id);
    snprintf(system, sizeof(system), "%s/sce_sys", base);
    snprintf(marker, sizeof(marker), "%s/.keyboardsense-webui", base);
    snprintf(param, sizeof(param), "%s/param.json", system);
    snprintf(icon, sizeof(icon), "%s/icon0.png", system);
    int rc = backend->initialize();
    if (rc != 0) { snprintf(error, capacity, "Media initialize failed (0x%08x)", (unsigned)rc); return -1; }
    int result = -1;
    const char *stage = "create app directory";
    if (directory(root) != 0) goto done;
    bool created = mkdir(base, 0755) == 0;
    if (!created) {
        struct stat st;
        if (errno != EEXIST || lstat(base, &st) != 0) goto done;
        if (!S_ISDIR(st.st_mode) || !matches(marker, owner, sizeof(owner) - 1)) {
            snprintf(error, capacity, "Media title %s already exists and is not owned by KeyboardSense", ks_media_title_id);
            goto done;
        }
    } else if (write_asset(marker, owner, sizeof(owner) - 1) < 0) {
        int saved = errno; rmdir(base); errno = saved; goto done;
    }
    stage = "write launcher assets";
    if (directory(system) != 0) goto done;
    int param_result = write_asset(param, ks_media_param, ks_media_param_size);
    if (param_result < 0) goto done;
    int icon_result = write_asset(icon, ks_media_icon, ks_media_icon_size);
    if (icon_result < 0) goto done;
    rc = backend->register_title(ks_media_title_id, app_root);
    if (rc != 0) { snprintf(error, capacity, "Media registration failed (0x%08x); will retry next start", (unsigned)rc); goto done; }
    result = created || param_result || icon_result ? 1 : 0;
done:
    if (result < 0 && capacity && !*error) snprintf(error, capacity, "Cannot %s: %s", stage, strerror(errno));
    backend->terminate();
    return result;
}
