#include "scene.hpp"
#include "sejp.hpp"
#include "data_path.hpp"

#include <fstream>
#include <iostream>
#include <unordered_map>

Scene::Scene(std::string filename, std::optional<std::string> camera, uint8_t animation_setting_)
:animation_setting(animation_setting_)
{
    load(data_path(filename), camera);
}

void Scene::load(std::string filename, std::optional<std::string> requested_camera)
{
    if (filename.substr(filename.size()-4, 4) != ".s72") {
        throw std::runtime_error("Scene " + filename + " is not a compatible format (s72 required). Last 4 char is " + filename.substr(filename.size()-5, 4));
    }
    scene_path = filename.substr(0, filename.rfind('/'));;
    sejp::value val = sejp::load(filename);
    try {
        std::vector<sejp::value > const &object = val.as_array().value();
        if (object[0].as_string() != "s72-v2") {

            throw std::runtime_error("cannot find the correct header");
        }
        std::unordered_map<std::string, uint32_t> nodes_map;
        std::unordered_map<std::string, uint32_t> meshes_map;
        std::unordered_map<std::string, uint32_t> materials_map;
        std::unordered_map<std::string, uint32_t> textures_map;
        std::unordered_map<std::string, uint32_t> cameras_map;

        for (int32_t i = 1; i < int32_t(object.size()); ++i) {
            auto object_i = object[i].as_object().value();
            std::optional<std::string> type = object_i.find("type")->second.as_string();
            if (!type) {
                throw std::runtime_error("expected a type value in objects in .s72 format");
            }
            if (type.value() == "SCENE") {
                if (auto res = object_i.find("roots"); res != object_i.end()) {
                    auto roots_opt = res->second.as_array();
                    if (roots_opt.has_value()) {
                        std::vector<sejp::value> roots = roots_opt.value();
                        root_nodes.reserve(roots.size());
                        // find node index through the map, insert index to node, if node doesn't exist in the map, create a placeholder entry
                        for (int32_t j = 0; j < int32_t(roots.size()); ++j) {
                            std::string child_name = roots[j].as_string().value();
                            if (auto node_found = nodes_map.find(child_name); node_found != nodes_map.end()) {
                                root_nodes.push_back(node_found->second);
                            }
                            else {
                                Node new_node = {.name = child_name};
                                int32_t index = int32_t(nodes.size());
                                nodes.push_back(new_node);
                                nodes_map.insert({child_name, index});
                                root_nodes.push_back(index);
                            }
                        }
                    }
                }
            } else if (type.value() == "NODE") {
                std::string node_name = object_i.find("name")->second.as_string().value();
                int32_t cur_node_index;
                // look at the map and see if the node has been made already
                if (auto node_found = nodes_map.find(node_name); node_found != nodes_map.end()) {
                    cur_node_index = node_found->second;
                }
                else {
                    Node new_node = {.name = node_name};
                    cur_node_index = int32_t(nodes.size());
                    nodes.push_back(new_node);
                    nodes_map.insert({node_name, cur_node_index});
                }
                // set position
                if (auto translation = object_i.find("translation"); translation != object_i.end()) {
                    std::vector<sejp::value> res = translation->second.as_array().value();
                    assert(res.size() == 3);
                    nodes[cur_node_index].transform.position.x = float(res[0].as_number().value());
                    nodes[cur_node_index].transform.position.y = float(res[1].as_number().value());
                    nodes[cur_node_index].transform.position.z = float(res[2].as_number().value());
                }
                // set rotation
                if (auto rotation = object_i.find("rotation"); rotation != object_i.end()) {
                    std::vector<sejp::value> res = rotation->second.as_array().value();
                    assert(res.size() == 4);
                    nodes[cur_node_index].transform.rotation.x = float(res[0].as_number().value());
                    nodes[cur_node_index].transform.rotation.y = float(res[1].as_number().value());
                    nodes[cur_node_index].transform.rotation.z = float(res[2].as_number().value());
                    nodes[cur_node_index].transform.rotation.w = float(res[3].as_number().value());
                }
                // set scale
                if (auto scale = object_i.find("scale"); scale != object_i.end()) {
                    std::vector<sejp::value> res = scale->second.as_array().value();
                    assert(res.size() == 3);
                    nodes[cur_node_index].transform.scale.x = float(res[0].as_number().value());
                    nodes[cur_node_index].transform.scale.y = float(res[1].as_number().value());
                    nodes[cur_node_index].transform.scale.z = float(res[2].as_number().value());
                }
                // set children
                if (auto res = object_i.find("children"); res != object_i.end()) {
                    std::vector<sejp::value> children = res->second.as_array().value();
                    for (int32_t j = 0; j < int32_t(children.size()); ++j) {
                            std::string child_name = children[j].as_string().value();
                            if (auto node_found = nodes_map.find(child_name); node_found != nodes_map.end()) {
                                nodes[cur_node_index].children.push_back(node_found->second);
                            } else {
                                Node new_node = {.name = child_name};
                                int32_t index = int32_t(nodes.size());
                                nodes.push_back(new_node);
                                nodes_map.insert({child_name, index});
                                nodes[cur_node_index].children.push_back(index);
                            }
                        }
                }

                // set mesh
                if (auto res = object_i.find("mesh"); res != object_i.end()) {
                    std::string mesh_name = res->second.as_string().value();
                    if (auto mesh_found = meshes_map.find(mesh_name); mesh_found != meshes_map.end()) {
                        nodes[cur_node_index].mesh_index = mesh_found->second;
                    } else {
                        Mesh new_mesh = {.name = mesh_name};
                        int32_t index = int32_t(meshes.size());
                        meshes.push_back(new_mesh);
                        meshes_map.insert({mesh_name, index});
                        nodes[cur_node_index].mesh_index = index;
                    }
                }

                // set camera
                if (auto res = object_i.find("camera"); res != object_i.end()) {
                    std::string camera_name = res->second.as_string().value();
                    if (auto camera_found = cameras_map.find(camera_name); camera_found != cameras_map.end()) {
                        nodes[cur_node_index].cameras_index = camera_found->second;
                    } else {
                        Camera new_camera = {.name = camera_name};
                        int32_t index = int32_t(cameras.size());
                        cameras.push_back(new_camera);
                        cameras_map.insert({camera_name, index});
                        nodes[cur_node_index].cameras_index = index;
                    }
                }

                // set light
                if (auto res = object_i.find("light"); res != object_i.end()) {
                    std::string light_name = res->second.as_string().value();
                    int32_t light_index = -1;
                    for (int32_t j = 0; j < lights.size(); ++j) {
                        if (lights[j].name == light_name) {
                            light_index = j;
                            break;
                        }
                    }

                    if (light_index == -1) {
                        Light new_light = {.name = light_name};
                        int32_t index = int32_t(lights.size());
                        lights.push_back(new_light);
                        nodes[cur_node_index].light_index = index;
                    }
                    else {
                        nodes[cur_node_index].light_index = light_index;
                    }
                }

            } else if (type.value() == "MESH") {
                std::string mesh_name = object_i.find("name")->second.as_string().value();
                int32_t cur_mesh_index;
                // look at the map and see if the node has been made already
                if (auto mesh_found = meshes_map.find(mesh_name); mesh_found != meshes_map.end()) {
                    cur_mesh_index = mesh_found->second;
                }
                else {
                    Mesh new_mesh = {.name = mesh_name};
                    cur_mesh_index = int32_t(meshes.size());
                    meshes.push_back(new_mesh);
                    meshes_map.insert({mesh_name, cur_mesh_index});
                }

                // Assuming all topology is triangle list
                // Assuming all attributes are in the same PosNorTanTex format

                // get count
                meshes[cur_mesh_index].count = int(object_i.find("count")->second.as_number().value());
                vertices_count += meshes[cur_mesh_index].count;
                
                // get attributes
                if (auto attributes_res = object_i.find("attributes"); attributes_res != object_i.end()) {
                    auto attributes = attributes_res->second.as_object().value();
                    // get position
                    if (auto attribute_res = attributes.find("POSITION"); attribute_res != attributes.end()) {
                        auto position = attribute_res->second.as_object().value();
                        meshes[cur_mesh_index].attributes[0].source = position.find("src")->second.as_string().value();
                        meshes[cur_mesh_index].attributes[0].offset = uint32_t(int32_t(position.find("offset")->second.as_number().value()));
                        meshes[cur_mesh_index].attributes[0].stride = uint32_t(int32_t(position.find("stride")->second.as_number().value()));
                        std::string format = position.find("format")->second.as_string().value();
                        if (format == "R32G32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
                        } else if (format == "R32G32B32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                        } else if (format == "R32G32B32A32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        } else if (format == "R8G8B8A8_UNORM") {
                            meshes[cur_mesh_index].attributes[0].format = VK_FORMAT_R8G8B8A8_UNORM;
                        } else {
                            throw std::runtime_error("Unsupported mesh format " + format + " for " + mesh_name);
                        }
                    }
                    // get normal
                    if (auto attribute_res = attributes.find("NORMAL"); attribute_res != attributes.end()) {
                        auto normal = attribute_res->second.as_object().value();
                        meshes[cur_mesh_index].attributes[1].source = normal.find("src")->second.as_string().value();
                        meshes[cur_mesh_index].attributes[1].offset = uint32_t(int32_t(normal.find("offset")->second.as_number().value()));
                        meshes[cur_mesh_index].attributes[1].stride = uint32_t(int32_t(normal.find("stride")->second.as_number().value()));
                        std::string format = normal.find("format")->second.as_string().value();
                        if (format == "R32G32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
                        } else if (format == "R32G32B32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                        } else if (format == "R32G32B32A32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        } else if (format == "R8G8B8A8_UNORM") {
                            meshes[cur_mesh_index].attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
                        } else {
                            throw std::runtime_error("Unsupported mesh format " + format + " for " + mesh_name);
                        }
                    }
                    // get tangent
                    if (auto attribute_res = attributes.find("TANGENT"); attribute_res != attributes.end()) {
                        auto tangent = attribute_res->second.as_object().value();
                        meshes[cur_mesh_index].attributes[2].source = tangent.find("src")->second.as_string().value();
                        meshes[cur_mesh_index].attributes[2].offset = uint32_t(int32_t(tangent.find("offset")->second.as_number().value()));
                        meshes[cur_mesh_index].attributes[2].stride = uint32_t(int32_t(tangent.find("stride")->second.as_number().value()));
                        std::string format = tangent.find("format")->second.as_string().value();
                        if (format == "R32G32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
                        } else if (format == "R32G32B32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
                        } else if (format == "R32G32B32A32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        } else if (format == "R8G8B8A8_UNORM") {
                            meshes[cur_mesh_index].attributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
                        } else {
                            throw std::runtime_error("Unsupported mesh format " + format + " for " + mesh_name);
                        }
                    }
                    // get texture coords
                    if (auto attribute_res = attributes.find("TEXCOORD"); attribute_res != attributes.end()) {
                        auto texcoord = attribute_res->second.as_object().value();
                        meshes[cur_mesh_index].attributes[3].source = texcoord.find("src")->second.as_string().value();
                        meshes[cur_mesh_index].attributes[3].offset = uint32_t(int32_t(texcoord.find("offset")->second.as_number().value()));
                        meshes[cur_mesh_index].attributes[3].stride = uint32_t(int32_t(texcoord.find("stride")->second.as_number().value()));
                        std::string format = texcoord.find("format")->second.as_string().value();
                        if (format == "R32G32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
                        } else if (format == "R32G32B32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
                        } else if (format == "R32G32B32A32_SFLOAT") {
                            meshes[cur_mesh_index].attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        } else if (format == "R8G8B8A8_UNORM") {
                            meshes[cur_mesh_index].attributes[3].format = VK_FORMAT_R8G8B8A8_UNORM;
                        } else {
                            throw std::runtime_error("Unsupported mesh format " + format + " for " + mesh_name);
                        }
                    }
                }

                // get material
                if (auto res = object_i.find("material"); res != object_i.end()) {
                    std::string material_name = res->second.as_string().value();
                    if (auto material_found = materials_map.find(material_name); material_found != materials_map.end()) {
                        meshes[cur_mesh_index].material_index = material_found->second;
                    } else {
                        Material new_material = {.name = material_name};
                        int32_t index = int32_t(materials.size());
                        materials.push_back(new_material);
                        materials_map.insert({material_name, index});
                        meshes[cur_mesh_index].material_index = index;
                    }
                }

            } else if (type.value() == "CAMERA") {
                std::string camera_name = object_i.find("name")->second.as_string().value();
                int32_t cur_camera_index;
                // look at the map and see if the node has been made already
                if (auto camera_found = cameras_map.find(camera_name); camera_found != cameras_map.end()) {
                    cur_camera_index = camera_found->second;
                }
                else {
                    Camera new_camera = {.name = camera_name};
                    cur_camera_index = int32_t(cameras.size());
                    cameras.push_back(new_camera);
                    cameras_map.insert({camera_name, cur_camera_index});
                }
                // get perspective
                if (auto res = object_i.find("perspective"); res != object_i.end()) {
                    auto perspective = res->second.as_object().value();
                    //aspect, vfov, and near are required
                    cameras[cur_camera_index].aspect = float(perspective.find("aspect")->second.as_number().value());
                    cameras[cur_camera_index].vfov = float(perspective.find("vfov")->second.as_number().value());
                    cameras[cur_camera_index].near = float(perspective.find("near")->second.as_number().value());

                    // see if there is far
                    if (auto far_res = perspective.find("far"); far_res != perspective.end()) {
                        cameras[cur_camera_index].far = float(far_res->second.as_number().value());
                    }
                }

            } else if (type.value() == "MATERIAL") {
                std::string material_name = object_i.find("name")->second.as_string().value();
                int32_t cur_material_index;
                // look at the map and see if the node has been made already
                if (auto material_found = materials_map.find(material_name); material_found != materials_map.end()) {
                    cur_material_index = material_found->second;
                }
                else {
                    Material new_material = {.name = material_name};
                    cur_material_index = int32_t(materials.size());
                    materials.push_back(new_material);
                    materials_map.insert({material_name, cur_material_index});
                }
                //find lambertian
                if (auto res = object_i.find("lambertian"); res != object_i.end()) {
                    if (auto albeto_res = res->second.as_object().value().find("albedo"); albeto_res != res->second.as_object().value().end()) {
                        auto albedo_vals = albeto_res->second.as_array();
                        if (albedo_vals) {
                            std::vector<sejp::value> albedo_vector = albedo_vals.value();
                            assert(albedo_vector.size() == 3);
                            Texture new_texture = Texture(glm::vec3(float(albedo_vector[0].as_number().value()), float(albedo_vector[1].as_number().value()), float(albedo_vector[2].as_number().value())));
                            std::string tex_name = material_name;
                            if (auto tex_map_entry = textures_map.find(tex_name); tex_map_entry != textures_map.end()) {
                                materials[cur_material_index].texture_index = tex_map_entry->second;
                            } else {
                                uint32_t index = uint32_t(textures.size());
                                textures.push_back(new_texture);
                                textures_map.insert({tex_name, index});
                                materials[cur_material_index].texture_index = index;
                            }
                        } else {
                            // check whether or not the albedo has a texture
                            if (auto tex_res = albeto_res->second.as_object().value().find("src"); tex_res != albeto_res->second.as_object().value().end()) {
                                std::string tex_name = tex_res->second.as_string().value();
                                if (auto tex_map_entry = textures_map.find(tex_name); tex_map_entry != textures_map.end()) {
                                    materials[cur_material_index].texture_index = tex_map_entry->second;
                                } else {
                                    Texture new_texture = Texture(tex_name);
                                    // find type, uncomment when we support cubemap and environment
                                    // if (auto type_res = albeto_res->second.as_object().value().find("type"); type_res != albeto_res->second.as_object().value().end()) {
                                    //     std::string tex_type = type_res->second.as_string().value();
                                    //     if (tex_type == "2D") new_texture.is_2D = true;
                                    //     else if (tex_type == "cube") new_texture.is_2D = false;
                                    //     else {
                                    //         throw std::runtime_error("unrecognizable type for texture " + tex_name);
                                    //     }
                                    // }
                                    uint32_t index = uint32_t(textures.size());
                                    textures.push_back(new_texture);
                                    textures_map.insert({tex_name, index});
                                    materials[cur_material_index].texture_index = index;
                                }
                            }
                            else {
                                Texture new_texture = Texture();
                                std::string tex_name = material_name;
                                if (auto tex_map_entry = textures_map.find(tex_name); tex_map_entry != textures_map.end()) {
                                    materials[cur_material_index].texture_index = tex_map_entry->second;
                                } else {
                                    uint32_t index = uint32_t(textures.size());
                                    textures.push_back(new_texture);
                                    textures_map.insert({tex_name, index});
                                    materials[cur_material_index].texture_index = index;
                                }
                            }

                        }
                    }
                }

            } else if (type.value() == "ENVIRONMENT") {
                std::cout << "Ignoring Environment Objects" <<std::endl;
            } else if (type.value() == "LIGHT") {
                std::string light_name = object_i.find("name")->second.as_string().value();
                glm::vec3 tint = glm::vec3(1.0f,1.0f,1.0f);
                if (auto tint_res = object_i.find("tint"); tint_res != object_i.end()) {
                    auto tint_arr = tint_res->second.as_array().value();
                    assert(tint_arr.size() == 3);
                    tint.x = float(tint_arr[0].as_number().value());
                    tint.y = float(tint_arr[1].as_number().value());
                    tint.z = float(tint_arr[2].as_number().value());
                }
                float shadow = 0.0f;
                if (auto shadow_res = object_i.find("shadow"); shadow_res != object_i.end()) {
                    shadow = float(shadow_res->second.as_number().value());
                }
                float angle = 0.0f;
                float strength = 1.0f;
                if (auto sun_res = object_i.find("sun"); sun_res != object_i.end()) {
                    auto sun_obj = sun_res->second.as_object().value();
                    if (auto angle_res = sun_obj.find("angle"); angle_res != sun_obj.end()) {
                        angle = float(angle_res->second.as_number().value());
                    }
                    if (auto strength_res = sun_obj.find("strength"); strength_res != sun_obj.end()) {
                        strength = float(strength_res->second.as_number().value());
                    }
                }
                else {
                    std::cerr<<"Only 'sun' light type supported for now.\n";
                    continue;
                }
                int32_t light_index = -1;
                for (int32_t j = 0; j < lights.size(); ++j) {
                    if (lights[j].name == light_name) {
                        light_index = j;
                        lights[j].tint = tint;
                        lights[j].angle = angle;
                        lights[j].strength = strength;
                        break;
                    }
                }

                if (light_index == -1) {
                    Light new_light = {
                        .name = light_name,
                        .tint = tint,
                        .angle = angle,
                        .strength = strength
                    };
                    lights.push_back(new_light);
                }

            } else if (type.value() == "DRIVER") {
                std::string driver_name = object_i.find("name")->second.as_string().value();
                std::string node_name = object_i.find("node")->second.as_string().value();
                std::string channel_str = object_i.find("channel")->second.as_string().value();
                Driver::Channel channel;
                if (channel_str == "translation") {
                    channel = Driver::Channel::Translation;
                }
                else if (channel_str == "scale") {
                    channel = Driver::Channel::Scale;
                }
                else if (channel_str == "rotation") {
                    channel = Driver::Channel::Rotation;
                }
                else {
                    throw std::runtime_error("Unrecognized channel: " + channel_str);
                }
                Driver::InterpolationMode interp = Driver::InterpolationMode::LINEAR;
                if (auto interp_res = object_i.find("interpolation"); interp_res != object_i.end()) {
                    std::string interp_str = interp_res->second.as_string().value();
                    if (interp_str == "STEP") interp = Driver::InterpolationMode::STEP;
                    else if (interp_str == "LINEAR") interp = Driver::InterpolationMode::LINEAR;
                    else if (interp_str == "SLERP") interp = Driver::InterpolationMode::SLERP;
                    else {
                        std::cerr<<"Unrecognized interpolation mode for driver "<< driver_name<<": '"<<interp_str<<"', defaulting to LINEAR\n";
                    }
                }
                uint32_t node_index = 0;
                if (auto node_found = nodes_map.find(node_name); node_found != nodes_map.end()) {
                    node_index = node_found->second;
                }
                else {
                    Node new_node = {.name = node_name};
                    node_index = int32_t(nodes.size());
                    nodes.push_back(new_node);
                    nodes_map.insert({node_name, node_index});
                    root_nodes.push_back(node_index);
                }
                Driver driver = {
                    .name = driver_name,
                    .node_index = node_index,
                    .channel = channel,
                    .interpolation = interp,
                };
                std::vector<sejp::value> times = object_i.find("times")->second.as_array().value();
                std::vector<sejp::value> values = object_i.find("values")->second.as_array().value();
                if (channel == Driver::Channel::Rotation) {
                    if (times.size() * 4 != values.size()) {
                        std::cerr<<"Value size: "<<values.size()<< "; Time Size" << times.size()<<std::endl;
                        throw std::runtime_error("Rotation driver " + driver_name +" does not have correct number of values (4 * time)");
                    }
                }
                else if (times.size() * 3 != values.size()){
                    std::cerr<<"Value size: "<<values.size()<< "; Time Size" << times.size()<<std::endl;
                    throw std::runtime_error("Translation/Scaling driver " + driver_name +" does not have correct number of values (3 * time)");
                }
                for (uint32_t time_i = 0; time_i < times.size(); ++time_i) {
                    driver.times.push_back(float(times[time_i].as_number().value()));
                }
                for (uint32_t value_i = 0; value_i < values.size(); ++value_i) {
                    driver.values.push_back(float(values[value_i].as_number().value()));
                }
                drivers.push_back(driver);
            } else {
                std::cerr << "Unknown type: " + type.value() <<std::endl;
            }
        }


    } catch (std::exception &e) {
        std::cerr<<"Exception occured while trying to parse .s72 scene file\n";
        throw e;
    }

    std::cout<< "----Finished loading " + filename +"----"<<std::endl;

    { //build the camera and light local to world transform vectors
        std::vector<uint32_t> cur_transform_list;
		std::function<void(uint32_t)> fill_camera_and_light_transforms = [&](uint32_t i) {
			const Scene::Node& cur_node = nodes[i];
            cur_transform_list.push_back(i);
            if (cur_node.light_index != -1) {
                lights[cur_node.light_index].local_to_world = cur_transform_list;
            }
			if (cur_node.cameras_index != -1) {
                cameras[cur_node.cameras_index].local_to_world = cur_transform_list;
                if (requested_camera.has_value() && requested_camera.value() == cameras[cur_node.cameras_index].name) {
                    requested_camera_index = cur_node.cameras_index;
                }
            }
			// look for cameras in children
			for (uint32_t child_index : cur_node.children) {
				fill_camera_and_light_transforms(child_index);
			}
			cur_transform_list.pop_back();
		};

		//traverse the scene hiearchy:
		for (uint32_t i = 0; i < root_nodes.size(); ++i) {
			fill_camera_and_light_transforms(root_nodes[i]);
		}
	}
    // could not find requested camera
    if (requested_camera.has_value() && requested_camera_index == -1) {
        throw std::runtime_error("Did not find camera with name: " + requested_camera.value() + ", aborting...");
    }
    if (requested_camera_index == -1) {
        requested_camera_index = 0;
    }

    debug();
}

void Scene::debug() {
    for (const auto& node : nodes) {
        // Print node name
        std::cout << "Node Name: " << node.name << "\n";

        std::cout << "Node Transforms:"<<std::endl;
        std::cout << "Node Position: (" 
                          << node.transform.position.x << ", "
                          << node.transform.position.y << ", "
                          << node.transform.position.z << ")\n";
        std::cout << "Node Rotation: (" 
                          << node.transform.rotation.x << ", "
                          << node.transform.rotation.y << ", "
                          << node.transform.rotation.z << ", "
                          << node.transform.rotation.w << ")\n";
        std::cout << "Node Scale: (" 
                          << node.transform.scale.x << ", "
                          << node.transform.scale.y << ", "
                          << node.transform.scale.z << ")\n";

        // Check if the node is a root
        bool is_root = (std::find(root_nodes.begin(), root_nodes.end(), &node - &nodes[0]) != root_nodes.end());
        std::cout << "Is Root: " << (is_root ? "Yes" : "No") << "\n";

        // Print children names
        std::cout << "Children: ";
        if (node.children.empty()) {
            std::cout << "None";
        } else {
            for (auto child_index : node.children) {
                std::cout << nodes[child_index].name << " ";
            }
        }
        std::cout << "\n";

        // Print camera information (if available)
        if (node.cameras_index != -1) {
            const Camera& camera = cameras[node.cameras_index];
            std::cout << "Camera Name: " << camera.name << "\n";
        }

        // Print mesh information (if available)
        if (node.mesh_index != -1) {
            const Mesh& mesh = meshes[node.mesh_index];
            std::cout << "Mesh Name: " << mesh.name <<", Mesh Index: "<< node.mesh_index<< "\n";
            for (int i = 0; i < 4; ++i) {
                std::string attribute_name;
                if (i == 0) attribute_name = "position";
                else if (i == 1) attribute_name = "normal";
                else if (i == 2) attribute_name = "tangent";
                else if (i == 3) attribute_name = "texcoord";
                std::cout<< "Attribute: " << attribute_name << "\n";
                std::cout<<"    Source: "<< mesh.attributes[i].source << ", Offset: " << mesh.attributes[i].offset << ", Stride: "<< mesh.attributes[i].stride<<std::endl;
            }

            // Print material associated with the mesh
            if (mesh.material_index != -1) {
                const Material& material = materials[mesh.material_index];
                std::cout << "Material Name: " << material.name << "\n";

                // Print texture associated with the material (if available)
                const Texture& texture = textures[material.texture_index];
                if (texture.has_src) {
                    std::cout << "Texture Source: " << texture.source << "\n";
                } else {
                    std::cout << "Albedo Color: (" 
                            << texture.value.r << ", "
                            << texture.value.g << ", "
                            << texture.value.b << ")\n";
                }
            }
            else {
                std::cout << "Material Name: Default" << "\n";
            }
        }

        // Print light information (if available)
        if (node.light_index != -1) {
            const Light& light = lights[node.light_index];
            std::cout << "Light Name: " << light.name << "\n";
            std::cout << "Light Tint: (" 
                      << light.tint.r << ", "
                      << light.tint.g << ", "
                      << light.tint.b << ")\n";
            std::cout << "Light Strength: " << light.strength << "\n";
            std::cout << "Light Angle: " << light.angle << "\n";
        }

        std::cout << "-----------------------------\n";
    }
    // print driver info
    for (const auto& driver: drivers) {
        std::cout << "Driver: " << driver.name << std::endl;
        std::cout << "  Node Index: " << driver.node_index << std::endl;
        
        std::cout << "  Channel: ";
        switch (driver.channel) {
            case Driver::Translation: std::cout << "Translation"; break;
            case Driver::Scale: std::cout << "Scale"; break;
            case Driver::Rotation: std::cout << "Rotation"; break;
        }
        std::cout << std::endl;

        // std::cout << "  Times: ";
        // for (const auto& time : driver.times) {
        //     std::cout << time << " ";
        // }
        // std::cout << std::endl;

        // std::cout << "  Values: ";
        // for (const auto& value : driver.values) {
        //     std::cout << value << " ";
        // }
        // std::cout << std::endl;

        std::cout << "  Interpolation Mode: ";
        switch (driver.interpolation) {
            case Driver::STEP: std::cout << "STEP"; break;
            case Driver::LINEAR: std::cout << "LINEAR"; break;
            case Driver::SLERP: std::cout << "SLERP"; break;
        }
        std::cout << std::endl;

        std::cout << "-----------------------------\n";
    }
}

void Scene::update_drivers(float dt)
{
    if (animation_setting == 2) return;
    for (Scene::Driver& driver : drivers) {
        if (driver.cur_time_index == driver.times.size()) continue;
        driver.cur_time += dt;
        auto found_it = std::upper_bound(driver.times.begin()+driver.cur_time_index,driver.times.end(), driver.cur_time);

        //extrapolate constant value at the beginning
        if (found_it == driver.times.begin()) {
            if (driver.channel == Driver::Channel::Rotation) {
                uint32_t cur_value_index = driver.cur_time_index * 4;
                nodes[driver.node_index].transform.rotation = glm::quat(
                    driver.values[cur_value_index + 3],
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            else if (driver.channel == Driver::Channel::Translation) {
                uint32_t cur_value_index = driver.cur_time_index * 3;
                nodes[driver.node_index].transform.position = glm::vec3(
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            else if (driver.channel == Driver::Channel::Scale) {
                uint32_t cur_value_index = driver.cur_time_index * 3;
                nodes[driver.node_index].transform.scale = glm::vec3(
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            continue;
        }
        // extrapolate constant value at the end
        if (found_it == driver.times.end()) {
            driver.cur_time_index = uint32_t(driver.times.size() - 1);
            if (driver.channel == Driver::Channel::Rotation) {
                uint32_t cur_value_index = driver.cur_time_index * 4;
                nodes[driver.node_index].transform.rotation = glm::quat(
                    driver.values[cur_value_index + 3],
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            else if (driver.channel == Driver::Channel::Translation) {
                uint32_t cur_value_index = driver.cur_time_index * 3;
                nodes[driver.node_index].transform.position = glm::vec3(
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            else if (driver.channel == Driver::Channel::Scale) {
                uint32_t cur_value_index = driver.cur_time_index * 3;
                nodes[driver.node_index].transform.scale = glm::vec3(
                    driver.values[cur_value_index], 
                    driver.values[cur_value_index + 1], 
                    driver.values[cur_value_index + 2]
                );
            }
            // reset animation to start again
            if (animation_setting == 1) {
                driver.cur_time_index = 0;
                driver.cur_time = 0.0f;
            }
            continue;
        }
        // time should be between cur_time_index and cur_time_index + 1
        driver.cur_time_index = uint32_t(found_it - driver.times.begin() - 1);
        switch (driver.interpolation) {
            case Driver::InterpolationMode::STEP: {
                    if (driver.channel == Driver::Channel::Rotation) {
                        uint32_t cur_value_index = driver.cur_time_index * 4;
                        nodes[driver.node_index].transform.rotation = glm::quat(
                            driver.values[cur_value_index + 3],
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        );
                    }
                    else if (driver.channel == Driver::Channel::Translation) {
                        uint32_t cur_value_index = driver.cur_time_index * 3;
                        nodes[driver.node_index].transform.position = glm::vec3(
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        );
                    }
                    else if (driver.channel == Driver::Channel::Scale) {
                        uint32_t cur_value_index = driver.cur_time_index * 3;
                        nodes[driver.node_index].transform.scale = glm::vec3(
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        );
                    }
                }
                break;
            case Driver::InterpolationMode::LINEAR: {
                    float interval = driver.times[driver.cur_time_index + 1] - driver.times[driver.cur_time_index];
                    float t = (driver.cur_time - driver.times[driver.cur_time_index]) / interval;
                    if (driver.channel == Driver::Channel::Rotation) {
                        uint32_t n = 4;
                        uint32_t cur_value_index = driver.cur_time_index * n;
                        nodes[driver.node_index].transform.rotation = glm::quat(
                            driver.values[cur_value_index + 3],
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        ) * (1.0f - t) + glm::quat(
                            driver.values[cur_value_index + n + 3],
                            driver.values[cur_value_index + n], 
                            driver.values[cur_value_index + n + 1], 
                            driver.values[cur_value_index + n + 2]
                        ) * t;
                    }
                    else if (driver.channel == Driver::Channel::Translation) {
                        uint32_t n = 3;
                        uint32_t cur_value_index = driver.cur_time_index * n;
                        nodes[driver.node_index].transform.position = glm::vec3(
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        )* (1.0f - t) + glm::vec3(
                            driver.values[cur_value_index + n], 
                            driver.values[cur_value_index + n + 1], 
                            driver.values[cur_value_index + n + 2]
                        ) * t;
                    }
                    else if (driver.channel == Driver::Channel::Scale) {
                        uint32_t n = 3;
                        uint32_t cur_value_index = driver.cur_time_index * n;
                        nodes[driver.node_index].transform.scale = glm::vec3(
                            driver.values[cur_value_index], 
                            driver.values[cur_value_index + 1], 
                            driver.values[cur_value_index + 2]
                        )* (1.0f - t) + glm::vec3(
                            driver.values[cur_value_index + n], 
                            driver.values[cur_value_index + n + 1], 
                            driver.values[cur_value_index + n + 2]
                        ) * t;
                    }
                }
                break;
            case Driver::InterpolationMode::SLERP: {
                    if (driver.channel == Driver::Channel::Rotation) {
                        float interval_slerp = driver.times[driver.cur_time_index + 1] - driver.times[driver.cur_time_index];
                        float t_slerp = (driver.cur_time - driver.times[driver.cur_time_index]) / interval_slerp;

                        // Retrieve the quaternions at the current and next time indices
                        uint32_t n = 4;
                        uint32_t cur_value_index = driver.cur_time_index * n;

                        glm::quat q1 = glm::quat(
                            driver.values[cur_value_index + 3],
                            driver.values[cur_value_index],
                            driver.values[cur_value_index + 1],
                            driver.values[cur_value_index + 2]
                        );
                        
                        glm::quat q2 = glm::quat(
                            driver.values[cur_value_index + n + 3],
                            driver.values[cur_value_index + n],
                            driver.values[cur_value_index + n + 1],
                            driver.values[cur_value_index + n + 2]
                        );
                        
                        // Perform SLERP between q1 and q2 based on t
                        nodes[driver.node_index].transform.rotation = glm::slerp(q1, q2, t_slerp);
                    }
                    else if (driver.channel == Driver::Channel::Translation) {
                        throw std::runtime_error("Shouldn't call SLERP on translation");
                    }
                    else {
                        throw std::runtime_error("Shouldn't call SLERP on scale");
                    }
                }
                break;
        }
        
    }
}

void Scene::set_driver_time(float time)
{
    for (Scene::Driver& driver : drivers) {
        driver.cur_time_index = 0;
        driver.cur_time = time;
    }
    update_drivers(0.0f);
}

glm::mat4x4 Scene::Transform::parent_from_local() const
{
    //compute:
	//   translate   *   rotate    *   scale
	// [ 1 0 0 p.x ]   [       0 ]   [ s.x 0 0 0 ]
	// [ 0 1 0 p.y ] * [ rot   0 ] * [ 0 s.y 0 0 ]
	// [ 0 0 1 p.z ]   [       0 ]   [ 0 0 s.z 0 ]
	//                 [ 0 0 0 1 ]   [ 0 0   0 1 ]

	glm::mat3 rot = glm::mat3_cast(rotation);

	// Construct the 4x4 matrix:
	return glm::mat4(
		glm::vec4(rot[0] * scale.x, 0.0f), // First column
		glm::vec4(rot[1] * scale.y, 0.0f), // Second column
		glm::vec4(rot[2] * scale.z, 0.0f), // Third column
		glm::vec4(position, 1.0f)          // Fourth column (position, with homogeneous coordinate 1.0)
	);
}

glm::mat4x4 Scene::Transform::local_from_parent() const
{
    //compute:
	//   1/scale       *    rot^-1   *  translate^-1
	// [ 1/s.x 0 0 0 ]   [       0 ]   [ 0 0 0 -p.x ]
	// [ 0 1/s.y 0 0 ] * [rot^-1 0 ] * [ 0 0 0 -p.y ]
	// [ 0 0 1/s.z 0 ]   [       0 ]   [ 0 0 0 -p.z ]
	//                   [ 0 0 0 1 ]   [ 0 0 0  1   ]

	glm::vec3 inv_scale;
	//taking some care so that we don't end up with NaN's , just a degenerate matrix, if scale is zero:
	inv_scale.x = (scale.x == 0.0f ? 0.0f : 1.0f / scale.x);
	inv_scale.y = (scale.y == 0.0f ? 0.0f : 1.0f / scale.y);
	inv_scale.z = (scale.z == 0.0f ? 0.0f : 1.0f / scale.z);

	//compute inverse of rotation:
	glm::mat3 inv_rot = glm::mat3_cast(glm::inverse(rotation));

	//scale the rows of rot:
	inv_rot[0] *= inv_scale;
	inv_rot[1] *= inv_scale;
	inv_rot[2] *= inv_scale;

	return glm::mat4x4(
		glm::vec4(inv_rot[0], 0.0f),         // First column
		glm::vec4(inv_rot[1], 0.0f),         // Second column
		glm::vec4(inv_rot[2], 0.0f),         // Third column
		glm::vec4(inv_rot * -position, 1.0f) // Fourth column (inverse translation, homogeneous coordinate 1.0)
	);
}