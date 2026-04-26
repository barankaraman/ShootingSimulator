#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <iostream>

// GLSL shader derleyip link eden yardımcı sınıf
class Shader {
public:
    GLuint id = 0;

    Shader(const char* vertSrc, const char* fragSrc) {
        GLuint v = compile(GL_VERTEX_SHADER,   vertSrc);
        GLuint f = compile(GL_FRAGMENT_SHADER, fragSrc);
        id = glCreateProgram();
        glAttachShader(id, v); glAttachShader(id, f);
        glLinkProgram(id);
        GLint ok; glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetProgramInfoLog(id, 512, nullptr, log);
            std::cerr << "[Shader link] " << log << "\n";
        }
        glDeleteShader(v); glDeleteShader(f);
    }

    void use() const { glUseProgram(id); }

    void setMat4 (const char* n, const glm::mat4& v) const { glUniformMatrix4fv(glGetUniformLocation(id,n),1,GL_FALSE,glm::value_ptr(v)); }
    void setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(id,n),1,glm::value_ptr(v)); }
    void setVec2 (const char* n, const glm::vec2& v) const { glUniform2fv(glGetUniformLocation(id,n),1,glm::value_ptr(v)); }
    void setInt  (const char* n, int  v)             const { glUniform1i (glGetUniformLocation(id,n),v); }
    void setFloat(const char* n, float v)             const { glUniform1f (glGetUniformLocation(id,n),v); }
    void setBool (const char* n, bool v)             const { glUniform1i (glGetUniformLocation(id,n),(int)v); }
    void setVec4 (const char* n, const glm::vec4& v) const { glUniform4fv(glGetUniformLocation(id,n),1,glm::value_ptr(v)); }

private:
    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << "[Shader compile] " << log << "\n";
        }
        return s;
    }
};
