// OpenGL ES 2.0 function table loaded at runtime through SDL_GL_GetProcAddress.
// The binary never links against libGLESv2/libEGL: on ArkOS the Mali blob's sonames vary,
// and SDL already knows which library its video backend (KMSDRM, x11, wayland) needs.
#pragma once

#include <cstddef>

#if defined(_WIN32) && !defined(_WIN64)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef std::ptrdiff_t GLsizeiptr;
typedef std::ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_ALWAYS 0x0207
#define GL_NOTEQUAL 0x0205
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_DEPTH_BITS 0x0D56
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH24_STENCIL8_OES 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_EXTENSIONS 0x1F03

#define CR_GL_FUNCS(X) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glBindAttribLocation, (GLuint program, GLuint index, const GLchar *name)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint renderbuffer)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *renderbuffers)) \
    X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height)) \
    X(void, glBindTexture, (GLenum target, GLuint texture)) \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glClearStencil, (GLint s)) \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(GLuint, glCreateProgram, (void)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glCullFace, (GLenum mode)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void, glDeleteProgram, (GLuint program)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, glDepthFunc, (GLenum func)) \
    X(void, glDepthMask, (GLboolean flag)) \
    X(void, glPolygonOffset, (GLfloat factor, GLfloat units)) \
    X(void, glDisable, (GLenum cap)) \
    X(void, glDisableVertexAttribArray, (GLuint index)) \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices)) \
    X(void, glEnable, (GLenum cap)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glFinish, (void)) \
    X(void, glFrontFace, (GLenum mode)) \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, glGenTextures, (GLsizei n, GLuint *textures)) \
    X(GLenum, glGetError, (void)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(const GLubyte *, glGetString, (GLenum name)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glPixelStorei, (GLenum pname, GLint param)) \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)) \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
    X(void, glStencilFunc, (GLenum func, GLint ref, GLuint mask)) \
    X(void, glStencilOp, (GLenum fail, GLenum zfail, GLenum zpass)) \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, glUniform1f, (GLint location, GLfloat v0)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
    X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glUseProgram, (GLuint program)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))

#define CR_GL_DECLARE(ret, name, args) extern ret (GLAPIENTRY *name) args;
CR_GL_FUNCS(CR_GL_DECLARE)
#undef CR_GL_DECLARE

namespace gl {

// Fills every function pointer from the current SDL GL context. Returns false and names the
// first missing symbol if any function could not be resolved.
bool load(const char **missingName);

// True when the context is desktop GL (PC fallback) rather than GLES; shaders then need a
// different #version line and no precision qualifiers.
bool isDesktop();
void setDesktop(bool desktop);

// "#version 100\nprecision mediump float;\n" on GLES, "#version 120\n#define ..." on desktop GL.
const char *shaderPreamble();

} // namespace gl
