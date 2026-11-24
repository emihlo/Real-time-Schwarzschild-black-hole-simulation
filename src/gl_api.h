#pragma once

#include <cstddef>

#ifdef _WIN32
#  define GLAPI __stdcall
#else
#  define GLAPI
#endif

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float         GLfloat;
typedef char          GLchar;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_TRIANGLES            0x0004
#define GL_UNSIGNED_BYTE        0x1401
#define GL_FLOAT                0x1406
#define GL_RGBA                 0x1908
#define GL_RGBA16F              0x881A
#define GL_RGBA8                0x8058
#define GL_TEXTURE_2D           0x0DE1
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_LINEAR               0x2601
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_FRAMEBUFFER          0x8D40
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERTEX_SHADER        0x8B31
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_TEXTURE0             0x84C0
#define GL_TEXTURE1             0x84C1
#define GL_TEXTURE2             0x84C2
#define GL_NEAREST              0x2600
#define GL_ARRAY_BUFFER         0x8892
#define GL_STREAM_DRAW          0x88E0
#define GL_BLEND                0x0BE2
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_FALSE                0

#define GL_FUNCTIONS(X)                                                            \
    X(void,      GenVertexArrays,        (GLsizei, GLuint*))                       \
    X(void,      BindVertexArray,        (GLuint))                                 \
    X(GLuint,    CreateShader,           (GLenum))                                 \
    X(void,      ShaderSource,           (GLuint, GLsizei, const GLchar* const*,   \
                                          const GLint*))                           \
    X(void,      CompileShader,          (GLuint))                                 \
    X(void,      GetShaderiv,            (GLuint, GLenum, GLint*))                 \
    X(void,      GetShaderInfoLog,       (GLuint, GLsizei, GLsizei*, GLchar*))     \
    X(void,      DeleteShader,           (GLuint))                                 \
    X(GLuint,    CreateProgram,          ())                                       \
    X(void,      AttachShader,           (GLuint, GLuint))                         \
    X(void,      LinkProgram,            (GLuint))                                 \
    X(void,      GetProgramiv,           (GLuint, GLenum, GLint*))                 \
    X(void,      GetProgramInfoLog,      (GLuint, GLsizei, GLsizei*, GLchar*))     \
    X(void,      DeleteProgram,          (GLuint))                                 \
    X(void,      UseProgram,             (GLuint))                                 \
    X(GLint,     GetUniformLocation,     (GLuint, const GLchar*))                  \
    X(void,      Uniform1i,              (GLint, GLint))                           \
    X(void,      Uniform1f,              (GLint, GLfloat))                         \
    X(void,      Uniform2f,              (GLint, GLfloat, GLfloat))                \
    X(void,      Uniform3f,              (GLint, GLfloat, GLfloat, GLfloat))       \
    X(void,      GenTextures,            (GLsizei, GLuint*))                       \
    X(void,      BindTexture,            (GLenum, GLuint))                         \
    X(void,      TexImage2D,             (GLenum, GLint, GLint, GLsizei, GLsizei,  \
                                          GLint, GLenum, GLenum, const void*))     \
    X(void,      TexParameteri,          (GLenum, GLenum, GLint))                  \
    X(void,      DeleteTextures,         (GLsizei, const GLuint*))                 \
    X(void,      ActiveTexture,          (GLenum))                                 \
    X(void,      GenFramebuffers,        (GLsizei, GLuint*))                       \
    X(void,      BindFramebuffer,        (GLenum, GLuint))                         \
    X(void,      FramebufferTexture2D,   (GLenum, GLenum, GLenum, GLuint, GLint))  \
    X(GLenum,    CheckFramebufferStatus, (GLenum))                                 \
    X(void,      DeleteFramebuffers,     (GLsizei, const GLuint*))                 \
    X(void,      GenBuffers,             (GLsizei, GLuint*))                       \
    X(void,      DeleteBuffers,          (GLsizei, const GLuint*))                 \
    X(void,      BindBuffer,             (GLenum, GLuint))                         \
    X(void,      BufferData,             (GLenum, GLsizeiptr, const void*, GLenum))\
    X(void,      EnableVertexAttribArray,(GLuint))                                 \
    X(void,      VertexAttribPointer,    (GLuint, GLint, GLenum, GLboolean,        \
                                          GLsizei, const void*))                   \
    X(void,      Enable,                 (GLenum))                                 \
    X(void,      Disable,                (GLenum))                                 \
    X(void,      BlendFunc,              (GLenum, GLenum))                         \
    X(void,      DrawArrays,             (GLenum, GLint, GLsizei))                 \
    X(void,      Viewport,               (GLint, GLint, GLsizei, GLsizei))         \
    X(void,      Finish,                 ())                                       \
    X(void,      ReadPixels,             (GLint, GLint, GLsizei, GLsizei, GLenum,  \
                                          GLenum, void*))                          \
    X(const GLubyte*, GetString,         (GLenum))                                 \
    X(GLenum,    GetError,               ())

#define X(ret, name, sig)                                     \
    typedef ret (GLAPI *PFN_##name) sig;                      \
    extern PFN_##name gl##name;
GL_FUNCTIONS(X)
#undef X

typedef void (*GLProc)(void);
void glLoadAll(GLProc (*getProcAddress)(const char*));
