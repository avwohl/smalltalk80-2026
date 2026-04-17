// st80-2026 — PosixFileSystem.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/posixfilesystem.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Header-only POSIX implementation of IFileSystem. Works on macOS
// (and therefore Mac Catalyst), Linux, other Unixes. Windows support
// is retained in the `#ifdef _WIN32` branches so the file compiles
// unchanged when we hit Phase 4; no #pragma once was present in
// dbanay's original — added here.

#pragma once

#include "hal/IFileSystem.hpp"

#include <string>
#include <cstring>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cassert>

#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#else
#include <windows.h>
#include <io.h>
#endif

namespace st80 {

class PosixFileSystem : public IFileSystem {
 public:
    explicit PosixFileSystem(const std::string &root) : root_directory(root) {}

    std::string path_for_file(const char *name) {
        return root_directory + "/" + name;
    }

    int open_file(const char *name) override {
        int fd;
        std::string path = path_for_file(name);
#ifdef _WIN32
        errno_t code = _sopen_s(&fd, path.c_str(),
                                _O_RDWR | _O_BINARY, _SH_DENYNO,
                                _S_IREAD | _S_IWRITE);
        (void)code;
#else
        fd = open(path.c_str(), O_RDWR);
#endif
        return fd;
    }

    int create_file(const char *name) override {
        int fd;
        std::string path = path_for_file(name);
#ifdef _WIN32
        errno_t code = _sopen_s(&fd, path.c_str(),
                                _O_RDWR | _O_BINARY | _O_CREAT | _O_TRUNC,
                                _SH_DENYNO, _S_IREAD | _S_IWRITE);
        (void)code;
#else
        fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
#endif
        return fd;
    }

    int close_file(int file_handle) override {
#ifdef _WIN32
        if (file_handle == -1) return -1;
        return _close(file_handle);
#else
        return close(file_handle);
#endif
    }

    int read(int file_handle, char *buffer, int bytes) override {
#ifdef _WIN32
        return _read(file_handle, buffer, bytes);
#else
        return static_cast<int>(::read(file_handle, buffer, bytes));
#endif
    }

    int write(int file_handle, const char *buffer, int bytes) override {
#ifdef _WIN32
        return _write(file_handle, buffer, bytes);
#else
        return static_cast<int>(::write(file_handle, buffer, bytes));
#endif
    }

    bool truncate_to(int file_handle, int length) override {
#ifdef _WIN32
        return _chsize(file_handle, length) != -1;
#else
        return ftruncate(file_handle, length) != -1;
#endif
    }

    int file_size(int file_handle) override {
#ifdef _WIN32
        if (file_handle == -1) return -1;
        struct _stat s;
        if (_fstat(file_handle, &s) != -1)
            return static_cast<int>(s.st_size);
        return -1;
#else
        struct stat s;
        if (fstat(file_handle, &s) != -1)
            return static_cast<int>(s.st_size);
        return -1;
#endif
    }

    bool file_flush(int file_handle) override {
#ifndef _WIN32
        return fsync(file_handle) != -1;
#else
        (void)file_handle;
        return true;
#endif
    }

#ifndef _WIN32
    bool is_directory(const char *name) {
        std::string path = path_for_file(name);
        struct stat statbuf;
        if (stat(path.c_str(), &statbuf) != 0)
            return false;
        return S_ISDIR(statbuf.st_mode);
    }
#endif

    void enumerate_files(const std::function<void(const char *)> &each) override {
#ifndef _WIN32
        DIR *dir = opendir(root_directory.c_str());
        if (!dir) return;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] != '.' && !is_directory(entry->d_name)) {
                each(entry->d_name);
            }
        }
        closedir(dir);
#else
        HANDLE hFind = INVALID_HANDLE_VALUE;
        _WIN32_FIND_DATAA findFileData;
        std::string pattern = root_directory + "\\*";
        hFind = FindFirstFileA(pattern.c_str(), &findFileData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (findFileData.cFileName[0] != '.' &&
                    (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    each(findFileData.cFileName);
                }
            } while (FindNextFileA(hFind, &findFileData) != 0);
            FindClose(hFind);
        }
#endif
    }

    bool rename_file(const char *old_name, const char *new_name) override {
        std::string old_path = path_for_file(old_name);
        std::string new_path = path_for_file(new_name);
        return rename(old_path.c_str(), new_path.c_str()) != -1;
    }

    bool delete_file(const char *file_name) override {
        std::string path = path_for_file(file_name);
#ifdef _WIN32
        return _unlink(path.c_str()) != -1;
#else
        return unlink(path.c_str()) != -1;
#endif
    }

    int seek_to(int file_handle, int position) override {
#ifdef _WIN32
        return static_cast<int>(_lseek(file_handle, position, SEEK_SET));
#else
        return static_cast<int>(lseek(file_handle, position, SEEK_SET));
#endif
    }

    int tell(int file_handle) override {
#ifdef _WIN32
        return static_cast<int>(_lseek(file_handle, 0, SEEK_CUR));
#else
        return static_cast<int>(lseek(file_handle, 0, SEEK_CUR));
#endif
    }

    int last_error() override {
        return errno;
    }

    const char *error_text(int code) override {
#ifdef _WIN32
        static char errmsg[512];
        strerror_s(errmsg, sizeof(errmsg), code);
        return errmsg;
#else
        return std::strerror(code);
#endif
    }

 private:
    std::string root_directory;
};

}  // namespace st80
