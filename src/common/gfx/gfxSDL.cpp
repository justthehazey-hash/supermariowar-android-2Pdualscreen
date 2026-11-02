#include "gfxSDL.h"

#include "path.h"

#include <SDL3_image/SDL_image.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdlib>

extern SDL_Surface* screen;

#define GFX_BPP 16
#define GFX_SCREEN_W 640
#define GFX_SCREEN_H 480

template<typename... Args>
[[noreturn]] void throw_error(std::format_string<Args...> fmt, Args&&... args) {
    throw std::runtime_error(std::format(fmt, std::forward<Args>(args)...));
}

namespace {
void initSdl()
{
    if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
        throw_error("SDL error: {}", SDL_GetError());

    const auto sdl_version = SDL_GetVersion();
    printf("[init] SDL %d.%d.%d loaded.\n",
        SDL_VERSIONNUM_MAJOR(sdl_version),
        SDL_VERSIONNUM_MINOR(sdl_version),
        SDL_VERSIONNUM_MICRO(sdl_version));

    const auto sdlimg_version = IMG_Version();
    printf("[gfx] SDL_image %d.%d.%d loaded.\n",
        SDL_VERSIONNUM_MAJOR(sdlimg_version),
        SDL_VERSIONNUM_MINOR(sdlimg_version),
        SDL_VERSIONNUM_MICRO(sdlimg_version));
}

void quitSdl()
{
    SDL_Quit();
}

SDL_Window* createWindow(bool fullscreen)
{
#ifdef __ANDROID__
    fullscreen = true;
#endif
    // OpenGL so the optional CRT filter can draw with its own GL context.
    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
    if (fullscreen)
        window_flags |= SDL_WINDOW_FULLSCREEN;

    SDL_Window* window = SDL_CreateWindow("smw",
        GFX_SCREEN_W, GFX_SCREEN_H,
        window_flags);
    if (!window)
        throw_error("Couldn't create window: {}", SDL_GetError());

    return window;
}

const char* findPreferredRendererIndex() {
#if defined(SDL2_FORCE_GLES) || defined(__ANDROID__)
    return "opengles2";
#endif
    return nullptr;
}

SDL_Renderer* createRenderer(SDL_Window* window)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(window, findPreferredRendererIndex());
    if (!renderer && findPreferredRendererIndex()) {
        printf("[gfx][warning] Preferred renderer unavailable (%s), using the default\n", SDL_GetError());
        renderer = SDL_CreateRenderer(window, nullptr);
    }
    if (!renderer)
        throw_error("Couldn't create renderer: {}", SDL_GetError());

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_SetRenderLogicalPresentation(renderer, GFX_SCREEN_W, GFX_SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    printf("[gfx] Renderer: %s\n", SDL_GetRendererName(renderer));
    return renderer;
}

SDL_Surface* createScreenSurface()
{
    SDL_Surface* surface = SDL_CreateSurface(GFX_SCREEN_W, GFX_SCREEN_H, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
        throw_error("Couldn't create video buffer: {}", SDL_GetError());

    return surface;
}

SDL_Texture* createScreenTexture(SDL_Renderer* renderer)
{
    SDL_Texture* texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        GFX_SCREEN_W, GFX_SCREEN_H);
    if (!texture)
        throw_error("Couldn't create video texture: {}", SDL_GetError());

    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
        throw_error("Couldn't set up texture scaling mode: %s\n", SDL_GetError());

    return texture;
}
// Set when the GPU context was lost or reset (Android: app sent to the
// background and back). Events may arrive on another thread, hence atomic.
std::atomic<bool> renderDeviceReset {false};

bool SDLCALL watchRenderDevice(void*, SDL_Event* event)
{
    if (event->type == SDL_EVENT_RENDER_DEVICE_RESET || event->type == SDL_EVENT_RENDER_DEVICE_LOST)
        renderDeviceReset = true;
    return true;
}
} // namespace


bool GraphicsSDL::init(bool fullscreen)
{
    initSdl();
    sdl_window = createWindow(fullscreen);
    sdl_renderer = createRenderer(sdl_window);
    sdl_screen_surface = createScreenSurface();
    sdl_screen_texture = createScreenTexture(sdl_renderer);

    printf("[gfx] Game window initialized (%dx%d, %dbpp)\n",
        sdl_screen_surface->w,
        sdl_screen_surface->h,
        SDL_BITSPERPIXEL(sdl_screen_surface->format));

    screen = sdl_screen_surface;
    SDL_AddEventWatch(watchRenderDevice, nullptr);
    return true;
}

bool GraphicsSDL::recreateRenderer()
{
    if (!sdl_window)
        return false;

    SDL_DestroyTexture(sdl_screen_texture);
    sdl_screen_texture = nullptr;
    SDL_DestroyRenderer(sdl_renderer);
    sdl_renderer = nullptr;

    try {
        sdl_renderer = createRenderer(sdl_window);
        sdl_screen_texture = createScreenTexture(sdl_renderer);
    } catch (const std::exception& ex) {
        printf("[gfx][warning] Renderer recovery failed: %s\n", ex.what());
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = nullptr;
        return false;
    }

    printf("[gfx] Renderer recreated after the render device was reset\n");
    return true;
}

GraphicsSDL::~GraphicsSDL()
{
    crt.stop();
    SDL_DestroyTexture(sdl_screen_texture);
    SDL_DestroySurface(sdl_screen_surface);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    quitSdl();
}

void GraphicsSDL::showErrorBox(const char* message) const
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, sdl_window);
}

void GraphicsSDL::setTitle(const char* title) const
{
    SDL_SetWindowTitle(sdl_window, title);
}

short GraphicsSDL::setVideoFilter(short filter)
{
    if (filter <= CrtFilter::Off || filter >= CrtFilter::Count) {
        if (crt.active()) {
            crt.stop();
            recreateRenderer();
        }
        return CrtFilter::Off;
    }

    // The filter takes over the window: release the renderer first.
    SDL_DestroyTexture(sdl_screen_texture);
    sdl_screen_texture = nullptr;
    SDL_DestroyRenderer(sdl_renderer);
    sdl_renderer = nullptr;

    if (!crt.start(sdl_window, filter)) {
        recreateRenderer();
        return CrtFilter::Off;
    }
    return filter;
}

void GraphicsSDL::flipScreen()
{
    if (crt.active()) {
        if (!crt.present(sdl_screen_surface)) {
            // Could not draw with the filter: fall back to the plain renderer.
            printf("[gfx][warning] CRT filter failed, turning it off\n");
            crt.stop();
            recreateRenderer();
        }
        return;
    }

    // Recover from a lost GPU context (Android resume) instead of drawing nothing.
    if (renderDeviceReset.exchange(false))
        recreateRenderer();

    if (!sdl_screen_texture || !SDL_UpdateTexture(sdl_screen_texture, nullptr,
            sdl_screen_surface->pixels, sdl_screen_surface->pitch)) {
        if (!recreateRenderer() || !SDL_UpdateTexture(sdl_screen_texture, nullptr,
                sdl_screen_surface->pixels, sdl_screen_surface->pitch))
            return;
    }
    if (!SDL_RenderClear(sdl_renderer) || !SDL_RenderTexture(sdl_renderer, sdl_screen_texture, nullptr, nullptr)) {
        recreateRenderer();
        return;
    }
    SDL_RenderPresent(sdl_renderer);
}

void GraphicsSDL::changeFullScreen(bool fullscreen) const
{
#ifdef __ANDROID__
    fullscreen = true;
#endif
    if (!SDL_SetWindowFullscreen(sdl_window, fullscreen)) {
        fprintf(stderr, "[gfx] Couldn't toggle fullscreen mode: %s\n", SDL_GetError());
        return;
    }
}

void GraphicsSDL::takeScreenshot() const
{
    using std::chrono::system_clock;

    // NOTE: %F and %T don't work on Windows
    constexpr const char* path_format = "screenshots/%Y-%m-%d_%H%M%S.png";

    const std::time_t now = system_clock::to_time_t(system_clock::now());
    std::ostringstream path_ss;
    path_ss << std::put_time(std::localtime(&now), path_format);
    const std::string path = convertPath(path_ss.str());

    if (IMG_SavePNG(sdl_screen_surface, path.c_str()) != 0) {
        fprintf(stderr, "[gfx] Couldn't write the screenshot to file: %s\n", SDL_GetError());
        return;
    }

    printf("[gfx] Screenshot saved to file: %s\n", path.c_str());
}
