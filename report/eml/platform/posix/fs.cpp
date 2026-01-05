/**
 * @file fs.cpp
 * @brief POSIX Platform - Filesystem Implementation
 * 
 * Implements eml_fs.h interface for POSIX systems using standard C/C++ file I/O.
 */

#include "../../pal/eml_fs.h"
#include "../../pal/eml_io.h"
#include "eml_posix.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

namespace eml {
namespace pal {

// File handle wrapper
struct EmlFileHandle {
    FILE* fp;
    bool valid;
    
    EmlFileHandle() : fp(nullptr), valid(false) {}
    explicit EmlFileHandle(FILE* f) : fp(f), valid(f != nullptr) {}
};

static EmlStorageType g_active_storage = EmlStorageType::HOST_FS;
static char g_root_path[256] = {0};

// Ensure the root directory exists
static bool ensure_root_dir() {
    if (g_root_path[0] == '\0') {
        strncpy(g_root_path, EML_POSIX_ROOT_PATH, sizeof(g_root_path) - 1);
    }
    
    struct stat st;
    if (stat(g_root_path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    
    // Create directory
    return mkdir(g_root_path, 0755) == 0;
}

// Build full path from relative path
static void build_full_path(char* buffer, size_t buflen, const char* path) {
    if (!path || path[0] == '\0') {
        strncpy(buffer, g_root_path, buflen);
        return;
    }
    
    // If path starts with /, treat as relative to root
    if (path[0] == '/') {
        snprintf(buffer, buflen, "%s%s", g_root_path, path);
    } else {
        snprintf(buffer, buflen, "%s/%s", g_root_path, path);
    }
}

bool eml_fs_init(EmlStorageType type) {
    (void)type;  // POSIX only supports HOST_FS
    
    g_active_storage = EmlStorageType::HOST_FS;
    
    strncpy(g_root_path, EML_POSIX_ROOT_PATH, sizeof(g_root_path) - 1);
    
    if (!ensure_root_dir()) {
        eml_printf("Warning: Could not create root directory: %s\n", g_root_path);
        // Continue anyway, user may create it manually
    }
    
    eml_printf("✅ POSIX filesystem initialized (root: %s)\n", g_root_path);
    return true;
}

void eml_fs_deinit() {
    // Nothing to clean up for POSIX
}

const char* eml_fs_storage_name() {
    return "Host Filesystem";
}

EmlStorageType eml_fs_storage_type() {
    return g_active_storage;
}

bool eml_fs_exists(const char* path) {
    char full_path[512];
    build_full_path(full_path, sizeof(full_path), path);
    
    struct stat st;
    return stat(full_path, &st) == 0;
}

EmlFileHandle* eml_fs_open(const char* path, EmlFileMode mode) {
    char full_path[512];
    build_full_path(full_path, sizeof(full_path), path);
    
    const char* fmode;
    switch (mode) {
        case EmlFileMode::WRITE:
            fmode = "wb";
            break;
        case EmlFileMode::APPEND:
            fmode = "ab";
            break;
        case EmlFileMode::READ_WRITE:
            // Check if file exists
            if (eml_fs_exists(path)) {
                fmode = "r+b";
            } else {
                fmode = "w+b";
            }
            break;
        case EmlFileMode::READ:
        default:
            fmode = "rb";
            break;
    }
    
    // Create parent directories if writing
    if (mode != EmlFileMode::READ) {
        char* dir_path = strdup(full_path);
        char* last_slash = strrchr(dir_path, '/');
        if (last_slash && last_slash != dir_path) {
            *last_slash = '\0';
            // Recursively create directories
            char* p = dir_path;
            while (*p) {
                if (*p == '/' && p != dir_path) {
                    *p = '\0';
                    mkdir(dir_path, 0755);
                    *p = '/';
                }
                p++;
            }
            mkdir(dir_path, 0755);
        }
        free(dir_path);
    }
    
    FILE* fp = fopen(full_path, fmode);
    if (!fp) {
        return nullptr;
    }
    
    EmlFileHandle* handle = new EmlFileHandle(fp);
    return handle;
}

void eml_fs_close(EmlFileHandle* file) {
    if (!file) return;
    if (file->fp) {
        fclose(file->fp);
    }
    delete file;
}

size_t eml_fs_read(EmlFileHandle* file, void* buffer, size_t size) {
    if (!file || !file->fp || !buffer) return 0;
    return fread(buffer, 1, size, file->fp);
}

size_t eml_fs_write(EmlFileHandle* file, const void* buffer, size_t size) {
    if (!file || !file->fp || !buffer) return 0;
    return fwrite(buffer, 1, size, file->fp);
}

bool eml_fs_seek(EmlFileHandle* file, int64_t offset, EmlSeekOrigin origin) {
    if (!file || !file->fp) return false;
    
    int whence;
    switch (origin) {
        case EmlSeekOrigin::CURRENT:
            whence = SEEK_CUR;
            break;
        case EmlSeekOrigin::END:
            whence = SEEK_END;
            break;
        case EmlSeekOrigin::BEGIN:
        default:
            whence = SEEK_SET;
            break;
    }
    
    return fseek(file->fp, static_cast<long>(offset), whence) == 0;
}

int64_t eml_fs_tell(EmlFileHandle* file) {
    if (!file || !file->fp) return -1;
    return static_cast<int64_t>(ftell(file->fp));
}

int64_t eml_fs_size(EmlFileHandle* file) {
    if (!file || !file->fp) return -1;
    
    long current = ftell(file->fp);
    fseek(file->fp, 0, SEEK_END);
    long size = ftell(file->fp);
    fseek(file->fp, current, SEEK_SET);
    
    return static_cast<int64_t>(size);
}

void eml_fs_flush(EmlFileHandle* file) {
    if (!file || !file->fp) return;
    fflush(file->fp);
}

bool eml_fs_remove(const char* path) {
    char full_path[512];
    build_full_path(full_path, sizeof(full_path), path);
    return remove(full_path) == 0;
}

bool eml_fs_rename(const char* old_path, const char* new_path) {
    char old_full[512], new_full[512];
    build_full_path(old_full, sizeof(old_full), old_path);
    build_full_path(new_full, sizeof(new_full), new_path);
    return rename(old_full, new_full) == 0;
}

bool eml_fs_mkdir(const char* path) {
    char full_path[512];
    build_full_path(full_path, sizeof(full_path), path);
    
    struct stat st;
    if (stat(full_path, &st) == 0) {
        return S_ISDIR(st.st_mode);  // Already exists
    }
    
    return mkdir(full_path, 0755) == 0;
}

bool eml_fs_rmdir(const char* path) {
    char full_path[512];
    build_full_path(full_path, sizeof(full_path), path);
    return rmdir(full_path) == 0;
}

uint64_t eml_fs_total_bytes() {
    struct statvfs st;
    if (statvfs(g_root_path, &st) == 0) {
        return static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
    }
    return 0;
}

uint64_t eml_fs_used_bytes() {
    struct statvfs st;
    if (statvfs(g_root_path, &st) == 0) {
        uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
        uint64_t avail = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
        return total - avail;
    }
    return 0;
}

size_t eml_fs_max_dataset_bytes() {
    return posix::max_dataset_bytes();
}

size_t eml_fs_max_infer_log_bytes() {
    return posix::max_infer_log_bytes();
}

bool eml_fs_is_sd_based() {
    return false;
}

bool eml_fs_is_flash() {
    return false;  // It's host filesystem
}

} // namespace pal
} // namespace eml
