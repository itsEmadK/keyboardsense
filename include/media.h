/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_MEDIA_H
#define KS_MEDIA_H
#include <stddef.h>

extern const char ks_media_title_id[];
extern const unsigned char ks_media_param[], ks_media_icon[];
extern const size_t ks_media_param_size, ks_media_icon_size;

typedef struct {
    int (*initialize)(void);
    int (*register_title)(const char *title, const char *app_root);
    int (*terminate)(void);
} KsMediaBackend;

/* Register only KeyboardSense. Returns 1 for changed assets, 0 for unchanged
 * assets, or -1 with a diagnostic. Unchanged assets are still re-registered to
 * recover from a database rebuild or a previous registration failure. */
int ks_media_install(const char *app_root, const KsMediaBackend *, char *error, size_t);
int ks_media_install_ps5(char *error, size_t);
#endif
