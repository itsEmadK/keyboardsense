/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int initialized, registered, terminated, init_error, register_error;
static char expected_root[384];
static int initialize(void) { initialized++; return init_error; }
static int register_title(const char *title, const char *root)
{
    registered++;
    assert(!strcmp(title, "KBSN01405") && !strcmp(root, expected_root));
    return register_error;
}
static int terminate(void) { terminated++; return 0; }
static const KsMediaBackend backend = {initialize, register_title, terminate};

static void write_file(const char *path, const unsigned char *data, size_t length)
{
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(data, 1, length, file) == length); assert(fclose(file) == 0);
}
static void assert_file(const char *path, const unsigned char *data, size_t length)
{
    FILE *file = fopen(path, "rb"); assert(file);
    for (size_t i = 0; i < length; ++i) assert(fgetc(file) == data[i]);
    assert(fgetc(file) == EOF); assert(fclose(file) == 0);
}
static ino_t inode(const char *path) { struct stat st; assert(stat(path, &st) == 0); return st.st_ino; }

int main(void)
{
    char root[] = "/tmp/keyboardsense-media-XXXXXX"; assert(mkdtemp(root));
    char base[384], system[400], param[420], icon[420], marker[420], parked[420], outside[420], error[180];
    snprintf(expected_root, sizeof(expected_root), "%s/", root);
    snprintf(base, sizeof(base), "%s/KBSN01405", root);
    snprintf(system, sizeof(system), "%s/sce_sys", base);
    snprintf(param, sizeof(param), "%s/param.json", system);
    snprintf(icon, sizeof(icon), "%s/icon0.png", system);
    snprintf(marker, sizeof(marker), "%s/.keyboardsense-webui", base);
    snprintf(parked, sizeof(parked), "%s/parked", root);
    snprintf(outside, sizeof(outside), "%s/outside", root);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == 1);
    assert(initialized == 1 && registered == 1 && terminated == 1 && !*error);
    assert_file(param, ks_media_param, ks_media_param_size);
    assert_file(icon, ks_media_icon, ks_media_icon_size);
    ino_t param_inode = inode(param), icon_inode = inode(icon);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == 0);
    assert(registered == 2 && terminated == 2 && inode(param) == param_inode && inode(icon) == icon_inode);

    /* Refresh owned metadata without rewriting matching artwork. */
    write_file(param, (const unsigned char *)"bad", 3);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == 1);
    assert_file(param, ks_media_param, ks_media_param_size); assert(inode(icon) == icon_inode);

    /* Keep assets after a failed registration, retry even if assets match. */
    register_error = -37;
    assert(ks_media_install(root, &backend, error, sizeof(error)) == -1 && strstr(error, "registration failed"));
    register_error = 0;
    assert(ks_media_install(root, &backend, error, sizeof(error)) == 0 && registered == 5 && terminated == 5);
    init_error = -9;
    assert(ks_media_install(root, &backend, error, sizeof(error)) == -1 && strstr(error, "initialize failed"));
    assert(registered == 5 && terminated == 5); init_error = 0;

    /* A colliding title without our ownership marker must be untouched. */
    assert(rename(marker, parked) == 0); param_inode = inode(param);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == -1 && strstr(error, "not owned"));
    assert(registered == 5 && inode(param) == param_inode); assert(rename(parked, marker) == 0);

    /* Neither asset symlinks nor sce_sys symlinks may redirect writes. */
    const unsigned char sentinel[] = "leave this file alone";
    write_file(outside, sentinel, sizeof(sentinel));
    assert(unlink(param) == 0 && symlink(outside, param) == 0);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == -1 && registered == 5);
    assert_file(outside, sentinel, sizeof(sentinel)); assert(unlink(param) == 0);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == 1 && registered == 6);
    assert(unlink(outside) == 0 && mkdir(outside, 0700) == 0);
    assert(rename(system, parked) == 0 && symlink(outside, system) == 0);
    assert(ks_media_install(root, &backend, error, sizeof(error)) == -1 && registered == 6);
    assert(unlink(system) == 0 && rename(parked, system) == 0 && rmdir(outside) == 0);
    assert(initialized == terminated + 1); /* failed initialization has no terminate */
    assert(ks_media_install(root, NULL, error, sizeof(error)) == -1);

    assert(unlink(param) == 0 && unlink(icon) == 0 && unlink(marker) == 0);
    assert(rmdir(system) == 0 && rmdir(base) == 0 && rmdir(root) == 0);
    puts("Media shortcut passed: scoped registration, exact assets, repeat startup, retry, API errors, ownership and symlink protection.");
    return 0;
}
