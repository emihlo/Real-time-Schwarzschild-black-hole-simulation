#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#ifdef _WIN32
#  include <windows.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "gl_api.h"
#include "font8x8.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// ------------------------------------------------------------------ utils

static std::string gShaderDir;
static double gScroll = 0.0;

static bool readFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static std::string exeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) return ".";
    std::string p(buf, n);
    size_t s = p.find_last_of("\\/");
    return (s == std::string::npos) ? "." : p.substr(0, s);
#else
    return ".";
#endif
}

static std::string findShaderDir()
{
    std::string probe;
    if (const char* env = std::getenv("BLACKHOLE_SHADER_DIR")) {
        std::string p = env;
        if (readFile(p + "/blackhole.frag", probe)) return p;
    }
    std::string local = exeDir() + "/shaders";
    if (readFile(local + "/blackhole.frag", probe)) return local;
#ifdef SHADER_DIR
    if (readFile(std::string(SHADER_DIR) + "/blackhole.frag", probe))
        return SHADER_DIR;
#endif
    return "shaders";
}

static float halton(int index, int base)
{
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= float(base);
        r += f * float(index % base);
        index /= base;
    }
    return r;
}

// ------------------------------------------------------------------ shader plumbing

struct SceneU {
    GLint res, time, camPos, camFwd, camRight, camUp, tanHalfFov,
          steps, lens, disk, diskIn, diskOut, diskTemp, diskBright,
          rotSpeed, jitter;
};

struct PostU {
    GLint scene, prev, bloom, mode, weight, threshold, texel, dir,
          exposure, bloomStrength, res;
};

static SceneU getSceneU(GLuint p)
{
    return SceneU{
        glGetUniformLocation(p, "uRes"),
        glGetUniformLocation(p, "uTime"),
        glGetUniformLocation(p, "uCamPos"),
        glGetUniformLocation(p, "uCamFwd"),
        glGetUniformLocation(p, "uCamRight"),
        glGetUniformLocation(p, "uCamUp"),
        glGetUniformLocation(p, "uTanHalfFov"),
        glGetUniformLocation(p, "uSteps"),
        glGetUniformLocation(p, "uLens"),
        glGetUniformLocation(p, "uDisk"),
        glGetUniformLocation(p, "uDiskIn"),
        glGetUniformLocation(p, "uDiskOut"),
        glGetUniformLocation(p, "uDiskTemp"),
        glGetUniformLocation(p, "uDiskBright"),
        glGetUniformLocation(p, "uRotSpeed"),
        glGetUniformLocation(p, "uJitter"),
    };
}

static PostU getPostU(GLuint p)
{
    return PostU{
        glGetUniformLocation(p, "uScene"),
        glGetUniformLocation(p, "uPrev"),
        glGetUniformLocation(p, "uBloom"),
        glGetUniformLocation(p, "uMode"),
        glGetUniformLocation(p, "uWeight"),
        glGetUniformLocation(p, "uThreshold"),
        glGetUniformLocation(p, "uTexel"),
        glGetUniformLocation(p, "uDir"),
        glGetUniformLocation(p, "uExposure"),
        glGetUniformLocation(p, "uBloomStrength"),
        glGetUniformLocation(p, "uRes"),
    };
}

static GLuint compileShader(GLenum type, const char* src, const char* label)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192] = {};
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(log), &n, log);
        std::fprintf(stderr, "[shader] %s compile error:\n%.*s\n", label, int(n), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint buildProgram(const std::string& dir, const char* vertFile,
                           const char* fragFile, const char* label)
{
    std::string vsSrc, fsSrc;
    if (!readFile(dir + "/" + vertFile, vsSrc) ||
        !readFile(dir + "/" + fragFile, fsSrc)) {
        std::fprintf(stderr, "[shader] cannot read sources for %s\n", label);
        return 0;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc.c_str(), vertFile);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc.c_str(), label);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192] = {};
        GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof(log), &n, log);
        std::fprintf(stderr, "[shader] %s link error:\n%.*s\n", label, int(n), log);
        glDeleteProgram(p);
        p = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// ------------------------------------------------------------------ render targets

struct RT {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
};

static bool rtEnsure(RT& rt, int w, int h)
{
    if (rt.tex && rt.w == w && rt.h == h) return true;

    if (!rt.tex) glGenTextures(1, &rt.tex);
    glBindTexture(GL_TEXTURE_2D, rt.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!rt.fbo) glGenFramebuffers(1, &rt.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, rt.tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) std::fprintf(stderr, "[gl] framebuffer incomplete %dx%d\n", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    rt.w = w;
    rt.h = h;
    return ok;
}

// ------------------------------------------------------------------ overlay HUD

struct UiColor {
    float r, g, b, a;
};

struct UiVertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

class Overlay {
public:
    bool init(const std::string& shaderDir)
    {
        program = buildProgram(shaderDir, "overlay.vert", "overlay.frag", "overlay");
        if (!program) return false;

        fontLocation = glGetUniformLocation(program, "uFont");

        constexpr int atlasW = 17 * 8;
        constexpr int atlasH = 8 * 8;
        std::vector<unsigned char> pixels(size_t(atlasW) * atlasH * 4, 0);

        auto pixel = [&](int x, int y, unsigned char alpha) {
            size_t i = (size_t(y) * atlasW + x) * 4;
            pixels[i + 0] = 255;
            pixels[i + 1] = 255;
            pixels[i + 2] = 255;
            pixels[i + 3] = alpha;
        };

        for (int glyph = 0; glyph < 128; ++glyph) {
            int cx = (glyph % 16) * 8;
            int cy = (glyph / 16) * 8;
            for (int row = 0; row < 8; ++row) {
                uint8_t bits = font8x8_basic[glyph][row];
                for (int col = 0; col < 8; ++col) {
                    if (bits & (1u << col))
                        pixel(cx + col, cy + row, 255);
                }
            }
        }

        // A solid cell in the last column is used for panels and rules.
        for (int y = 0; y < 8; ++y)
            for (int x = 16 * 8; x < 17 * 8; ++x)
                pixel(x, y, 255);

        glGenTextures(1, &fontTex);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasW, atlasH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                              reinterpret_cast<const void*>(offsetof(UiVertex, x)));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                              reinterpret_cast<const void*>(offsetof(UiVertex, u)));
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                              reinterpret_cast<const void*>(offsetof(UiVertex, r)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glUseProgram(program);
        glUniform1i(fontLocation, 0);
        glUseProgram(0);
        return true;
    }

    bool ready() const { return program != 0 && vao != 0 && vbo != 0 && fontTex != 0; }

    bool reload(const std::string& shaderDir)
    {
        GLuint next = buildProgram(shaderDir, "overlay.vert", "overlay.frag", "overlay");
        if (!next) return false;
        if (program) glDeleteProgram(program);
        program = next;
        fontLocation = glGetUniformLocation(program, "uFont");
        glUseProgram(program);
        glUniform1i(fontLocation, 0);
        glUseProgram(0);
        return true;
    }

    void begin(int width, int height)
    {
        resW = std::max(width, 1);
        resH = std::max(height, 1);
        verts.clear();
    }

    void rect(float x, float y, float w, float h, UiColor color)
    {
        constexpr float uw = 16.0f * 8.0f / (17.0f * 8.0f);
        constexpr float uv0 = 0.0f;
        constexpr float uv1 = 8.0f / (8.0f * 8.0f);
        addQuad(x, y, w, h, uw, uv0, 1.0f, uv1, color);
    }

    void panel(float x, float y, float w, float h, UiColor accent, int scale)
    {
        UiColor shadow{0.0f, 0.0f, 0.0f, 0.22f};
        UiColor background{0.018f, 0.024f, 0.034f, 0.72f};
        UiColor edge{0.60f, 0.70f, 0.82f, 0.18f};

        rect(x + 2.0f * scale, y + 3.0f * scale, w, h, shadow);
        rect(x, y, w, h, background);
        rect(x, y, w, 1.0f * scale, edge);
        rect(x, y, 2.0f * scale, h, accent);
    }

    void text(float x, float y, int scale, const std::string& str,
              UiColor color, float tracking = 0.0f)
    {
        float advance = float(8 * scale) + tracking;
        for (unsigned char ch : str) {
            if (ch < 32 || ch > 127) ch = ' ';
            int cx = (ch % 16) * 8;
            int cy = (ch / 16) * 8;
            constexpr float atlasW = 17.0f * 8.0f;
            constexpr float atlasH = 8.0f * 8.0f;
            float u0 = float(cx) / atlasW;
            float v0 = float(cy) / atlasH;
            float u1 = float(cx + 8) / atlasW;
            float v1 = float(cy + 8) / atlasH;
            addQuad(x, y, 8.0f * scale, 8.0f * scale, u0, v0, u1, v1, color);
            x += advance;
        }
    }

    int textWidth(const std::string& str, int scale, float tracking = 0.0f) const
    {
        return int((float(str.size()) * (float(8 * scale) + tracking)));
    }

    void flush()
    {
        if (!ready() || verts.empty()) return;

        for (UiVertex& v : verts) {
            v.x = (v.x / float(resW)) * 2.0f - 1.0f;
            v.y = 1.0f - (v.y / float(resH)) * 2.0f;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(UiVertex)),
                     verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_BLEND);
        verts.clear();
    }

private:
    void addQuad(float x, float y, float w, float h,
                 float u0, float v0, float u1, float v1, UiColor c)
    {
        float x1 = x + w;
        float y1 = y + h;
        UiVertex a{x,  y,  u0, v0, c.r, c.g, c.b, c.a};
        UiVertex b{x1, y,  u1, v0, c.r, c.g, c.b, c.a};
        UiVertex d{x1, y1, u1, v1, c.r, c.g, c.b, c.a};
        UiVertex e{x,  y1, u0, v1, c.r, c.g, c.b, c.a};
        verts.push_back(a);
        verts.push_back(b);
        verts.push_back(d);
        verts.push_back(a);
        verts.push_back(d);
        verts.push_back(e);
    }

    GLuint program = 0;
    GLuint fontTex = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint  fontLocation = -1;
    int    resW = 1;
    int    resH = 1;
    std::vector<UiVertex> verts;
};

// ------------------------------------------------------------------ camera

struct Camera {
    float yaw   = 0.72f;
    float pitch = 0.235f;
    float dist  = 17.0f;
};

static void cameraBasis(const Camera& c,
                        float pos[3], float fwd[3], float right[3], float up[3])
{
    float cp = std::cos(c.pitch), sp = std::sin(c.pitch);
    pos[0] = c.dist * cp * std::sin(c.yaw);
    pos[1] = c.dist * sp;
    pos[2] = c.dist * cp * std::cos(c.yaw);

    fwd[0] = -pos[0] / c.dist;
    fwd[1] = -pos[1] / c.dist;
    fwd[2] = -pos[2] / c.dist;

    float rl = std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
    if (rl < 1e-5f) rl = 1e-5f;
    right[0] = -fwd[2] / rl;
    right[1] = 0.0f;
    right[2] =  fwd[0] / rl;

    up[0] = right[1] * fwd[2] - right[2] * fwd[1];
    up[1] = right[2] * fwd[0] - right[0] * fwd[2];
    up[2] = right[0] * fwd[1] - right[1] * fwd[0];
}

// ------------------------------------------------------------------ bmp writer

static bool writeBmp(const std::string& path, int w, int h,
                     const std::vector<unsigned char>& rgba)
{
    int row = (w * 3 + 3) & ~3;
    int dataSize = row * h;
    std::vector<unsigned char> buf(size_t(54 + dataSize), 0);

    auto w32 = [&](int off, uint32_t v) {
        buf[off + 0] = uint8_t(v);
        buf[off + 1] = uint8_t(v >> 8);
        buf[off + 2] = uint8_t(v >> 16);
        buf[off + 3] = uint8_t(v >> 24);
    };
    auto w16 = [&](int off, uint16_t v) {
        buf[off + 0] = uint8_t(v);
        buf[off + 1] = uint8_t(v >> 8);
    };

    buf[0] = 'B';
    buf[1] = 'M';
    w32(2, uint32_t(54 + dataSize));
    w32(10, 54);
    w32(14, 40);
    w32(18, uint32_t(w));
    w32(22, uint32_t(h));
    w16(26, 1);
    w16(28, 24);
    w32(34, uint32_t(dataSize));

    for (int y = 0; y < h; ++y) {
        const unsigned char* src = rgba.data() + size_t(y) * w * 4;
        unsigned char* dst = buf.data() + 54 + size_t(y) * row;
        for (int x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    std::fclose(f);
    return ok;
}

// ------------------------------------------------------------------ options

struct Options {
    int   w = 1600, h = 900;
    float scale = 0.85f;
    int   steps = 420;
    float fov = 55.0f;
    float exposure = 0.9f;
    float time = 6.0f;
    std::string shot;
    int   frames = 64;
    bool  visible = false;
    bool  hud = false;
    bool  help = false;
    int   bench = 0;
    bool  noDisk = false;
    bool  noLens = false;
    bool  noBloom = false;
    float diskBright = 1.15f;
    float diskTemp = 9500.0f;
    float bloom = 0.35f;
    float rot = 9.0f;
    Camera cam;
};

static Options parseArgs(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--shot") o.shot = next();
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--time") o.time = float(std::atof(next().c_str()));
        else if (a == "--steps") o.steps = std::atoi(next().c_str());
        else if (a == "--fov") o.fov = float(std::atof(next().c_str()));
        else if (a == "--exposure") o.exposure = float(std::atof(next().c_str()));
        else if (a == "--scale") o.scale = float(std::atof(next().c_str()));
        else if (a == "--visible") o.visible = true;
        else if (a == "--hud") o.hud = true;
        else if (a == "--bench") o.bench = std::atoi(next().c_str());
        else if (a == "--nodisk") o.noDisk = true;
        else if (a == "--nolens") o.noLens = true;
        else if (a == "--nobloom") o.noBloom = true;
        else if (a == "--bright") o.diskBright = float(std::atof(next().c_str()));
        else if (a == "--temp") o.diskTemp = float(std::atof(next().c_str()));
        else if (a == "--bloom") o.bloom = float(std::atof(next().c_str()));
        else if (a == "--rot") o.rot = float(std::atof(next().c_str()));
        else if (a == "--size") {
            std::string s = next();
            int ww = 0, hh = 0;
            if (std::sscanf(s.c_str(), "%dx%d", &ww, &hh) == 2 && ww > 0 && hh > 0) {
                o.w = ww;
                o.h = hh;
            }
        }
        else if (a == "--cam") {
            std::string s = next();
            float y = 0, p = 0, d = 0;
            if (std::sscanf(s.c_str(), "%f,%f,%f", &y, &p, &d) == 3) {
                o.cam.yaw = y;
                o.cam.pitch = p;
                o.cam.dist = d;
            }
        }
    }
    return o;
}

static void printUsage(const char* exe)
{
    std::printf(
        "Schwarzschild black hole renderer\n"
        "\n"
        "usage: %s [options]\n"
        "\n"
        "render options\n"
        "  --size WxH         window / output resolution (default 1600x900)\n"
        "  --scale N          internal resolution multiplier, 0.35-1.0 (default 0.85)\n"
        "  --steps N          geodesic integration steps, 80-1200 (default 420)\n"
        "  --fov DEG          vertical field of view (default 55)\n"
        "  --exposure N       tone-mapping exposure (default 0.9)\n"
        "  --time SEC         initial disk animation time (default 6)\n"
        "  --temp K           inner disk temperature in kelvin (default 9500)\n"
        "  --bright N         disk brightness (default 1.15)\n"
        "  --bloom N          bloom strength (default 0.35)\n"
        "  --rot N            disk angular speed (default 9)\n"
        "  --cam YAW,PITCH,D  camera orbit in radians and distance in rs (default 0.72,0.235,17)\n"
        "\n"
        "capture / diagnostics\n"
        "  --shot FILE.bmp    render a still, save it and exit\n"
        "  --frames N         samples per still (default 64)\n"
        "  --hud              include the HUD in a still\n"
        "  --visible          show the window while capturing\n"
        "  --bench N          benchmark N frames and exit\n"
        "\n"
        "scene toggles\n"
        "  --nodisk  --nolens  --nobloom\n"
        "\n"
        "examples\n"
        "  %s\n"
        "  %s --size 1920x1080 --scale 1 --steps 600 --frames 128 --shot still.bmp\n",
        exe, exe, exe);
}

// ------------------------------------------------------------------ main

int main(int argc, char** argv)
{
    Options opt = parseArgs(argc, argv);
    if (opt.help) {
        printUsage(argv[0]);
        return 0;
    }

    const bool shotMode  = !opt.shot.empty();
    const bool benchMode = opt.bench > 0;

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_VISIBLE,
                   (opt.visible || (!shotMode && !benchMode)) ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(opt.w, opt.h, "Black Hole", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval((shotMode || benchMode) ? 0 : 1);
    glfwSetScrollCallback(win, [](GLFWwindow*, double, double dy) { gScroll += dy; });

    glLoadAll(glfwGetProcAddress);

    const char* version  = reinterpret_cast<const char*>(glGetString(0x1F02));
    const char* renderer = reinterpret_cast<const char*>(glGetString(0x1F01));
    std::printf("OpenGL %s\n%s\n", version ? version : "?", renderer ? renderer : "?");

    gShaderDir = findShaderDir();
    std::printf("shaders: %s\n", gShaderDir.c_str());

    GLuint sceneProg = buildProgram(gShaderDir, "fullscreen.vert",
                                    "blackhole.frag", "blackhole.frag");
    GLuint postProg  = buildProgram(gShaderDir, "fullscreen.vert",
                                    "post.frag", "post.frag");
    if (!sceneProg || !postProg) {
        std::fprintf(stderr, "shader setup failed\n");
        glfwTerminate();
        return 1;
    }

    SceneU su = getSceneU(sceneProg);
    PostU  pu = getPostU(postProg);

    glUseProgram(postProg);
    glUniform1i(pu.scene, 0);
    glUniform1i(pu.prev, 1);
    glUniform1i(pu.bloom, 2);
    glUseProgram(0);

    Overlay overlay;
    const bool overlayReady = overlay.init(gShaderDir);
    if (!overlayReady)
        std::fprintf(stderr, "HUD disabled: overlay shaders unavailable\n");

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    RT sceneRT, accumA, accumB, bloomA, bloomB;

    Camera cam = opt.cam;
    if (std::fabs(cam.pitch) < 0.06f) cam.pitch = 0.06f;
    cam.dist = std::clamp(cam.dist, 5.0f, 80.0f);

    float diskTime = opt.time;
    int   steps = opt.steps;
    float scale = std::clamp(opt.scale, 0.25f, 1.0f);
    float exposure = opt.exposure;
    bool  lens = !opt.noLens, diskOn = !opt.noDisk, bloomOn = !opt.noBloom;
    bool  paused = shotMode || benchMode;
    bool  showHud = overlayReady && (opt.hud || (!shotMode && !benchMode));

    if (!shotMode && !benchMode) {
        std::printf(
            "\ncontrols\n"
            "  drag       orbit camera\n"
            "  wheel      zoom\n"
            "  space      pause + progressive refine\n"
            "  L          toggle lensing (A/B)\n"
            "  D          toggle disk\n"
            "  B          toggle bloom\n"
            "  H          toggle on-screen HUD\n"
            "  [ ]        exposure\n"
            "  , .        ray step budget\n"
            "  - =        render scale\n"
            "  F5         reload shaders\n"
            "  F12        save screenshot\n"
            "  R          reset view\n"
            "  esc        quit\n\n");
    }

    bool keys[GLFW_KEY_LAST + 1] = {};
    auto pressed = [&](int k) -> bool {
        bool now = glfwGetKey(win, k) == GLFW_PRESS;
        bool hit = now && !keys[k];
        keys[k] = now;
        return hit;
    };

    double lastT = glfwGetTime();
    double fpsT = lastT;
    int fpsCount = 0;
    int fpsShown = 0;
    int accumN = 0;
    bool dirty = true;
    int shotFrame = 0;
    std::vector<unsigned char> pixels;
    int fbW = opt.w, fbH = opt.h;
    bool wantShot = false;
    double benchT0 = glfwGetTime();

    auto drawHud = [&](int fw, int fh, int iw, int ih) {
        if (!showHud || !overlayReady) return;

        int ui = (fw < 1000 || fh < 540)
                     ? 1
                     : std::clamp((fh + 240) / 480, 2, 4);
        int margin = 9 * ui;
        int pad = 8 * ui;
        int accentW = 2 * ui;
        int content = margin + pad + accentW + 3 * ui;

        const std::string title = "BLACK HOLE";
        const std::string subtitle = "SCHWARZSCHILD RAYTRACER";
        char statsBuffer[160];
        if (paused) {
            std::snprintf(statsBuffer, sizeof(statsBuffer),
                          shotMode ? "RENDERING   %d SAMPLES   %d STEPS"
                                   : "PAUSED / REFINING   %d SAMPLES   %d STEPS",
                          accumN, steps);
        } else {
            std::snprintf(statsBuffer, sizeof(statsBuffer),
                          "FPS %d   %d STEPS   %d X %d",
                          fpsShown, steps, iw, ih);
        }
        const std::string stats = statsBuffer;

        const int titleScale = ui + 1;
        const float titleTrack = float(ui);
        int titleW = overlay.textWidth(title, titleScale, titleTrack);
        int subtitleW = overlay.textWidth(subtitle, ui);
        int statsW = overlay.textWidth(stats, ui);
        int panelW = std::max(titleW, std::max(subtitleW, statsW))
                   + pad * 2 + accentW + 2 * ui;

        int titleY = margin + 7 * ui;
        int lineH = 8 * ui;
        int subtitleY = titleY + 8 * titleScale + 4 * ui;
        int dividerY = subtitleY + lineH + 5 * ui;
        int statsY = dividerY + 4 * ui;
        int panelH = statsY + lineH + 7 * ui - margin;

        UiColor accent{0.35f, 0.72f, 1.00f, 0.95f};
        UiColor titleColor{0.94f, 0.97f, 1.00f, 1.00f};
        UiColor muted{0.53f, 0.62f, 0.72f, 1.00f};
        UiColor pausedColor{1.00f, 0.77f, 0.35f, 1.00f};
        UiColor rule{0.70f, 0.80f, 0.92f, 0.14f};

        const std::string controls1 =
            "DRAG ORBIT   WHEEL ZOOM   R RESET   SPACE PAUSE / REFINE";
        const std::string controls2 =
            "L LENSING   D DISK   B BLOOM   H HUD   F12 SAVE   ESC QUIT";
        int controlsW = std::max(overlay.textWidth(controls1, ui),
                                 overlay.textWidth(controls2, ui));
        int bottomW = controlsW + pad * 2 + accentW + 2 * ui;
        int bottomH = lineH * 2 + 3 * ui + 14 * ui;
        int bottomY = fh - margin - bottomH;

        overlay.begin(fw, fh);
        overlay.panel(float(margin), float(margin), float(panelW), float(panelH),
                      accent, ui);
        overlay.text(float(content), float(titleY), titleScale, title,
                     titleColor, titleTrack);
        overlay.text(float(content), float(subtitleY), ui, subtitle, muted);
        overlay.rect(float(content), float(dividerY),
                     float(panelW - (content - margin) - pad),
                     float(std::max(ui / 2, 1)), rule);
        overlay.text(float(content), float(statsY), ui, stats,
                     paused ? pausedColor : titleColor);

        overlay.panel(float(margin), float(bottomY), float(bottomW), float(bottomH),
                      accent, ui);
        overlay.text(float(content), float(bottomY + 7 * ui), ui, controls1, muted);
        overlay.text(float(content), float(bottomY + 7 * ui + lineH + 3 * ui),
                     ui, controls2, muted);
        overlay.flush();
    };

    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        double now = glfwGetTime();
        float dt = float(now - lastT);
        lastT = now;
        if (dt > 0.25f) dt = 0.25f;

        // ---- input
        if (!shotMode && !benchMode) {
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            static bool dragging = false;
            static double lx = 0, ly = 0;

            if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!dragging) {
                    dragging = true;
                    lx = mx;
                    ly = my;
                }
                float dx = float(mx - lx);
                float dy = float(my - ly);
                if (dx != 0.0f || dy != 0.0f) {
                    cam.yaw -= dx * 0.005f;
                    cam.pitch -= dy * 0.005f;
                    cam.pitch = std::clamp(cam.pitch, -1.45f, 1.45f);
                    if (std::fabs(cam.pitch) < 0.06f)
                        cam.pitch = cam.pitch < 0.0f ? -0.06f : 0.06f;
                    dirty = true;
                }
                lx = mx;
                ly = my;
            } else {
                dragging = false;
            }

            if (gScroll != 0.0) {
                cam.dist *= std::exp(float(-gScroll) * 0.12f);
                cam.dist = std::clamp(cam.dist, 5.0f, 80.0f);
                gScroll = 0.0;
                dirty = true;
            }

            if (pressed(GLFW_KEY_SPACE)) { paused = !paused; dirty = true; }
            if (pressed(GLFW_KEY_R))     { cam = Camera{}; dirty = true; }
            if (pressed(GLFW_KEY_L))     { lens = !lens; dirty = true; }
            if (pressed(GLFW_KEY_D))     { diskOn = !diskOn; dirty = true; }
            if (pressed(GLFW_KEY_B))     { bloomOn = !bloomOn; }
            if (pressed(GLFW_KEY_H))     { showHud = !showHud; }
            if (pressed(GLFW_KEY_COMMA)) { steps = std::max(80, steps - 60); std::printf("steps %d\n", steps); dirty = true; }
            if (pressed(GLFW_KEY_PERIOD)){ steps = std::min(1200, steps + 60); std::printf("steps %d\n", steps); dirty = true; }
            if (pressed(GLFW_KEY_LEFT_BRACKET))  exposure = std::max(0.1f, exposure * 0.9f);
            if (pressed(GLFW_KEY_RIGHT_BRACKET)) exposure = std::min(8.0f, exposure * 1.1f);
            if (pressed(GLFW_KEY_MINUS)) { scale = std::max(0.35f, scale - 0.05f); dirty = true; std::printf("scale %.2f\n", scale); }
            if (pressed(GLFW_KEY_EQUAL)) { scale = std::min(1.00f, scale + 0.05f); dirty = true; std::printf("scale %.2f\n", scale); }
            if (pressed(GLFW_KEY_F12))   wantShot = true;

            if (pressed(GLFW_KEY_F5)) {
                GLuint ns = buildProgram(gShaderDir, "fullscreen.vert",
                                         "blackhole.frag", "blackhole.frag");
                GLuint np = buildProgram(gShaderDir, "fullscreen.vert",
                                         "post.frag", "post.frag");
                if (ns && np) {
                    glDeleteProgram(sceneProg);
                    glDeleteProgram(postProg);
                    sceneProg = ns;
                    postProg  = np;
                    su = getSceneU(sceneProg);
                    pu = getPostU(postProg);
                    glUseProgram(postProg);
                    glUniform1i(pu.scene, 0);
                    glUniform1i(pu.prev, 1);
                    glUniform1i(pu.bloom, 2);
                    glUseProgram(0);
                    if (overlayReady && !overlay.reload(gShaderDir))
                        std::fprintf(stderr, "HUD shader reload failed\n");
                    dirty = true;
                    std::printf("shaders reloaded\n");
                } else {
                    if (ns) glDeleteProgram(ns);
                    if (np) glDeleteProgram(np);
                    std::fprintf(stderr, "shader reload failed, keeping previous\n");
                }
            }

            if (!paused) diskTime += dt;
        }

        // ---- sizes
        glfwGetFramebufferSize(win, &fbW, &fbH);
        if (fbW <= 0 || fbH <= 0) { glfwWaitEvents(); continue; }

        int rw = std::max(2, int(fbW * scale));
        int rh = std::max(2, int(fbH * scale));

        bool resized = (sceneRT.w != rw || sceneRT.h != rh);
        rtEnsure(sceneRT, rw, rh);
        rtEnsure(accumA, rw, rh);
        rtEnsure(accumB, rw, rh);
        if (resized) dirty = true;

        int bw = std::max(1, rw / 2);
        int bh = std::max(1, rh / 2);
        rtEnsure(bloomA, bw, bh);
        rtEnsure(bloomB, bw, bh);

        // ---- accumulation
        bool canAccum = shotMode || paused;
        if (!canAccum || dirty) accumN = 0;

        float jx = 0.0f, jy = 0.0f;
        if (canAccum && accumN > 0) {
            jx = halton(accumN + 1, 2) - 0.5f;
            jy = halton(accumN + 1, 3) - 0.5f;
        }
        float weight = (accumN == 0) ? 1.0f : 1.0f / float(accumN + 1);
        dirty = false;

        // ---- camera
        float pos[3], fwd[3], right[3], up[3];
        cameraBasis(cam, pos, fwd, right, up);
        float tanHalfFov = std::tan(opt.fov * 0.5f * 3.14159265f / 180.0f);

        // ---- scene
        glBindFramebuffer(GL_FRAMEBUFFER, sceneRT.fbo);
        glViewport(0, 0, rw, rh);
        glUseProgram(sceneProg);
        glUniform2f(su.res, float(rw), float(rh));
        glUniform1f(su.time, diskTime);
        glUniform3f(su.camPos, pos[0], pos[1], pos[2]);
        glUniform3f(su.camFwd, fwd[0], fwd[1], fwd[2]);
        glUniform3f(su.camRight, right[0], right[1], right[2]);
        glUniform3f(su.camUp, up[0], up[1], up[2]);
        glUniform1f(su.tanHalfFov, tanHalfFov);
        glUniform1i(su.steps, steps);
        glUniform1i(su.lens, lens ? 1 : 0);
        glUniform1i(su.disk, diskOn ? 1 : 0);
        glUniform1f(su.diskIn, 3.0f);
        glUniform1f(su.diskOut, 13.0f);
        glUniform1f(su.diskTemp, opt.diskTemp);
        glUniform1f(su.diskBright, opt.diskBright);
        glUniform1f(su.rotSpeed, opt.rot);
        glUniform2f(su.jitter, jx, jy);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- accumulate
        glBindFramebuffer(GL_FRAMEBUFFER, accumB.fbo);
        glViewport(0, 0, rw, rh);
        glUseProgram(postProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneRT.tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, accumA.tex);
        glUniform1i(pu.mode, 0);
        glUniform1f(pu.weight, weight);
        glUniform2f(pu.res, float(rw), float(rh));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::swap(accumA, accumB);
        ++accumN;

        // ---- bloom
        if (bloomOn) {
            glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo);
            glViewport(0, 0, bw, bh);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, accumA.tex);
            glUniform1i(pu.mode, 1);
            glUniform1f(pu.threshold, 0.75f);
            glUniform2f(pu.res, float(bw), float(bh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, bloomB.fbo);
            glViewport(0, 0, bw, bh);
            glBindTexture(GL_TEXTURE_2D, bloomA.tex);
            glUniform1i(pu.mode, 2);
            glUniform2f(pu.texel, 1.0f / bw, 1.0f / bh);
            glUniform2f(pu.dir, 1.0f, 0.0f);
            glUniform2f(pu.res, float(bw), float(bh));
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, bloomA.fbo);
            glViewport(0, 0, bw, bh);
            glBindTexture(GL_TEXTURE_2D, bloomB.tex);
            glUniform2f(pu.dir, 0.0f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        // ---- shot / bench handling
        if (shotMode || benchMode) {
            ++shotFrame;
            bool last = shotMode ? (shotFrame >= opt.frames)
                                 : (shotFrame >= opt.bench);
            if (!last) continue;

            if (benchMode) {
                glFinish();
                double elapsed = glfwGetTime() - benchT0;
                std::printf("bench: %d frames at %dx%d, %.2f ms/frame (%.1f fps)\n",
                            shotFrame, rw, rh, elapsed * 1000.0 / shotFrame,
                            shotFrame / elapsed);
                break;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbW, fbH);
            glUseProgram(postProg);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, accumA.tex);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, bloomOn ? bloomA.tex : accumA.tex);
            glUniform1i(pu.mode, 3);
            glUniform2f(pu.res, float(fbW), float(fbH));
            glUniform1f(pu.exposure, exposure);
            glUniform1f(pu.bloomStrength, bloomOn ? opt.bloom : 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            drawHud(fbW, fbH, rw, rh);
            glFinish();
            pixels.resize(size_t(fbW) * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            bool ok = writeBmp(opt.shot, fbW, fbH, pixels);
            std::printf("%s %s (%dx%d, %d samples)\n",
                        ok ? "wrote" : "FAILED to write",
                        opt.shot.c_str(), fbW, fbH, opt.frames);
            break;
        }

        // ---- composite to window
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glUseProgram(postProg);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accumA.tex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, bloomOn ? bloomA.tex : accumA.tex);
        glUniform1i(pu.mode, 3);
        glUniform2f(pu.res, float(fbW), float(fbH));
        glUniform1f(pu.exposure, exposure);
        glUniform1f(pu.bloomStrength, bloomOn ? opt.bloom : 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (wantShot) {
            wantShot = false;
            glFinish();
            pixels.resize(size_t(fbW) * fbH * 4);
            glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            std::time_t tt = std::time(nullptr);
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &tt);
#else
            localtime_r(&tt, &tmv);
#endif
            char name[128];
            std::snprintf(name, sizeof(name), "blackhole_%04d%02d%02d_%02d%02d%02d.bmp",
                          tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
            if (writeBmp(name, fbW, fbH, pixels))
                std::printf("saved %s (clean, no HUD)\n", name);
            else
                std::fprintf(stderr, "could not save %s\n", name);
        }

        drawHud(fbW, fbH, rw, rh);
        glfwSwapBuffers(win);

        ++fpsCount;
        if (now - fpsT > 1.0) {
            fpsShown = int(fpsCount / (now - fpsT) + 0.5);
            char title[256];
            std::snprintf(title, sizeof(title),
                          "Black Hole  %dx%d  %d fps  |  yaw %.0f pitch %.0f dist %.0f  |  steps %d scale %.2f  exp %.2f%s",
                          fbW, fbH, fpsShown,
                          cam.yaw * 57.2958f, cam.pitch * 57.2958f, cam.dist,
                          steps, scale, exposure, paused ? "  [paused]" : "");
            glfwSetWindowTitle(win, title);
            fpsT = now;
            fpsCount = 0;
        }
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
