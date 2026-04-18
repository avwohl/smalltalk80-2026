// st80-2026 — Linux app entry (SDL2).
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal desktop frontend for the Linux port. Mirrors the job the
// Swift frontend does on Apple: open a window at the VM's display
// size, drive the interpreter loop, push the 1-bit display buffer
// through the Bridge.h RGBA accessor onto an SDL texture, and
// translate SDL_Event into st80_post_* calls.
//
// Usage:
//   st80-linux [--cycles-per-frame N] [--no-window] <path-to-image>
//
// Flags:
//   --cycles-per-frame N  Interpreter cycles run between frames
//                         (default 4000 — same as the Apple frontend).
//   --no-window           Headless mode: run cycles, print status,
//                         don't open a window. Useful on servers and
//                         for CI smoke tests.
//
// Three-button mouse mapping (single-button-host fallback):
//   plain press   = red    (primary / select)
//   Alt  + press  = yellow (text / operation menu)
//   Ctrl + press  = blue   (frame / window menu)
// A real 3-button mouse uses its physical buttons directly.

#include "Bridge.h"

#if ST80_LINUX_HAS_LAUNCHER
#include "Launcher.hpp"
#endif

#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct Args {
    std::string imagePath;
    int cyclesPerFrame = 4000;
    bool noWindow      = false;
    bool forceLauncher = false;   // --launcher
};

void usage(const char *argv0) {
    std::fprintf(stderr,
        "usage: %s [--cycles-per-frame N] [--no-window]"
        " [--launcher] [<path-to-image>]\n"
        "\n"
        "With no image path, st80-linux opens the GTK4 launcher to"
        " pick or download an image (when built with the launcher"
        " enabled). --launcher forces the launcher even when an"
        " auto-launch image is starred.\n",
        argv0);
}

int parseArgs(int argc, char **argv, Args &out) {
    int i = 1;
    while (i < argc) {
        const char *a = argv[i];
        if (std::strcmp(a, "--cycles-per-frame") == 0 && i + 1 < argc) {
            out.cyclesPerFrame = std::atoi(argv[++i]);
            if (out.cyclesPerFrame < 1) out.cyclesPerFrame = 1;
            ++i;
        } else if (std::strcmp(a, "--no-window") == 0) {
            out.noWindow = true;
            ++i;
        } else if (std::strcmp(a, "--launcher") == 0) {
            out.forceLauncher = true;
            ++i;
        } else if (a[0] == '-' && a[1] != '\0') {
            usage(argv[0]);
            return 64;
        } else {
            if (!out.imagePath.empty()) {
                usage(argv[0]);
                return 64;
            }
            out.imagePath = a;
            ++i;
        }
    }
    return 0;
}

St80MouseButton buttonFromSdl(const SDL_MouseButtonEvent &e) {
    if (e.button == SDL_BUTTON_LEFT) {
        const auto mods = SDL_GetModState();
        if (mods & KMOD_CTRL)  return ST80_BTN_BLUE;
        if (mods & KMOD_ALT)   return ST80_BTN_YELLOW;
        return ST80_BTN_RED;
    }
    if (e.button == SDL_BUTTON_MIDDLE) return ST80_BTN_BLUE;
    if (e.button == SDL_BUTTON_RIGHT)  return ST80_BTN_YELLOW;
    return ST80_BTN_RED;
}

uint32_t modifiersFromSdl(SDL_Keymod mods) {
    uint32_t out = 0;
    if (mods & KMOD_SHIFT) out |= ST80_MOD_SHIFT;
    if (mods & KMOD_CTRL)  out |= ST80_MOD_CTRL;
    if (mods & KMOD_ALT)   out |= ST80_MOD_OPTION;
    if (mods & KMOD_GUI)   out |= ST80_MOD_COMMAND;
    return out;
}

int runHeadless(const Args &args) {
    std::fprintf(stderr,
        "st80-linux: headless mode (image=%s, cycles-per-tick=%d)\n",
        args.imagePath.c_str(), args.cyclesPerFrame);

    if (!st80_init(args.imagePath.c_str())) {
        std::fprintf(stderr, "st80-linux: st80_init FAILED\n");
        return 2;
    }

    // Run a fixed number of cycles, mimicking st80_run's CLI. Not a
    // real interactive loop — we have no input source in headless
    // mode. Exits cleanly after one big batch so the .deb / .rpm
    // post-install smoke test has a deterministic zero-exit path.
    const int total = args.cyclesPerFrame * 10;
    const int ran = st80_run(total);
    std::fprintf(stderr, "st80-linux: ran %d cycles, quit=%d\n",
                 ran, st80_quit_requested());

    st80_shutdown();
    return 0;
}

int runWindowed(const Args &args) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "st80-linux: SDL_Init failed: %s\n", SDL_GetError());
        return 3;
    }

    if (!st80_init(args.imagePath.c_str())) {
        std::fprintf(stderr, "st80-linux: st80_init FAILED\n");
        SDL_Quit();
        return 2;
    }

    // The image sets the display size during boot via
    // primitiveBeDisplay; we don't know it yet. Run a tiny priming
    // batch and then query. Matches the Apple frontend pattern.
    st80_run(4000);
    int W = st80_display_width();
    int H = st80_display_height();
    if (W <= 0 || H <= 0) { W = 640; H = 480; }

    SDL_Window *window = SDL_CreateWindow(
        "Smalltalk-80",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W, H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "st80-linux: SDL_CreateWindow failed: %s\n",
                     SDL_GetError());
        st80_shutdown();
        SDL_Quit();
        return 3;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);  // software fallback
    }
    if (!renderer) {
        std::fprintf(stderr, "st80-linux: SDL_CreateRenderer failed: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        st80_shutdown();
        SDL_Quit();
        return 3;
    }
    SDL_RenderSetLogicalSize(renderer, W, H);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, W, H);

    // Full initial upload so the window isn't blank between boot and
    // the first VM-driven display_changed rect.
    const uint32_t *pixels = st80_display_pixels();
    if (pixels) {
        SDL_UpdateTexture(texture, nullptr, pixels, W * 4);
    }

    SDL_ShowCursor(SDL_ENABLE);

    bool running = true;
    while (running && !st80_quit_requested()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_MOUSEMOTION:
                    st80_post_mouse_move(ev.motion.x, ev.motion.y);
                    break;

                case SDL_MOUSEBUTTONDOWN:
                    st80_post_mouse_down(ev.button.x, ev.button.y,
                                         buttonFromSdl(ev.button));
                    break;

                case SDL_MOUSEBUTTONUP:
                    st80_post_mouse_up(ev.button.x, ev.button.y,
                                       buttonFromSdl(ev.button));
                    break;

                case SDL_TEXTINPUT: {
                    // UTF-8. Feed 7-bit ASCII through directly; ignore
                    // multibyte glyphs for the Blue Book image's
                    // decoded-keyboard input path.
                    const char *t = ev.text.text;
                    for (int k = 0; t[k] != '\0'; ++k) {
                        const unsigned char c = static_cast<unsigned char>(t[k]);
                        if (c < 0x80) {
                            st80_post_key_down(c, 0);
                        }
                    }
                    break;
                }

                case SDL_KEYDOWN: {
                    const SDL_Keycode kc = ev.key.keysym.sym;
                    const uint32_t mods = modifiersFromSdl(
                        static_cast<SDL_Keymod>(ev.key.keysym.mod));
                    // Pass control keys (backspace, tab, return, esc)
                    // through as ASCII. Printable keys arrive via
                    // SDL_TEXTINPUT so shift/layout is respected.
                    int code = 0;
                    switch (kc) {
                        case SDLK_BACKSPACE: code = 8;  break;
                        case SDLK_TAB:       code = 9;  break;
                        case SDLK_RETURN:    code = 13; break;
                        case SDLK_ESCAPE:    code = 27; break;
                        case SDLK_DELETE:    code = 127; break;
                        default: break;
                    }
                    if (code) st80_post_key_down(code, mods);
                    break;
                }

                case SDL_WINDOWEVENT:
                    if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                        running = false;
                    }
                    break;
            }
        }

        // Cycle the VM.
        st80_run(args.cyclesPerFrame);

        // Handle display resize (image may have called
        // primitiveBeDisplay with a new form).
        const int newW = st80_display_width();
        const int newH = st80_display_height();
        if (newW > 0 && newH > 0 && (newW != W || newH != H)) {
            W = newW;
            H = newH;
            SDL_SetWindowSize(window, W, H);
            SDL_RenderSetLogicalSize(renderer, W, H);
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(
                renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, W, H);
            const uint32_t *p = st80_display_pixels();
            if (p) SDL_UpdateTexture(texture, nullptr, p, W * 4);
        }

        // Push any newly-dirty VM pixels into the texture.
        const St80Rect dirty = st80_display_sync();
        if (dirty.w > 0 && dirty.h > 0) {
            const uint32_t *p = st80_display_pixels();
            if (p) {
                SDL_Rect r{dirty.x, dirty.y, dirty.w, dirty.h};
                // Offset into the source buffer to match the dirty
                // rect. SDL_UpdateTexture takes a full row pitch so we
                // pass W*4 and the top-left pixel of the dirty rect.
                const uint32_t *rowStart = p + (dirty.y * W) + dirty.x;
                SDL_UpdateTexture(texture, &r, rowStart, W * 4);
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    st80_shutdown();
    SDL_Quit();
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Args args;
    if (int rc = parseArgs(argc, argv, args); rc != 0) return rc;

    // If no image path was given, route through the launcher
    // (auto-launch splash → picker → launch). Mirrors the same path
    // on Catalyst (ContentView.swift) and Windows (st80_windows_main.cpp).
    if (args.imagePath.empty()) {
#if ST80_LINUX_HAS_LAUNCHER
        if (!args.forceLauncher) {
            std::string displayName;
            std::string remembered = st80::LoadAutoLaunchInfo(displayName);
            if (!remembered.empty()) {
                if (st80::ShowAutoLaunchSplash(argc, argv, remembered,
                                               displayName)) {
                    args.imagePath = remembered;
                }
            }
        }
        if (args.imagePath.empty()) {
            std::string picked;
            if (!st80::ShowLauncher(argc, argv, picked)) {
                return 0;  // user closed launcher
            }
            args.imagePath = picked;
        }
#else
        usage(argv[0]);
        return 64;
#endif
    }

    int rc = args.noWindow ? runHeadless(args) : runWindowed(args);

#if ST80_LINUX_HAS_LAUNCHER
    if (rc == 0) st80::RememberLastImage(args.imagePath);
#endif
    return rc;
}
