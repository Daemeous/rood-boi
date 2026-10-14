/* OpenGL 3.3 core entry points used by gl_present.c and gpu_hw.c. They are
 * resolved at run time through SDL_GL_GetProcAddress (SDL loads libGL or
 * libEGL), so each is a function pointer named gl_<Name>. */
#ifndef ROOD_GL_API_H
#define ROOD_GL_API_H
#include <GL/gl.h>
#include <GL/glext.h>

#define GL_FUNCTIONS(X)                                                                     \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                     \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                               \
    X(void, Clear, (GLbitfield))                                                            \
    X(void, Enable, (GLenum))                                                               \
    X(void, Disable, (GLenum))                                                              \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                      \
    X(void, BlendFunc, (GLenum, GLenum))                                                    \
    X(void, BlendEquation, (GLenum))                                                        \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                               \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                        \
    X(void, GenTextures, (GLsizei, GLuint*))                                                \
    X(void, BindTexture, (GLenum, GLuint))                                                  \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                         \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,      \
                           const void*))                                                    \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,   \
                              const void*))                                                 \
    X(void, PixelStorei, (GLenum, GLint))                                                   \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,     \
                           GLenum, const void*))                                            \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei,   \
                              GLenum, GLenum, const void*))                                 \
    X(GLuint, CreateShader, (GLenum))                                                       \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))            \
    X(void, CompileShader, (GLuint))                                                        \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                          \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                         \
    X(GLuint, CreateProgram, (void))                                                        \
    X(void, AttachShader, (GLuint, GLuint))                                                 \
    X(void, LinkProgram, (GLuint))                                                          \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                         \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                        \
    X(void, UseProgram, (GLuint))                                                           \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                   \
    X(void, Uniform1i, (GLint, GLint))                                                      \
    X(void, Uniform1ui, (GLint, GLuint))                                                      \
    X(void, Uniform1f, (GLint, GLfloat))                                                    \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                           \
    X(void, Uniform2i, (GLint, GLint, GLint))                                               \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                  \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat*))                                   \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                           \
    X(void, Uniform4i, (GLint, GLint, GLint, GLint, GLint))                                 \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                            \
    X(void, BindVertexArray, (GLuint))                                                      \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                 \
    X(void, BindBuffer, (GLenum, GLuint))                                                   \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                          \
    X(void, GetBufferSubData, (GLenum, GLsizeiptr, GLsizeiptr, void*))                      \
    X(void, BufferSubData, (GLenum, GLsizeiptr, GLsizeiptr, const void*))                   \
    X(void, EnableVertexAttribArray, (GLuint))                                              \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))  \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*))            \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                           \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                           \
    X(void, ActiveTexture, (GLenum))                                                        \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                            \
    X(void, BindFramebuffer, (GLenum, GLuint))                                              \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                  \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                             \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,       \
                                GLbitfield, GLenum))                                        \
    X(void, ReadBuffer, (GLenum))                                                           \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                           \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                             \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                        \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                      \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                  \
    X(void, Uniform1iv, (GLint, GLsizei, const GLint*))                                     \
    X(void, CullFace, (GLenum))                                                           \
    X(void, DrawBuffers, (GLsizei, const GLenum*))                                          \
    X(void, ClearBufferfv, (GLenum, GLint, const GLfloat*))                                 \
    X(void, ColorMaski, (GLuint, GLboolean, GLboolean, GLboolean, GLboolean))               \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))                                   \
    X(void, Uniform4iv, (GLint, GLsizei, const GLint*))                                     \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat*))                  \
    X(void, DepthFunc, (GLenum))                                                            \
    X(void, DepthMask, (GLboolean))                                                         \
    X(void, ClearDepth, (GLdouble))                                                         \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                       \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                   \
    X(GLenum, GetError, (void))                                                             \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                        \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                   \
    X(void, DrawBuffer, (GLenum))                                                           \
    X(void, Finish, (void))                                                                 \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                                                             \
    X(void, GenQueries, (GLsizei, GLuint*))                                                 \
    X(void, BeginQuery, (GLenum, GLuint))                                                   \
    X(void, EndQuery, (GLenum))                                                             \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*))                                     \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, GLuint64*))                               \
    X(void, QueryCounter, (GLuint, GLenum))                                                 \
    X(GLsync, FenceSync, (GLenum, GLbitfield))                                              \
    X(GLenum, ClientWaitSync, (GLsync, GLbitfield, GLuint64))                               \
    X(void, DeleteSync, (GLsync))                                                           \
    X(void, GetIntegerv, (GLenum, GLint*))                                                  \
    X(void, TexImage2DMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean))  \
    X(const GLubyte*, GetString, (GLenum))

/* OpenGL 4.3+ (compute shaders, storage buffers, images), for the path
 * tracer; loaded when the context offers them (pc_gl_compute). */
#define GL_COMPUTE_FUNCTIONS(X)                                                             \
    X(void, DispatchCompute, (GLuint, GLuint, GLuint))                                      \
    X(void, MemoryBarrier, (GLbitfield))                                                    \
    X(void, BindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum))    \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint))                                       \
    X(void, TexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                      \
    X(void, Uniform1uiv, (GLint, GLsizei, const GLuint*))                                   \
    X(void, CopyImageSubData, (GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, \
                                  GLint, GLint, GLint, GLsizei, GLsizei, GLsizei))

#define DECLARE(ret, name, args) extern ret(*gl_##name) args;
GL_FUNCTIONS(DECLARE)
GL_COMPUTE_FUNCTIONS(DECLARE)
#undef DECLARE
extern int pc_gl_compute; /* the context has the GL 4.3 compute entry points */
GLuint pc_gl_compute_program(const char* source);

GLuint pc_gl_program(const char* vertex, const char* fragment);
GLuint pc_gl_program3(const char* vertex, const char* geometry, const char* fragment);
#endif
