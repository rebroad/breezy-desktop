/*
 * Shader loader - loads and compiles GLSL shaders
 * Uses Sombrero.frag directly (it's already GLSL-compatible)
 */

#include "breezy_x11_renderer.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

static char *read_file_contents(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("[Shader] Failed to open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *contents = malloc(size + 1);
    if (!contents) {
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(contents, 1, size, f);
    contents[read_size] = '\0';
    fclose(f);
    
    if (out_size) {
        *out_size = read_size;
    }
    
    return contents;
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        log_error("[Shader] Failed to create shader\n");
        return 0;
    }
    
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            log_error("[Shader] Compile error: %s\n", info_log);
            free(info_log);
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    if (program == 0) {
        log_error("[Shader] Failed to create program\n");
        return 0;
    }
    
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = malloc(info_len);
            glGetProgramInfoLog(program, info_len, NULL, info_log);
            log_error("[Shader] Link error: %s\n", info_log);
            free(info_log);
        }
        glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

int load_sombrero_shaders(RenderThread *thread, const char *frag_shader_path) {
    // Simple vertex shader for fullscreen quad
    const char *vertex_shader_src = 
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 texCoord;\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    texCoord = aTexCoord;\n"
        "}\n";
    
    // Load fragment shader from file
    size_t frag_size;
    char *frag_shader_src = read_file_contents(frag_shader_path, &frag_size);
    if (!frag_shader_src) {
        log_error("[Shader] Failed to load fragment shader from %s\n", frag_shader_path);
        return -1;
    }
    
    // Compile shaders
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    if (vertex_shader == 0) {
        free(frag_shader_src);
        return -1;
    }
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, frag_shader_src);
    free(frag_shader_src);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return -1;
    }
    
    // Link program
    GLuint program = link_program(vertex_shader, fragment_shader);
    if (program == 0) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return -1;
    }
    
    // Store in thread
    thread->vertex_shader = vertex_shader;
    thread->fragment_shader = fragment_shader;
    thread->shader_program = program;
    
    log_info("[Shader] Shaders loaded and compiled successfully\n");
    return 0;
}

