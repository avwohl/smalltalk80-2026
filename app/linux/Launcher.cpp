// st80-2026 — Launcher.cpp (Linux / GTK4)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Adapted from app/windows/Launcher.cpp. Same three-layer split:
//
//   1. Library catalog (`library.json`) — user-facing state under
//      $XDG_DATA_HOME/st80-2026/.
//   2. Manifest (`manifest.json`) — fetched from the st80-images
//      companion repo via libcurl. SHA256-verified.
//   3. GTK4 UI — a top-level window with a filterable, sortable
//      GtkListBox, detail panel, button strip, and right-click
//      context menu. Downloads run on a worker thread; progress is
//      pumped back to the GTK main thread via g_idle_add().

#include "Launcher.hpp"
#include "Json.hpp"
#include "Sha256.hpp"

#include <gtk/gtk.h>
#include <curl/curl.h>

#include <strings.h>      // strcasestr
#include <sys/stat.h>     // stat() for mtime (file_clock::to_sys is C++20)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace st80 {

namespace {

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

constexpr const char *kManifestUrl =
    "https://raw.githubusercontent.com/avwohl/st80-images/main/manifest.json";

constexpr const char *kProjectURL =
    "https://github.com/avwohl/smalltalk80-2026";

const char *kKnownImageNames[] = {
    "VirtualImage", "VirtualImageLSB",
    "Smalltalk.image", "smalltalk.image",
};

// ----------------------------------------------------------------------------
// XDG / filesystem helpers
// ----------------------------------------------------------------------------

fs::path getXdgDataHome() {
    if (const char *xdh = std::getenv("XDG_DATA_HOME"); xdh && *xdh) {
        return fs::path(xdh);
    }
    if (const char *home = std::getenv("HOME"); home && *home) {
        return fs::path(home) / ".local" / "share";
    }
    return fs::path(".local") / "share";
}

fs::path st80Root()         { return getXdgDataHome() / "st80-2026"; }
fs::path imagesRoot()       { return st80Root() / "Images"; }
fs::path libraryJsonPath()  { return st80Root() / "library.json"; }
fs::path manifestCachePath(){ return st80Root() / "manifest-cache.json"; }

bool ensureDir(const fs::path &p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return !ec;
}

bool fileExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool dirExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

std::uint64_t fileSizeBytes(const fs::path &p) {
    std::error_code ec;
    auto n = fs::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(n);
}

std::uint64_t fileLastModUnix(const fs::path &p) {
    // std::filesystem::last_write_time returns a file_clock time_point
    // and converting to system_clock requires C++20's
    // std::chrono::clock_cast / file_clock::to_sys. The project sets
    // CMAKE_CXX_STANDARD=17, so reach for stat() directly instead.
    struct stat sb;
    if (stat(p.string().c_str(), &sb) != 0) return 0;
    return static_cast<std::uint64_t>(sb.st_mtime);
}

std::string formatLocalDateTime(std::uint64_t unix) {
    if (unix == 0) return {};
    std::time_t t = static_cast<std::time_t>(unix);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min);
    return buf;
}

std::string nowIso8601() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[80];
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

std::string readFileUtf8(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::string out((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return out;
}

bool writeFileUtf8(const fs::path &p, const std::string &data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return f.good();
}

bool deleteDirRecursive(const fs::path &p) {
    std::error_code ec;
    fs::remove_all(p, ec);
    return !ec;
}

bool copyDirRecursive(const fs::path &src, const fs::path &dst) {
    std::error_code ec;
    fs::copy(src, dst,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             ec);
    return !ec;
}

// ----------------------------------------------------------------------------
// Slug + UUID
// ----------------------------------------------------------------------------

std::string makeSlug(const std::string &base) {
    std::string out;
    out.reserve(base.size() + 8);
    for (char c : base) {
        unsigned char ch = static_cast<unsigned char>(c);
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '-') {
            out += static_cast<char>(ch);
        } else if (ch >= 'A' && ch <= 'Z') {
            out += static_cast<char>(ch + ('a' - 'A'));
        } else if (ch == ' ' || ch == '_' || ch == '.') {
            out += '-';
        }
    }
    if (out.empty()) out = "image";
    if (out.size() > 40) out.resize(40);

    // 4 hex chars of randomness so duplicate-named imports don't
    // collide with each other.
    static thread_local std::mt19937
        rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned> d(0, 0xffff);
    char tag[16];
    std::snprintf(tag, sizeof(tag), "-%04x", d(rng));
    out += tag;
    return out;
}

std::string newUuidHex() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uint64_t a = rng();
    std::uint64_t b = rng();
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016lx%016lx",
                  static_cast<unsigned long>(a),
                  static_cast<unsigned long>(b));
    return buf;
}

std::string findImageFileIn(const fs::path &dir) {
    for (const char *name : kKnownImageNames) {
        if (fileExists(dir / name)) return name;
    }
    std::error_code ec;
    for (auto &entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() > 6
            && name.compare(name.size() - 6, 6, ".image") == 0) {
            return name;
        }
    }
    return {};
}

std::string formatBytes(std::uint64_t n) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double d = static_cast<double>(n);
    int ui = 0;
    while (d >= 1024.0 && ui < 3) { d /= 1024.0; ++ui; }
    char buf[32];
    if (ui == 0) std::snprintf(buf, sizeof(buf), "%llu %s",
                               static_cast<unsigned long long>(n),
                               units[ui]);
    else         std::snprintf(buf, sizeof(buf), "%.1f %s", d, units[ui]);
    return buf;
}

// ----------------------------------------------------------------------------
// Library catalog
// ----------------------------------------------------------------------------

struct LibraryEntry {
    std::string id;
    std::string name;
    std::string slug;
    std::string imageFileName;
    std::string addedAtIso;
    std::string lastLaunchedAtIso;

    // Derived — not persisted:
    std::string   fullImagePath;
    std::uint64_t sizeBytes      = 0;
    std::uint64_t lastModUnix    = 0;
    std::string   lastModFormatted;
};

struct Library {
    std::vector<LibraryEntry> images;
    std::string               autoLaunchId;
};

void hydrate(LibraryEntry &e) {
    fs::path slugDir = imagesRoot() / e.slug;
    if (e.imageFileName.empty()) {
        e.imageFileName = findImageFileIn(slugDir);
    }
    if (e.imageFileName.empty()) {
        e.fullImagePath.clear();
        return;
    }
    fs::path imgPath = slugDir / e.imageFileName;
    e.fullImagePath = imgPath.string();
    e.sizeBytes        = fileSizeBytes(imgPath);
    e.lastModUnix      = fileLastModUnix(imgPath);
    e.lastModFormatted = formatLocalDateTime(e.lastModUnix);
}

Library loadLibraryJson() {
    Library lib;
    fs::path path = libraryJsonPath();
    if (!fileExists(path)) return lib;
    JsonValue root = JsonValue::parse(readFileUtf8(path));
    if (!root.isObject()) return lib;
    lib.autoLaunchId = root.getString("autoLaunchId");
    for (const auto &v : root.get("images").asArray()) {
        if (!v.isObject()) continue;
        LibraryEntry e;
        e.id                = v.getString("id");
        e.name              = v.getString("name");
        e.slug              = v.getString("slug");
        e.imageFileName     = v.getString("imageFileName");
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
    if (!lib.autoLaunchId.empty()) {
        root.mutableObject()["autoLaunchId"] = JsonValue(lib.autoLaunchId);
    }
    JsonValue arr = JsonValue::Array();
    for (const auto &e : lib.images) {
        JsonValue obj = JsonValue::Object();
        obj.mutableObject()["id"]            = JsonValue(e.id);
        obj.mutableObject()["name"]          = JsonValue(e.name);
        obj.mutableObject()["slug"]          = JsonValue(e.slug);
        obj.mutableObject()["imageFileName"] = JsonValue(e.imageFileName);
        obj.mutableObject()["addedAt"]       = JsonValue(e.addedAtIso);
        if (!e.lastLaunchedAtIso.empty()) {
            obj.mutableObject()["lastLaunchedAt"] =
                JsonValue(e.lastLaunchedAtIso);
        }
        arr.mutableArray().push_back(std::move(obj));
    }
    root.mutableObject()["images"] = std::move(arr);
    ensureDir(st80Root());
    writeFileUtf8(libraryJsonPath(), root.dump(2));
}

Library reconcileWithFilesystem(Library lib) {
    if (!dirExists(imagesRoot())) {
        lib.images.erase(
            std::remove_if(lib.images.begin(), lib.images.end(),
                [](const LibraryEntry &e) {
                    return !dirExists(imagesRoot() / e.slug);
                }),
            lib.images.end());
        return lib;
    }
    lib.images.erase(
        std::remove_if(lib.images.begin(), lib.images.end(),
            [](const LibraryEntry &e) {
                return !dirExists(imagesRoot() / e.slug);
            }),
        lib.images.end());

    std::error_code ec;
    for (auto &entry : fs::directory_iterator(imagesRoot(), ec)) {
        if (!entry.is_directory()) continue;
        std::string slug = entry.path().filename().string();
        if (slug.empty() || slug.front() == '.') continue;
        bool known = false;
        for (const auto &e : lib.images) {
            if (e.slug == slug) { known = true; break; }
        }
        if (known) continue;
        std::string imgFile = findImageFileIn(entry.path());
        if (imgFile.empty()) continue;
        LibraryEntry e;
        e.id            = newUuidHex();
        e.slug          = slug;
        e.imageFileName = imgFile;
        e.name          = (slug == "xerox-v2")
            ? "Xerox Smalltalk-80 v2 (1983)" : slug;
        e.addedAtIso    = nowIso8601();
        lib.images.push_back(std::move(e));
    }

    for (auto &e : lib.images) hydrate(e);
    return lib;
}

// ----------------------------------------------------------------------------
// Manifest
// ----------------------------------------------------------------------------

struct ManifestAsset {
    std::string  name;
    std::string  url;
    std::string  sha256;
    std::uint64_t size = 0;
};

struct ManifestImage {
    std::string id;
    std::string label;
    std::string slug;
    std::string imageFileName;
    std::vector<ManifestAsset> assets;
};

struct Manifest {
    std::vector<ManifestImage> images;
};

Manifest parseManifest(const std::string &text) {
    Manifest m;
    JsonValue root = JsonValue::parse(text);
    if (!root.isObject()) return m;
    for (const auto &v : root.get("images").asArray()) {
        if (!v.isObject()) continue;
        ManifestImage img;
        img.id            = v.getString("id");
        img.label         = v.getString("label");
        img.slug          = v.getString("slug");
        img.imageFileName = v.getString("imageFileName");
        for (const auto &av : v.get("assets").asArray()) {
            if (!av.isObject()) continue;
            ManifestAsset a;
            a.name   = av.getString("name");
            a.url    = av.getString("url");
            a.sha256 = av.getString("sha256");
            a.size   = static_cast<std::uint64_t>(av.getNumber("size", 0.0));
            if (a.name.empty() || a.url.empty()) continue;
            img.assets.push_back(std::move(a));
        }
        if (img.slug.empty() || img.assets.empty()) continue;
        m.images.push_back(std::move(img));
    }
    return m;
}

Manifest fallbackManifest() {
    Manifest m;
    ManifestImage img;
    img.id            = "xerox-v2";
    img.label         = "Xerox Smalltalk-80 v2 (1983)";
    img.slug          = "xerox-v2";
    img.imageFileName = "VirtualImage";
    const std::string base =
        "https://github.com/avwohl/st80-images"
        "/releases/download/xerox-v2/";
    img.assets.push_back({"VirtualImage", base + "VirtualImage", "", 0});
    img.assets.push_back({"Smalltalk-80.sources",
                          base + "Smalltalk-80.sources", "", 0});
    m.images.push_back(std::move(img));
    return m;
}

// ----------------------------------------------------------------------------
// libcurl helpers
// ----------------------------------------------------------------------------

size_t curlWriteToString(char *p, size_t sz, size_t n, void *ud) {
    auto *out = static_cast<std::string *>(ud);
    out->append(p, sz * n);
    if (out->size() > (4 * 1024 * 1024)) return 0;  // 4 MB cap
    return sz * n;
}

std::string httpGetToMemory(const std::string &url) {
    std::string body;
    CURL *c = curl_easy_init();
    if (!c) return body;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "st80-2026/0.1 libcurl");
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteToString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    if (curl_easy_perform(c) != CURLE_OK) body.clear();
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    if (http != 200) body.clear();
    curl_easy_cleanup(c);
    return body;
}

Manifest fetchManifest() {
    std::string body = httpGetToMemory(kManifestUrl);
    if (!body.empty()) {
        Manifest m = parseManifest(body);
        if (!m.images.empty()) {
            ensureDir(st80Root());
            writeFileUtf8(manifestCachePath(), body);
            return m;
        }
    }
    if (fileExists(manifestCachePath())) {
        Manifest m = parseManifest(readFileUtf8(manifestCachePath()));
        if (!m.images.empty()) return m;
    }
    return fallbackManifest();
}

// ----------------------------------------------------------------------------
// Downloader (worker thread)
// ----------------------------------------------------------------------------

struct DownloadJob {
    std::string slug;
    std::string imageFileNameResult;
    std::vector<ManifestAsset> assets;
};

// Cancel flag — read by the curl progress callback. Cleared by the
// UI thread when a fresh download is launched.
std::atomic<int> g_downloadCancel{0};

// Progress reporting — UI thread reads `g_dlPercent` and `g_dlStatus`
// in an idle handler. Worker writes them under a mutex.
std::mutex          g_dlMutex;
int                 g_dlPercent  = 0;
std::string         g_dlStatus;
bool                g_dlInFlight = false;
bool                g_dlComplete = false;
bool                g_dlSuccess  = false;
std::string         g_dlError;
std::string         g_dlResultPath;

struct DlPerAssetCtx {
    int          assetIndex;
    int          assetCount;
    Sha256       hasher;
    std::FILE   *fp = nullptr;
};

size_t curlWriteToFile(char *p, size_t sz, size_t n, void *ud) {
    auto *ctx = static_cast<DlPerAssetCtx *>(ud);
    size_t bytes = sz * n;
    if (std::fwrite(p, 1, bytes, ctx->fp) != bytes) return 0;
    ctx->hasher.update(reinterpret_cast<const unsigned char *>(p), bytes);
    return bytes;
}

int curlProgressCb(void *ud, curl_off_t dlTotal, curl_off_t dlNow,
                   curl_off_t /*ul1*/, curl_off_t /*ul2*/) {
    auto *ctx = static_cast<DlPerAssetCtx *>(ud);
    if (g_downloadCancel.load(std::memory_order_relaxed)) return 1;
    if (dlTotal > 0) {
        double assetFrac = static_cast<double>(dlNow)
                         / static_cast<double>(dlTotal);
        int pct = static_cast<int>(
            ((static_cast<double>(ctx->assetIndex) + assetFrac)
             / static_cast<double>(ctx->assetCount)) * 100.0);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        std::lock_guard<std::mutex> lk(g_dlMutex);
        g_dlPercent = pct;
    }
    return 0;
}

// Download one asset. Returns lowercase hex SHA256 on success, empty
// on failure (curl error, HTTP non-200, or user cancel).
std::string downloadAsset(const ManifestAsset &a, const fs::path &dst,
                          int assetIndex, int assetCount) {
    DlPerAssetCtx ctx;
    ctx.assetIndex = assetIndex;
    ctx.assetCount = assetCount;
    ctx.fp = std::fopen(dst.string().c_str(), "wb");
    if (!ctx.fp) return {};

    CURL *c = curl_easy_init();
    if (!c) { std::fclose(ctx.fp); return {}; }
    curl_easy_setopt(c, CURLOPT_URL, a.url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "st80-2026/0.1 libcurl");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curlProgressCb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ctx);
    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(c);
    std::fclose(ctx.fp);

    if (rc != CURLE_OK || http != 200) {
        std::error_code ec;
        fs::remove(dst, ec);
        return {};
    }
    return ctx.hasher.hexFinalize();
}

void downloadThreadMain(std::shared_ptr<DownloadJob> job) {
    g_downloadCancel.store(0);

    fs::path slugDir = imagesRoot() / job->slug;
    if (!ensureDir(slugDir)) {
        std::lock_guard<std::mutex> lk(g_dlMutex);
        g_dlSuccess  = false;
        g_dlError    = "Could not create destination directory.";
        g_dlComplete = true;
        return;
    }

    std::string resultPath;
    bool ok = true;
    std::string failMsg;

    for (size_t i = 0; ok && i < job->assets.size(); ++i) {
        const auto &a = job->assets[i];
        {
            std::lock_guard<std::mutex> lk(g_dlMutex);
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "Downloading %s (%d of %d)…",
                a.name.c_str(),
                static_cast<int>(i + 1),
                static_cast<int>(job->assets.size()));
            g_dlStatus = buf;
        }
        fs::path dst = slugDir / a.name;
        if (a.name == job->imageFileNameResult
            || (resultPath.empty() && i == 0
                && job->imageFileNameResult.empty())) {
            resultPath = dst.string();
        }

        std::string digest = downloadAsset(a, dst,
            static_cast<int>(i), static_cast<int>(job->assets.size()));
        if (digest.empty()) {
            ok = false;
            if (g_downloadCancel.load()) failMsg = "Download cancelled.";
            else failMsg = "Download failed for " + a.name;
            break;
        }
        if (!a.sha256.empty() && a.sha256 != digest) {
            std::error_code ec;
            fs::remove(dst, ec);
            ok = false;
            failMsg = "SHA256 mismatch for " + a.name + ".";
            break;
        }
    }

    if (ok && resultPath.empty() && !job->assets.empty()) {
        resultPath = (slugDir / job->assets.front().name).string();
    }

    std::lock_guard<std::mutex> lk(g_dlMutex);
    g_dlSuccess  = ok;
    if (ok) g_dlResultPath = resultPath;
    else    g_dlError      = failMsg;
    g_dlComplete = true;
}

// ----------------------------------------------------------------------------
// GTK4 UI
// ----------------------------------------------------------------------------

enum class SortKey { Name, Source, Size, Modified };

struct LauncherState {
    GtkApplication *app           = nullptr;
    GtkWindow      *window        = nullptr;

    GtkWidget      *filterEntry   = nullptr;
    GtkWidget      *listBox       = nullptr;
    GtkWidget      *progressBar   = nullptr;
    GtkWidget      *progressBox   = nullptr;
    GtkWidget      *statusLabel   = nullptr;
    GtkWidget      *cancelDlBtn   = nullptr;

    GtkWidget      *btnNameSort   = nullptr;
    GtkWidget      *btnSourceSort = nullptr;
    GtkWidget      *btnSizeSort   = nullptr;
    GtkWidget      *btnModSort    = nullptr;

    GtkWidget      *detailName    = nullptr;
    GtkWidget      *detailMeta    = nullptr;
    GtkWidget      *btnLaunch     = nullptr;
    GtkWidget      *btnRename     = nullptr;
    GtkWidget      *btnDuplicate  = nullptr;
    GtkWidget      *btnShowFolder = nullptr;
    GtkWidget      *btnAutoLaunch = nullptr;
    GtkWidget      *btnDelete     = nullptr;

    Library         lib;
    Manifest        manifest;
    bool            manifestLoaded = false;

    std::string     filter;
    SortKey         sortKey  = SortKey::Name;
    bool            sortAsc  = true;

    // Indices into lib.images for the rows currently visible.
    std::vector<int> visible;

    bool            downloading       = false;
    std::shared_ptr<DownloadJob>      activeJob;
    std::thread     downloadThread;
    guint           progressTimerId   = 0;

    // Output:
    bool            chosen        = false;
    std::string     chosenPath;
};

// Forward decls.
void rebuildList(LauncherState *st);
void updateDetailPanel(LauncherState *st);
void setStatus(LauncherState *st, const std::string &msg);
void onLaunch(LauncherState *st);
void onRename(LauncherState *st);
void onDuplicate(LauncherState *st);
void onShowFolder(LauncherState *st);
void onToggleAutoLaunch(LauncherState *st);
void onDelete(LauncherState *st);
void onAddFromFile(LauncherState *st);
void onDownload(LauncherState *st);
void onCancelDownload(LauncherState *st);
void onShowSettings(LauncherState *st);
void startDownload(LauncherState *st, const ManifestImage &img);

int  selectedLibIndex(LauncherState *st);

// Status line.
void setStatus(LauncherState *st, const std::string &msg) {
    if (st->statusLabel) gtk_label_set_text(GTK_LABEL(st->statusLabel),
                                            msg.c_str());
}

// Compare entries by current sort key.
bool entryLess(const LibraryEntry &a, const LibraryEntry &b, SortKey k) {
    switch (k) {
        case SortKey::Name:
            return g_utf8_collate(a.name.c_str(), b.name.c_str()) < 0;
        case SortKey::Source: {
            // Use the imageFileName as the "source" label fallback.
            const std::string &as = a.imageFileName;
            const std::string &bs = b.imageFileName;
            return g_utf8_collate(as.c_str(), bs.c_str()) < 0;
        }
        case SortKey::Size:
            return a.sizeBytes < b.sizeBytes;
        case SortKey::Modified:
            return a.lastModUnix < b.lastModUnix;
    }
    return false;
}

void recomputeVisible(LauncherState *st) {
    st->visible.clear();
    st->visible.reserve(st->lib.images.size());
    for (int i = 0; i < static_cast<int>(st->lib.images.size()); ++i) {
        const auto &e = st->lib.images[i];
        if (st->filter.empty()
            || strcasestr(e.name.c_str(), st->filter.c_str()) != nullptr) {
            st->visible.push_back(i);
        }
    }
    std::sort(st->visible.begin(), st->visible.end(),
        [st](int ia, int ib) {
            bool less = entryLess(st->lib.images[ia],
                                  st->lib.images[ib], st->sortKey);
            return st->sortAsc ? less : !less;
        });
}

// Make a single row widget, tagged with the lib.images index.
GtkWidget *makeRow(LauncherState *st, int libIdx) {
    const auto &e = st->lib.images[libIdx];
    GtkWidget *row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "st80-lib-idx",
                      GINT_TO_POINTER(libIdx));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    bool starred = !st->lib.autoLaunchId.empty()
                && st->lib.autoLaunchId == e.id;
    GtkWidget *star = gtk_image_new_from_icon_name(
        starred ? "starred-symbolic" : "non-starred-symbolic");
    gtk_widget_set_size_request(star, 16, 16);
    gtk_box_append(GTK_BOX(box), star);

    GtkWidget *name = gtk_label_new(e.name.c_str());
    gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
    gtk_widget_set_hexpand(name, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), name);

    GtkWidget *src = gtk_label_new(e.imageFileName.c_str());
    gtk_label_set_xalign(GTK_LABEL(src), 0.0f);
    gtk_widget_set_size_request(src, 180, -1);
    gtk_label_set_ellipsize(GTK_LABEL(src), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(src, "dim-label");
    gtk_box_append(GTK_BOX(box), src);

    GtkWidget *sz = gtk_label_new(formatBytes(e.sizeBytes).c_str());
    gtk_label_set_xalign(GTK_LABEL(sz), 1.0f);
    gtk_widget_set_size_request(sz, 80, -1);
    gtk_widget_add_css_class(sz, "dim-label");
    gtk_box_append(GTK_BOX(box), sz);

    GtkWidget *mt = gtk_label_new(e.lastModFormatted.c_str());
    gtk_label_set_xalign(GTK_LABEL(mt), 0.0f);
    gtk_widget_set_size_request(mt, 140, -1);
    gtk_widget_add_css_class(mt, "dim-label");
    gtk_box_append(GTK_BOX(box), mt);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    return row;
}

void clearListBox(GtkWidget *lb) {
    GtkWidget *child = gtk_widget_get_first_child(lb);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(lb), child);
        child = next;
    }
}

void rebuildList(LauncherState *st) {
    recomputeVisible(st);
    clearListBox(st->listBox);
    for (int idx : st->visible) {
        gtk_list_box_append(GTK_LIST_BOX(st->listBox), makeRow(st, idx));
    }
    if (!st->visible.empty()) {
        GtkListBoxRow *first = gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(st->listBox), 0);
        gtk_list_box_select_row(GTK_LIST_BOX(st->listBox), first);
    }
    // Update sort-arrow indicators on the headers.
    auto setArrow = [&](GtkWidget *btn, SortKey k, const char *base) {
        std::string label = base;
        if (st->sortKey == k) label += st->sortAsc ? " \xe2\x86\x91"
                                                   : " \xe2\x86\x93";
        gtk_button_set_label(GTK_BUTTON(btn), label.c_str());
    };
    setArrow(st->btnNameSort,   SortKey::Name,     "Name");
    setArrow(st->btnSourceSort, SortKey::Source,   "Source");
    setArrow(st->btnSizeSort,   SortKey::Size,     "Size");
    setArrow(st->btnModSort,    SortKey::Modified, "Last Modified");

    updateDetailPanel(st);
}

int selectedLibIndex(LauncherState *st) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(
        GTK_LIST_BOX(st->listBox));
    if (!row) return -1;
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "st80-lib-idx"));
}

void updateDetailPanel(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) {
        gtk_label_set_text(GTK_LABEL(st->detailName),
                           "Select an image for details.");
        gtk_label_set_text(GTK_LABEL(st->detailMeta), "");
    } else {
        const auto &e = st->lib.images[idx];
        bool starred = !st->lib.autoLaunchId.empty()
                    && st->lib.autoLaunchId == e.id;
        std::string title = e.name;
        if (starred) title += "  \xe2\x98\x85";
        gtk_label_set_text(GTK_LABEL(st->detailName), title.c_str());

        std::string meta;
        meta += "Image file: " + e.imageFileName + "\n";
        meta += "Location: " + e.fullImagePath + "\n";
        meta += "Size: " + formatBytes(e.sizeBytes) + "\n";
        meta += "Last modified: " + e.lastModFormatted + "\n";
        meta += "Added: " + e.addedAtIso;
        if (!e.lastLaunchedAtIso.empty()) {
            meta += "\nLast launched: " + e.lastLaunchedAtIso;
        }
        gtk_label_set_text(GTK_LABEL(st->detailMeta), meta.c_str());
    }
    bool any = (idx >= 0);
    gtk_widget_set_sensitive(st->btnLaunch,     any && !st->downloading);
    gtk_widget_set_sensitive(st->btnRename,     any && !st->downloading);
    gtk_widget_set_sensitive(st->btnDuplicate,  any && !st->downloading);
    gtk_widget_set_sensitive(st->btnShowFolder, any && !st->downloading);
    gtk_widget_set_sensitive(st->btnAutoLaunch, any && !st->downloading);
    gtk_widget_set_sensitive(st->btnDelete,     any && !st->downloading);
    if (any) {
        const auto &e = st->lib.images[idx];
        bool starred = !st->lib.autoLaunchId.empty()
                    && st->lib.autoLaunchId == e.id;
        gtk_button_set_label(GTK_BUTTON(st->btnAutoLaunch),
            starred ? "Clear Auto-Launch" : "Set Auto-Launch");
    }
}

void refreshAll(LauncherState *st) {
    st->lib = reconcileWithFilesystem(std::move(st->lib));
    rebuildList(st);
}

// ---- Filter / sort signals -------------------------------------------------

void on_filter_changed(GtkEditable *ed, gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    const char *txt = gtk_editable_get_text(ed);
    st->filter = txt ? txt : "";
    rebuildList(st);
}

void on_named_sort_clicked(GtkButton *btn, gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    SortKey k = static_cast<SortKey>(GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(btn), "st80-sortkey")));
    if (st->sortKey == k) st->sortAsc = !st->sortAsc;
    else { st->sortKey = k; st->sortAsc = true; }
    rebuildList(st);
}

// ---- Selection -------------------------------------------------------------

void on_row_selected(GtkListBox * /*lb*/, GtkListBoxRow * /*row*/,
                     gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    updateDetailPanel(st);
}

void on_row_activated(GtkListBox * /*lb*/, GtkListBoxRow * /*row*/,
                      gpointer ud) {
    onLaunch(static_cast<LauncherState *>(ud));
}

// ---- Right-click context menu ---------------------------------------------
//
// GTK4 doesn't ship a GtkPopoverMenu shortcut, but it does have
// GMenu + gtk_popover_menu_new_from_model. We build the menu, attach
// actions to a GSimpleActionGroup, and pop it up at the click.

void on_ctx_launch     (GSimpleAction *, GVariant *, gpointer ud) { onLaunch(static_cast<LauncherState *>(ud)); }
void on_ctx_rename     (GSimpleAction *, GVariant *, gpointer ud) { onRename(static_cast<LauncherState *>(ud)); }
void on_ctx_duplicate  (GSimpleAction *, GVariant *, gpointer ud) { onDuplicate(static_cast<LauncherState *>(ud)); }
void on_ctx_showfolder (GSimpleAction *, GVariant *, gpointer ud) { onShowFolder(static_cast<LauncherState *>(ud)); }
void on_ctx_autolaunch (GSimpleAction *, GVariant *, gpointer ud) { onToggleAutoLaunch(static_cast<LauncherState *>(ud)); }
void on_ctx_delete     (GSimpleAction *, GVariant *, gpointer ud) { onDelete(static_cast<LauncherState *>(ud)); }

void on_right_click(GtkGestureClick *g, int /*n*/, double x, double y,
                    gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    // Select the row under the click.
    GtkWidget *picker = GTK_WIDGET(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)));
    GtkListBoxRow *row = gtk_list_box_get_row_at_y(
        GTK_LIST_BOX(picker), static_cast<int>(y));
    if (!row) return;
    gtk_list_box_select_row(GTK_LIST_BOX(picker), row);
    updateDetailPanel(st);

    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st->lib.images[idx];
    bool starred = !st->lib.autoLaunchId.empty()
                && st->lib.autoLaunchId == e.id;

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Launch",            "ctx.launch");
    g_menu_append(menu, "Rename…",           "ctx.rename");
    g_menu_append(menu, "Duplicate",         "ctx.duplicate");
    g_menu_append(menu, "Show in Files",     "ctx.showfolder");
    g_menu_append(menu, starred ? "Clear Auto-Launch"
                                : "Set Auto-Launch", "ctx.autolaunch");
    g_menu_append(menu, "Delete…",           "ctx.delete");

    GSimpleActionGroup *grp = g_simple_action_group_new();
    auto add = [&](const char *name, GCallback cb) {
        GSimpleAction *a = g_simple_action_new(name, nullptr);
        g_signal_connect(a, "activate", cb, st);
        g_action_map_add_action(G_ACTION_MAP(grp), G_ACTION(a));
        g_object_unref(a);
    };
    add("launch",     G_CALLBACK(on_ctx_launch));
    add("rename",     G_CALLBACK(on_ctx_rename));
    add("duplicate",  G_CALLBACK(on_ctx_duplicate));
    add("showfolder", G_CALLBACK(on_ctx_showfolder));
    add("autolaunch", G_CALLBACK(on_ctx_autolaunch));
    add("delete",     G_CALLBACK(on_ctx_delete));
    gtk_widget_insert_action_group(picker, "ctx", G_ACTION_GROUP(grp));
    g_object_unref(grp);

    GtkWidget *pop = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(pop, picker);
    GdkRectangle rect{static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &rect);
    gtk_popover_set_has_arrow(GTK_POPOVER(pop), FALSE);
    gtk_popover_popup(GTK_POPOVER(pop));
}

// ---- Action handlers -------------------------------------------------------

void onLaunch(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st->lib.images[idx];
    if (e.fullImagePath.empty()) {
        setStatus(st, "That image is missing on disk.");
        return;
    }
    st->chosenPath = e.fullImagePath;
    st->chosen     = true;
    gtk_window_close(st->window);
}

// Modal text-input dialog. Used for Rename and Custom URL.
struct InputCtx {
    GtkWidget   *entry  = nullptr;
    bool         ok     = false;
    bool         done   = false;
    std::string  result;
};
void on_input_ok(GtkButton *, gpointer ud) {
    auto *c = static_cast<InputCtx *>(ud);
    c->result = gtk_editable_get_text(GTK_EDITABLE(c->entry));
    c->ok = true; c->done = true;
}
void on_input_cancel(GtkButton *, gpointer ud) {
    auto *c = static_cast<InputCtx *>(ud);
    c->ok = false; c->done = true;
}
void on_input_close(GtkWindow *, gpointer ud) {
    auto *c = static_cast<InputCtx *>(ud);
    c->done = true;
}

bool promptForText(GtkWindow *owner, const char *title, const char *label,
                   const char *initial, std::string &out) {
    InputCtx ctx;
    GtkWidget *win = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(win), owner);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), title);
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 140);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), lbl);

    ctx.entry = gtk_entry_new();
    if (initial) gtk_editable_set_text(GTK_EDITABLE(ctx.entry), initial);
    gtk_box_append(GTK_BOX(box), ctx.entry);

    GtkWidget *btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btnRow, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), btnRow);
    GtkWidget *btnCancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btnOk     = gtk_button_new_with_label("OK");
    gtk_widget_add_css_class(btnOk, "suggested-action");
    gtk_box_append(GTK_BOX(btnRow), btnCancel);
    gtk_box_append(GTK_BOX(btnRow), btnOk);

    g_signal_connect(btnCancel, "clicked",
                     G_CALLBACK(on_input_cancel), &ctx);
    g_signal_connect(btnOk,     "clicked",
                     G_CALLBACK(on_input_ok), &ctx);
    g_signal_connect(win, "close-request",
                     G_CALLBACK(on_input_close), &ctx);

    gtk_window_present(GTK_WINDOW(win));
    while (!ctx.done) g_main_context_iteration(nullptr, TRUE);
    gtk_window_destroy(GTK_WINDOW(win));

    if (ctx.ok) {
        // Trim whitespace.
        while (!ctx.result.empty()
               && (ctx.result.back() == ' ' || ctx.result.back() == '\t'))
            ctx.result.pop_back();
        while (!ctx.result.empty()
               && (ctx.result.front() == ' ' || ctx.result.front() == '\t'))
            ctx.result.erase(ctx.result.begin());
        out = ctx.result;
    }
    return ctx.ok;
}

void onRename(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    auto &e = st->lib.images[idx];
    std::string newName;
    if (!promptForText(st->window, "Rename image",
                       "New display name:", e.name.c_str(), newName))
        return;
    if (newName.empty()) return;
    e.name = newName;
    saveLibraryJson(st->lib);
    rebuildList(st);
    setStatus(st, "Renamed.");
}

void onDuplicate(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    LibraryEntry src = st->lib.images[idx];   // copy
    std::string newSlug = makeSlug(src.slug);
    fs::path srcDir = imagesRoot() / src.slug;
    fs::path dstDir = imagesRoot() / newSlug;
    setStatus(st, "Duplicating…");
    if (!copyDirRecursive(srcDir, dstDir)) {
        setStatus(st, "Duplicate failed.");
        return;
    }
    LibraryEntry dup = src;
    dup.id          = newUuidHex();
    dup.slug        = newSlug;
    dup.name        = src.name + " (copy)";
    dup.addedAtIso  = nowIso8601();
    dup.lastLaunchedAtIso.clear();
    st->lib.images.push_back(std::move(dup));
    saveLibraryJson(st->lib);
    refreshAll(st);
    setStatus(st, "Duplicated.");
}

void onShowFolder(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    fs::path slugDir = imagesRoot() / st->lib.images[idx].slug;
    GFile *file = g_file_new_for_path(slugDir.string().c_str());
    char *uri = g_file_get_uri(file);
    g_object_unref(file);
    GtkUriLauncher *l = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(l, st->window, nullptr, nullptr, nullptr);
    g_object_unref(l);
    g_free(uri);
}

void onToggleAutoLaunch(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st->lib.images[idx];
    if (st->lib.autoLaunchId == e.id) st->lib.autoLaunchId.clear();
    else                              st->lib.autoLaunchId = e.id;
    saveLibraryJson(st->lib);
    rebuildList(st);
}

// Confirmation dialog for destructive actions.
struct ConfirmCtx { bool yes = false; bool done = false; };
void on_confirm_yes(GtkButton *, gpointer ud) {
    auto *c = static_cast<ConfirmCtx *>(ud); c->yes = true; c->done = true;
}
void on_confirm_no(GtkButton *, gpointer ud) {
    auto *c = static_cast<ConfirmCtx *>(ud); c->yes = false; c->done = true;
}
void on_confirm_close(GtkWindow *, gpointer ud) {
    auto *c = static_cast<ConfirmCtx *>(ud); c->done = true;
}

bool confirm(GtkWindow *owner, const char *title, const std::string &body) {
    ConfirmCtx ctx;
    GtkWidget *win = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(win), owner);
    gtk_window_set_modal(GTK_WINDOW(win), TRUE);
    gtk_window_set_title(GTK_WINDOW(win), title);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_window_set_child(GTK_WINDOW(win), box);
    GtkWidget *lbl = gtk_label_new(body.c_str());
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_box_append(GTK_BOX(box), lbl);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(row, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(box), row);
    GtkWidget *bn = gtk_button_new_with_label("Cancel");
    GtkWidget *by = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(by, "destructive-action");
    gtk_box_append(GTK_BOX(row), bn);
    gtk_box_append(GTK_BOX(row), by);
    g_signal_connect(bn, "clicked", G_CALLBACK(on_confirm_no), &ctx);
    g_signal_connect(by, "clicked", G_CALLBACK(on_confirm_yes), &ctx);
    g_signal_connect(win, "close-request",
                     G_CALLBACK(on_confirm_close), &ctx);
    gtk_window_present(GTK_WINDOW(win));
    while (!ctx.done) g_main_context_iteration(nullptr, TRUE);
    gtk_window_destroy(GTK_WINDOW(win));
    return ctx.yes;
}

void onDelete(LauncherState *st) {
    int idx = selectedLibIndex(st);
    if (idx < 0) return;
    const auto &e = st->lib.images[idx];
    std::string body = "Delete \"" + e.name
        + "\" and every file in Images/" + e.slug + "/?";
    if (!confirm(st->window, "Delete image", body)) return;
    fs::path slugDir = imagesRoot() / e.slug;
    if (!deleteDirRecursive(slugDir)) {
        setStatus(st, "Could not delete image directory.");
        return;
    }
    if (st->lib.autoLaunchId == e.id) st->lib.autoLaunchId.clear();
    st->lib.images.erase(st->lib.images.begin() + idx);
    saveLibraryJson(st->lib);
    refreshAll(st);
    setStatus(st, "Deleted.");
}

// Add-from-file picker via GtkFileDialog (GTK 4.10+).
void on_addfile_done(GObject *src, GAsyncResult *res, gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    GError *err = nullptr;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src),
                                              res, &err);
    if (!file) {
        if (err) {
            setStatus(st, std::string("Import: ") + err->message);
            g_error_free(err);
        }
        return;
    }
    char *path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;

    fs::path src_path(path);
    g_free(path);
    std::string fileName = src_path.filename().string();
    std::string base = src_path.stem().string();
    std::string slug = makeSlug(base.empty() ? fileName : base);
    fs::path slugDir = imagesRoot() / slug;
    if (!ensureDir(slugDir)) {
        setStatus(st, "Could not create image directory.");
        return;
    }
    std::error_code ec;
    fs::copy_file(src_path, slugDir / fileName,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) { setStatus(st, "Copy failed."); return; }

    fs::path parent = src_path.parent_path();
    for (const char *companion : {"Smalltalk-80.sources",
                                  "Smalltalk-80.changes"}) {
        fs::path src_c = parent / companion;
        if (fileExists(src_c)) {
            fs::copy_file(src_c, slugDir / companion,
                          fs::copy_options::overwrite_existing, ec);
        }
    }
    for (const char *ext : {".sources", ".changes"}) {
        fs::path src_c = parent / (base + ext);
        if (fileExists(src_c)) {
            fs::copy_file(src_c, slugDir / (base + ext),
                          fs::copy_options::overwrite_existing, ec);
        }
    }

    LibraryEntry e;
    e.id            = newUuidHex();
    e.name          = base.empty() ? "Imported image" : base;
    e.slug          = slug;
    e.imageFileName = fileName;
    e.addedAtIso    = nowIso8601();
    st->lib.images.push_back(std::move(e));
    saveLibraryJson(st->lib);
    refreshAll(st);
    setStatus(st, "Imported.");
}

void onAddFromFile(LauncherState *st) {
    GtkFileDialog *dlg = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dlg, "Choose a Smalltalk-80 image file");
    gtk_file_dialog_open(dlg, st->window, nullptr,
                         on_addfile_done, st);
    g_object_unref(dlg);
}

// Download dialog: lists manifest images + a custom-URL row.
struct DownloadDialogCtx {
    LauncherState *st        = nullptr;
    GtkWidget     *win       = nullptr;
    int            picked    = -1;       // manifest index
    bool           customSel = false;
    std::string    customUrl;
    bool           done      = false;
};

void on_download_pick(GtkButton *b, gpointer ud) {
    auto *c = static_cast<DownloadDialogCtx *>(ud);
    c->picked = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx"));
    c->done = true;
}
void on_download_custom(GtkButton *, gpointer ud) {
    auto *c = static_cast<DownloadDialogCtx *>(ud);
    GtkWidget *e = static_cast<GtkWidget *>(
        g_object_get_data(G_OBJECT(c->win), "url-entry"));
    const char *txt = gtk_editable_get_text(GTK_EDITABLE(e));
    if (!txt || !*txt) return;
    c->customUrl = txt;
    c->customSel = true;
    c->done      = true;
}
void on_download_cancel(GtkButton *, gpointer ud) {
    auto *c = static_cast<DownloadDialogCtx *>(ud);
    c->done = true;
}
void on_download_close(GtkWindow *, gpointer ud) {
    auto *c = static_cast<DownloadDialogCtx *>(ud);
    c->done = true;
}

void runDownloadDialog(LauncherState *st) {
    DownloadDialogCtx ctx;
    ctx.st = st;
    ctx.win = gtk_window_new();
    gtk_window_set_transient_for(GTK_WINDOW(ctx.win), st->window);
    gtk_window_set_modal(GTK_WINDOW(ctx.win), TRUE);
    gtk_window_set_title(GTK_WINDOW(ctx.win), "Download Image");
    gtk_window_set_default_size(GTK_WINDOW(ctx.win), 480, 380);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_window_set_child(GTK_WINDOW(ctx.win), box);

    GtkWidget *header = gtk_label_new("Built-in templates:");
    gtk_label_set_xalign(GTK_LABEL(header), 0.0f);
    gtk_box_append(GTK_BOX(box), header);

    for (size_t i = 0; i < st->manifest.images.size(); ++i) {
        const auto &m = st->manifest.images[i];
        std::string label = m.label.empty() ? m.slug : m.label;
        GtkWidget *btn = gtk_button_new_with_label(label.c_str());
        g_object_set_data(G_OBJECT(btn), "idx",
                          GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(btn, "clicked",
                         G_CALLBACK(on_download_pick), &ctx);
        gtk_box_append(GTK_BOX(box), btn);
    }

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 6);
    gtk_widget_set_margin_bottom(sep, 6);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget *urlLabel = gtk_label_new("Or download from URL:");
    gtk_label_set_xalign(GTK_LABEL(urlLabel), 0.0f);
    gtk_box_append(GTK_BOX(box), urlLabel);

    GtkWidget *urlEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(urlEntry),
                                   "https://example.com/VirtualImage");
    g_object_set_data(G_OBJECT(ctx.win), "url-entry", urlEntry);
    gtk_box_append(GTK_BOX(box), urlEntry);

    GtkWidget *btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btnRow, GTK_ALIGN_END);
    gtk_widget_set_margin_top(btnRow, 8);
    gtk_box_append(GTK_BOX(box), btnRow);

    GtkWidget *btnCancel = gtk_button_new_with_label("Cancel");
    GtkWidget *btnUrl    = gtk_button_new_with_label("Download URL");
    gtk_widget_add_css_class(btnUrl, "suggested-action");
    gtk_box_append(GTK_BOX(btnRow), btnCancel);
    gtk_box_append(GTK_BOX(btnRow), btnUrl);

    g_signal_connect(btnCancel, "clicked",
                     G_CALLBACK(on_download_cancel), &ctx);
    g_signal_connect(btnUrl, "clicked",
                     G_CALLBACK(on_download_custom), &ctx);
    g_signal_connect(ctx.win, "close-request",
                     G_CALLBACK(on_download_close), &ctx);

    gtk_window_present(GTK_WINDOW(ctx.win));
    while (!ctx.done) g_main_context_iteration(nullptr, TRUE);
    gtk_window_destroy(GTK_WINDOW(ctx.win));

    if (ctx.picked >= 0
        && ctx.picked < static_cast<int>(st->manifest.images.size())) {
        startDownload(st, st->manifest.images[ctx.picked]);
        return;
    }
    if (ctx.customSel && !ctx.customUrl.empty()) {
        // Build a synthetic single-asset ManifestImage.
        std::string url = ctx.customUrl;
        // Trim.
        while (!url.empty() && (url.back() == ' ' || url.back() == '\t'))
            url.pop_back();
        while (!url.empty() && (url.front() == ' ' || url.front() == '\t'))
            url.erase(url.begin());
        if (url.empty()) return;
        // Derive filename from URL last segment.
        std::string fileName = url;
        auto slash = fileName.find_last_of('/');
        if (slash != std::string::npos) fileName = fileName.substr(slash + 1);
        auto query = fileName.find('?');
        if (query != std::string::npos) fileName = fileName.substr(0, query);
        if (fileName.empty()) fileName = "VirtualImage";
        std::string base = fileName;
        auto dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
        if (base.empty()) base = "image";

        ManifestImage img;
        img.slug          = makeSlug(base);
        img.label         = base;
        img.imageFileName = fileName;
        img.assets.push_back({fileName, url, "", 0});
        startDownload(st, img);
    }
}

void onDownload(LauncherState *st) {
    if (st->downloading) return;
    if (!st->manifestLoaded) {
        setStatus(st, "Fetching manifest…");
        // Pump events so the status repaints.
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        st->manifest = fetchManifest();
        st->manifestLoaded = true;
    }
    runDownloadDialog(st);
}

void onCancelDownload(LauncherState *st) {
    if (!st->downloading) return;
    g_downloadCancel.store(1);
    setStatus(st, "Cancelling download…");
}

// Periodic poll of the worker thread. Pulls progress + completion
// status across the thread boundary.
gboolean onProgressTick(gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    int pct;
    bool complete;
    bool ok;
    std::string status, error, resultPath;
    {
        std::lock_guard<std::mutex> lk(g_dlMutex);
        pct        = g_dlPercent;
        status     = g_dlStatus;
        complete   = g_dlComplete;
        ok         = g_dlSuccess;
        error      = g_dlError;
        resultPath = g_dlResultPath;
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progressBar),
                                  pct / 100.0);
    if (!status.empty()) setStatus(st, status);

    if (complete) {
        if (st->downloadThread.joinable()) st->downloadThread.join();
        st->downloading = false;
        st->activeJob.reset();
        gtk_widget_set_visible(st->progressBox, FALSE);
        if (ok) {
            setStatus(st, "Download complete.");
            refreshAll(st);
        } else {
            setStatus(st, error);
        }
        // Reset shared state.
        std::lock_guard<std::mutex> lk(g_dlMutex);
        g_dlPercent    = 0;
        g_dlStatus.clear();
        g_dlInFlight   = false;
        g_dlComplete   = false;
        g_dlSuccess    = false;
        g_dlError.clear();
        g_dlResultPath.clear();
        st->progressTimerId = 0;
        updateDetailPanel(st);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void startDownload(LauncherState *st, const ManifestImage &img) {
    if (st->downloading) return;
    auto job = std::make_shared<DownloadJob>();
    job->slug                = img.slug;
    job->imageFileNameResult = img.imageFileName;
    job->assets              = img.assets;

    // Optimistic library entry so UI selection works post-download.
    bool existing = false;
    for (auto &e : st->lib.images) {
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
        e.imageFileName = img.imageFileName;
        e.addedAtIso    = nowIso8601();
        st->lib.images.push_back(std::move(e));
    }
    saveLibraryJson(st->lib);

    {
        std::lock_guard<std::mutex> lk(g_dlMutex);
        g_dlPercent    = 0;
        g_dlStatus     = "Starting download…";
        g_dlComplete   = false;
        g_dlSuccess    = false;
        g_dlError.clear();
        g_dlResultPath.clear();
        g_dlInFlight   = true;
    }
    g_downloadCancel.store(0);

    st->activeJob   = job;
    st->downloading = true;
    gtk_widget_set_visible(st->progressBox, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progressBar), 0.0);
    setStatus(st, "Starting download…");
    updateDetailPanel(st);

    st->downloadThread = std::thread(downloadThreadMain, job);
    st->progressTimerId = g_timeout_add(200, onProgressTick, st);
}

// ---- Settings dialog -------------------------------------------------------

void onShowSettings(LauncherState *st) {
    GtkAboutDialog *dlg = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
    gtk_window_set_transient_for(GTK_WINDOW(dlg), st->window);
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    gtk_about_dialog_set_program_name(dlg, "Smalltalk-80");
    gtk_about_dialog_set_version(dlg, "0.1.0");
    gtk_about_dialog_set_comments(dlg,
        "Blue Book VM (1983 Xerox virtual image).");
    gtk_about_dialog_set_website(dlg, kProjectURL);
    gtk_about_dialog_set_website_label(dlg, "GitHub Project");
    gtk_about_dialog_set_license_type(dlg, GTK_LICENSE_MIT_X11);
    const char *authors[] = {"Aaron Wohl", nullptr};
    gtk_about_dialog_set_authors(dlg, authors);
    const char *credits[] = {
        "Dan Banay — dbanay/Smalltalk (MIT) — primary C++ port source",
        "Mario Wolczko — Xerox v2 image distribution",
        "iriyak/Smalltalk — additional reference implementation",
        "Goldberg & Robson — Smalltalk-80: The Language and its "
        "Implementation (Addison-Wesley, 1983)",
        nullptr };
    gtk_about_dialog_add_credit_section(dlg, "Acknowledgements", credits);
    gtk_window_present(GTK_WINDOW(dlg));
}

// ---- Build window ---------------------------------------------------------

GtkWidget *buildHeaderRow(LauncherState *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(box, "toolbar");
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);

    auto mkSort = [&](const char *label, SortKey k, int width) {
        GtkWidget *b = gtk_button_new_with_label(label);
        gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
        g_object_set_data(G_OBJECT(b), "st80-sortkey",
                          GINT_TO_POINTER(static_cast<int>(k)));
        g_signal_connect(b, "clicked",
                         G_CALLBACK(on_named_sort_clicked), st);
        if (width > 0) gtk_widget_set_size_request(b, width, -1);
        return b;
    };

    GtkWidget *spacer = gtk_image_new();
    gtk_widget_set_size_request(spacer, 16, 1);
    gtk_box_append(GTK_BOX(box), spacer);

    st->btnNameSort   = mkSort("Name",          SortKey::Name,    -1);
    gtk_widget_set_hexpand(st->btnNameSort, TRUE);
    gtk_box_append(GTK_BOX(box), st->btnNameSort);

    st->btnSourceSort = mkSort("Source",        SortKey::Source,  186);
    gtk_box_append(GTK_BOX(box), st->btnSourceSort);

    st->btnSizeSort   = mkSort("Size",          SortKey::Size,    86);
    gtk_box_append(GTK_BOX(box), st->btnSizeSort);

    st->btnModSort    = mkSort("Last Modified", SortKey::Modified,146);
    gtk_box_append(GTK_BOX(box), st->btnModSort);

    return box;
}

GtkWidget *buildDetailPanel(LauncherState *st) {
    GtkWidget *frame = gtk_frame_new(nullptr);
    gtk_widget_set_margin_start(frame, 8);
    gtk_widget_set_margin_end(frame, 8);
    gtk_widget_set_margin_bottom(frame, 8);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_frame_set_child(GTK_FRAME(frame), box);

    st->detailName = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->detailName), 0.0f);
    gtk_widget_add_css_class(st->detailName, "title-3");
    gtk_box_append(GTK_BOX(box), st->detailName);

    st->detailMeta = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->detailMeta), 0.0f);
    gtk_label_set_yalign(GTK_LABEL(st->detailMeta), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(st->detailMeta), TRUE);
    gtk_label_set_wrap(GTK_LABEL(st->detailMeta), TRUE);
    gtk_widget_add_css_class(st->detailMeta, "dim-label");
    gtk_box_append(GTK_BOX(box), st->detailMeta);

    GtkWidget *btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(btnRow, 6);
    gtk_box_append(GTK_BOX(box), btnRow);

    st->btnLaunch     = gtk_button_new_with_label("Launch");
    gtk_widget_add_css_class(st->btnLaunch, "suggested-action");
    g_signal_connect_swapped(st->btnLaunch, "clicked",
                             G_CALLBACK(onLaunch), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnLaunch);

    st->btnRename     = gtk_button_new_with_label("Rename…");
    g_signal_connect_swapped(st->btnRename, "clicked",
                             G_CALLBACK(onRename), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnRename);

    st->btnDuplicate  = gtk_button_new_with_label("Duplicate");
    g_signal_connect_swapped(st->btnDuplicate, "clicked",
                             G_CALLBACK(onDuplicate), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnDuplicate);

    st->btnShowFolder = gtk_button_new_with_label("Show in Files");
    g_signal_connect_swapped(st->btnShowFolder, "clicked",
                             G_CALLBACK(onShowFolder), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnShowFolder);

    st->btnAutoLaunch = gtk_button_new_with_label("Set Auto-Launch");
    g_signal_connect_swapped(st->btnAutoLaunch, "clicked",
                             G_CALLBACK(onToggleAutoLaunch), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnAutoLaunch);

    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(btnRow), spacer);

    st->btnDelete     = gtk_button_new_with_label("Delete");
    gtk_widget_add_css_class(st->btnDelete, "destructive-action");
    g_signal_connect_swapped(st->btnDelete, "clicked",
                             G_CALLBACK(onDelete), st);
    gtk_box_append(GTK_BOX(btnRow), st->btnDelete);

    return frame;
}

GtkWidget *buildBottomBar(LauncherState *st) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_bottom(box, 8);

    GtkWidget *settings = gtk_button_new_with_label("Settings…");
    g_signal_connect_swapped(settings, "clicked",
                             G_CALLBACK(onShowSettings), st);
    gtk_box_append(GTK_BOX(box), settings);

    st->statusLabel = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->statusLabel), 0.0f);
    gtk_widget_set_hexpand(st->statusLabel, TRUE);
    gtk_widget_add_css_class(st->statusLabel, "dim-label");
    gtk_box_append(GTK_BOX(box), st->statusLabel);

    GtkWidget *cancel = gtk_button_new_with_label("Close");
    g_signal_connect_swapped(cancel, "clicked",
                             G_CALLBACK(gtk_window_close), st->window);
    gtk_box_append(GTK_BOX(box), cancel);

    return box;
}

void on_window_destroy(GtkWindow * /*w*/, gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    if (st->downloading) {
        g_downloadCancel.store(1);
        if (st->downloadThread.joinable()) st->downloadThread.join();
    }
}

void buildWindow(LauncherState *st, GtkApplication *app) {
    GtkWidget *win = gtk_application_window_new(app);
    st->window = GTK_WINDOW(win);
    gtk_window_set_title(st->window, "Smalltalk-80");
    gtk_window_set_default_size(st->window, 880, 660);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_window_set_child(st->window, root);

    // --- Header bar with action buttons --------------------------------
    GtkWidget *hb = gtk_header_bar_new();
    gtk_window_set_titlebar(st->window, hb);

    GtkWidget *download = gtk_button_new_from_icon_name("document-save-symbolic");
    gtk_widget_set_tooltip_text(download, "Download an image…");
    g_signal_connect_swapped(download, "clicked",
                             G_CALLBACK(onDownload), st);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), download);

    GtkWidget *addfile = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_set_tooltip_text(addfile, "Add image from file…");
    g_signal_connect_swapped(addfile, "clicked",
                             G_CALLBACK(onAddFromFile), st);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), addfile);

    GtkWidget *settings = gtk_button_new_from_icon_name(
        "preferences-system-symbolic");
    gtk_widget_set_tooltip_text(settings, "Settings");
    g_signal_connect_swapped(settings, "clicked",
                             G_CALLBACK(onShowSettings), st);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(hb), settings);

    // --- Filter row ---------------------------------------------------
    GtkWidget *filterRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(filterRow, 8);
    gtk_widget_set_margin_start(filterRow, 8);
    gtk_widget_set_margin_end(filterRow, 8);
    gtk_box_append(GTK_BOX(root), filterRow);

    st->filterEntry = gtk_search_entry_new();
    gtk_widget_set_hexpand(st->filterEntry, TRUE);
    g_signal_connect(st->filterEntry, "search-changed",
                     G_CALLBACK(on_filter_changed), st);
    gtk_box_append(GTK_BOX(filterRow), st->filterEntry);

    // --- Sort header --------------------------------------------------
    gtk_box_append(GTK_BOX(root), buildHeaderRow(st));

    // --- List (scrolled) ----------------------------------------------
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_margin_start(scroll, 8);
    gtk_widget_set_margin_end(scroll, 8);
    gtk_box_append(GTK_BOX(root), scroll);

    st->listBox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->listBox),
                                    GTK_SELECTION_SINGLE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), st->listBox);
    g_signal_connect(st->listBox, "row-selected",
                     G_CALLBACK(on_row_selected), st);
    g_signal_connect(st->listBox, "row-activated",
                     G_CALLBACK(on_row_activated), st);

    GtkGesture *rclick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rclick), 3);
    g_signal_connect(rclick, "pressed",
                     G_CALLBACK(on_right_click), st);
    gtk_widget_add_controller(st->listBox, GTK_EVENT_CONTROLLER(rclick));

    // --- Progress row (initially hidden) ------------------------------
    st->progressBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(st->progressBox, 8);
    gtk_widget_set_margin_end(st->progressBox, 8);
    gtk_widget_set_visible(st->progressBox, FALSE);
    gtk_box_append(GTK_BOX(root), st->progressBox);

    st->progressBar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(st->progressBar, TRUE);
    gtk_box_append(GTK_BOX(st->progressBox), st->progressBar);

    st->cancelDlBtn = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(st->cancelDlBtn, "clicked",
                             G_CALLBACK(onCancelDownload), st);
    gtk_box_append(GTK_BOX(st->progressBox), st->cancelDlBtn);

    // --- Detail panel + bottom bar -----------------------------------
    gtk_box_append(GTK_BOX(root), buildDetailPanel(st));
    gtk_box_append(GTK_BOX(root), buildBottomBar(st));

    g_signal_connect(st->window, "destroy",
                     G_CALLBACK(on_window_destroy), st);
}

void on_app_activate(GtkApplication *app, gpointer ud) {
    auto *st = static_cast<LauncherState *>(ud);
    st->lib = reconcileWithFilesystem(loadLibraryJson());
    saveLibraryJson(st->lib);
    buildWindow(st, app);
    rebuildList(st);
    gtk_window_present(st->window);
}

// ---- Empty-state quick-download bootstrap --------------------------------

// If the library is empty on first launch, automatically populate the
// filter / list with the fallback so the user has something to click.
// (The real "click to download Xerox v2" call still has to come from
// the user via the Download button.)

}  // namespace

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

bool ShowLauncher(int argc, char **argv, std::string &outImagePath) {
    LauncherState st;
    GtkApplication *app = gtk_application_new(
        "com.aaronwohl.smalltalk80.Launcher",
        G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate",
                     G_CALLBACK(on_app_activate), &st);
    // GTK's argv parser would choke on flags main() already
    // consumed (--launcher, --no-window, etc). Hand it just the
    // program name; the launcher doesn't take any GTK options.
    char *gtk_argv0 = (argc > 0) ? argv[0] : (char *)"st80-linux";
    char *gtk_argv[] = { gtk_argv0, nullptr };
    int   gtk_argc   = 1;
    (void)argc; (void)argv;
    int rc = g_application_run(G_APPLICATION(app), gtk_argc, gtk_argv);
    g_object_unref(app);

    if (st.downloadThread.joinable()) {
        g_downloadCancel.store(1);
        st.downloadThread.join();
    }

    if (rc != 0 || !st.chosen || st.chosenPath.empty()) return false;

    // Stamp lastLaunchedAt for the chosen entry.
    for (auto &e : st.lib.images) {
        if (e.fullImagePath == st.chosenPath) {
            e.lastLaunchedAtIso = nowIso8601();
            break;
        }
    }
    saveLibraryJson(st.lib);

    outImagePath = st.chosenPath;
    return true;
}

void RememberLastImage(const std::string &imagePath) {
    Library lib = reconcileWithFilesystem(loadLibraryJson());
    for (auto &e : lib.images) {
        if (e.fullImagePath == imagePath) {
            e.lastLaunchedAtIso = nowIso8601();
            break;
        }
    }
    saveLibraryJson(lib);
}

std::string LoadLastImage() {
    std::string ignored;
    return LoadAutoLaunchInfo(ignored);
}

std::string LoadAutoLaunchInfo(std::string &outDisplayName) {
    outDisplayName.clear();
    Library lib = reconcileWithFilesystem(loadLibraryJson());
    if (lib.autoLaunchId.empty()) return {};
    for (const auto &e : lib.images) {
        if (e.id == lib.autoLaunchId && !e.fullImagePath.empty()
            && fileExists(e.fullImagePath)) {
            outDisplayName = e.name;
            return e.fullImagePath;
        }
    }
    return {};
}

// ----------------------------------------------------------------------------
// Auto-launch splash (3-second countdown with "Show Library" escape)
// ----------------------------------------------------------------------------

namespace {

struct SplashState {
    GtkApplication *app          = nullptr;
    GtkWindow      *window       = nullptr;
    GtkWidget      *countLabel   = nullptr;
    int             countdown    = 3;
    bool            proceed      = false;
    bool            done         = false;
    guint           timerId      = 0;
};

gboolean on_splash_tick(gpointer ud) {
    auto *s = static_cast<SplashState *>(ud);
    if (s->countdown > 1) {
        s->countdown -= 1;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", s->countdown);
        gtk_label_set_text(GTK_LABEL(s->countLabel), buf);
        return G_SOURCE_CONTINUE;
    }
    s->proceed = true;
    s->done    = true;
    s->timerId = 0;
    gtk_window_close(s->window);
    return G_SOURCE_REMOVE;
}

void on_splash_show_library(GtkButton *, gpointer ud) {
    auto *s = static_cast<SplashState *>(ud);
    s->proceed = false;
    s->done    = true;
    if (s->timerId) { g_source_remove(s->timerId); s->timerId = 0; }
    gtk_window_close(s->window);
}

void on_splash_destroy(GtkWindow *, gpointer ud) {
    auto *s = static_cast<SplashState *>(ud);
    s->done = true;
}

}  // namespace

bool ShowAutoLaunchSplash(int argc, char **argv,
                          const std::string & /*imagePath*/,
                          const std::string &displayName) {
    SplashState s;
    SplashState *slotPtr = &s;
    GtkApplication *app = gtk_application_new(
        "com.aaronwohl.smalltalk80.Splash",
        G_APPLICATION_NON_UNIQUE);
    char *gtk_argv0 = (argc > 0) ? argv[0] : (char *)"st80-linux";
    char *gtk_argv[] = { gtk_argv0, nullptr };
    int   gtk_argc   = 1;
    (void)argc; (void)argv;

    // Stash the display name on the app object so the activate
    // handler can pick it up. Keep the heap-allocated string alive
    // until after run() returns by storing via g_object_set_data_full.
    g_object_set_data_full(G_OBJECT(app), "splash-display-name",
        g_strdup(displayName.c_str()), g_free);
    // Activate handler reads display name from the future window;
    // pass it via a small adapter that looks it up on the app.
    auto thunk = +[](GtkApplication *a, gpointer ud) {
        auto **slot = static_cast<SplashState **>(ud);
        SplashState *s = *slot;
        s->app = a;
        GtkWidget *win = gtk_application_window_new(a);
        s->window = GTK_WINDOW(win);
        gtk_window_set_title(s->window, "Smalltalk-80");
        gtk_window_set_default_size(s->window, 420, 260);
        gtk_window_set_resizable(s->window, FALSE);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(box, 24);
        gtk_widget_set_margin_bottom(box, 24);
        gtk_widget_set_margin_start(box, 24);
        gtk_widget_set_margin_end(box, 24);
        gtk_window_set_child(s->window, box);

        GtkWidget *title = gtk_label_new("Auto-launching");
        gtk_widget_add_css_class(title, "title-2");
        gtk_box_append(GTK_BOX(box), title);

        const char *displayN = static_cast<const char *>(
            g_object_get_data(G_OBJECT(a), "splash-display-name"));
        GtkWidget *name = gtk_label_new(displayN ? displayN : "image");
        gtk_widget_add_css_class(name, "dim-label");
        gtk_box_append(GTK_BOX(box), name);

        s->countLabel = gtk_label_new("3");
        gtk_widget_add_css_class(s->countLabel, "title-1");
        gtk_box_append(GTK_BOX(box), s->countLabel);

        GtkWidget *btn = gtk_button_new_with_label("Show Library");
        gtk_widget_set_halign(btn, GTK_ALIGN_CENTER);
        g_signal_connect(btn, "clicked",
                         G_CALLBACK(on_splash_show_library), s);
        gtk_box_append(GTK_BOX(box), btn);

        g_signal_connect(s->window, "destroy",
                         G_CALLBACK(on_splash_destroy), s);

        s->timerId = g_timeout_add(1000, on_splash_tick, s);
        gtk_window_present(s->window);
    };
    g_signal_connect(app, "activate", G_CALLBACK(thunk), &slotPtr);

    g_application_run(G_APPLICATION(app), gtk_argc, gtk_argv);
    g_object_unref(app);
    return s.proceed;
}

}  // namespace st80
