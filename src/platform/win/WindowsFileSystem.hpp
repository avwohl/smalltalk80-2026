// st80-2026 — WindowsFileSystem.hpp
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Header-only Win32 implementation of IFileSystem. Mirrors
// PosixFileSystem.hpp in shape; the difference is which CRT /
// Win32 entry points it calls. Kept out of src/platform/windows/
// because the tools (st80_probe, st80_run, st80_validate) include
// it directly without dragging in the rest of the Windows slice,
// just as the POSIX / macOS / Linux tools share
// src/platform/posix/PosixFileSystem.hpp.
//
// Filesystem contract (from IFileSystem.hpp) returns `int` for
// file handles. We use the MSVC CRT's `_open` / `_close` / `_read`
// / `_write` / `_lseek` / `_chsize` / `_fstat64` / `_commit` —
// they give us int file descriptors that match the interface.
// Directory enumeration, rename, and delete go through the native
// Win32 API (FindFirstFile / MoveFileEx / DeleteFile) since
// MSVC's CRT has no `<dirent.h>`.

#pragma once

#include "hal/IFileSystem.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <io.h>
#include <string>
#include <sys/stat.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace st80 {

class WindowsFileSystem : public IFileSystem {
 public:
    explicit WindowsFileSystem(const std::string &root)
        : root_directory(root) {}

    std::string path_for_file(const char *name) {
        std::string p = root_directory;
        if (!p.empty()) {
            const char last = p.back();
            if (last != '\\' && last != '/') p.push_back('\\');
        }
        p.append(name);
        return p;
    }

    int open_file(const char *name) override {
        std::string path = path_for_file(name);
        return ::_open(path.c_str(), _O_RDWR | _O_BINARY);
    }

    int create_file(const char *name) override {
        std::string path = path_for_file(name);
        return ::_open(path.c_str(),
                       _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
    }

    int close_file(int file_handle) override {
        return ::_close(file_handle);
    }

    int read(int file_handle, char *buffer, int bytes) override {
        return ::_read(file_handle, buffer, static_cast<unsigned>(bytes));
    }

    int write(int file_handle, const char *buffer, int bytes) override {
        return ::_write(file_handle, buffer, static_cast<unsigned>(bytes));
    }

    bool truncate_to(int file_handle, int length) override {
        return ::_chsize(file_handle, length) == 0;
    }

    int file_size(int file_handle) override {
        struct _stat64 s;
        if (::_fstat64(file_handle, &s) == 0)
            return static_cast<int>(s.st_size);
        return -1;
    }

    bool file_flush(int file_handle) override {
        return ::_commit(file_handle) == 0;
    }

    bool is_directory(const char *name) {
        std::string path = path_for_file(name);
        const DWORD attrs = ::GetFileAttributesA(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return false;
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    void enumerate_files(
        const std::function<void(const char *)> &each) override {
        std::string pattern = root_directory;
        if (!pattern.empty()) {
            const char last = pattern.back();
            if (last != '\\' && last != '/') pattern.push_back('\\');
        }
        pattern.append("*");

        WIN32_FIND_DATAA fd;
        HANDLE hFind = ::FindFirstFileA(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.cFileName[0] == '.') continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            each(fd.cFileName);
        } while (::FindNextFileA(hFind, &fd));
        ::FindClose(hFind);
    }

    bool rename_file(const char *old_name, const char *new_name) override {
        std::string old_path = path_for_file(old_name);
        std::string new_path = path_for_file(new_name);
        return ::MoveFileExA(old_path.c_str(), new_path.c_str(),
                             MOVEFILE_REPLACE_EXISTING) != 0;
    }

    bool delete_file(const char *file_name) override {
        std::string path = path_for_file(file_name);
        return ::DeleteFileA(path.c_str()) != 0;
    }

    int seek_to(int file_handle, int position) override {
        return ::_lseek(file_handle, position, SEEK_SET);
    }

    int tell(int file_handle) override {
        return ::_lseek(file_handle, 0, SEEK_CUR);
    }

    int last_error() override {
        return errno;
    }

    const char *error_text(int code) override {
        return std::strerror(code);
    }

 private:
    std::string root_directory;
};

}  // namespace st80
