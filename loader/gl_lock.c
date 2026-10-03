// AUTO-GENERATED: every GL entry the game imports, serialized through one recursive mutex.
// VitaGL has a single global context and is not thread-safe; the game issues GL from worker threads (texture
// uploads, FontToTexture, LoopRender). Serialization prevents SceGxm crashes from concurrent calls.
#include <vitaGL.h>
#include <pthread.h>

static pthread_mutex_t gl_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
void gl_lock_acquire(void) { pthread_mutex_lock(&gl_lock); }
void gl_lock_release(void) { pthread_mutex_unlock(&gl_lock); }

void glActiveTexture_locked(GLenum texture) { pthread_mutex_lock(&gl_lock); glActiveTexture(texture); pthread_mutex_unlock(&gl_lock); }
void glAttachShader_locked(GLuint prog, GLuint shad) { pthread_mutex_lock(&gl_lock); glAttachShader(prog, shad); pthread_mutex_unlock(&gl_lock); }
void glBindAttribLocation_locked(GLuint program, GLuint index, const GLchar *name) { pthread_mutex_lock(&gl_lock); glBindAttribLocation(program, index, name); pthread_mutex_unlock(&gl_lock); }
void glBindBuffer_locked(GLenum target, GLuint buffer) { pthread_mutex_lock(&gl_lock); glBindBuffer(target, buffer); pthread_mutex_unlock(&gl_lock); }
void glBindFramebuffer_locked(GLenum target, GLuint framebuffer) { pthread_mutex_lock(&gl_lock); glBindFramebuffer(target, framebuffer); pthread_mutex_unlock(&gl_lock); }
void glBindRenderbuffer_locked(GLenum target, GLuint renderbuffer) { pthread_mutex_lock(&gl_lock); glBindRenderbuffer(target, renderbuffer); pthread_mutex_unlock(&gl_lock); }
void glBindTexture_locked(GLenum target, GLuint texture) { pthread_mutex_lock(&gl_lock); glBindTexture(target, texture); pthread_mutex_unlock(&gl_lock); }
void glBlendEquation_locked(GLenum mode) { pthread_mutex_lock(&gl_lock); glBlendEquation(mode); pthread_mutex_unlock(&gl_lock); }
void glBlendEquationSeparate_locked(GLenum modeRGB, GLenum modeAlpha) { pthread_mutex_lock(&gl_lock); glBlendEquationSeparate(modeRGB, modeAlpha); pthread_mutex_unlock(&gl_lock); }
void glBlendFunc_locked(GLenum sfactor, GLenum dfactor) { pthread_mutex_lock(&gl_lock); glBlendFunc(sfactor, dfactor); pthread_mutex_unlock(&gl_lock); }
void glBlendFuncSeparate_locked(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { pthread_mutex_lock(&gl_lock); glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha); pthread_mutex_unlock(&gl_lock); }
void glBufferData_locked(GLenum target, GLsizei size, const GLvoid *data, GLenum usage) { pthread_mutex_lock(&gl_lock); glBufferData(target, size, data, usage); pthread_mutex_unlock(&gl_lock); }
void glBufferSubData_locked(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) { pthread_mutex_lock(&gl_lock); glBufferSubData(target, offset, size, data); pthread_mutex_unlock(&gl_lock); }
GLenum glCheckFramebufferStatus_locked(GLenum target) { pthread_mutex_lock(&gl_lock); GLenum r = glCheckFramebufferStatus(target); pthread_mutex_unlock(&gl_lock); return r; }
void glClear_locked(GLbitfield mask) { pthread_mutex_lock(&gl_lock); glClear(mask); pthread_mutex_unlock(&gl_lock); }
void glClearColor_locked(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { pthread_mutex_lock(&gl_lock); glClearColor(red, green, blue, alpha); pthread_mutex_unlock(&gl_lock); }
void glClearDepthf_locked(GLclampf depth) { pthread_mutex_lock(&gl_lock); glClearDepthf(depth); pthread_mutex_unlock(&gl_lock); }
void glClearStencil_locked(GLint s) { pthread_mutex_lock(&gl_lock); glClearStencil(s); pthread_mutex_unlock(&gl_lock); }
void glColorMask_locked(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { pthread_mutex_lock(&gl_lock); glColorMask(red, green, blue, alpha); pthread_mutex_unlock(&gl_lock); }
void glCompileShader_locked(GLuint shader) { pthread_mutex_lock(&gl_lock); glCompileShader_log(shader); pthread_mutex_unlock(&gl_lock); }
void glCompressedTexImage2D_locked(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) { pthread_mutex_lock(&gl_lock); glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data); pthread_mutex_unlock(&gl_lock); }
void glCopyTexImage2D_locked(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { pthread_mutex_lock(&gl_lock); glCopyTexImage2D(target, level, internalformat, x, y, width, height, border); pthread_mutex_unlock(&gl_lock); }
void glCopyTexSubImage2D_locked(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { pthread_mutex_lock(&gl_lock); glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height); pthread_mutex_unlock(&gl_lock); }
GLuint glCreateProgram_locked(void) { pthread_mutex_lock(&gl_lock); GLuint r = glCreateProgram(); pthread_mutex_unlock(&gl_lock); return r; }
GLuint glCreateShader_locked(GLenum shaderType) { pthread_mutex_lock(&gl_lock); GLuint r = glCreateShader(shaderType); pthread_mutex_unlock(&gl_lock); return r; }
void glCullFace_locked(GLenum mode) { pthread_mutex_lock(&gl_lock); glCullFace(mode); pthread_mutex_unlock(&gl_lock); }
void glDeleteBuffers_locked(GLsizei n, const GLuint *gl_buffers) { pthread_mutex_lock(&gl_lock); glDeleteBuffers(n, gl_buffers); pthread_mutex_unlock(&gl_lock); }
void glDeleteFramebuffers_locked(GLsizei n, const GLuint *framebuffers) { pthread_mutex_lock(&gl_lock); glDeleteFramebuffers(n, framebuffers); pthread_mutex_unlock(&gl_lock); }
void glDeleteProgram_locked(GLuint prog) { pthread_mutex_lock(&gl_lock); glDeleteProgram(prog); pthread_mutex_unlock(&gl_lock); }
void glDeleteRenderbuffers_locked(GLsizei n, const GLuint *renderbuffers) { pthread_mutex_lock(&gl_lock); glDeleteRenderbuffers(n, renderbuffers); pthread_mutex_unlock(&gl_lock); }
void glDeleteShader_locked(GLuint shad) { pthread_mutex_lock(&gl_lock); glDeleteShader(shad); pthread_mutex_unlock(&gl_lock); }
void glDeleteTextures_locked(GLsizei n, const GLuint *textures) { pthread_mutex_lock(&gl_lock); glDeleteTextures(n, textures); pthread_mutex_unlock(&gl_lock); }
void glDepthFunc_locked(GLenum func) { pthread_mutex_lock(&gl_lock); glDepthFunc(func); pthread_mutex_unlock(&gl_lock); }
void glDepthMask_locked(GLboolean flag) { pthread_mutex_lock(&gl_lock); glDepthMask(flag); pthread_mutex_unlock(&gl_lock); }
void glDepthRangef_locked(GLfloat nearVal, GLfloat farVal) { pthread_mutex_lock(&gl_lock); glDepthRangef(nearVal, farVal); pthread_mutex_unlock(&gl_lock); }
void glDisable_locked(GLenum cap) { pthread_mutex_lock(&gl_lock); glDisable(cap); pthread_mutex_unlock(&gl_lock); }
void glDisableVertexAttribArray_locked(GLuint index) { pthread_mutex_lock(&gl_lock); glDisableVertexAttribArray(index); pthread_mutex_unlock(&gl_lock); }
void glDrawArrays_locked(GLenum mode, GLint first, GLsizei count) { pthread_mutex_lock(&gl_lock); glDrawArrays(mode, first, count); pthread_mutex_unlock(&gl_lock); }
void glDrawElements_locked(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices) { pthread_mutex_lock(&gl_lock); glDrawElements(mode, count, type, indices); pthread_mutex_unlock(&gl_lock); }
void glEnable_locked(GLenum cap) { pthread_mutex_lock(&gl_lock); glEnable(cap); pthread_mutex_unlock(&gl_lock); }
void glEnableVertexAttribArray_locked(GLuint index) { pthread_mutex_lock(&gl_lock); glEnableVertexAttribArray(index); pthread_mutex_unlock(&gl_lock); }
void glFinish_locked(void) { pthread_mutex_lock(&gl_lock); glFinish(); pthread_mutex_unlock(&gl_lock); }
void glFlush_locked(void) { pthread_mutex_lock(&gl_lock); glFlush(); pthread_mutex_unlock(&gl_lock); }
void glFramebufferRenderbuffer_locked(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { pthread_mutex_lock(&gl_lock); glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer); pthread_mutex_unlock(&gl_lock); }
void glFramebufferTexture2D_locked(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { pthread_mutex_lock(&gl_lock); glFramebufferTexture2D(target, attachment, textarget, texture, level); pthread_mutex_unlock(&gl_lock); }
void glFrontFace_locked(GLenum mode) { pthread_mutex_lock(&gl_lock); glFrontFace(mode); pthread_mutex_unlock(&gl_lock); }
void glGenBuffers_locked(GLsizei n, GLuint *buffers) { pthread_mutex_lock(&gl_lock); glGenBuffers(n, buffers); pthread_mutex_unlock(&gl_lock); }
void glGenFramebuffers_locked(GLsizei n, GLuint *framebuffers) { pthread_mutex_lock(&gl_lock); glGenFramebuffers(n, framebuffers); pthread_mutex_unlock(&gl_lock); }
void glGenRenderbuffers_locked(GLsizei n, GLuint *renderbuffers) { pthread_mutex_lock(&gl_lock); glGenRenderbuffers(n, renderbuffers); pthread_mutex_unlock(&gl_lock); }
void glGenTextures_locked(GLsizei n, GLuint *textures) { pthread_mutex_lock(&gl_lock); glGenTextures(n, textures); pthread_mutex_unlock(&gl_lock); }
void glGenerateMipmap_locked(GLenum target) { pthread_mutex_lock(&gl_lock); glGenerateMipmap(target); pthread_mutex_unlock(&gl_lock); }
void glGetActiveAttrib_locked(GLuint prog, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { pthread_mutex_lock(&gl_lock); glGetActiveAttrib(prog, index, bufSize, length, size, type, name); pthread_mutex_unlock(&gl_lock); }
void glGetActiveUniform_locked(GLuint prog, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) { pthread_mutex_lock(&gl_lock); glGetActiveUniform(prog, index, bufSize, length, size, type, name); pthread_mutex_unlock(&gl_lock); }
void glGetAttachedShaders_locked(GLuint prog, GLsizei maxCount, GLsizei *count, GLuint *shads) { pthread_mutex_lock(&gl_lock); glGetAttachedShaders(prog, maxCount, count, shads); pthread_mutex_unlock(&gl_lock); }
GLint glGetAttribLocation_locked(GLuint prog, const GLchar *name) { pthread_mutex_lock(&gl_lock); GLint r = glGetAttribLocation(prog, name); pthread_mutex_unlock(&gl_lock); return r; }
void glGetBooleanv_locked(GLenum pname, GLboolean *params) { pthread_mutex_lock(&gl_lock); glGetBooleanv(pname, params); pthread_mutex_unlock(&gl_lock); }
void glGetBufferParameteriv_locked(GLenum target, GLenum pname, GLint *params) { pthread_mutex_lock(&gl_lock); glGetBufferParameteriv(target, pname, params); pthread_mutex_unlock(&gl_lock); }
GLenum glGetError_locked(void) { pthread_mutex_lock(&gl_lock); GLenum r = glGetError(); pthread_mutex_unlock(&gl_lock); return r; }
void glGetFloatv_locked(GLenum pname, GLfloat *data) { pthread_mutex_lock(&gl_lock); glGetFloatv(pname, data); pthread_mutex_unlock(&gl_lock); }
void glGetFramebufferAttachmentParameteriv_locked(GLenum target, GLenum attachment, GLenum pname, GLint *params) { pthread_mutex_lock(&gl_lock); glGetFramebufferAttachmentParameteriv(target, attachment, pname, params); pthread_mutex_unlock(&gl_lock); }
void glGetIntegerv_locked(GLenum pname, GLint *data) { pthread_mutex_lock(&gl_lock); glGetIntegerv(pname, data); pthread_mutex_unlock(&gl_lock); }
void glGetProgramInfoLog_locked(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { pthread_mutex_lock(&gl_lock); glGetProgramInfoLog(program, maxLength, length, infoLog); pthread_mutex_unlock(&gl_lock); }
void glGetProgramiv_locked(GLuint program, GLenum pname, GLint *params) { pthread_mutex_lock(&gl_lock); glGetProgramiv(program, pname, params); pthread_mutex_unlock(&gl_lock); }
void glGetShaderInfoLog_locked(GLuint handle, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { pthread_mutex_lock(&gl_lock); glGetShaderInfoLog(handle, maxLength, length, infoLog); pthread_mutex_unlock(&gl_lock); }
void glGetShaderSource_locked(GLuint handle, GLsizei bufSize, GLsizei *length, GLchar *source) { pthread_mutex_lock(&gl_lock); glGetShaderSource(handle, bufSize, length, source); pthread_mutex_unlock(&gl_lock); }
void glGetShaderiv_locked(GLuint handle, GLenum pname, GLint *params) { pthread_mutex_lock(&gl_lock); glGetShaderiv(handle, pname, params); pthread_mutex_unlock(&gl_lock); }
const GLubyte * glGetString_locked(GLenum name) { pthread_mutex_lock(&gl_lock); const GLubyte * r = glGetString(name); pthread_mutex_unlock(&gl_lock); return r; }
GLint glGetUniformLocation_locked(GLuint prog, const GLchar *name) { pthread_mutex_lock(&gl_lock); GLint r = glGetUniformLocation(prog, name); pthread_mutex_unlock(&gl_lock); return r; }
void glGetVertexAttribPointerv_locked(GLuint index, GLenum pname, void **pointer) { pthread_mutex_lock(&gl_lock); glGetVertexAttribPointerv(index, pname, pointer); pthread_mutex_unlock(&gl_lock); }
void glGetVertexAttribfv_locked(GLuint index, GLenum pname, GLfloat *params) { pthread_mutex_lock(&gl_lock); glGetVertexAttribfv(index, pname, params); pthread_mutex_unlock(&gl_lock); }
void glGetVertexAttribiv_locked(GLuint index, GLenum pname, GLint *params) { pthread_mutex_lock(&gl_lock); glGetVertexAttribiv(index, pname, params); pthread_mutex_unlock(&gl_lock); }
void glHint_locked(GLenum target, GLenum mode) { pthread_mutex_lock(&gl_lock); glHint(target, mode); pthread_mutex_unlock(&gl_lock); }
GLboolean glIsEnabled_locked(GLenum cap) { pthread_mutex_lock(&gl_lock); GLboolean r = glIsEnabled(cap); pthread_mutex_unlock(&gl_lock); return r; }
GLboolean glIsFramebuffer_locked(GLuint fb) { pthread_mutex_lock(&gl_lock); GLboolean r = glIsFramebuffer(fb); pthread_mutex_unlock(&gl_lock); return r; }
GLboolean glIsProgram_locked(GLuint program) { pthread_mutex_lock(&gl_lock); GLboolean r = glIsProgram(program); pthread_mutex_unlock(&gl_lock); return r; }
GLboolean glIsRenderbuffer_locked(GLuint rb) { pthread_mutex_lock(&gl_lock); GLboolean r = glIsRenderbuffer(rb); pthread_mutex_unlock(&gl_lock); return r; }
GLboolean glIsTexture_locked(GLuint texture) { pthread_mutex_lock(&gl_lock); GLboolean r = glIsTexture(texture); pthread_mutex_unlock(&gl_lock); return r; }
void glLineWidth_locked(GLfloat width) { pthread_mutex_lock(&gl_lock); glLineWidth(width); pthread_mutex_unlock(&gl_lock); }
void glLinkProgram_locked(GLuint progr) { pthread_mutex_lock(&gl_lock); glLinkProgram_log(progr); pthread_mutex_unlock(&gl_lock); }
void glPixelStorei_locked(GLenum pname, GLint param) { pthread_mutex_lock(&gl_lock); glPixelStorei(pname, param); pthread_mutex_unlock(&gl_lock); }
void glPolygonOffset_locked(GLfloat factor, GLfloat units) { pthread_mutex_lock(&gl_lock); glPolygonOffset(factor, units); pthread_mutex_unlock(&gl_lock); }
void glReadPixels_locked(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) { pthread_mutex_lock(&gl_lock); glReadPixels(x, y, width, height, format, type, data); pthread_mutex_unlock(&gl_lock); }
void glReleaseShaderCompiler_locked(void) { pthread_mutex_lock(&gl_lock); glReleaseShaderCompiler(); pthread_mutex_unlock(&gl_lock); }
void glRenderbufferStorage_locked(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { pthread_mutex_lock(&gl_lock); glRenderbufferStorage(target, internalformat, width, height); pthread_mutex_unlock(&gl_lock); }
void glScissor_locked(GLint x, GLint y, GLsizei width, GLsizei height) { pthread_mutex_lock(&gl_lock); glScissor(x, y, width, height); pthread_mutex_unlock(&gl_lock); }
void glShaderBinary_locked(GLsizei count, const GLuint *handles, GLenum binaryFormat, const void *binary, GLsizei length) { pthread_mutex_lock(&gl_lock); glShaderBinary(count, handles, binaryFormat, binary, length); pthread_mutex_unlock(&gl_lock); }
void glShaderSource_locked(GLuint handle, GLsizei count, const GLchar *const *string, const GLint *length) { pthread_mutex_lock(&gl_lock); glShaderSource_log(handle, count, string, length); pthread_mutex_unlock(&gl_lock); }
void glStencilFunc_locked(GLenum func, GLint ref, GLuint mask) { pthread_mutex_lock(&gl_lock); glStencilFunc(func, ref, mask); pthread_mutex_unlock(&gl_lock); }
void glStencilFuncSeparate_locked(GLenum face, GLenum func, GLint ref, GLuint mask) { pthread_mutex_lock(&gl_lock); glStencilFuncSeparate(face, func, ref, mask); pthread_mutex_unlock(&gl_lock); }
void glStencilMask_locked(GLuint mask) { pthread_mutex_lock(&gl_lock); glStencilMask(mask); pthread_mutex_unlock(&gl_lock); }
void glStencilMaskSeparate_locked(GLenum face, GLuint mask) { pthread_mutex_lock(&gl_lock); glStencilMaskSeparate(face, mask); pthread_mutex_unlock(&gl_lock); }
void glStencilOp_locked(GLenum sfail, GLenum dpfail, GLenum dppass) { pthread_mutex_lock(&gl_lock); glStencilOp(sfail, dpfail, dppass); pthread_mutex_unlock(&gl_lock); }
void glStencilOpSeparate_locked(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { pthread_mutex_lock(&gl_lock); glStencilOpSeparate(face, sfail, dpfail, dppass); pthread_mutex_unlock(&gl_lock); }
void glTexImage2D_locked(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *data) { pthread_mutex_lock(&gl_lock); glTexImage2D(target, level, internalFormat, width, height, border, format, type, data); pthread_mutex_unlock(&gl_lock); }
void glTexParameterf_locked(GLenum target, GLenum pname, GLfloat param) { pthread_mutex_lock(&gl_lock); glTexParameterf(target, pname, param); pthread_mutex_unlock(&gl_lock); }
void glTexParameteri_locked(GLenum target, GLenum pname, GLint param) { pthread_mutex_lock(&gl_lock); glTexParameteri(target, pname, param); pthread_mutex_unlock(&gl_lock); }
void glTexParameteriv_locked(GLenum target, GLenum pname, GLint *param) { pthread_mutex_lock(&gl_lock); glTexParameteriv(target, pname, param); pthread_mutex_unlock(&gl_lock); }
void glTexSubImage2D_locked(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { pthread_mutex_lock(&gl_lock); glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels); pthread_mutex_unlock(&gl_lock); }
void glUniform1f_locked(GLint location, GLfloat v0) { pthread_mutex_lock(&gl_lock); glUniform1f(location, v0); pthread_mutex_unlock(&gl_lock); }
void glUniform1fv_locked(GLint location, GLsizei count, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniform1fv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform1i_locked(GLint location, GLint v0) { pthread_mutex_lock(&gl_lock); glUniform1i(location, v0); pthread_mutex_unlock(&gl_lock); }
void glUniform1iv_locked(GLint location, GLsizei count, const GLint *value) { pthread_mutex_lock(&gl_lock); glUniform1iv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform2f_locked(GLint location, GLfloat v0, GLfloat v1) { pthread_mutex_lock(&gl_lock); glUniform2f(location, v0, v1); pthread_mutex_unlock(&gl_lock); }
void glUniform2fv_locked(GLint location, GLsizei count, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniform2fv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform2i_locked(GLint location, GLint v0, GLint v1) { pthread_mutex_lock(&gl_lock); glUniform2i(location, v0, v1); pthread_mutex_unlock(&gl_lock); }
void glUniform2iv_locked(GLint location, GLsizei count, const GLint *value) { pthread_mutex_lock(&gl_lock); glUniform2iv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform3f_locked(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { pthread_mutex_lock(&gl_lock); glUniform3f(location, v0, v1, v2); pthread_mutex_unlock(&gl_lock); }
void glUniform3fv_locked(GLint location, GLsizei count, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniform3fv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform3i_locked(GLint location, GLint v0, GLint v1, GLint v2) { pthread_mutex_lock(&gl_lock); glUniform3i(location, v0, v1, v2); pthread_mutex_unlock(&gl_lock); }
void glUniform3iv_locked(GLint location, GLsizei count, const GLint *value) { pthread_mutex_lock(&gl_lock); glUniform3iv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform4f_locked(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { pthread_mutex_lock(&gl_lock); glUniform4f(location, v0, v1, v2, v3); pthread_mutex_unlock(&gl_lock); }
void glUniform4fv_locked(GLint location, GLsizei count, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniform4fv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniform4i_locked(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { pthread_mutex_lock(&gl_lock); glUniform4i(location, v0, v1, v2, v3); pthread_mutex_unlock(&gl_lock); }
void glUniform4iv_locked(GLint location, GLsizei count, const GLint *value) { pthread_mutex_lock(&gl_lock); glUniform4iv(location, count, value); pthread_mutex_unlock(&gl_lock); }
void glUniformMatrix2fv_locked(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniformMatrix2fv(location, count, transpose, value); pthread_mutex_unlock(&gl_lock); }
void glUniformMatrix3fv_locked(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniformMatrix3fv(location, count, transpose, value); pthread_mutex_unlock(&gl_lock); }
void glUniformMatrix4fv_locked(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { pthread_mutex_lock(&gl_lock); glUniformMatrix4fv(location, count, transpose, value); pthread_mutex_unlock(&gl_lock); }
void glUseProgram_locked(GLuint program) { pthread_mutex_lock(&gl_lock); glUseProgram(program); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib1f_locked(GLuint index, GLfloat v0) { pthread_mutex_lock(&gl_lock); glVertexAttrib1f(index, v0); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib1fv_locked(GLuint index, const GLfloat *v) { pthread_mutex_lock(&gl_lock); glVertexAttrib1fv(index, v); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib2f_locked(GLuint index, GLfloat v0, GLfloat v1) { pthread_mutex_lock(&gl_lock); glVertexAttrib2f(index, v0, v1); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib2fv_locked(GLuint index, const GLfloat *v) { pthread_mutex_lock(&gl_lock); glVertexAttrib2fv(index, v); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib3f_locked(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2) { pthread_mutex_lock(&gl_lock); glVertexAttrib3f(index, v0, v1, v2); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib3fv_locked(GLuint index, const GLfloat *v) { pthread_mutex_lock(&gl_lock); glVertexAttrib3fv(index, v); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib4f_locked(GLuint index, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { pthread_mutex_lock(&gl_lock); glVertexAttrib4f(index, v0, v1, v2, v3); pthread_mutex_unlock(&gl_lock); }
void glVertexAttrib4fv_locked(GLuint index, const GLfloat *v) { pthread_mutex_lock(&gl_lock); glVertexAttrib4fv(index, v); pthread_mutex_unlock(&gl_lock); }
void glVertexAttribPointer_locked(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) { pthread_mutex_lock(&gl_lock); glVertexAttribPointer(index, size, type, normalized, stride, pointer); pthread_mutex_unlock(&gl_lock); }
void glViewport_locked(GLint x, GLint y, GLsizei width, GLsizei height) { pthread_mutex_lock(&gl_lock); glViewport(x, y, width, height); pthread_mutex_unlock(&gl_lock); }
