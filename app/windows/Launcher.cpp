// st80-2026 — Launcher.cpp (Windows)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Pure-Win32 implementation of the launcher described in
// Launcher.hpp. No SDL/MFC/WTL — only USER32, GDI32, COMCTL32,
// COMDLG32, SHELL32, SHLWAPI, ADVAPI32, OLE32, and WINHTTP, all of
// which ship with Windows.
//
// Three moving parts:
//
//   1. Library catalog (`library.json`) — user-facing state: image
//      entries, custom names, the auto-launch pointer. Lives under
//      %USERPROFILE%\Documents\Smalltalk-80\library.json, mirroring
//      the Catalyst frontend's `Documents/image-library.json`.
//
//   2. Manifest (`manifest.json`) — catalog of downloadable images,
//      fetched from st80-images on demand. Each asset carries a
//      SHA256 digest; the launcher verifies after download and
//      deletes the file on mismatch. A bundled fallback entry lets
//      the launcher still offer Xerox v2 if the manifest fetch
//      fails (no network, DNS down, repo moved).
//
//   3. Win32 UI — sortable ListView with filter box, detail panel,
//      rename / duplicate / show-in-Explorer / auto-launch star /
//      delete, all wired to both a button strip and a right-click
//      context menu. Downloads run on a worker thread and post
//      progress back via WM_APP_* messages.
//
// Ported layout from app/iospharo/iospharo/Views/ImageLibraryView.swift.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Launcher.hpp"
#include "Json.hpp"
#include "Sha256.hpp"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace st80 {

namespace {

// ----------------------------------------------------------------------------
// IDs / messages / constants
// ----------------------------------------------------------------------------

constexpr int ID_LIST         = 1001;
constexpr int ID_LAUNCH       = 1002;
constexpr int ID_DOWNLOAD     = 1003;
constexpr int ID_ADD_FILE     = 1004;
constexpr int ID_DELETE       = 1005;
constexpr int ID_CANCEL       = 1006;
constexpr int ID_RENAME       = 1007;
constexpr int ID_DUPLICATE    = 1008;
constexpr int ID_SHOW_FOLDER  = 1009;
constexpr int ID_AUTO_LAUNCH  = 1010;
constexpr int ID_FILTER_EDIT  = 1011;
constexpr int ID_CONTEXT_BASE = 1100;   // popup menu IDs

// WPARAM = 0..100 percent, LPARAM = 0
constexpr UINT WM_APP_DL_PROGRESS = WM_APP + 1;
// WPARAM = 0 (ok) or 1 (fail)
// LPARAM = heap-allocated wchar_t* (dest path on success,
//          error message on failure) — receiver frees with delete[].
constexpr UINT WM_APP_DL_DONE     = WM_APP + 2;

constexpr wchar_t kWindowClass[]   = L"St80LauncherClass";
constexpr wchar_t kRegSubkey[]     = L"Software\\Aaron Wohl\\st80-2026";
constexpr wchar_t kRegLastValue[]  = L"LastImagePath";  // legacy migration

// Where the manifest is fetched from. Hosted out-of-tree in the
// st80-images companion repo. Keep the path stable — app builds in
// the wild depend on it.
constexpr wchar_t kManifestHost[]  = L"raw.githubusercontent.com";
constexpr wchar_t kManifestPath[]  =
    L"/avwohl/st80-images/main/manifest.json";

// ----------------------------------------------------------------------------
// UTF-8 / UTF-16 helpers
// ----------------------------------------------------------------------------

std::string utf16to8(const std::wstring &w) {
    if (w.empty()) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                         static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

std::wstring utf8to16(const std::string &s) {
    if (s.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                         static_cast<int>(s.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), need);
    return out;
}

// ----------------------------------------------------------------------------
// Filesystem helpers
// ----------------------------------------------------------------------------

std::wstring getDocumentsDir() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw))) {
        return {};
    }
    std::wstring out(raw);
    CoTaskMemFree(raw);
    return out;
}

std::wstring getSt80Root() {
    std::wstring d = getDocumentsDir();
    if (d.empty()) return {};
    return d + L"\\Smalltalk-80";
}

std::wstring getImagesRoot() {
    std::wstring r = getSt80Root();
    if (r.empty()) return {};
    return r + L"\\Images";
}

std::wstring getLibraryJsonPath() {
    std::wstring r = getSt80Root();
    if (r.empty()) return {};
    return r + L"\\library.json";
}

std::wstring getManifestCachePath() {
    std::wstring r = getSt80Root();
    if (r.empty()) return {};
    return r + L"\\manifest-cache.json";
}

bool ensureDirectoryTree(const std::wstring &path) {
    if (path.empty()) return false;
    const int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return rc == ERROR_SUCCESS ||
           rc == ERROR_ALREADY_EXISTS ||
           rc == ERROR_FILE_EXISTS;
}

bool fileExistsW(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExistsW(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES &&
           (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool deleteDirectoryRecursive(const std::wstring &path) {
    std::vector<wchar_t> buf(path.size() + 2, L'\0');
    std::copy(path.begin(), path.end(), buf.begin());
    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = buf.data();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION |
                FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&op) == 0;
}

bool copyDirectoryRecursive(const std::wstring &src,
                            const std::wstring &dst) {
    // SHFileOperation CopyItem on a directory source will recurse.
    const std::wstring srcStar = src + L"\\*";
    std::vector<wchar_t> fromBuf(srcStar.size() + 2, L'\0');
    std::copy(srcStar.begin(), srcStar.end(), fromBuf.begin());
    std::vector<wchar_t> toBuf(dst.size() + 2, L'\0');
    std::copy(dst.begin(), dst.end(), toBuf.begin());
    if (!ensureDirectoryTree(dst)) return false;

    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_COPY;
    op.pFrom  = fromBuf.data();
    op.pTo    = toBuf.data();
    op.fFlags = FOF_NO_UI | FOF_NOCONFIRMATION |
                FOF_NOERRORUI | FOF_SILENT | FOF_NOCONFIRMMKDIR;
    return SHFileOperationW(&op) == 0;
}

std::uint64_t fileSizeBytes(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
        return 0;
    return (static_cast<std::uint64_t>(fa.nFileSizeHigh) << 32) |
           fa.nFileSizeLow;
}

FILETIME fileModifiedTime(const std::wstring &path) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    FILETIME zero{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa))
        return zero;
    return fa.ftLastWriteTime;
}

std::uint64_t filetimeToUnix(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // 100-ns intervals since 1601-01-01; to Unix seconds:
    //   subtract 116444736000000000 (intervals between 1601 and 1970)
    //   divide by 10'000'000
    if (u.QuadPart < 116444736000000000ULL) return 0;
    return (u.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

// ---- filesystem I/O helpers -------------------------------------------------

std::string readFileUtf8(const std::wstring &path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::string out(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    out.resize(read);
    CloseHandle(h);
    return out;
}

bool writeFileUtf8(const std::wstring &path, const std::string &data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()),
                        &written, nullptr);
    CloseHandle(h);
    return ok && written == data.size();
}

// ----------------------------------------------------------------------------
// ISO-8601 timestamp helpers
// ----------------------------------------------------------------------------

std::string nowIso8601() {
    SYSTEMTIME t;
    GetSystemTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf),
                  "%04u-%02u-%02uT%02u:%02u:%02uZ",
                  t.wYear, t.wMonth, t.wDay,
                  t.wHour, t.wMinute, t.wSecond);
    return buf;
}

// ----------------------------------------------------------------------------
// Slug helpers
// ----------------------------------------------------------------------------

std::wstring makeSlug(const std::wstring &base) {
    std::wstring out;
    out.reserve(base.size() + 8);
    for (wchar_t ch : base) {
        if ((ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') ||
            ch == L'-') {
            out += ch;
        } else if (ch >= L'A' && ch <= L'Z') {
            out += static_cast<wchar_t>(ch + (L'a' - L'A'));
        } else if (ch == L' ' || ch == L'_' || ch == L'.') {
            out += L'-';
        }
    }
    if (out.empty()) out = L"image";
    if (out.size() > 40) out.resize(40);
    wchar_t tag[16];
    std::swprintf(tag, 16, L"-%04x",
                  static_cast<unsigned>(GetTickCount()) & 0xffff);
    out += tag;
    return out;
}

std::wstring newUuidHex() {
    // We don't need RFC 4122; a 16-byte random hex string is enough
    // to key entries in the catalog against each other.
    unsigned char buf[16];
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr,
                             PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        CryptGenRandom(prov, sizeof(buf), buf);
        CryptReleaseContext(prov, 0);
    } else {
        // Fallback: rand()-based (only reachable on a severely broken
        // box where CryptoAPI itself is missing).
        for (auto &b : buf) b = static_cast<unsigned char>(std::rand() & 0xff);
    }
    wchar_t out[33];
    for (int i = 0; i < 16; ++i)
        std::swprintf(out + i * 2, 3, L"%02x", buf[i]);
    out[32] = 0;
    return out;
}

// ----------------------------------------------------------------------------
// Known image filenames
// ----------------------------------------------------------------------------

const wchar_t *kKnownImageNames[] = {
    L"VirtualImage",
    L"VirtualImageLSB",
    L"Smalltalk.image",
    L"smalltalk.image",
};

std::wstring findImageFileIn(const std::wstring &slugDir) {
    for (const wchar_t *name : kKnownImageNames) {
        std::wstring candidate = slugDir + L"\\" + name;
        if (fileExistsW(candidate)) return name;
    }
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((slugDir + L"\\*.image").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        std::wstring name = fd.cFileName;
        FindClose(h);
        return name;
    }
    return {};
}

// ----------------------------------------------------------------------------
// Library catalog
// ----------------------------------------------------------------------------
//
// library.json format:
//   {
//     "version": 1,
//     "autoLaunchId": "<uuid>" (optional),
//     "images": [
//       { "id": "...", "name": "display", "slug": "dirname",
//         "imageFileName": "VirtualImage",
//         "addedAt": "ISO-8601",
//         "lastLaunchedAt": "ISO-8601" (optional) }
//     ]
//   }

struct LibraryEntry {
    std::wstring id;
    std::wstring name;
    std::wstring slug;
    std::wstring imageFileName;
    std::string  addedAtIso;
    std::string  lastLaunchedAtIso;

    // Derived — not persisted:
    std::wstring   fullImagePath;
    std::uint64_t  sizeBytes = 0;
    std::uint64_t  lastModUnix = 0;
    std::wstring   lastModFormatted;
};

struct Library {
    std::vector<LibraryEntry> images;
    std::wstring              autoLaunchId;
};

std::wstring formatFileTimeLocal(FILETIME ft) {
    FILETIME local{};
    FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st)) return {};
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%04u-%02u-%02u %02u:%02u",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

// Populate derived fields on an entry from its filesystem state.
void hydrate(LibraryEntry &e) {
    const std::wstring root = getImagesRoot();
    if (root.empty()) { e.fullImagePath.clear(); return; }
    const std::wstring slugDir = root + L"\\" + e.slug;
    if (e.imageFileName.empty()) {
        e.imageFileName = findImageFileIn(slugDir);
    }
    if (e.imageFileName.empty()) {
        e.fullImagePath.clear();
        return;
    }
    e.fullImagePath    = slugDir + L"\\" + e.imageFileName;
    e.sizeBytes        = fileSizeBytes(e.fullImagePath);
    FILETIME ft        = fileModifiedTime(e.fullImagePath);
    e.lastModUnix      = filetimeToUnix(ft);
    e.lastModFormatted = formatFileTimeLocal(ft);
}

Library loadLibraryJson() {
    Library lib;
    const std::wstring path = getLibraryJsonPath();
    if (path.empty() || !fileExistsW(path)) return lib;

    JsonValue root = JsonValue::parse(readFileUtf8(path));
    if (!root.isObject()) return lib;

    lib.autoLaunchId = utf8to16(root.getString("autoLaunchId"));
    const auto &arr = root.get("images").asArray();
    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        LibraryEntry e;
        e.id                = utf8to16(v.getString("id"));
        e.name              = utf8to16(v.getString("name"));
        e.slug              = utf8to16(v.getString("slug"));
        e.imageFileName     = utf8to16(v.getString("imageFileName"));
        e.addedAtIso        = v.getString("addedAt");
        e.lastLaunchedAtIso = v.getString("lastLaunchedAt");
        if (e.slug.empty()) continue;
        lib.images.push_back(std::move(e));
    }
    return lib;
}

void saveLibraryJson(const Library &lib) {
    JsonValue root = JsonValue::Object();
    root.mutableObject()["version"] = JsonValue(1.0);
    if (!lib.autoLaunchId.empty())
        root.mutableObject()["autoLaunchId"] =
            JsonValue(utf16to8(lib.autoLaunchId));

    JsonValue arr = JsonValue::Array();
    for (const auto &e : lib.images) {
        JsonValue obj = JsonValue::Object();
        obj.mutableObject()["id"]            = JsonValue(utf16to8(e.id));
        obj.mutableObject()["name"]          = JsonValue(utf16to8(e.name));
        obj.mutableObject()["slug"]          = JsonValue(utf16to8(e.slug));
        obj.mutableObject()["imageFileName"] = JsonValue(utf16to8(e.imageFileName));
        obj.mutableObject()["addedAt"]       = JsonValue(e.addedAtIso);
        if (!e.lastLaunchedAtIso.empty())
            obj.mutableObject()["lastLaunchedAt"] = JsonValue(e.lastLaunchedAtIso);
        arr.mutableArray().push_back(std::move(obj));
    }
    root.mutableObject()["images"] = std::move(arr);

    const std::wstring path = getLibraryJsonPath();
    if (path.empty()) return;
    ensureDirectoryTree(getSt80Root());
    writeFileUtf8(path, root.dump(2));
}

// Scan Images\ for any slug directories not already in the library
// catalog (e.g. the user placed files there manually, or the legacy
// registry-only code path created Images/xerox-v2/ without writing
// a catalog). Produces a library synced with disk.
Library reconcileWithFilesystem(Library lib) {
    const std::wstring root = getImagesRoot();
    if (root.empty() || !dirExistsW(root)) {
        // Drop entries whose directories disappeared.
        lib.images.erase(
            std::remove_if(lib.images.begin(), lib.images.end(),
                [](const LibraryEntry &e) {
                    return !dirExistsW(getImagesRoot() + L"\\" + e.slug);
                }),
            lib.images.end());
        return lib;
    }

    // Drop entries whose directories disappeared.
    lib.images.erase(
        std::remove_if(lib.images.begin(), lib.images.end(),
            [&](const LibraryEntry &e) {
                return !dirExistsW(root + L"\\" + e.slug);
            }),
        lib.images.end());

    // Add entries for orphan slug directories.
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            std::wstring slug = fd.cFileName;
            bool known = false;
            for (const auto &e : lib.images) {
                if (_wcsicmp(e.slug.c_str(), slug.c_str()) == 0) {
                    known = true; break;
                }
            }
            if (known) continue;
            std::wstring slugDir = root + L"\\" + slug;
            std::wstring imgFile = findImageFileIn(slugDir);
            if (imgFile.empty()) continue;

            LibraryEntry e;
            e.id            = newUuidHex();
            e.slug          = slug;
            e.imageFileName = imgFile;
            // Canonical name for the canonical slug:
            if (_wcsicmp(slug.c_str(), L"xerox-v2") == 0)
                e.name = L"Xerox Smalltalk-80 v2 (1983)";
            else
                e.name = slug;
            e.addedAtIso = nowIso8601();
            lib.images.push_back(std::move(e));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    // If we have a legacy registry LastImagePath but no autoLaunchId,
    // migrate it over.
    if (lib.autoLaunchId.empty()) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubkey, 0,
                          KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH * 2];
            DWORD type = 0;
            DWORD cb   = sizeof(buf);
            if (RegQueryValueExW(key, kRegLastValue, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buf), &cb)
                == ERROR_SUCCESS && type == REG_SZ) {
                size_t chars = cb / sizeof(wchar_t);
                while (chars > 0 && buf[chars - 1] == L'\0') --chars;
                std::wstring lastPath(buf, chars);
                // Match against fullImagePath of each entry.
                for (auto &e : lib.images) {
                    hydrate(e);
                    if (!e.fullImagePath.empty() &&
                        _wcsicmp(e.fullImagePath.c_str(),
                                 lastPath.c_str()) == 0) {
                        lib.autoLaunchId = e.id;
                        break;
                    }
                }
            }
            RegCloseKey(key);
        }
    }

    // Hydrate everyone for the UI.
    for (auto &e : lib.images) hydrate(e);
    return lib;
}

// ----------------------------------------------------------------------------
// Manifest (downloadable image list)
// ----------------------------------------------------------------------------

struct ManifestAsset {
    std::wstring name;   // filename under slug dir
    std::wstring url;    // full URL
    std::string  sha256; // lowercase hex
    std::uint64_t size = 0;
};

struct ManifestImage {
    std::wstring id;
    std::wstring label;
    std::wstring slug;
    std::wstring imageFileName;
    std::vector<ManifestAsset> assets;
};

struct Manifest {
    std::vector<ManifestImage> images;
};

Manifest parseManifest(const std::string &text) {
    Manifest m;
    JsonValue root = JsonValue::parse(text);
    if (!root.isObject()) return m;
    const auto &arr = root.get("images").asArray();
    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        ManifestImage img;
        img.id            = utf8to16(v.getString("id"));
        img.label         = utf8to16(v.getString("label"));
        img.slug          = utf8to16(v.getString("slug"));
        img.imageFileName = utf8to16(v.getString("imageFileName"));
        for (const auto &av : v.get("assets").asArray()) {
            if (!av.isObject()) continue;
            ManifestAsset a;
            a.name   = utf8to16(av.getString("name"));
            a.url    = utf8to16(av.getString("url"));
            a.sha256 = av.getString("sha256");
            a.size   = static_cast<std::uint64_t>(
                           av.getNumber("size", 0.0));
            if (a.name.empty() || a.url.empty()) continue;
            img.assets.push_back(std::move(a));
        }
        if (img.slug.empty() || img.assets.empty()) continue;
        m.images.push_back(std::move(img));
    }
    return m;
}

// Built-in fallback. Used when we can't reach the manifest host.
Manifest fallbackManifest() {
    Manifest m;
    ManifestImage img;
    img.id            = L"xerox-v2";
    img.label         = L"Xerox Smalltalk-80 v2 (1983)";
    img.slug          = L"xerox-v2";
    img.imageFileName = L"VirtualImage";

    const wchar_t *base = L"https://github.com/avwohl/st80-images"
                          L"/releases/download/xerox-v2/";
    ManifestAsset a1, a2;
    a1.name   = L"VirtualImage";
    a1.url    = std::wstring(base) + L"VirtualImage";
    a2.name   = L"Smalltalk-80.sources";
    a2.url    = std::wstring(base) + L"Smalltalk-80.sources";
    img.assets.push_back(std::move(a1));
    img.assets.push_back(std::move(a2));

    m.images.push_back(std::move(img));
    return m;
}

// ----------------------------------------------------------------------------
// WinHTTP helpers
// ----------------------------------------------------------------------------

struct Url {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

Url parseUrl(const std::wstring &url) {
    Url u;
    URL_COMPONENTSW c{};
    c.dwStructSize      = sizeof(c);
    wchar_t host[256]   = {};
    wchar_t urlPath[1024]= {};
    c.lpszHostName      = host;
    c.dwHostNameLength  = ARRAYSIZE(host);
    c.lpszUrlPath       = urlPath;
    c.dwUrlPathLength   = ARRAYSIZE(urlPath);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c)) return u;
    u.host   = host;
    u.path   = urlPath;
    u.secure = (c.nScheme == INTERNET_SCHEME_HTTPS);
    u.port   = c.nPort;
    return u;
}

// Fetch a small HTTP resource into memory. Intended for the manifest
// (bounded to 1 MB defensively). Returns empty on any failure.
std::string httpGetToMemory(const std::wstring &url, size_t maxBytes = 1 << 20) {
    Url u = parseUrl(url);
    if (u.host.empty()) return {};
    HINTERNET hSession = WinHttpOpen(L"st80-2026/0.1 WinHTTP",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};
    HINTERNET hConn = WinHttpConnect(hSession, u.host.c_str(), u.port, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return {}; }
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", u.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        u.secure ? WINHTTP_FLAG_SECURE : 0);
    std::string out;
    if (hReq) {
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirect, sizeof(redirect));
        BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (ok) ok = WinHttpReceiveResponse(hReq, nullptr);
        if (ok) {
            char buf[8 * 1024];
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
                if (avail == 0) break;
                DWORD got = 0;
                DWORD want = avail < sizeof(buf) ? avail : sizeof(buf);
                if (!WinHttpReadData(hReq, buf, want, &got) || got == 0) break;
                out.append(buf, got);
                if (out.size() > maxBytes) { out.clear(); break; }
            }
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    return out;
}

Manifest fetchManifest() {
    const std::wstring cachePath = getManifestCachePath();

    // Try network first; fall back to cache, then to bundled.
    std::wstring url = std::wstring(L"https://") + kManifestHost +
                       kManifestPath;
    std::string body = httpGetToMemory(url);
    if (!body.empty()) {
        Manifest m = parseManifest(body);
        if (!m.images.empty()) {
            // Write through to cache on success.
            ensureDirectoryTree(getSt80Root());
            writeFileUtf8(cachePath, body);
            return m;
        }
    }
    if (!cachePath.empty() && fileExistsW(cachePath)) {
        Manifest m = parseManifest(readFileUtf8(cachePath));
        if (!m.images.empty()) return m;
    }
    return fallbackManifest();
}

// ----------------------------------------------------------------------------
// Downloader (WinHTTP worker thread)
// ----------------------------------------------------------------------------

struct DownloadJob {
    HWND         notifyHwnd = nullptr;
    std::wstring slugDir;
    std::wstring imageFileNameResult;
    struct Asset {
        std::wstring url;
        std::wstring fileName;
        std::string  sha256;
    };
    std::vector<Asset> assets;
};

// Report cumulative 0..100 progress to the UI thread.
void postProgress(HWND hwnd, int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    PostMessageW(hwnd, WM_APP_DL_PROGRESS,
                 static_cast<WPARAM>(percent), 0);
}

// Download one asset, streaming into `dst`. Returns SHA256 hex of
// what was written, or empty on failure.
std::string downloadAsset(HINTERNET hSession, const Url &u,
                          const std::wstring &dst,
                          HWND notifyHwnd,
                          int assetIndex, int assetCount) {
    HINTERNET hConn = WinHttpConnect(hSession, u.host.c_str(), u.port, 0);
    if (!hConn) return {};
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", u.path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        u.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!hReq) { WinHttpCloseHandle(hConn); return {}; }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirect, sizeof(redirect));

    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hReq, nullptr);
    if (!ok) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        return {};
    }

    DWORD64 contentLength = 0;
    {
        wchar_t lenBuf[64] = {};
        DWORD   lenSize    = sizeof(lenBuf);
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                lenBuf, &lenSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            contentLength = _wcstoui64(lenBuf, nullptr, 10);
        }
    }

    HANDLE hFile = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        return {};
    }

    Sha256 hasher;
    std::uint64_t received = 0;
    bool success = true;
    unsigned char chunk[16 * 1024];
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) { success = false; break; }
        if (avail == 0) break;
        DWORD readSize = (avail < sizeof(chunk)) ? avail : sizeof(chunk);
        DWORD got = 0;
        if (!WinHttpReadData(hReq, chunk, readSize, &got) || got == 0) {
            success = false; break;
        }
        DWORD written = 0;
        if (!WriteFile(hFile, chunk, got, &written, nullptr) ||
            written != got) {
            success = false; break;
        }
        hasher.update(chunk, got);
        received += got;

        int percent;
        if (contentLength > 0) {
            double assetFrac = static_cast<double>(received) /
                               static_cast<double>(contentLength);
            percent = static_cast<int>(
                ((static_cast<double>(assetIndex) + assetFrac) /
                 static_cast<double>(assetCount)) * 100.0);
        } else {
            percent = (assetIndex * 100) / assetCount;
        }
        postProgress(notifyHwnd, percent);
    }

    CloseHandle(hFile);
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);

    if (!success) {
        DeleteFileW(dst.c_str());
        return {};
    }
    return hasher.hexFinalize();
}

DWORD WINAPI downloadThreadProc(LPVOID raw) {
    std::unique_ptr<DownloadJob> job(static_cast<DownloadJob *>(raw));
    HINTERNET hSession = WinHttpOpen(
        L"st80-2026/0.1 WinHTTP",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    bool ok = (hSession != nullptr);
    std::wstring failMsg;
    std::wstring imageFile;

    if (!ensureDirectoryTree(job->slugDir)) {
        ok = false;
        failMsg = L"Could not create destination directory.";
    }

    for (size_t i = 0; ok && i < job->assets.size(); ++i) {
        const auto &a = job->assets[i];
        Url u = parseUrl(a.url);
        if (u.host.empty()) {
            ok = false;
            failMsg = L"Bad URL in manifest for " + a.fileName;
            break;
        }
        const std::wstring dst = job->slugDir + L"\\" + a.fileName;
        if (_wcsicmp(a.fileName.c_str(),
                     job->imageFileNameResult.c_str()) == 0) {
            imageFile = dst;
        } else if (imageFile.empty() && i == 0) {
            // Convention: first asset is the image if not otherwise
            // specified by job->imageFileNameResult.
            imageFile = dst;
        }

        std::string digest = downloadAsset(hSession, u, dst, job->notifyHwnd,
                                           static_cast<int>(i),
                                           static_cast<int>(job->assets.size()));
        if (digest.empty()) {
            ok = false;
            failMsg = L"Download failed for " + a.fileName;
            break;
        }
        if (!a.sha256.empty() && a.sha256 != digest) {
            // Integrity mismatch — nuke the file and stop.
            DeleteFileW(dst.c_str());
            ok = false;
            failMsg = L"SHA256 mismatch for " + a.fileName +
                      L". Download rejected.";
            break;
        }
    }
    if (hSession) WinHttpCloseHandle(hSession);

    if (ok && imageFile.empty() && !job->assets.empty()) {
        imageFile = job->slugDir + L"\\" + job->assets.front().fileName;
    }

    if (ok) {
        wchar_t *heap = new wchar_t[imageFile.size() + 1];
        std::wmemcpy(heap, imageFile.c_str(), imageFile.size() + 1);
        PostMessageW(job->notifyHwnd, WM_APP_DL_DONE,
                     0, reinterpret_cast<LPARAM>(heap));
    } else {
        wchar_t *heap = new wchar_t[failMsg.size() + 1];
        std::wmemcpy(heap, failMsg.c_str(), failMsg.size() + 1);
        PostMessageW(job->notifyHwnd, WM_APP_DL_DONE,
                     1, reinterpret_cast<LPARAM>(heap));
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Input dialog (for Rename)
// ----------------------------------------------------------------------------
//
// Win32 has no built-in InputBox. We create a small modal window with
// a label + edit + OK/Cancel. Returns true if the user clicked OK;
// fills `out` with the entered text.

struct InputDialogState {
    HWND         hwnd      = nullptr;
    HWND         hEdit     = nullptr;
    std::wstring result;
    bool         ok        = false;
    bool         done      = false;
};

LRESULT CALLBACK inputDialogProc(HWND hwnd, UINT msg,
                                 WPARAM wp, LPARAM lp) {
    InputDialogState *st = reinterpret_cast<InputDialogState *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == IDOK) {
                wchar_t buf[512] = {};
                GetWindowTextW(st->hEdit, buf, 512);
                st->result = buf;
                st->ok     = true;
                st->done   = true;
                PostQuitMessage(0);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) {
                st->ok   = false;
                st->done = true;
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) { st->ok = false; st->done = true; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool promptForText(HWND owner, const wchar_t *title,
                   const wchar_t *label, const wchar_t *initial,
                   std::wstring &out) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = inputDialogProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"St80InputDialog";
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }

    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    const int cw = 380, ch = 150;
    RECT rc{0, 0, S(cw), S(ch)};
    AdjustWindowRectExForDpi(&rc,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    const int outW = rc.right - rc.left;
    const int outH = rc.bottom - rc.top;

    // Center on the owner.
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (owner) {
        RECT or_{};
        GetWindowRect(owner, &or_);
        x = or_.left + ((or_.right  - or_.left) - outW) / 2;
        y = or_.top  + ((or_.bottom - or_.top)  - outH) / 2;
    }

    InputDialogState st;
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"St80InputDialog", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, outW, outH,
        owner, nullptr, hInst, &st);
    if (!hwnd) return false;
    st.hwnd = hwnd;

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
                               sizeof(ncm), &ncm, 0, dpi);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    auto mk = [&](const wchar_t *cls, const wchar_t *text, DWORD style,
                  int lx, int ly, int lw, int lh, int id) {
        HWND c = CreateWindowExW(0, cls, text,
            WS_CHILD | WS_VISIBLE | style,
            S(lx), S(ly), S(lw), S(lh), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return c;
    };

    mk(L"STATIC", label, SS_LEFT, 16, 16, cw - 32, 20, 9001);
    st.hEdit = mk(L"EDIT", initial ? initial : L"",
                  ES_AUTOHSCROLL | WS_BORDER, 16, 44, cw - 32, 22, 9002);
    mk(L"BUTTON", L"OK",     BS_DEFPUSHBUTTON,
       cw - 190, 90, 80, 28, IDOK);
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON,
       cw - 100, 90, 80, 28, IDCANCEL);

    // Select all text in the edit so typing replaces it.
    SendMessageW(st.hEdit, EM_SETSEL, 0, -1);
    SetFocus(st.hEdit);

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.ok = false; st.done = true; break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    DeleteObject(hFont);
    if (owner) SetForegroundWindow(owner);

    if (st.ok) out = st.result;
    return st.ok;
}

// ----------------------------------------------------------------------------
// Download-picker dialog
// ----------------------------------------------------------------------------
//
// Lists images from the manifest. User picks one; we return its index.
// Index of -1 = user cancelled.

struct DownloadPickerState {
    HWND hwnd    = nullptr;
    HWND hList   = nullptr;
    int  picked  = -1;
    bool done    = false;
};

LRESULT CALLBACK downloadPickerProc(HWND hwnd, UINT msg,
                                    WPARAM wp, LPARAM lp) {
    DownloadPickerState *st = reinterpret_cast<DownloadPickerState *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == IDOK ||
                (LOWORD(wp) == 9010 && HIWORD(wp) == LBN_DBLCLK)) {
                int sel = static_cast<int>(SendMessageW(st->hList,
                                                        LB_GETCURSEL, 0, 0));
                st->picked = (sel == LB_ERR) ? -1 : sel;
                st->done   = true;
                PostQuitMessage(0);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) {
                st->picked = -1;
                st->done   = true;
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) { st->picked = -1; st->done = true; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int showDownloadPicker(HWND owner, const Manifest &m) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = downloadPickerProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"St80DownloadPicker";
        if (!RegisterClassExW(&wc)) return -1;
        registered = true;
    }

    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const int cw = 460, ch = 320;
    RECT rc{0, 0, S(cw), S(ch)};
    AdjustWindowRectExForDpi(&rc,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    const int outW = rc.right - rc.left, outH = rc.bottom - rc.top;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (owner) {
        RECT or_{};
        GetWindowRect(owner, &or_);
        x = or_.left + ((or_.right  - or_.left) - outW) / 2;
        y = or_.top  + ((or_.bottom - or_.top)  - outH) / 2;
    }

    DownloadPickerState st;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"St80DownloadPicker", L"Download Image",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, outW, outH, owner, nullptr, hInst, &st);
    if (!hwnd) return -1;
    st.hwnd = hwnd;

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
                               sizeof(ncm), &ncm, 0, dpi);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    auto mk = [&](const wchar_t *cls, const wchar_t *text, DWORD style,
                  int lx, int ly, int lw, int lh, int id) {
        HWND c = CreateWindowExW(0, cls, text,
            WS_CHILD | WS_VISIBLE | style,
            S(lx), S(ly), S(lw), S(lh), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            hInst, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return c;
    };

    mk(L"STATIC", L"Choose an image to download:", SS_LEFT,
       16, 14, cw - 32, 20, 9011);
    st.hList = mk(L"LISTBOX", L"",
       LBS_NOTIFY | WS_BORDER | WS_VSCROLL,
       16, 40, cw - 32, ch - 110, 9010);
    for (const auto &img : m.images) {
        std::wstring line = img.label.empty() ? img.slug : img.label;
        SendMessageW(st.hList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(line.c_str()));
    }
    if (!m.images.empty())
        SendMessageW(st.hList, LB_SETCURSEL, 0, 0);

    mk(L"BUTTON", L"Download", BS_DEFPUSHBUTTON,
       cw - 210, ch - 50, 90, 30, IDOK);
    mk(L"BUTTON", L"Cancel",   BS_PUSHBUTTON,
       cw - 110, ch - 50, 90, 30, IDCANCEL);

    SetFocus(st.hList);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.picked = -1; st.done = true; break;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    DeleteObject(hFont);
    if (owner) SetForegroundWindow(owner);
    return st.picked;
}

// ----------------------------------------------------------------------------
// Formatting helpers
// ----------------------------------------------------------------------------

std::wstring formatBytes(std::uint64_t n) {
    const wchar_t *units[] = {L"B", L"KB", L"MB", L"GB"};
    double d = static_cast<double>(n);
    int ui = 0;
    while (d >= 1024.0 && ui < 3) { d /= 1024.0; ++ui; }
    wchar_t buf[32];
    if (ui == 0) std::swprintf(buf, 32, L"%llu %s",
                               static_cast<unsigned long long>(n), units[ui]);
    else         std::swprintf(buf, 32, L"%.1f %s", d, units[ui]);
    return buf;
}

// ----------------------------------------------------------------------------
// Launcher state + UI layout
// ----------------------------------------------------------------------------

enum class SortKey { Name, Size, Modified };

struct Layout {
    static constexpr int kClientW   = 800;
    static constexpr int kClientH   = 640;
    static constexpr int kMargin    = 16;
    static constexpr int kHeaderY   = 12;
    static constexpr int kHeaderH   = 28;
    static constexpr int kSubY      = 44;
    static constexpr int kSubH      = 18;
    static constexpr int kFilterY   = 74;
    static constexpr int kFilterH   = 24;
    static constexpr int kListY     = 108;
    static constexpr int kListH     = 220;
    static constexpr int kActionY   = 340;
    static constexpr int kActionH   = 30;
    static constexpr int kDetailY   = 384;
    static constexpr int kDetailH   = 150;
    static constexpr int kProgY     = 546;
    static constexpr int kProgH     = 14;
    static constexpr int kStatusY   = 566;
    static constexpr int kStatusH   = 20;
    static constexpr int kBottomY   = 594;
    static constexpr int kBottomH   = 30;
};

struct LauncherState {
    HWND hwnd         = nullptr;

    // Controls:
    HWND hFilter      = nullptr;
    HWND hList        = nullptr;
    HWND hProgress    = nullptr;
    HWND hStatus      = nullptr;

    HWND hLaunchBtn   = nullptr;
    HWND hRenameBtn   = nullptr;
    HWND hDupBtn      = nullptr;
    HWND hShowBtn     = nullptr;
    HWND hStarBtn     = nullptr;
    HWND hDeleteBtn   = nullptr;

    HWND hDownloadBtn = nullptr;
    HWND hAddBtn      = nullptr;
    HWND hCancelBtn   = nullptr;

    HWND hDetName     = nullptr;
    HWND hDetPath     = nullptr;
    HWND hDetSize     = nullptr;
    HWND hDetAdded    = nullptr;
    HWND hDetMod      = nullptr;
    HWND hDetLaunched = nullptr;

    HFONT hFont       = nullptr;
    HFONT hHeaderFont = nullptr;

    Library   lib;
    Manifest  manifest;
    bool      manifestLoaded = false;

    std::wstring filter;
    SortKey  sortKey = SortKey::Name;
    bool     sortAsc = true;

    // Indices into lib.images for the rows currently shown (post-filter
    // + sort).
    std::vector<int> visible;

    bool   downloading = false;
    HANDLE dlThread    = nullptr;

    bool         finished = false;
    std::wstring chosenPath;
};

// ---- Filter + sort ---------------------------------------------------------

void rebuildVisible(LauncherState &st) {
    st.visible.clear();
    st.visible.reserve(st.lib.images.size());
    for (int i = 0; i < static_cast<int>(st.lib.images.size()); ++i) {
        const auto &e = st.lib.images[i];
        if (st.filter.empty()) {
            st.visible.push_back(i);
            continue;
        }
        // Case-insensitive substring match on display name.
        if (StrStrIW(e.name.c_str(), st.filter.c_str()) != nullptr) {
            st.visible.push_back(i);
        }
    }

    auto cmp = [&](int ia, int ib) {
        const auto &a = st.lib.images[ia];
        const auto &b = st.lib.images[ib];
        int r = 0;
        switch (st.sortKey) {
            case SortKey::Name:
                r = _wcsicmp(a.name.c_str(), b.name.c_str());
                break;
            case SortKey::Size:
                r = (a.sizeBytes < b.sizeBytes) ? -1
                  : (a.sizeBytes > b.sizeBytes) ?  1 : 0;
                break;
            case SortKey::Modified:
                r = (a.lastModUnix < b.lastModUnix) ? -1
                  : (a.lastModUnix > b.lastModUnix) ?  1 : 0;
                break;
        }
        return st.sortAsc ? (r < 0) : (r > 0);
    };
    std::sort(st.visible.begin(), st.visible.end(), cmp);
}

// ---- ListView population ---------------------------------------------------

void updateListViewColumns(LauncherState &st) {
    const wchar_t *titles[] = {L"", L"Name", L"Size", L"Last modified"};
    const int widths[]      = {28, 340, 100, 180};
    // Set sort arrows on headers.
    HWND header = ListView_GetHeader(st.hList);
    for (int c = 0; c < 4; ++c) {
        LVCOLUMNW col{};
        col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = const_cast<LPWSTR>(titles[c]);
        col.cx      = widths[c];
        col.iSubItem= c;
        if (ListView_GetColumn(st.hList, c, &col)) {
            ListView_SetColumn(st.hList, c, &col);
        } else {
            ListView_InsertColumn(st.hList, c, &col);
        }
        if (header) {
            HDITEMW hi{};
            hi.mask = HDI_FORMAT;
            Header_GetItem(header, c, &hi);
            hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if ((c == 1 && st.sortKey == SortKey::Name) ||
                (c == 2 && st.sortKey == SortKey::Size) ||
                (c == 3 && st.sortKey == SortKey::Modified)) {
                hi.fmt |= st.sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
            }
            Header_SetItem(header, c, &hi);
        }
    }
}

void populateListView(LauncherState &st) {
    rebuildVisible(st);
    updateListViewColumns(st);

    ListView_DeleteAllItems(st.hList);
    for (int row = 0; row < static_cast<int>(st.visible.size()); ++row) {
        const auto &e = st.lib.images[st.visible[row]];

        const bool starred = !st.lib.autoLaunchId.empty() &&
                              st.lib.autoLaunchId == e.id;

        LVITEMW it{};
        it.mask     = LVIF_TEXT | LVIF_PARAM;
        it.iItem    = row;
        it.iSubItem = 0;
        wchar_t star[2] = { starred ? L'\x2605' : L' ', 0 };
        it.pszText  = star;
        it.lParam   = static_cast<LPARAM>(st.visible[row]);
        ListView_InsertItem(st.hList, &it);

        ListView_SetItemText(st.hList, row, 1,
            const_cast<LPWSTR>(e.name.c_str()));
        std::wstring sz = formatBytes(e.sizeBytes);
        ListView_SetItemText(st.hList, row, 2,
            const_cast<LPWSTR>(sz.c_str()));
        ListView_SetItemText(st.hList, row, 3,
            const_cast<LPWSTR>(e.lastModFormatted.c_str()));
    }

    if (!st.visible.empty()) {
        ListView_SetItemState(st.hList, 0,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
    }
}

// Get the lib.images index for the row currently selected in the
// ListView, or -1 if nothing is selected.
int selectedLibIndex(LauncherState &st) {
    int row = ListView_GetNextItem(st.hList, -1, LVNI_SELECTED);
    if (row < 0) return -1;
    LVITEMW it{};
    it.mask    = LVIF_PARAM;
    it.iItem   = row;
    if (!ListView_GetItem(st.hList, &it)) return -1;
    return static_cast<int>(it.lParam);
}

// ---- Detail panel ----------------------------------------------------------

void setDetailField(HWND h, const wchar_t *label, const std::wstring &val) {
    std::wstring text = label;
    text += L" ";
    text += val;
    SetWindowTextW(h, text.c_str());
}

void updateDetailPanel(LauncherState &st) {
    const int idx = selectedLibIndex(st);
    const bool any = (idx >= 0);
    if (!any) {
        SetWindowTextW(st.hDetName,     L"Select an image for details.");
        SetWindowTextW(st.hDetPath,     L"");
        SetWindowTextW(st.hDetSize,     L"");
        SetWindowTextW(st.hDetAdded,    L"");
        SetWindowTextW(st.hDetMod,      L"");
        SetWindowTextW(st.hDetLaunched, L"");
    } else {
        const auto &e = st.lib.images[idx];
        const bool starred = !st.lib.autoLaunchId.empty() &&
                              st.lib.autoLaunchId == e.id;
        std::wstring title = e.name;
        if (starred) title += L"  (auto-launch)";
        SetWindowTextW(st.hDetName, title.c_str());
        setDetailField(st.hDetPath,     L"Location:",      e.fullImagePath);
        setDetailField(st.hDetSize,     L"Size:",          formatBytes(e.sizeBytes));
        setDetailField(st.hDetAdded,    L"Added:",         utf8to16(e.addedAtIso));
        setDetailField(st.hDetMod,      L"Last modified:", e.lastModFormatted);
        setDetailField(st.hDetLaunched, L"Last launched:",
                       utf8to16(e.lastLaunchedAtIso));
    }
    EnableWindow(st.hLaunchBtn,  any && !st.downloading);
    EnableWindow(st.hRenameBtn,  any && !st.downloading);
    EnableWindow(st.hDupBtn,     any && !st.downloading);
    EnableWindow(st.hShowBtn,    any && !st.downloading);
    EnableWindow(st.hStarBtn,    any && !st.downloading);
    EnableWindow(st.hDeleteBtn,  any && !st.downloading);
    if (any) {
        const auto &e = st.lib.images[idx];
        const bool starred = !st.lib.autoLaunchId.empty() &&
                              st.lib.autoLaunchId == e.id;
        SetWindowTextW(st.hStarBtn,
            starred ? L"Clear Auto-Launch" : L"Set Auto-Launch");
    } else {
        SetWindowTextW(st.hStarBtn, L"Set Auto-Launch");
    }
}

void refreshAll(LauncherState &st) {
    st.lib = reconcileWithFilesystem(std::move(st.lib));
    populateListView(st);
    updateDetailPanel(st);
}

void setStatus(LauncherState &st, const wchar_t *msg) {
    SetWindowTextW(st.hStatus, msg ? msg : L"");
}

// ---- Actions ---------------------------------------------------------------

void onLaunch(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st.lib.images[idx];
    if (e.fullImagePath.empty()) {
        setStatus(st, L"That image is missing on disk.");
        return;
    }
    st.chosenPath = e.fullImagePath;
    st.finished   = true;
    PostQuitMessage(0);
}

void onRename(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    auto &e = st.lib.images[idx];
    std::wstring newName;
    if (!promptForText(st.hwnd, L"Rename image",
                       L"New display name:",
                       e.name.c_str(), newName)) return;
    // Trim whitespace.
    while (!newName.empty() &&
           (newName.back() == L' ' || newName.back() == L'\t'))
        newName.pop_back();
    while (!newName.empty() &&
           (newName.front() == L' ' || newName.front() == L'\t'))
        newName.erase(newName.begin());
    if (newName.empty()) return;
    e.name = newName;
    saveLibraryJson(st.lib);
    refreshAll(st);
    setStatus(st, L"Renamed.");
}

void onDuplicate(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto srcEntry = st.lib.images[idx];  // copy
    const std::wstring root = getImagesRoot();
    if (root.empty()) return;
    const std::wstring srcDir = root + L"\\" + srcEntry.slug;

    std::wstring newSlug = makeSlug(srcEntry.slug);
    std::wstring dstDir  = root + L"\\" + newSlug;

    setStatus(st, L"Duplicating…");
    if (!copyDirectoryRecursive(srcDir, dstDir)) {
        setStatus(st, L"Duplicate failed.");
        return;
    }
    LibraryEntry dup = srcEntry;
    dup.id          = newUuidHex();
    dup.slug        = newSlug;
    dup.name        = srcEntry.name + L" (copy)";
    dup.addedAtIso  = nowIso8601();
    dup.lastLaunchedAtIso.clear();
    st.lib.images.push_back(std::move(dup));
    saveLibraryJson(st.lib);
    refreshAll(st);

    // Select the new entry.
    for (int row = 0; row < static_cast<int>(st.visible.size()); ++row) {
        const auto &e = st.lib.images[st.visible[row]];
        if (e.slug == newSlug) {
            ListView_SetItemState(st.hList, row,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(st.hList, row, FALSE);
            break;
        }
    }
    updateDetailPanel(st);
    setStatus(st, L"Duplicated.");
}

void onShowFolder(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st.lib.images[idx];
    const std::wstring root = getImagesRoot();
    if (root.empty()) return;
    const std::wstring slugDir = root + L"\\" + e.slug;
    ShellExecuteW(st.hwnd, L"open", slugDir.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void onToggleStar(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st.lib.images[idx];
    if (st.lib.autoLaunchId == e.id) st.lib.autoLaunchId.clear();
    else                             st.lib.autoLaunchId = e.id;
    saveLibraryJson(st.lib);
    populateListView(st);
    // Re-select the same entry post-repopulate.
    for (int row = 0; row < static_cast<int>(st.visible.size()); ++row) {
        if (st.lib.images[st.visible[row]].id == e.id) {
            ListView_SetItemState(st.hList, row,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            break;
        }
    }
    updateDetailPanel(st);
}

void onDelete(LauncherState &st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st.lib.images[idx];
    std::wstring prompt = L"Delete \"" + e.name +
                          L"\" from your image library?\nThis removes all"
                          L" files under Images\\" + e.slug + L"\\.";
    if (MessageBoxW(st.hwnd, prompt.c_str(), L"Delete image",
                    MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    const std::wstring root = getImagesRoot();
    if (root.empty()) return;
    const std::wstring slugDir = root + L"\\" + e.slug;
    if (!deleteDirectoryRecursive(slugDir)) {
        setStatus(st, L"Could not delete image directory.");
        return;
    }
    if (st.lib.autoLaunchId == e.id) st.lib.autoLaunchId.clear();
    st.lib.images.erase(st.lib.images.begin() + idx);
    saveLibraryJson(st.lib);
    refreshAll(st);
    setStatus(st, L"Deleted.");
}

void onAddFile(LauncherState &st) {
    wchar_t buf[MAX_PATH * 2] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = st.hwnd;
    ofn.lpstrFilter =
        L"Smalltalk-80 image files\0"
        L"*;VirtualImage;VirtualImageLSB;*.image\0"
        L"All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf) / sizeof(buf[0]);
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle  = L"Choose a Smalltalk-80 image file";
    if (!GetOpenFileNameW(&ofn)) return;

    const std::wstring srcPath = buf;
    std::wstring fileName = srcPath;
    auto slash = fileName.find_last_of(L"\\/");
    std::wstring parent;
    if (slash != std::wstring::npos) {
        parent   = fileName.substr(0, slash);
        fileName = fileName.substr(slash + 1);
    }
    std::wstring base = fileName;
    auto dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);

    const std::wstring slug    = makeSlug(base);
    const std::wstring root    = getImagesRoot();
    if (root.empty()) return;
    const std::wstring slugDir = root + L"\\" + slug;
    if (!ensureDirectoryTree(slugDir)) {
        setStatus(st, L"Could not create image directory.");
        return;
    }
    const std::wstring dst = slugDir + L"\\" + fileName;
    if (!CopyFileW(srcPath.c_str(), dst.c_str(), FALSE)) {
        setStatus(st, L"Copy failed.");
        return;
    }
    for (const wchar_t *c : {L"Smalltalk-80.sources",
                             L"Smalltalk-80.changes"}) {
        std::wstring src = parent + L"\\" + c;
        if (fileExistsW(src)) {
            CopyFileW(src.c_str(),
                      (slugDir + L"\\" + c).c_str(), FALSE);
        }
    }
    for (const wchar_t *ext : {L".sources", L".changes"}) {
        std::wstring src = parent + L"\\" + base + ext;
        if (fileExistsW(src)) {
            CopyFileW(src.c_str(),
                      (slugDir + L"\\" + base + ext).c_str(), FALSE);
        }
    }

    LibraryEntry e;
    e.id            = newUuidHex();
    e.name          = base.empty() ? L"Imported image" : base;
    e.slug          = slug;
    e.imageFileName = fileName;
    e.addedAtIso    = nowIso8601();
    st.lib.images.push_back(std::move(e));
    saveLibraryJson(st.lib);
    refreshAll(st);

    // Select imported entry.
    for (int row = 0; row < static_cast<int>(st.visible.size()); ++row) {
        if (st.lib.images[st.visible[row]].slug == slug) {
            ListView_SetItemState(st.hList, row,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(st.hList, row, FALSE);
            break;
        }
    }
    updateDetailPanel(st);
    setStatus(st, L"Imported.");
}

void onDownload(LauncherState &st);  // fwd
void onDownloadDone(LauncherState &st, WPARAM ok, LPARAM lp);

void startDownloadFromManifest(LauncherState &st, const ManifestImage &img) {
    if (st.downloading) return;
    const std::wstring root = getImagesRoot();
    if (root.empty()) {
        setStatus(st, L"Could not locate Documents folder.");
        return;
    }
    auto job = std::make_unique<DownloadJob>();
    job->notifyHwnd = st.hwnd;
    job->slugDir    = root + L"\\" + img.slug;
    job->imageFileNameResult = img.imageFileName.empty()
        ? img.assets.front().name
        : img.imageFileName;
    for (const auto &a : img.assets) {
        DownloadJob::Asset da;
        da.url      = a.url;
        da.fileName = a.name;
        da.sha256   = a.sha256;
        job->assets.push_back(std::move(da));
    }

    // Register (or update) the library entry optimistically so the
    // launch-selection after download has something to point at.
    bool existing = false;
    for (auto &e : st.lib.images) {
        if (e.slug == img.slug) {
            e.name          = img.label;
            e.imageFileName = job->imageFileNameResult;
            existing = true;
            break;
        }
    }
    if (!existing) {
        LibraryEntry e;
        e.id            = newUuidHex();
        e.name          = img.label.empty() ? img.slug : img.label;
        e.slug          = img.slug;
        e.imageFileName = job->imageFileNameResult;
        e.addedAtIso    = nowIso8601();
        st.lib.images.push_back(std::move(e));
    }
    saveLibraryJson(st.lib);

    DownloadJob *raw = job.release();
    HANDLE th = CreateThread(nullptr, 0, downloadThreadProc, raw, 0, nullptr);
    if (!th) { delete raw; setStatus(st, L"Could not start download."); return; }
    st.dlThread    = th;
    st.downloading = true;

    EnableWindow(st.hDownloadBtn, FALSE);
    EnableWindow(st.hAddBtn,      FALSE);
    updateDetailPanel(st);
    SendMessageW(st.hProgress, PBM_SETPOS, 0, 0);
    ShowWindow(st.hProgress, SW_SHOW);
    std::wstring msg = L"Downloading " + img.label + L"…";
    setStatus(st, msg.c_str());
}

void onDownload(LauncherState &st) {
    if (!st.manifestLoaded) {
        setStatus(st, L"Fetching manifest…");
        st.manifest = fetchManifest();
        st.manifestLoaded = true;
    }
    if (st.manifest.images.empty()) {
        setStatus(st, L"No downloadable images in manifest.");
        return;
    }
    int pick = showDownloadPicker(st.hwnd, st.manifest);
    if (pick < 0) return;
    startDownloadFromManifest(st, st.manifest.images[pick]);
}

void onDownloadDone(LauncherState &st, WPARAM ok, LPARAM lp) {
    std::unique_ptr<wchar_t[]> heap(reinterpret_cast<wchar_t *>(lp));
    if (st.dlThread) {
        WaitForSingleObject(st.dlThread, 1000);
        CloseHandle(st.dlThread);
        st.dlThread = nullptr;
    }
    st.downloading = false;
    ShowWindow(st.hProgress, SW_HIDE);
    EnableWindow(st.hDownloadBtn, TRUE);
    EnableWindow(st.hAddBtn,      TRUE);

    if (ok == 0 && heap) {
        refreshAll(st);
        // Select the row whose full path matches the downloaded file.
        for (int row = 0; row < static_cast<int>(st.visible.size()); ++row) {
            const auto &e = st.lib.images[st.visible[row]];
            if (_wcsicmp(e.fullImagePath.c_str(), heap.get()) == 0) {
                ListView_SetItemState(st.hList, row,
                    LVIS_SELECTED | LVIS_FOCUSED,
                    LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(st.hList, row, FALSE);
                break;
            }
        }
        updateDetailPanel(st);
        setStatus(st, L"Download complete.");
    } else {
        setStatus(st, heap ? heap.get() : L"Download failed.");
    }
}

// ---- Window proc -----------------------------------------------------------

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg,
                                 WPARAM wp, LPARAM lp) {
    LauncherState *st = reinterpret_cast<LauncherState *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            const int id   = LOWORD(wp);
            const int code = HIWORD(wp);
            switch (id) {
                case ID_LAUNCH:       onLaunch(*st);     return 0;
                case ID_RENAME:       onRename(*st);     return 0;
                case ID_DUPLICATE:    onDuplicate(*st);  return 0;
                case ID_SHOW_FOLDER:  onShowFolder(*st); return 0;
                case ID_AUTO_LAUNCH:  onToggleStar(*st); return 0;
                case ID_DELETE:       onDelete(*st);     return 0;
                case ID_DOWNLOAD:     onDownload(*st);   return 0;
                case ID_ADD_FILE:     onAddFile(*st);    return 0;
                case ID_CANCEL:
                    st->finished = false;
                    PostQuitMessage(0);
                    return 0;
                case ID_FILTER_EDIT:
                    if (code == EN_CHANGE) {
                        wchar_t buf[256] = {};
                        GetWindowTextW(st->hFilter, buf, 256);
                        st->filter = buf;
                        populateListView(*st);
                        updateDetailPanel(*st);
                    }
                    return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            if (!st) break;
            NMHDR *hdr = reinterpret_cast<NMHDR *>(lp);
            if (hdr->idFrom != ID_LIST) break;
            if (hdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *lv = reinterpret_cast<NMLISTVIEW *>(lp);
                if (lv->uChanged & LVIF_STATE) updateDetailPanel(*st);
            } else if (hdr->code == NM_DBLCLK) {
                onLaunch(*st);
            } else if (hdr->code == LVN_COLUMNCLICK) {
                NMLISTVIEW *lv = reinterpret_cast<NMLISTVIEW *>(lp);
                SortKey want = st->sortKey;
                switch (lv->iSubItem) {
                    case 1: want = SortKey::Name;     break;
                    case 2: want = SortKey::Size;     break;
                    case 3: want = SortKey::Modified; break;
                    default: return 0;
                }
                if (want == st->sortKey) st->sortAsc = !st->sortAsc;
                else { st->sortKey = want; st->sortAsc = true; }
                populateListView(*st);
                updateDetailPanel(*st);
            } else if (hdr->code == NM_RCLICK) {
                int idx = selectedLibIndex(*st);
                if (idx < 0) return 0;
                HMENU m = CreatePopupMenu();
                const auto &e = st->lib.images[idx];
                const bool starred = !st->lib.autoLaunchId.empty() &&
                                      st->lib.autoLaunchId == e.id;
                AppendMenuW(m, MF_STRING, ID_LAUNCH,       L"&Launch");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, ID_RENAME,       L"&Rename…");
                AppendMenuW(m, MF_STRING, ID_DUPLICATE,    L"&Duplicate");
                AppendMenuW(m, MF_STRING, ID_SHOW_FOLDER,  L"&Show in Explorer");
                AppendMenuW(m, MF_STRING, ID_AUTO_LAUNCH,
                            starred ? L"&Clear Auto-Launch"
                                    : L"Set &Auto-Launch");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, ID_DELETE,       L"De&lete");
                POINT pt;
                GetCursorPos(&pt);
                TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                               hwnd, nullptr);
                DestroyMenu(m);
            }
            return 0;
        }
        case WM_APP_DL_PROGRESS:
            if (st) SendMessageW(st->hProgress, PBM_SETPOS,
                                 static_cast<WPARAM>(wp), 0);
            return 0;
        case WM_APP_DL_DONE:
            if (st) onDownloadDone(*st, wp, lp);
            return 0;
        case WM_CLOSE:
            if (st) st->finished = false;
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- Control construction --------------------------------------------------

void buildControls(LauncherState &st, HWND hwnd, UINT dpi) {
    auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,
                               sizeof(ncm), &ncm, 0, dpi);
    st.hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    LOGFONTW big = ncm.lfMessageFont;
    big.lfHeight = static_cast<LONG>(big.lfHeight * 14 / 10);
    big.lfWeight = FW_BOLD;
    st.hHeaderFont = CreateFontIndirectW(&big);

    auto mk = [&](const wchar_t *cls, const wchar_t *text, DWORD style,
                  int x, int y, int w, int h, int id, HFONT font) {
        HWND c = CreateWindowExW(0, cls, text,
            WS_CHILD | WS_VISIBLE | style,
            S(x), S(y), S(w), S(h), hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT,
                     reinterpret_cast<WPARAM>(font), TRUE);
        return c;
    };

    constexpr int M  = Layout::kMargin;
    constexpr int CW = Layout::kClientW - 2 * Layout::kMargin;

    mk(L"STATIC", L"Smalltalk-80 Image Library", SS_LEFT,
       M, Layout::kHeaderY, CW, Layout::kHeaderH, 9001, st.hHeaderFont);
    mk(L"STATIC",
       L"1983 Xerox virtual image — pick an image to launch, or download a new one",
       SS_LEFT, M, Layout::kSubY, CW, Layout::kSubH, 9002, st.hFont);

    // Filter row.
    mk(L"STATIC", L"Filter:", SS_RIGHT,
       M, Layout::kFilterY + 3, 50, Layout::kFilterH - 4, 9003, st.hFont);
    st.hFilter = mk(L"EDIT", L"",
       ES_AUTOHSCROLL | WS_BORDER,
       M + 54, Layout::kFilterY, 300, Layout::kFilterH,
       ID_FILTER_EDIT, st.hFont);

    st.hDownloadBtn = mk(L"BUTTON", L"Download…", BS_PUSHBUTTON,
       Layout::kClientW - M - 260, Layout::kFilterY - 3,
       125, Layout::kFilterH + 6, ID_DOWNLOAD, st.hFont);
    st.hAddBtn = mk(L"BUTTON", L"Add from file…", BS_PUSHBUTTON,
       Layout::kClientW - M - 130, Layout::kFilterY - 3,
       130, Layout::kFilterH + 6, ID_ADD_FILE, st.hFont);

    // ListView.
    st.hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
        LVS_SINGLESEL,
        S(M), S(Layout::kListY), S(CW), S(Layout::kListH),
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)),
        GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(st.hList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SendMessageW(st.hList, WM_SETFONT,
                 reinterpret_cast<WPARAM>(st.hFont), TRUE);
    updateListViewColumns(st);

    // Action buttons row under the list.
    constexpr int bw = 120, bg = 8;
    int ax = M;
    st.hLaunchBtn = mk(L"BUTTON", L"Launch", BS_DEFPUSHBUTTON,
       ax, Layout::kActionY, bw, Layout::kActionH, ID_LAUNCH, st.hFont);
    ax += bw + bg;
    st.hRenameBtn = mk(L"BUTTON", L"Rename…", BS_PUSHBUTTON,
       ax, Layout::kActionY, 90, Layout::kActionH, ID_RENAME, st.hFont);
    ax += 90 + bg;
    st.hDupBtn = mk(L"BUTTON", L"Duplicate", BS_PUSHBUTTON,
       ax, Layout::kActionY, 100, Layout::kActionH, ID_DUPLICATE, st.hFont);
    ax += 100 + bg;
    st.hShowBtn = mk(L"BUTTON", L"Show in Explorer", BS_PUSHBUTTON,
       ax, Layout::kActionY, 140, Layout::kActionH, ID_SHOW_FOLDER, st.hFont);
    ax += 140 + bg;
    st.hStarBtn = mk(L"BUTTON", L"Set Auto-Launch", BS_PUSHBUTTON,
       ax, Layout::kActionY, 140, Layout::kActionH, ID_AUTO_LAUNCH, st.hFont);
    // Right-justified Delete:
    st.hDeleteBtn = mk(L"BUTTON", L"Delete", BS_PUSHBUTTON,
       Layout::kClientW - M - 90, Layout::kActionY,
       90, Layout::kActionH, ID_DELETE, st.hFont);

    // Detail panel — stacked statics.
    st.hDetName     = mk(L"STATIC", L"", SS_LEFT,
       M, Layout::kDetailY,        CW, 22, 9101, st.hHeaderFont);
    st.hDetPath     = mk(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS,
       M, Layout::kDetailY + 26,   CW, 20, 9102, st.hFont);
    st.hDetSize     = mk(L"STATIC", L"", SS_LEFT,
       M, Layout::kDetailY + 48,   CW, 20, 9103, st.hFont);
    st.hDetAdded    = mk(L"STATIC", L"", SS_LEFT,
       M, Layout::kDetailY + 70,   CW, 20, 9104, st.hFont);
    st.hDetMod      = mk(L"STATIC", L"", SS_LEFT,
       M, Layout::kDetailY + 92,   CW, 20, 9105, st.hFont);
    st.hDetLaunched = mk(L"STATIC", L"", SS_LEFT,
       M, Layout::kDetailY + 114,  CW, 20, 9106, st.hFont);

    // Progress + status.
    st.hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | PBS_SMOOTH,
        S(M), S(Layout::kProgY), S(CW), S(Layout::kProgH), hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(9201)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(st.hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    st.hStatus = mk(L"STATIC", L"", SS_LEFT,
        M, Layout::kStatusY, CW, Layout::kStatusH, 9202, st.hFont);

    // Bottom-right: Cancel.
    st.hCancelBtn = mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON,
        Layout::kClientW - M - 100, Layout::kBottomY,
        100, Layout::kBottomH, ID_CANCEL, st.hFont);
}

}  // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

bool ShowLauncher(HINSTANCE hInstance, std::string &outImagePath) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES |
                 ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = LauncherWndProc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kWindowClass;
        wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
        wc.hIconSm       = wc.hIcon;
        if (!RegisterClassExW(&wc)) return false;
        registered = true;
    }

    LauncherState st;
    st.lib = reconcileWithFilesystem(loadLibraryJson());
    saveLibraryJson(st.lib);

    HWND hwnd = CreateWindowExW(0, kWindowClass, L"Smalltalk-80",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        Layout::kClientW, Layout::kClientH,
        nullptr, nullptr, hInstance, &st);
    if (!hwnd) return false;
    st.hwnd = hwnd;

    const UINT dpi = GetDpiForWindow(hwnd);
    RECT want{0, 0,
              MulDiv(Layout::kClientW, dpi, 96),
              MulDiv(Layout::kClientH, dpi, 96)};
    AdjustWindowRectExForDpi(&want, WS_OVERLAPPED | WS_CAPTION |
                                    WS_SYSMENU | WS_MINIMIZEBOX,
                             FALSE, 0, dpi);
    const int outW = want.right  - want.left;
    const int outH = want.bottom - want.top;
    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hmon, &mi);
    const int x = mi.rcWork.left +
                  ((mi.rcWork.right  - mi.rcWork.left) - outW) / 2;
    const int y = mi.rcWork.top +
                  ((mi.rcWork.bottom - mi.rcWork.top) - outH) / 2;
    SetWindowPos(hwnd, nullptr, x, y, outW, outH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    buildControls(st, hwnd, dpi);
    populateListView(st);
    updateDetailPanel(st);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(st.hList);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.finished = false;
            PostQuitMessage(0);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (st.dlThread) {
        WaitForSingleObject(st.dlThread, 5000);
        CloseHandle(st.dlThread);
    }
    if (st.hFont)       DeleteObject(st.hFont);
    if (st.hHeaderFont) DeleteObject(st.hHeaderFont);
    if (IsWindow(hwnd)) DestroyWindow(hwnd);

    if (!st.finished || st.chosenPath.empty()) return false;

    // Stamp lastLaunchedAt on the chosen entry.
    for (auto &e : st.lib.images) {
        if (!e.fullImagePath.empty() &&
            _wcsicmp(e.fullImagePath.c_str(),
                     st.chosenPath.c_str()) == 0) {
            e.lastLaunchedAtIso = nowIso8601();
            break;
        }
    }
    saveLibraryJson(st.lib);

    outImagePath = utf16to8(st.chosenPath);
    return true;
}

void RememberLastImage(const std::string &imagePath) {
    // Update lastLaunchedAt stamp on the matching entry. This is how
    // "recent launches" would be presented; auto-launch is explicit
    // via the star.
    Library lib = reconcileWithFilesystem(loadLibraryJson());
    const std::wstring wantW = utf8to16(imagePath);
    for (auto &e : lib.images) {
        if (!e.fullImagePath.empty() &&
            _wcsicmp(e.fullImagePath.c_str(), wantW.c_str()) == 0) {
            e.lastLaunchedAtIso = nowIso8601();
            break;
        }
    }
    saveLibraryJson(lib);
}

std::string LoadLastImage() {
    // Return the full path of the explicitly-starred auto-launch
    // image, or empty if none is set (or the starred image no longer
    // exists on disk). This is what the main.cpp skip-launcher path
    // consults.
    Library lib = reconcileWithFilesystem(loadLibraryJson());
    if (lib.autoLaunchId.empty()) return {};
    for (const auto &e : lib.images) {
        if (e.id == lib.autoLaunchId && !e.fullImagePath.empty() &&
            fileExistsW(e.fullImagePath)) {
            return utf16to8(e.fullImagePath);
        }
    }
    return {};
}

}  // namespace st80
