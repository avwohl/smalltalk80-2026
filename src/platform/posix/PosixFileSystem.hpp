// st80-2026 — PosixFileSystem.hpp
//
// Ported from dbanay/Smalltalk @ ab6ab55:src/posixfilesystem.h
//   Copyright (c) 2020 Dan Banay. MIT License.
//   See THIRD_PARTY_LICENSES for full text.
// Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Header-only POSIX implementation of IFileSystem. Shared between
// macOS (native + Mac Catalyst) and Linux. When a Win32 port lands
// it gets its own `src/platform/win32/WindowsFileSystem.hpp`; we
// deliberately do not maintain mixed `#ifdef _WIN32` branches here.

#pragma once

#include "hal/IFileSystem.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace st80 {

class PosixFileSystem : public IFileSystem {
 public:
    explicit PosixFileSystem(const std::string &root) : root_directory(root) {}

    std::string path_for_file(const char *name) {
        return root_directory + "/" + name;
    }

    int open_file(const char *name) override {
        std::string path = path_for_file(name);
        return open(path.c_str(), O_RDWR);
    }

    int create_file(const char *name) override {
        std::string path = path_for_file(name);
        return open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    }

    int close_file(int file_handle) override {
        return close(file_handle);
    }

    int read(int file_handle, char *buffer, int bytes) override {
        return static_cast<int>(::read(file_handle, buffer, bytes));
    }

    int write(int file_handle, const char *buffer, int bytes) override {
        return static_cast<int>(::write(file_handle, buffer, bytes));
    }

    bool truncate_to(int file_handle, int length) override {
        return ftruncate(file_handle, length) != -1;
    }

    int file_size(int file_handle) override {
        struct stat s;
        if (fstat(file_handle, &s) != -1)
            return static_cast<int>(s.st_size);
        return -1;
    }

    bool file_flush(int file_handle) override {
        return fsync(file_handle) != -1;
    }

    bool is_directory(const char *name) {
        std::string path = path_for_file(name);
        struct stat statbuf;
        if (stat(path.c_str(), &statbuf) != 0)
            return false;
        return S_ISDIR(statbuf.st_mode);
    }

    void enumerate_files(const std::function<void(const char *)> &each) override {
        DIR *dir = opendir(root_directory.c_str());
        if (!dir) return;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (entry->d_name[0] != '.' && !is_directory(entry->d_name)) {
                each(entry->d_name);
            }
        }
        closedir(dir);
    }

    bool rename_file(const char *old_name, const char *new_name) override {
        std::string old_path = path_for_file(old_name);
        std::string new_path = path_for_file(new_name);
        return rename(old_path.c_str(), new_path.c_str()) != -1;
    }

    bool delete_file(const char *file_name) override {
        std::string path = path_for_file(file_name);
        return unlink(path.c_str()) != -1;
    }

    int seek_to(int file_handle, int position) override {
        return static_cast<int>(lseek(file_handle, position, SEEK_SET));
    }

    int tell(int file_handle) override {
        return static_cast<int>(lseek(file_handle, 0, SEEK_CUR));
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
