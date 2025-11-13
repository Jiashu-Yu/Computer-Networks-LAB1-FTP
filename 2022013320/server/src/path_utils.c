#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "path_utils.h"
#include <sys/stat.h>

static int path_under_root(const char *root, const char *path) {
    size_t root_len = strlen(root);
    if (root_len == 0) {
        return 0;
    }
    if (strcmp(root, "/") == 0) {
        return path[0] == '/';
    }
    if (strncmp(root, path, root_len) != 0) {
        return 0;
    }
    if (path[root_len] == '\0') {
        return 1;
    }
    return path[root_len] == '/';
}

int ftp_build_path(const ftp_session *session, const char *param, char *out, size_t out_sz) {
    if (!session || !param || param[0] == '\0' || !out || out_sz == 0) {
        return 0;
    }

    int written = -1;
    bool root_is_root = strcmp(session->root, "/") == 0;

    if (param[0] == '/') {
        if (root_is_root) {
            written = snprintf(out, out_sz, "%s", param);
        } else {
            written = snprintf(out, out_sz, "%s%s", session->root, param);
        }
    } else {
        if (root_is_root) {
            if (strcmp(session->cwd, "/") == 0) {
                written = snprintf(out, out_sz, "/%s", param);
            } else {
                written = snprintf(out, out_sz, "%s/%s", session->cwd, param);
            }
        } else {
            if (strcmp(session->cwd, "/") == 0) {
                written = snprintf(out, out_sz, "%s/%s", session->root, param);
            } else {
                written = snprintf(out, out_sz, "%s%s/%s", session->root, session->cwd, param);
            }
        }
    }

    if (written < 0 || (size_t)written >= out_sz) {
        return 0;
    }
    return 1;
}

int ftp_resolve_existing(const ftp_session *session, const char *param, char *out, size_t out_sz) {
    if (!ftp_build_path(session, param, out, out_sz)) {
        return 0;
    }

    char real_path[PATH_MAX];
    if (!realpath(out, real_path)) {
        return 0;
    }

    if (!path_under_root(session->root, real_path)) {
        return 0;
    }

    if (snprintf(out, out_sz, "%s", real_path) >= (int)out_sz) {
        return 0;
    }
    return 1;
}

int ftp_resolve_existing_directory(const ftp_session *session, const char *param, char *out, size_t out_sz) {
    if (!ftp_resolve_existing(session, param, out, out_sz)) {
        return 0;
    }

    struct stat st;
    if (stat(out, &st) < 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    return 1;
}

int ftp_resolve_for_creation(const ftp_session *session, const char *param,
                             char *full_out, size_t full_out_sz,
                             char *parent_out, size_t parent_out_sz) {
    if (!session || !param || param[0] == '\0' || !full_out || full_out_sz == 0) {
        return 0;
    }

    char candidate[PATH_MAX];
    if (!ftp_build_path(session, param, candidate, sizeof(candidate))) {
        return 0;
    }

    char trimmed[PATH_MAX];
    if (snprintf(trimmed, sizeof(trimmed), "%s", candidate) >= (int)sizeof(trimmed)) {
        return 0;
    }

    size_t len = strlen(trimmed);
    while (len > 1 && trimmed[len - 1] == '/') {
        trimmed[len - 1] = '\0';
        len--;
    }
    if (len == 0) {
        return 0;
    }

    char *slash = strrchr(trimmed, '/');
    if (!slash) {
        return 0;
    }

    const char *leaf_ptr = slash + 1;
    char leaf[NAME_MAX + 1];
    int lw = snprintf(leaf, sizeof(leaf), "%s", leaf_ptr);
    if (lw < 0 || (size_t)lw >= sizeof(leaf)) {
        return 0;
    }
    if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
        return 0;
    }
    for (const char *p = leaf; *p; ++p) {
        if (*p == '/') {
            return 0;
        }
    }

    char parent_candidate[PATH_MAX];
    if (slash == trimmed) {
        if (strcmp(session->root, "/") != 0) {
            return 0;
        }
        if (trimmed[1] == '\0') {
            return 0;
        }
        if (snprintf(parent_candidate, sizeof(parent_candidate), "%s", "/") >= (int)sizeof(parent_candidate)) {
            return 0;
        }
    } else {
        *slash = '\0';
        if (snprintf(parent_candidate, sizeof(parent_candidate), "%s", trimmed) >= (int)sizeof(parent_candidate)) {
            return 0;
        }
    }

    char parent_real[PATH_MAX];
    if (!realpath(parent_candidate, parent_real)) {
        return 0;
    }

    if (!path_under_root(session->root, parent_real)) {
        return 0;
    }

    size_t parent_len = strlen(parent_real);
    const char *sep = (parent_len > 0 && parent_real[parent_len - 1] == '/') ? "" : "/";
    if (snprintf(full_out, full_out_sz, "%s%s%s", parent_real, sep, leaf) >= (int)full_out_sz) {
        return 0;
    }

    if (!path_under_root(session->root, full_out)) {
        return 0;
    }

    if (parent_out && parent_out_sz > 0) {
        if (snprintf(parent_out, parent_out_sz, "%s", parent_real) >= (int)parent_out_sz) {
            return 0;
        }
    }

    return 1;
}

int ftp_get_cwd_realpath(const ftp_session *session, char *out, size_t out_sz) {
    if (!session || !out || out_sz == 0) {
        return 0;
    }

    char candidate[PATH_MAX];
    if (!ftp_build_path(session, session->cwd, candidate, sizeof(candidate))) {
        return 0;
    }

    char resolved[PATH_MAX];
    if (!realpath(candidate, resolved)) {
        return 0;
    }

    if (!path_under_root(session->root, resolved)) {
        return 0;
    }

    if (snprintf(out, out_sz, "%s", resolved) >= (int)out_sz) {
        return 0;
    }
    return 1;
}

int ftp_make_relative_path(const ftp_session *session,
                           const char *abs_path,
                           char *rel_out,
                           size_t rel_out_sz) {
    if (!session || !abs_path || !rel_out || rel_out_sz == 0) {
        return 0;
    }

    if (!path_under_root(session->root, abs_path)) {
        return 0;
    }

    size_t root_len = strlen(session->root);
    const char *suffix = abs_path + root_len;

    if (*suffix == '\0') {
        if (snprintf(rel_out, rel_out_sz, "%s", "/") >= (int)rel_out_sz) {
            return 0;
        }
        return 1;
    }

    const char *prefix = (*suffix == '/') ? "" : "/";
    if (snprintf(rel_out, rel_out_sz, "%s%s", prefix, suffix) >= (int)rel_out_sz) {
        return 0;
    }

    if (rel_out[0] != '/') {
        size_t len = strnlen(rel_out, rel_out_sz);
        if (len + 2 > rel_out_sz) {        // 需要 '/' 和 '\0'
            return 0;
        }
        memmove(rel_out + 1, rel_out, len + 1); // 包括终止符一起后移
        rel_out[0] = '/';
    }

    return 1;
}