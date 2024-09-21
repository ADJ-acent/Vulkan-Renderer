#include "scene.hpp"
#include "sejp.hpp"
#include <fstream>
#include <iostream>
#include <unordered_map>
void Scene::load(std::string filename)
{
    if (filename.substr(filename.size()-5, 4) != ".s72") {
        throw std::runtime_error("Scene " + filename + " is not a compatible format (s72 required).");
    }
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

        for (uint32_t i = 1; i < uint32_t(object.size()); ++i) {
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
                        for (uint32_t j = 0; j < uint32_t(roots.size()); ++j) {
                            std::string child_name = roots[j].as_string().value();
                            if (auto node_found = nodes_map.find(child_name); node_found != nodes_map.end()) {
                                root_nodes.push_back(node_found->second);
                            }
                            else {
                                Node new_node = {.name = child_name};
                                uint32_t index = uint32_t(nodes.size());
                                nodes.push_back(new_node);
                                nodes_map.insert({child_name, index});
                                root_nodes.push_back(index);
                            }
                        }
                    }
                }
            } else if (type.value() == "NODE") {
                std::string node_name = object_i.find("name")->second.as_string().value();
                uint32_t cur_node_index;
                // look at the map and see if the node has been made already
                if (auto node_found = nodes_map.find(node_name); node_found != nodes_map.end()) {
                    cur_node_index = node_found->second;
                }
                else {
                    Node new_node = {.name = node_name};
                    cur_node_index = uint32_t(nodes.size());
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
                    for (uint32_t j = 0; j < uint32_t(children.size()); ++j) {
                            std::string child_name = children[j].as_string().value();
                            if (auto node_found = nodes_map.find(child_name); node_found != nodes_map.end()) {
                                nodes[cur_node_index].children.push_back(node_found->second);
                            } else {
                                Node new_node = {.name = child_name};
                                uint32_t index = uint32_t(nodes.size());
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
                        uint32_t index = uint32_t(meshes.size());
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
                        uint32_t index = uint32_t(cameras.size());
                        cameras.push_back(new_camera);
                        cameras_map.insert({camera_name, index});
                        nodes[cur_node_index].cameras_index = index;
                    }
                }

                // set light
                if (auto res = object_i.find("light"); res != object_i.end()) {
                    std::string light_name = res->second.as_string().value();
                    int32_t light_index = -1;
                    for (uint32_t j = 0; j < lights.size(); ++j) {
                        if (lights[j].name == light_name) {
                            light_index = j;
                            break;
                        }
                    }

                    if (light_index == -1) {
                        Light new_light = {.name = light_name};
                        uint32_t index = uint32_t(lights.size());
                        lights.push_back(new_light);
                        nodes[cur_node_index].light_index = index;
                    }
                    else {
                        nodes[cur_node_index].light_index = light_index;
                    }
                }

            } else if (type.value() == "MESH") {
                std::string mesh_name = object_i.find("name")->second.as_string().value();
                uint32_t cur_mesh_index;
                // look at the map and see if the node has been made already
                if (auto mesh_found = meshes_map.find(mesh_name); mesh_found != meshes_map.end()) {
                    cur_mesh_index = mesh_found->second;
                }
                else {
                    Mesh new_mesh = {.name = mesh_name};
                    cur_mesh_index = uint32_t(meshes.size());
                    meshes.push_back(new_mesh);
                    meshes_map.insert({mesh_name, cur_mesh_index});
                }

                // Assuming all topology is triangle list
                // Assuming all attributes are in the same PosNorTanTex format

                // get count
                meshes[cur_mesh_index].count = int(object_i.find("count")->second.as_number().value());
                
                // get source
                meshes[cur_mesh_index].source = object_i.find("attributes")->second.as_object().value().find("POSITION")->second.as_object().value().find("src")->second.as_string().value();

                // get material
                if (auto res = object_i.find("material"); res != object_i.end()) {
                    std::string material_name = res->second.as_string().value();
                    if (auto material_found = materials_map.find(material_name); material_found != materials_map.end()) {
                        meshes[cur_mesh_index].material_index = material_found->second;
                    } else {
                        Material new_material = {.name = material_name};
                        uint32_t index = uint32_t(materials.size());
                        materials.push_back(new_material);
                        materials_map.insert({material_name, index});
                        meshes[cur_mesh_index].material_index = index;
                    }
                }

            } else if (type.value() == "CAMERA") {
                std::string camera_name = object_i.find("name")->second.as_string().value();
                uint32_t cur_camera_index;
                // look at the map and see if the node has been made already
                if (auto camera_found = cameras_map.find(camera_name); camera_found != cameras_map.end()) {
                    cur_camera_index = camera_found->second;
                }
                else {
                    Camera new_camera = {.name = camera_name};
                    cur_camera_index = uint32_t(cameras.size());
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

            } else if (type.value() == "DRIVER") {
                //TODO add driver support
            } else if (type.value() == "MATERIAL") {
                std::string material_name = object_i.find("name")->second.as_string().value();
                uint32_t cur_material_index;
                // look at the map and see if the node has been made already
                if (auto material_found = materials_map.find(material_name); material_found != materials_map.end()) {
                    cur_material_index = material_found->second;
                }
                else {
                    Material new_material = {.name = material_name};
                    cur_material_index = uint32_t(materials.size());
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
                            materials[cur_material_index].albedo.value_albedo.x = float(albedo_vector[0].as_number().value());
                            materials[cur_material_index].albedo.value_albedo.y = float(albedo_vector[1].as_number().value());
                            materials[cur_material_index].albedo.value_albedo.z = float(albedo_vector[2].as_number().value());
                        } else {
                            std::string tex_name = albeto_res->second.as_object().value().find("src")->second.as_string().value();
                            if (auto tex_res = textures_map.find(tex_name); tex_res != textures_map.end()) {
                                materials[cur_material_index].albedo.texture_index = tex_res->second;
                            } else {
                                Texture new_texture = {.source = tex_name};
                                // find type
                                if (auto type_res = albeto_res->second.as_object().value().find("type"); type_res != albeto_res->second.as_object().value().end()) {
                                    std::string tex_type = type_res->second.as_string().value();
                                    if (tex_type == "2D") new_texture.is_2D = true;
                                    else if (tex_type == "cube") new_texture.is_2D = false;
                                    else {
                                        throw std::runtime_error("unrecognizable type for texture " + tex_name);
                                    }
                                }
                                // Maybe TODO: find format
                            }

                        }
                    }
                }

            } else if (type.value() == "ENVIRONMENT") {
                std::cout << "Ignoring Environment Objects" <<std::endl;
            } else if (type.value() == "LIGHT") {
                std::string light_name = object_i.find("name")->second.as_string().value();
                int32_t light_index = -1;
                for (uint32_t j = 0; j < lights.size(); ++j) {
                    if (lights[j].name == light_name) {
                        light_index = j;
                        break;
                    }
                }

                if (light_index == -1) {
                    Light new_light = {.name = light_name};
                    lights.push_back(new_light);
                }

            } else {
                std::cerr << "Unknown type: " + type.value() <<std::endl;
            }
            
        }


    } catch (std::exception &e) {
        throw e;
    }

}

Scene::Scene(std::string filename)
{
    load(filename);
}
