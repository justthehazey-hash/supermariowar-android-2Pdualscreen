#pragma once

#include <SDL3/SDL.h>

#include <vector>

// Optional final pass that draws the finished 640x480 frame through a
// RetroArch-style single-pass GLSL shader (CRT emulation) with OpenGL ES 2.
// While a filter is active it owns the window's GL context instead of the
// SDL renderer; with the filter off the game draws exactly as before.
class CrtFilter {
public:
    enum Filter : short {
        Off = 0,
        Lottes = 1,
        EasyMode = 2,
        Count
    };

    ~CrtFilter();

    // Create the GL context and shader for the filter. On failure nothing is
    // left behind and the caller keeps using its renderer.
    bool start(SDL_Window* window, short filter);
    void stop();
    bool active() const { return m_context != nullptr; }

    // Draw the frame letterboxed to the window and swap. False when the GPU
    // state could not be (re)built; the caller should turn the filter off.
    bool present(const SDL_Surface* frame);

private:
    bool createContext();
    bool createResources();
    void destroyResources();
    unsigned compile(unsigned type, const char* stage);

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_context = nullptr;
    short m_filter = Off;

    unsigned m_program = 0;
    unsigned m_texture = 0;
    unsigned m_buffer = 0;
    bool m_bgra = false;
    unsigned m_frameCount = 0;
    std::vector<Uint8> m_staging;
};
