/*!
 * \brief
 *
 * \author ddubois 
 * \date 20-Sep-16.
 */

#include "shader_facade.h"
#include <algorithm>

namespace nova {
    namespace model {

        shader_tree_node::shader_tree_node(std::string name, std::function<bool(const render_object&)> filter) : shader_name(name), filter(filter) {}

        shader_tree_node::shader_tree_node(std::string name, std::function<bool(const render_object&)> filter, std::vector<shader_tree_node> children) :
                shader_name(name), filter(filter), children(children) { }

        void shader_tree_node::calculate_filters(
                std::vector<std::string> loaded_shaders
        ) {
            filters.push_back(filter);   // The filters for this shader always include the original filter for this shader

            for(auto& child : children) {
                child.calculate_filters(loaded_shaders);

                if(std::find(loaded_shaders.begin(), loaded_shaders.end(), child.shader_name) == loaded_shaders.end()) {
                    // Only add the child's filters to our own if the child's shader isn't loaded
                    filters.push_back(child.get_filter_function());
                }
            }

            filter = [&](const render_object& geom) {
                for(auto& filter : filters) {
                    if(filter(geom)) {
                        return true;
                    }
                }

                return false;
            };
        }

        std::function<bool(const render_object&)> shader_tree_node::get_filter_function() {
            return filter;
        }

        void shader_tree_node::foreach_df(std::function<void(shader_tree_node&)> f) {
            f(*this);
            for(auto& child : children) {
                child.foreach_df(f);
            }
        }

        shader_tree_node shader_facade::gbuffers_shaders = shader_tree_node(
                "gbuffers_basic",
                [](const auto& geom) {return geom.type == geometry_type::selection_box;},
                {
                shader_tree_node(
                        "gbuffers_skybasic",
                        [](const auto& geom) {return geom.name == "sky" || geom.name == "horizon" || geom.name == "stars" || geom.name == "void";}
                ),
                shader_tree_node(
                        "gbuffers_textured",
                        [](const auto& geom) {return geom.type == geometry_type::particle;},
                        {
                        shader_tree_node(
                                "gbuffers_spidereyes",
                                [](const auto& geom) {return geom.type == geometry_type::eyes;}
                        ),
                        shader_tree_node(
                                "gbuffers_armor_glint",
                                [](const auto& geom) {return geom.type == geometry_type::glint;}
                        ),
                        shader_tree_node(
                                "gbuffers_clouds",
                                [](const auto& geom) {return geom.type == geometry_type::cloud;}
                        ),
                        shader_tree_node(
                                "gbuffers_skytextured",
                                [](const auto& geom) {return geom.name == "sun" || geom.name == "moon";}
                        ),
                        shader_tree_node(
                                "gbuffers_textured_lit",
                                [](const auto& geom) {return geom.type == geometry_type::lit_particle || geom.name == "world_border";},
                                {
                                shader_tree_node(
                                        "gbuffers_entities",
                                        [](const auto& geom) {return geom.type == geometry_type::entity;}
                                    ),
                                shader_tree_node(
                                        "gbuffers_hand",
                                        [](const auto& geom) {return geom.type == geometry_type::hand;}
                                ),
                                shader_tree_node(
                                        "gbuffers_weather",
                                        [](const auto& geom) {return geom.type == geometry_type::weather;}
                                ),
                                shader_tree_node(
                                        "gbuffers_terrain",
                                        [](const auto& geom) {return geom.type == geometry_type::block && !geom.is_transparent;},
                                        {
                                        shader_tree_node(
                                                "gbuffers_damagedblock",
                                                [](const auto& geom) {return geom.type == geometry_type::block && geom.damage_level > 0;}
                                        ),
                                        shader_tree_node(
                                                "gbuffers_water",
                                                [](const auto& geom) {return geom.type == geometry_type::block && geom.is_transparent;}
                                        ),
                                        shader_tree_node(
                                                "gbuffers_block",
                                                [](const auto& geom) {return geom.type == geometry_type::falling_block;}
                                        )
                                })
                        })
                })
        });

        void shader_facade::build_filters() {
            // Which shaders are actually loaded?
            std::vector<std::string> loaded_shader_names;
            loaded_shader_names.reserve(loaded_shaders.size());
            for(auto kv : loaded_shaders) {
                loaded_shader_names.push_back(kv.first);
            }

            // Compute which filters apply to which shaders
            gbuffers_shaders.calculate_filters(loaded_shader_names);

            // Save the filter with the shader that uses it
            gbuffers_shaders.foreach_df(
                    [&](auto &node) {
                        if(loaded_shaders.find(node.shader_name) != loaded_shaders.end()) {
                            // If we've loaded this shader, let's set its filtering function as the filtering function for the shader
                            loaded_shaders[node.shader_name].set_filter(node.get_filter_function());
                        }
                    }
            );
        }

        gl_shader_program& shader_facade::operator[](std::string key) {
            return loaded_shaders[key];
        }

        void shader_facade::operator=(std::unordered_map<std::string, gl_shader_program>&& shaders) {
            loaded_shaders = shaders;
        }

        std::unordered_map<std::string, gl_shader_program> &shader_facade::get_loaded_shaders() {
            return loaded_shaders;
        }
    }
}