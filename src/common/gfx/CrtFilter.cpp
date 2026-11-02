#include "CrtFilter.h"

#include <SDL3/SDL_opengles2.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {
// RetroArch GLSL shaders, unmodified; each source holds both stages selected
// with VERTEX / FRAGMENT. Without PARAMETER_UNIFORM they use their defaults.
const char* const kLottes =
#include "shaders/crt_lottes.glsl.inc"
    ;
const char* const kEasyMode =
#include "shaders/crt_easymode.glsl.inc"
    ;

const char* shaderSource(short filter)
{
    switch (filter) {
        case CrtFilter::Lottes: return kLottes;
        case CrtFilter::EasyMode: return kEasyMode;
        default: return nullptr;
    }
}

constexpr int kWidth = 640;
constexpr int kHeight = 480;

// GL entry points, loaded through SDL so no GL library needs linking.
struct Gl {
#define SMW_GL(name, type) type name = nullptr;
#define SMW_GL_LIST \
    SMW_GL(ActiveTexture, PFNGLACTIVETEXTUREPROC) \
    SMW_GL(AttachShader, PFNGLATTACHSHADERPROC) \
    SMW_GL(BindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC) \
    SMW_GL(BindBuffer, PFNGLBINDBUFFERPROC) \
    SMW_GL(BindTexture, PFNGLBINDTEXTUREPROC) \
    SMW_GL(BufferData, PFNGLBUFFERDATAPROC) \
    SMW_GL(Clear, PFNGLCLEARPROC) \
    SMW_GL(ClearColor, PFNGLCLEARCOLORPROC) \
    SMW_GL(CompileShader, PFNGLCOMPILESHADERPROC) \
    SMW_GL(CreateProgram, PFNGLCREATEPROGRAMPROC) \
    SMW_GL(CreateShader, PFNGLCREATESHADERPROC) \
    SMW_GL(DeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    SMW_GL(DeleteProgram, PFNGLDELETEPROGRAMPROC) \
    SMW_GL(DeleteShader, PFNGLDELETESHADERPROC) \
    SMW_GL(DeleteTextures, PFNGLDELETETEXTURESPROC) \
    SMW_GL(Disable, PFNGLDISABLEPROC) \
    SMW_GL(DisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC) \
    SMW_GL(DrawArrays, PFNGLDRAWARRAYSPROC) \
    SMW_GL(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    SMW_GL(GenBuffers, PFNGLGENBUFFERSPROC) \
    SMW_GL(GenTextures, PFNGLGENTEXTURESPROC) \
    SMW_GL(GetAttribLocation, PFNGLGETATTRIBLOCATIONPROC) \
    SMW_GL(GetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC) \
    SMW_GL(GetProgramiv, PFNGLGETPROGRAMIVPROC) \
    SMW_GL(GetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC) \
    SMW_GL(GetShaderiv, PFNGLGETSHADERIVPROC) \
    SMW_GL(GetString, PFNGLGETSTRINGPROC) \
    SMW_GL(GetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC) \
    SMW_GL(IsProgram, PFNGLISPROGRAMPROC) \
    SMW_GL(LinkProgram, PFNGLLINKPROGRAMPROC) \
    SMW_GL(PixelStorei, PFNGLPIXELSTOREIPROC) \
    SMW_GL(ShaderSource, PFNGLSHADERSOURCEPROC) \
    SMW_GL(TexImage2D, PFNGLTEXIMAGE2DPROC) \
    SMW_GL(TexParameteri, PFNGLTEXPARAMETERIPROC) \
    SMW_GL(TexSubImage2D, PFNGLTEXSUBIMAGE2DPROC) \
    SMW_GL(Uniform1i, PFNGLUNIFORM1IPROC) \
    SMW_GL(Uniform2f, PFNGLUNIFORM2FPROC) \
    SMW_GL(UniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC) \
    SMW_GL(UseProgram, PFNGLUSEPROGRAMPROC) \
    SMW_GL(VertexAttrib4f, PFNGLVERTEXATTRIB4FPROC) \
    SMW_GL(VertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC) \
    SMW_GL(Viewport, PFNGLVIEWPORTPROC)
    SMW_GL_LIST
#undef SMW_GL

    bool load()
    {
        bool ok = true;
#define SMW_GL(name, type) \
        name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); \
        ok = ok && name;
        SMW_GL_LIST
#undef SMW_GL
        return ok;
    }
#undef SMW_GL_LIST
} gl;

// Fixed attribute slots, bound before linking.
enum : unsigned { kVertexSlot = 0, kTexSlot = 1, kColorSlot = 2 };
} // namespace


CrtFilter::~CrtFilter()
{
    stop();
}

bool CrtFilter::start(SDL_Window* window, short filter)
{
    stop();
    if (!window || !shaderSource(filter))
        return false;

    m_window = window;
    m_filter = filter;
    if (!createContext() || !createResources()) {
        stop();
        return false;
    }
    printf("[gfx] CRT filter %d active (%s)\n", filter, reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)));
    return true;
}

void CrtFilter::stop()
{
    if (m_context) {
        SDL_GL_MakeCurrent(m_window, m_context);
        destroyResources();
        SDL_GL_MakeCurrent(m_window, nullptr);
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    m_window = nullptr;
    m_filter = Off;
}

bool CrtFilter::createContext()
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context) {
        printf("[gfx][warning] CRT filter: no OpenGL ES context: %s\n", SDL_GetError());
        return false;
    }
    if (!gl.load()) {
        printf("[gfx][warning] CRT filter: missing OpenGL ES functions\n");
        return false;
    }
    SDL_GL_SetSwapInterval(0); // pacing stays with the game's frame limiter
    return true;
}

unsigned CrtFilter::compile(unsigned type, const char* stage)
{
    const std::string header = std::string("#version 100\n#define ") + stage + "\n";
    const char* parts[] = {header.c_str(), shaderSource(m_filter)};
    const unsigned shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 2, parts, nullptr);
    gl.CompileShader(shader);

    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        gl.GetShaderInfoLog(shader, sizeof log, nullptr, log);
        printf("[gfx][warning] CRT filter: %s shader: %s\n", stage, log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

bool CrtFilter::createResources()
{
    destroyResources();

    const unsigned vertex = compile(GL_VERTEX_SHADER, "VERTEX");
    const unsigned fragment = compile(GL_FRAGMENT_SHADER, "FRAGMENT");
    if (!vertex || !fragment) {
        if (vertex) gl.DeleteShader(vertex);
        if (fragment) gl.DeleteShader(fragment);
        return false;
    }
    m_program = gl.CreateProgram();
    gl.AttachShader(m_program, vertex);
    gl.AttachShader(m_program, fragment);
    gl.BindAttribLocation(m_program, kVertexSlot, "VertexCoord");
    gl.BindAttribLocation(m_program, kTexSlot, "TexCoord");
    gl.BindAttribLocation(m_program, kColorSlot, "COLOR");
    gl.LinkProgram(m_program);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);

    GLint linked = 0;
    gl.GetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {};
        gl.GetProgramInfoLog(m_program, sizeof log, nullptr, log);
        printf("[gfx][warning] CRT filter: link: %s\n", log);
        return false;
    }

    // Unit quad (0..1) mapped to clip space by MVPMatrix, as RetroArch does.
    // The frame's first row is the top of the picture, hence the flipped V.
    const float quad[] = {
        // VertexCoord           TexCoord
        0, 0, 0, 1,   0, 1, 0, 0,
        1, 0, 0, 1,   1, 1, 0, 0,
        0, 1, 0, 1,   0, 0, 0, 0,
        1, 1, 0, 1,   1, 0, 0, 0,
    };
    gl.GenBuffers(1, &m_buffer);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    // The frame is ARGB8888: bytes B,G,R,A in memory. Upload as BGRA when the
    // GPU supports it, otherwise reorder into RGBA on the CPU.
    const char* extensions = reinterpret_cast<const char*>(gl.GetString(GL_EXTENSIONS));
    m_bgra = extensions && strstr(extensions, "GL_EXT_texture_format_BGRA8888");

    gl.GenTextures(1, &m_texture);
    gl.BindTexture(GL_TEXTURE_2D, m_texture);
    // Both presets sample with filter_linear = false.
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const GLenum format = m_bgra ? GL_BGRA_EXT : GL_RGBA;
    gl.TexImage2D(GL_TEXTURE_2D, 0, format, kWidth, kHeight, 0, format, GL_UNSIGNED_BYTE, nullptr);
    return true;
}

void CrtFilter::destroyResources()
{
    if (!gl.DeleteProgram)
        return;
    if (m_program) gl.DeleteProgram(m_program);
    if (m_texture) gl.DeleteTextures(1, &m_texture);
    if (m_buffer) gl.DeleteBuffers(1, &m_buffer);
    m_program = m_texture = m_buffer = 0;
}

bool CrtFilter::present(const SDL_Surface* frame)
{
    if (!m_context || !frame || frame->w != kWidth || frame->h != kHeight)
        return false;

    if (SDL_GL_GetCurrentContext() != m_context && !SDL_GL_MakeCurrent(m_window, m_context)) {
        // The context itself is gone (rare on Android after a long pause): rebuild it.
        const short filter = m_filter;
        SDL_Window* window = m_window;
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
        m_program = m_texture = m_buffer = 0;
        if (!start(window, filter))
            return false;
    }
    // Android may drop GPU objects while the app is in the background.
    if (!m_program || !gl.IsProgram(m_program)) {
        m_program = m_texture = m_buffer = 0;
        if (!createResources())
            return false;
    }

    gl.BindTexture(GL_TEXTURE_2D, m_texture);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const Uint8* pixels = static_cast<const Uint8*>(frame->pixels);
    const int rowBytes = kWidth * 4;
    if (m_bgra && frame->pitch == rowBytes) {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
    } else {
        m_staging.resize(size_t(rowBytes) * kHeight);
        for (int y = 0; y < kHeight; y++) {
            const Uint8* src = pixels + size_t(y) * frame->pitch;
            Uint8* dst = m_staging.data() + size_t(y) * rowBytes;
            if (m_bgra) {
                memcpy(dst, src, rowBytes);
            } else {
                for (int x = 0; x < rowBytes; x += 4) {
                    dst[x] = src[x + 2];
                    dst[x + 1] = src[x + 1];
                    dst[x + 2] = src[x];
                    dst[x + 3] = 255;
                }
            }
        }
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight, m_bgra ? GL_BGRA_EXT : GL_RGBA,
            GL_UNSIGNED_BYTE, m_staging.data());
    }

    // Letterbox 4:3 into the window, like the renderer's logical presentation.
    int windowW = 0, windowH = 0;
    SDL_GetWindowSizeInPixels(m_window, &windowW, &windowH);
    const float scale = SDL_min(windowW / float(kWidth), windowH / float(kHeight));
    const int outW = int(kWidth * scale + 0.5f), outH = int(kHeight * scale + 0.5f);

    gl.Viewport(0, 0, windowW, windowH);
    gl.Disable(GL_BLEND);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.Viewport((windowW - outW) / 2, (windowH - outH) / 2, outW, outH);

    gl.UseProgram(m_program);
    const float mvp[16] = {2, 0, 0, 0,  0, 2, 0, 0,  0, 0, 1, 0,  -1, -1, 0, 1};
    auto uniform = [](unsigned program, const char* name) { return gl.GetUniformLocation(program, name); };
    gl.UniformMatrix4fv(uniform(m_program, "MVPMatrix"), 1, GL_FALSE, mvp);
    gl.Uniform1i(uniform(m_program, "FrameDirection"), 1);
    gl.Uniform1i(uniform(m_program, "FrameCount"), int(m_frameCount++));
    gl.Uniform2f(uniform(m_program, "OutputSize"), float(outW), float(outH));
    gl.Uniform2f(uniform(m_program, "TextureSize"), float(kWidth), float(kHeight));
    gl.Uniform2f(uniform(m_program, "InputSize"), float(kWidth), float(kHeight));
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(uniform(m_program, "Texture"), 0);

    gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
    gl.EnableVertexAttribArray(kVertexSlot);
    gl.VertexAttribPointer(kVertexSlot, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    gl.EnableVertexAttribArray(kTexSlot);
    gl.VertexAttribPointer(kTexSlot, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
        reinterpret_cast<const void*>(4 * sizeof(float)));
    gl.DisableVertexAttribArray(kColorSlot);
    gl.VertexAttrib4f(kColorSlot, 1, 1, 1, 1);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    return SDL_GL_SwapWindow(m_window);
}
