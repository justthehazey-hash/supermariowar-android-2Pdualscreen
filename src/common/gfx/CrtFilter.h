#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <utility>
#include <vector>

// Optional final stage that draws the finished 640x480 frame through a
// RetroArch-style GLSL shader preset (CRT emulation) with OpenGL ES.
// Presets may have several passes; intermediate passes render at the source
// size and later passes can read earlier ones (PassPrevNTexture), as in
// RetroArch. While a filter is active it owns the window's GL context instead
// of the SDL renderer; with the filter off the game draws exactly as before.
class CrtFilter {
public:
    enum Filter : short {
        Off = 0,
        Lottes = 1,
        EasyMode = 2,
        EasyModeHalation = 3,
        Count
    };

    ~CrtFilter();

    // Create the GL context and shaders for the filter. On failure nothing is
    // left behind and the caller keeps using its renderer. With native set,
    // the shaders see 240 lines, the art's real line count (graphics are drawn
    // at 2x), so each row of art gets one scanline as on a console in
    // RetroArch; the full 640 columns are kept so the finer text stays sharp.
    // Otherwise they see the whole 640x480 frame.
    bool start(SDL_Window* window, short filter, bool native);
    void stop();
    bool active() const { return m_context != nullptr; }

    // Draw the frame letterboxed to the window and swap. False when the GPU
    // state could not be (re)built; the caller should turn the filter off.
    bool present(const SDL_Surface* frame);

private:
    struct Pass {
        unsigned program = 0;
        unsigned framebuffer = 0;  // 0 for the last pass (draws to the window)
        unsigned texture = 0;      // this pass's output, read by later passes
        std::vector<std::pair<std::string, float>> parameters;  // #pragma parameter defaults
    };

    bool createContext();
    bool createResources();
    void destroyResources();
    unsigned compile(const char* source, unsigned type, const char* stage);
    bool createTarget(Pass& pass, bool srgb);
    unsigned link(const char* source, int index);
    void bindInput(unsigned unit, unsigned texture, bool linear);

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    short m_filter = Off;
    bool m_native = true;
    int m_sourceW = 0, m_sourceH = 0;  // what the preset sees as the game frame
    Pass m_downscale;                   // 640x480 -> 640x240 when native
    bool m_gles3 = false;

    std::vector<Pass> m_passes;
    unsigned m_frameTexture = 0;
    unsigned m_buffer = 0;
    bool m_bgra = false;
    unsigned m_frameCount = 0;
    std::vector<Uint8> m_staging;
};
