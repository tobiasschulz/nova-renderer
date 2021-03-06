/*!
 * \author David
 * \date 17-May-16.
 */

#include <cstdlib>
#include <algorithm>

#include <easylogging++.h>
#include <malloc.h>
#include "gl_shader_program.h"

namespace nova {
    gl_shader_program::gl_shader_program(const shader_definition &source) : name(source.name), filter(figure_out_filters(source.filters)) {
        create_shader(source.vertex_source, GL_VERTEX_SHADER);
        create_shader(source.fragment_source, GL_FRAGMENT_SHADER);

        link();
    }

    gl_shader_program::gl_shader_program(gl_shader_program &&other) :
            name(std::move(other.name)), filter(std::move(other.filter)) {

        this->gl_name = other.gl_name;

        // Make the other shader not a thing
        other.gl_name = 0;
        other.added_shaders.clear();
    }

    void gl_shader_program::link() {
        gl_name = glCreateProgram();
        glObjectLabel(GL_PROGRAM, gl_name, (GLsizei) name.length(), name.c_str());
        LOG(TRACE) << "Created shader program " << gl_name;

        for(GLuint shader : added_shaders) {
            glAttachShader(gl_name, shader);
        }

        glLinkProgram(gl_name);
        check_for_linking_errors();

        LOG(DEBUG) << "Program " << name << " linked successfully";

        for(GLuint shader : added_shaders) {
            // Clean up our resources. I'm told that this is a good thing.
            glDetachShader(gl_name, shader);
            glDeleteShader(shader);
        }
    }

    void gl_shader_program::check_for_shader_errors(GLuint shader_to_check, const std::vector<shader_line> line_map) {
        GLint success = 0;

        glGetShaderiv(shader_to_check, GL_COMPILE_STATUS, &success);

        if(success == GL_FALSE) {
            GLint log_size = 0;
            glGetShaderiv(shader_to_check, GL_INFO_LOG_LENGTH, &log_size);

            std::vector<GLchar> error_log((unsigned long long int) log_size);
            glGetShaderInfoLog(shader_to_check, log_size, &log_size, &error_log[0]);

            if(log_size > 0) {
                glDeleteShader(shader_to_check);
                throw compilation_error(error_log.data(), line_map);
            }
        }
    }

    void gl_shader_program::check_for_linking_errors() {
        GLint is_linked = 0;
        glGetProgramiv(gl_name, GL_LINK_STATUS, &is_linked);

        if(is_linked == GL_FALSE) {
            GLint log_length = 0;
            glGetProgramiv(gl_name, GL_INFO_LOG_LENGTH, &log_length);

            GLchar *info_log = (GLchar *) malloc(log_length * sizeof(GLchar));
            glGetProgramInfoLog(gl_name, log_length, &log_length, info_log);

            if(log_length > 0) {
                glDeleteProgram(gl_name);

                LOG(ERROR) << "Error linking program " << gl_name << ":\n" << info_log;

                throw program_linking_failure(name);
            }

        }
    }

    void gl_shader_program::bind() noexcept {
        //LOG(INFO) << "Binding program " << name;
        glUseProgram(gl_name);
    }

    gl_shader_program::~gl_shader_program() {
        // Commented because move semantics are hard
        //LOG(DEBUG) << "Deleting program " << gl_name;
        //glDeleteProgram(gl_name);
    }

    void gl_shader_program::create_shader(const std::vector<shader_line> shader_source, const GLenum shader_type) {
        // Check what kind of shader we're dealing with

        std::string full_shader_source;
        auto version_line = shader_source[0].line;

        if(version_line == "#version 450") {
            // GLSL 450 code! This is the simplest: just concatenate all the lines in the shader file
            std::for_each(
                    std::begin(shader_source), std::end(shader_source),
                    [&](auto &line) { full_shader_source.append(line.line + "\n"); }
            );

        } else {
            throw wrong_shader_version(shader_source[0].line);
        }

        auto shader_name = glCreateShader(shader_type);

        const char *shader_source_char = full_shader_source.c_str();

        glShaderSource(shader_name, 1, &shader_source_char, NULL);

        glCompileShader(shader_name);

        check_for_shader_errors(shader_name, shader_source);

        added_shaders.push_back(shader_name);
    }

    gl_shader_program::gl_shader_program(gl_shader_program &other) {
        this->name = other.name;
        this->added_shaders = other.added_shaders;
        this->gl_name = other.gl_name;
        this->filter = other.filter;
    }

    gl_shader_program::gl_shader_program(const gl_shader_program &other) {
        this->name = other.name;
        this->added_shaders = other.added_shaders;
        this->gl_name = other.gl_name;
        this->filter = other.filter;
    }

    void gl_shader_program::set_filter(geometry_filter filter) noexcept {
        this->filter = filter;
    }

    geometry_filter& gl_shader_program::get_filter() noexcept {
        return filter;
    }

    std::string &gl_shader_program::get_name() noexcept {
        return name;
    }

    GLuint gl_shader_program::get_gl_name() const noexcept {
        return gl_name;
    }

    geometry_filter figure_out_filters(std::vector<std::string> filter_names) {
        geometry_filter filter = {};

        for(auto& filter_name : filter_names) {
            if(filter_name.find("geometry_type::") == 0) {
                auto type_name_str = filter_name.substr(15);
                auto type_name = geometry_type::from_string(type_name_str);
                filter.geometry_types.push_back(type_name);

            } else if(filter_name.find("name::") == 0) {
                auto name = filter_name.substr(6);
                filter.names.push_back(name);

            } else if(filter_name.find("name_part::") == 0) {
                auto name_part = filter_name.substr(11);
                filter.name_parts.push_back(name_part);

            } else {
                auto modify_function = geometry_filter::modifying_functions[filter_name];
                modify_function(filter);
            }
        }

        return filter;
    }

    wrong_shader_version::wrong_shader_version(const std::string &version_line) :
            std::runtime_error(
                    "Invalid version line: " + version_line +
                    ". Please only use GLSL version 450 (NOT compatibility profile)"
            ) {}

    compilation_error::compilation_error(const std::string &error_message,
                                         const std::vector<shader_line> source_lines) :
            std::runtime_error(error_message + get_original_line_message(error_message, source_lines)) {}

    std::string compilation_error::get_original_line_message(const std::string &error_message,
                                                             const std::vector<shader_line> source_lines) {
        return "This logic isn't implemented yet";
    }

}
