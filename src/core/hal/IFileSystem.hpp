// st80-2026 — IFileSystem.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/filesystem.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal file-system abstraction used by ObjectMemory to load and
// save snapshots and by primitive file operations. Platform
// implementations live under src/platform/.

#pragma once

#include <cstdint>
#include <functional>

namespace st80 {

class IFileSystem {
 public:
    virtual ~IFileSystem() = default;

    // File oriented operations
    virtual int create_file(const char *name) = 0;
    virtual int open_file(const char *name) = 0;
    virtual int close_file(int file_handle) = 0;

    virtual int seek_to(int file_handle, int position) = 0;
    virtual int tell(int file_handle) = 0;
    virtual int read(int file_handle, char *buffer, int bytes) = 0;
    virtual int write(int file_handle, const char *buffer, int bytes) = 0;

    virtual bool truncate_to(int file_handle, int length) = 0;
    virtual int file_size(int file_handle) = 0;
    virtual bool file_flush(int file_handle) = 0;

    // Directory orientated operations
    virtual void enumerate_files(const std::function<void(const char *)> &each) = 0;

    virtual bool rename_file(const char *old_name, const char *new_name) = 0;
    virtual bool delete_file(const char *file_name) = 0;

    // Error handling
    virtual int last_error() = 0;
    virtual const char *error_text(int code) = 0;
};

}  // namespace st80
