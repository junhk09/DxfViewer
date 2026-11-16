#pragma once

#include <GL/gl.h>
#include <cstddef>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_ARRAY_BUFFER                 0x8892
#define GL_STATIC_DRAW                  0x88E4
#define GL_DYNAMIC_DRAW                 0x88E8
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_VERTEX_SHADER                0x8B31
#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_INFO_LOG_LENGTH              0x8B84
#define GL_PROGRAM_POINT_SIZE           0x8642
#define GL_FRAMEBUFFER                  0x8D40
#define GL_RENDERBUFFER                 0x8D41
#define GL_READ_FRAMEBUFFER             0x8CA8
#define GL_DRAW_FRAMEBUFFER             0x8CA9
#define GL_COLOR_ATTACHMENT0            0x8CE0
#define GL_FRAMEBUFFER_COMPLETE         0x8CD5
#define GL_RGBA8                        0x8058

#define WGL_CONTEXT_MAJOR_VERSION_ARB   0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB   0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB    0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFN_wglSwapIntervalEXT)(int);

#define GL_FUNCTIONS(X) \
    X(void,   glGenVertexArrays,        (GLsizei n, GLuint* arrays)) \
    X(void,   glBindVertexArray,        (GLuint array)) \
    X(void,   glDeleteVertexArrays,     (GLsizei n, const GLuint* arrays)) \
    X(void,   glGenBuffers,             (GLsizei n, GLuint* buffers)) \
    X(void,   glBindBuffer,             (GLenum target, GLuint buffer)) \
    X(void,   glBufferData,             (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X(void,   glDeleteBuffers,          (GLsizei n, const GLuint* buffers)) \
    X(void,   glVertexAttribPointer,    (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
    X(void,   glEnableVertexAttribArray,(GLuint index)) \
    X(GLuint, glCreateShader,           (GLenum type)) \
    X(void,   glShaderSource,           (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void,   glCompileShader,          (GLuint shader)) \
    X(void,   glGetShaderiv,            (GLuint shader, GLenum pname, GLint* params)) \
    X(void,   glGetShaderInfoLog,       (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void,   glDeleteShader,           (GLuint shader)) \
    X(GLuint, glCreateProgram,          (void)) \
    X(void,   glAttachShader,           (GLuint program, GLuint shader)) \
    X(void,   glLinkProgram,            (GLuint program)) \
    X(void,   glGetProgramiv,           (GLuint program, GLenum pname, GLint* params)) \
    X(void,   glGetProgramInfoLog,      (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void,   glUseProgram,             (GLuint program)) \
    X(void,   glDeleteProgram,          (GLuint program)) \
    X(GLint,  glGetUniformLocation,     (GLuint program, const GLchar* name)) \
    X(void,   glUniform1f,              (GLint location, GLfloat v0)) \
    X(void,   glUniformMatrix4fv,       (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void,   glGenFramebuffers,        (GLsizei n, GLuint* framebuffers)) \
    X(void,   glBindFramebuffer,        (GLenum target, GLuint framebuffer)) \
    X(void,   glDeleteFramebuffers,     (GLsizei n, const GLuint* framebuffers)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void,   glFramebufferRenderbuffer,(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)) \
    X(void,   glGenRenderbuffers,       (GLsizei n, GLuint* renderbuffers)) \
    X(void,   glBindRenderbuffer,       (GLenum target, GLuint renderbuffer)) \
    X(void,   glDeleteRenderbuffers,    (GLsizei n, const GLuint* renderbuffers)) \
    X(void,   glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void,   glBlitFramebuffer,        (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))

#define GL_DECLARE_FUNCTION(ret, name, params) \
    typedef ret(APIENTRY* PFN_##name) params; \
    extern PFN_##name name;
GL_FUNCTIONS(GL_DECLARE_FUNCTION)
#undef GL_DECLARE_FUNCTION

bool LoadGLFunctions(const char** missing);
