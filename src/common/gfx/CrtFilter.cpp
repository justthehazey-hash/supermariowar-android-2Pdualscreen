#include "CrtFilter.h"

#include <SDL3/SDL_opengles2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace {
// RetroArch GLSL shaders, unmodified; each source holds both stages selected
// with VERTEX / FRAGMENT. Like RetroArch, PARAMETER_UNIFORM is defined and every
// "#pragma parameter" is passed as a uniform set to its default value.
const char* const kLottes =
#include "shaders/crt_lottes.glsl.inc"
    ;
const char* const kEasyMode =
#include "shaders/crt_easymode.glsl.inc"
    ;
const char* const kHalationLinearize =
#include "shaders/halation_linearize.glsl.inc"
    ;
const char* const kHalationBlurHoriz =
#include "shaders/halation_blur_horiz.glsl.inc"
    ;
const char* const kHalationBlurVert =
#include "shaders/halation_blur_vert.glsl.inc"
    ;
const char* const kHalationThreshold =
#include "shaders/halation_threshold.glsl.inc"
    ;
const char* const kHalationCrt =
#include "shaders/halation_crt.glsl.inc"
    ;

// One pass of a RetroArch .glslp preset. Every pass but the last renders at the
// source size (scale_type = source, scale = 1), the last one to the screen.
struct PassDef {
    const char* source;
    bool filterLinear;     // filter_linearN: how this pass samples its inputs
    bool srgbFramebuffer;  // srgb_framebufferN
};

struct Preset {
    const PassDef* passes;
    int count;
};

const PassDef kLottesPasses[] = {{kLottes, false, false}};          // crt-lottes.glslp
const PassDef kEasyModePasses[] = {{kEasyMode, false, false}};      // crt-easymode.glslp
const PassDef kHalationPasses[] = {                                 // crt-easymode-halation.glslp
    {kHalationLinearize, false, true},
    {kHalationBlurHoriz, false, true},
    {kHalationBlurVert, false, true},
    {kHalationThreshold, false, true},
    {kHalationCrt, true, false},
};

Preset preset(short filter)
{
    switch (filter) {
        case CrtFilter::Lottes: return {kLottesPasses, 1};
        case CrtFilter::EasyMode: return {kEasyModePasses, 1};
        case CrtFilter::EasyModeHalation: return {kHalationPasses, 5};
        default: return {nullptr, 0};
    }
}

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr GLenum kSrgb8Alpha8 = 0x8C43;  // GL_SRGB8_ALPHA8 (OpenGL ES 3)

// GL entry points, loaded through SDL so no GL library needs linking.
struct Gl {
#define SMW_GL(name, type) type name = nullptr;
#define SMW_GL_LIST \
    SMW_GL(ActiveTexture, PFNGLACTIVETEXTUREPROC) \
    SMW_GL(AttachShader, PFNGLATTACHSHADERPROC) \
    SMW_GL(BindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC) \
    SMW_GL(BindBuffer, PFNGLBINDBUFFERPROC) \
    SMW_GL(BindFramebuffer, PFNGLBINDFRAMEBUFFERPROC) \
    SMW_GL(BindTexture, PFNGLBINDTEXTUREPROC) \
    SMW_GL(BufferData, PFNGLBUFFERDATAPROC) \
    SMW_GL(CheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC) \
    SMW_GL(Clear, PFNGLCLEARPROC) \
    SMW_GL(ClearColor, PFNGLCLEARCOLORPROC) \
    SMW_GL(CompileShader, PFNGLCOMPILESHADERPROC) \
    SMW_GL(CreateProgram, PFNGLCREATEPROGRAMPROC) \
    SMW_GL(CreateShader, PFNGLCREATESHADERPROC) \
    SMW_GL(DeleteBuffers, PFNGLDELETEBUFFERSPROC) \
    SMW_GL(DeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC) \
    SMW_GL(DeleteProgram, PFNGLDELETEPROGRAMPROC) \
    SMW_GL(DeleteShader, PFNGLDELETESHADERPROC) \
    SMW_GL(DeleteTextures, PFNGLDELETETEXTURESPROC) \
    SMW_GL(Disable, PFNGLDISABLEPROC) \
    SMW_GL(DisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC) \
    SMW_GL(DrawArrays, PFNGLDRAWARRAYSPROC) \
    SMW_GL(EnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC) \
    SMW_GL(FramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC) \
    SMW_GL(GenBuffers, PFNGLGENBUFFERSPROC) \
    SMW_GL(GenFramebuffers, PFNGLGENFRAMEBUFFERSPROC) \
    SMW_GL(GenTextures, PFNGLGENTEXTURESPROC) \
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
    SMW_GL(Uniform1f, PFNGLUNIFORM1FPROC) \
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

// "#pragma parameter NAME "Description" default min max step" lines.
std::vector<std::pair<std::string, float>> parameterDefaults(const char* source)
{
    std::vector<std::pair<std::string, float>> params;
    std::istringstream lines(source);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string prefix = "#pragma parameter ";
        if (line.compare(0, prefix.size(), prefix) != 0)
            continue;
        std::istringstream fields(line.substr(prefix.size()));
        std::string name;
        fields >> name;
        const size_t open = line.find('"'), close = line.find('"', open + 1);
        if (name.empty() || open == std::string::npos || close == std::string::npos)
            continue;
        params.emplace_back(name, strtof(line.c_str() + close + 1, nullptr));
    }
    return params;
}

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
    if (!window || !preset(filter).passes)
        return false;

    m_window = window;
    m_filter = filter;
    if (!createContext() || !createResources()) {
        stop();
        return false;
    }
    printf("[gfx] CRT filter %d active, %d pass(es), %s (%s)\n", filter, int(m_passes.size()),
        reinterpret_cast<const char*>(gl.GetString(GL_VERSION)),
        reinterpret_cast<const char*>(gl.GetString(GL_RENDERER)));
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
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // OpenGL ES 3 gives sRGB framebuffers (multi-pass presets keep linear-light
    // intermediates without banding); ES 2 still runs every preset.
    for (int major : {3, 2}) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        m_context = SDL_GL_CreateContext(m_window);
        if (m_context) {
            m_gles3 = major >= 3;
            break;
        }
    }
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

unsigned CrtFilter::compile(const char* source, unsigned type, const char* stage)
{
    const std::string header = std::string("#version 100\n#define PARAMETER_UNIFORM\n#define ") + stage + "\n";
    const char* parts[] = {header.c_str(), source};
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

bool CrtFilter::createTarget(Pass& pass, bool srgb)
{
    gl.GenTextures(1, &pass.texture);
    gl.BindTexture(GL_TEXTURE_2D, pass.texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.GenFramebuffers(1, &pass.framebuffer);

    // Prefer an sRGB target when the preset asks for one and ES 3 has it.
    for (bool useSrgb : {srgb && m_gles3, false}) {
        gl.BindTexture(GL_TEXTURE_2D, pass.texture);
        gl.TexImage2D(GL_TEXTURE_2D, 0, useSrgb ? kSrgb8Alpha8 : GL_RGBA, kWidth, kHeight, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, pass.framebuffer);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pass.texture, 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            return true;
        }
        if (!useSrgb)
            break;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    printf("[gfx][warning] CRT filter: intermediate framebuffer unsupported\n");
    return false;
}

bool CrtFilter::createResources()
{
    destroyResources();

    const Preset chain = preset(m_filter);
    m_passes.resize(chain.count);
    for (int index = 0; index < chain.count; index++) {
        Pass& pass = m_passes[index];
        const PassDef& def = chain.passes[index];
        const unsigned vertex = compile(def.source, GL_VERTEX_SHADER, "VERTEX");
        const unsigned fragment = compile(def.source, GL_FRAGMENT_SHADER, "FRAGMENT");
        if (!vertex || !fragment) {
            if (vertex) gl.DeleteShader(vertex);
            if (fragment) gl.DeleteShader(fragment);
            return false;
        }
        pass.program = gl.CreateProgram();
        gl.AttachShader(pass.program, vertex);
        gl.AttachShader(pass.program, fragment);
        gl.BindAttribLocation(pass.program, kVertexSlot, "VertexCoord");
        gl.BindAttribLocation(pass.program, kTexSlot, "TexCoord");
        gl.BindAttribLocation(pass.program, kColorSlot, "COLOR");
        gl.LinkProgram(pass.program);
        gl.DeleteShader(vertex);
        gl.DeleteShader(fragment);

        GLint linked = 0;
        gl.GetProgramiv(pass.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024] = {};
            gl.GetProgramInfoLog(pass.program, sizeof log, nullptr, log);
            printf("[gfx][warning] CRT filter: pass %d link: %s\n", index, log);
            return false;
        }
        pass.parameters = parameterDefaults(def.source);
        if (index + 1 < chain.count && !createTarget(pass, def.srgbFramebuffer))
            return false;
    }

    // Unit quad (0..1) mapped to clip space by MVPMatrix, as RetroArch does.
    // V runs 0 at the bottom; the game frame is uploaded top row first, so its
    // quad uses flipped V, while intermediate targets are already upright.
    const float quad[] = {
        // VertexCoord        TexCoord (frame)   TexCoord (targets)
        0, 0, 0, 1,   0, 1, 0, 0,   0, 0, 0, 0,
        1, 0, 0, 1,   1, 1, 0, 0,   1, 0, 0, 0,
        0, 1, 0, 1,   0, 0, 0, 0,   0, 1, 0, 0,
        1, 1, 0, 1,   1, 0, 0, 0,   1, 1, 0, 0,
    };
    gl.GenBuffers(1, &m_buffer);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    // The frame is ARGB8888: bytes B,G,R,A in memory. Upload as BGRA when the
    // GPU supports it, otherwise reorder into RGBA on the CPU.
    const char* extensions = reinterpret_cast<const char*>(gl.GetString(GL_EXTENSIONS));
    m_bgra = extensions && strstr(extensions, "GL_EXT_texture_format_BGRA8888");

    gl.GenTextures(1, &m_frameTexture);
    gl.BindTexture(GL_TEXTURE_2D, m_frameTexture);
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
    for (Pass& pass : m_passes) {
        if (pass.program) gl.DeleteProgram(pass.program);
        if (pass.framebuffer) gl.DeleteFramebuffers(1, &pass.framebuffer);
        if (pass.texture) gl.DeleteTextures(1, &pass.texture);
    }
    m_passes.clear();
    if (m_frameTexture) gl.DeleteTextures(1, &m_frameTexture);
    if (m_buffer) gl.DeleteBuffers(1, &m_buffer);
    m_frameTexture = m_buffer = 0;
}

void CrtFilter::bindInput(unsigned unit, unsigned texture, bool linear)
{
    gl.ActiveTexture(GL_TEXTURE0 + unit);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = linear ? GL_LINEAR : GL_NEAREST;
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
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
        m_passes.clear();
        m_frameTexture = m_buffer = 0;
        if (!start(window, filter))
            return false;
    }
    // Android may drop GPU objects while the app is in the background.
    if (m_passes.empty() || !gl.IsProgram(m_passes[0].program)) {
        m_passes.clear();
        m_frameTexture = m_buffer = 0;
        if (!createResources())
            return false;
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, m_frameTexture);
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

    const Preset chain = preset(m_filter);
    const float mvp[16] = {2, 0, 0, 0,  0, 2, 0, 0,  0, 0, 1, 0,  -1, -1, 0, 1};
    gl.Disable(GL_BLEND);
    gl.BindBuffer(GL_ARRAY_BUFFER, m_buffer);
    gl.EnableVertexAttribArray(kVertexSlot);
    gl.EnableVertexAttribArray(kTexSlot);
    gl.DisableVertexAttribArray(kColorSlot);
    gl.VertexAttrib4f(kColorSlot, 1, 1, 1, 1);
    gl.VertexAttribPointer(kVertexSlot, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), nullptr);

    const int frameCount = int(m_frameCount++);
    for (size_t index = 0; index < m_passes.size(); index++) {
        const Pass& pass = m_passes[index];
        const bool last = index + 1 == m_passes.size();
        const bool linear = chain.passes[index].filterLinear;

        if (last) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.Viewport(0, 0, windowW, windowH);
            gl.ClearColor(0, 0, 0, 1);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            gl.Viewport((windowW - outW) / 2, (windowH - outH) / 2, outW, outH);
        } else {
            gl.BindFramebuffer(GL_FRAMEBUFFER, pass.framebuffer);
            gl.Viewport(0, 0, kWidth, kHeight);
        }

        // The first pass reads the game frame, later ones an upright target.
        const size_t texOffset = (index == 0 ? 4 : 8) * sizeof(float);
        gl.VertexAttribPointer(kTexSlot, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float),
            reinterpret_cast<const void*>(texOffset));

        gl.UseProgram(pass.program);
        auto uniform = [&](const char* name) { return gl.GetUniformLocation(pass.program, name); };
        gl.UniformMatrix4fv(uniform("MVPMatrix"), 1, GL_FALSE, mvp);
        gl.Uniform1i(uniform("FrameDirection"), 1);
        gl.Uniform1i(uniform("FrameCount"), frameCount);
        gl.Uniform2f(uniform("OutputSize"), last ? float(outW) : float(kWidth), last ? float(outH) : float(kHeight));
        gl.Uniform2f(uniform("TextureSize"), float(kWidth), float(kHeight));
        gl.Uniform2f(uniform("InputSize"), float(kWidth), float(kHeight));
        for (const auto& [name, value] : pass.parameters)
            gl.Uniform1f(uniform(name.c_str()), value);

        // Texture = previous pass (or the frame); PassPrevNTexture = output of
        // the pass N steps back, where one step before pass 0 is the frame.
        auto output = [&](int pass) { return pass < 0 ? m_frameTexture : m_passes[pass].texture; };
        bindInput(0, output(int(index) - 1), linear);
        gl.Uniform1i(uniform("Texture"), 0);
        unsigned unit = 1;
        for (int back = 1; back <= int(index) + 1; back++) {
            const std::string name = "PassPrev" + std::to_string(back) + "Texture";
            const GLint location = uniform(name.c_str());
            if (location < 0)
                continue;
            bindInput(unit, output(int(index) - back), linear);
            gl.Uniform1i(location, int(unit));
            unit++;
        }
        const GLint orig = uniform("OrigTexture");
        if (orig >= 0) {
            bindInput(unit, m_frameTexture, linear);
            gl.Uniform1i(orig, int(unit));
        }

        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    gl.ActiveTexture(GL_TEXTURE0);

    return SDL_GL_SwapWindow(m_window);
}
