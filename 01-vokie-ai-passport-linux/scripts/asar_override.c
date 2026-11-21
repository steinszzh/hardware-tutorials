#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *ORIG_ASAR = "/opt/Vokie/resources/app.asar";
static char PATCHED_ASAR[PATH_MAX] = {0};

static const char *get_patched_asar(void) {
    if (PATCHED_ASAR[0] == '\0') {
        const char *env_override = getenv("VOKIE_PATCHED_ASAR");
        if (env_override && env_override[0] != '\0') {
            snprintf(PATCHED_ASAR, sizeof(PATCHED_ASAR), "%s", env_override);
        } else {
            const char *home = getenv("HOME");
            if (home && home[0] != '\0') {
                snprintf(PATCHED_ASAR, sizeof(PATCHED_ASAR), "%s/.config/vokie/app.asar", home);
            } else {
                snprintf(PATCHED_ASAR, sizeof(PATCHED_ASAR), "%s", ORIG_ASAR);
            }
        }
    }
    return PATCHED_ASAR;
}

static const char *remap_path(const char *path, char *buf, size_t buflen) {
    if (!path) return path;
    size_t orig_len = strlen(ORIG_ASAR);
    if (strncmp(path, ORIG_ASAR, orig_len) == 0) {
        const char *patched = get_patched_asar();
        if (path[orig_len] == '\0') {
            return patched;
        }
        if (path[orig_len] == '/') {
            snprintf(buf, buflen, "%s%s", patched, path + orig_len);
            return buf;
        }
    }
    return path;
}

int open(const char *pathname, int flags, ...) {
    static int (*real_open)(const char *, int, ...) = NULL;
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    char buf[PATH_MAX];
    const char *p = remap_path(pathname, buf, sizeof(buf));
    mode_t mode = 0;
    if (__OPEN_NEEDS_MODE(flags)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_open(p, flags, mode);
    }
    return real_open(p, flags);
}

int open64(const char *pathname, int flags, ...) {
    static int (*real_open64)(const char *, int, ...) = NULL;
    if (!real_open64) real_open64 = dlsym(RTLD_NEXT, "open64");
    char buf[PATH_MAX];
    const char *p = remap_path(pathname, buf, sizeof(buf));
    mode_t mode = 0;
    if (__OPEN_NEEDS_MODE(flags)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_open64(p, flags, mode);
    }
    return real_open64(p, flags);
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    static int (*real_openat)(int, const char *, int, ...) = NULL;
    if (!real_openat) real_openat = dlsym(RTLD_NEXT, "openat");
    char buf[PATH_MAX];
    const char *p = remap_path(pathname, buf, sizeof(buf));
    mode_t mode = 0;
    if (__OPEN_NEEDS_MODE(flags)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_openat(dirfd, p, flags, mode);
    }
    return real_openat(dirfd, p, flags);
}

int openat64(int dirfd, const char *pathname, int flags, ...) {
    static int (*real_openat64)(int, const char *, int, ...) = NULL;
    if (!real_openat64) real_openat64 = dlsym(RTLD_NEXT, "openat64");
    char buf[PATH_MAX];
    const char *p = remap_path(pathname, buf, sizeof(buf));
    mode_t mode = 0;
    if (__OPEN_NEEDS_MODE(flags)) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_openat64(dirfd, p, flags, mode);
    }
    return real_openat64(dirfd, p, flags);
}

int stat(const char *pathname, struct stat *statbuf) {
    static int (*real_stat)(const char *, struct stat *) = NULL;
    if (!real_stat) real_stat = dlsym(RTLD_NEXT, "stat");
    char buf[PATH_MAX];
    return real_stat(remap_path(pathname, buf, sizeof(buf)), statbuf);
}

int stat64(const char *pathname, struct stat64 *statbuf) {
    static int (*real_stat64)(const char *, struct stat64 *) = NULL;
    if (!real_stat64) real_stat64 = dlsym(RTLD_NEXT, "stat64");
    char buf[PATH_MAX];
    return real_stat64(remap_path(pathname, buf, sizeof(buf)), statbuf);
}

int lstat(const char *pathname, struct stat *statbuf) {
    static int (*real_lstat)(const char *, struct stat *) = NULL;
    if (!real_lstat) real_lstat = dlsym(RTLD_NEXT, "lstat");
    char buf[PATH_MAX];
    return real_lstat(remap_path(pathname, buf, sizeof(buf)), statbuf);
}

int lstat64(const char *pathname, struct stat64 *statbuf) {
    static int (*real_lstat64)(const char *, struct stat64 *) = NULL;
    if (!real_lstat64) real_lstat64 = dlsym(RTLD_NEXT, "lstat64");
    char buf[PATH_MAX];
    return real_lstat64(remap_path(pathname, buf, sizeof(buf)), statbuf);
}

int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags) {
    static int (*real_fstatat)(int, const char *, struct stat *, int) = NULL;
    if (!real_fstatat) real_fstatat = dlsym(RTLD_NEXT, "fstatat");
    char buf[PATH_MAX];
    return real_fstatat(dirfd, remap_path(pathname, buf, sizeof(buf)), statbuf, flags);
}

int fstatat64(int dirfd, const char *pathname, struct stat64 *statbuf, int flags) {
    static int (*real_fstatat64)(int, const char *, struct stat64 *, int) = NULL;
    if (!real_fstatat64) real_fstatat64 = dlsym(RTLD_NEXT, "fstatat64");
    char buf[PATH_MAX];
    return real_fstatat64(dirfd, remap_path(pathname, buf, sizeof(buf)), statbuf, flags);
}

int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *statxbuf) {
    static int (*real_statx)(int, const char *, int, unsigned int, struct statx *) = NULL;
    if (!real_statx) real_statx = dlsym(RTLD_NEXT, "statx");
    char buf[PATH_MAX];
    return real_statx(dirfd, remap_path(pathname, buf, sizeof(buf)), flags, mask, statxbuf);
}

int access(const char *pathname, int mode) {
    static int (*real_access)(const char *, int) = NULL;
    if (!real_access) real_access = dlsym(RTLD_NEXT, "access");
    char buf[PATH_MAX];
    return real_access(remap_path(pathname, buf, sizeof(buf)), mode);
}

FILE *fopen(const char *pathname, const char *mode) {
    static FILE *(*real_fopen)(const char *, const char *) = NULL;
    if (!real_fopen) real_fopen = dlsym(RTLD_NEXT, "fopen");
    char buf[PATH_MAX];
    return real_fopen(remap_path(pathname, buf, sizeof(buf)), mode);
}

FILE *fopen64(const char *pathname, const char *mode) {
    static FILE *(*real_fopen64)(const char *, const char *) = NULL;
    if (!real_fopen64) real_fopen64 = dlsym(RTLD_NEXT, "fopen64");
    char buf[PATH_MAX];
    return real_fopen64(remap_path(pathname, buf, sizeof(buf)), mode);
}
