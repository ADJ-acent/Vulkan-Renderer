#include "NaniteMeshApp.hpp"
#include "miniball/Seb.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <array>
#include "read_cluster.hpp"

std::random_device rd;
std::mt19937 gen(rd()); 
std::uniform_int_distribution<> distrib(1, 6);

void write_clsr(std::string save_path, uint32_t lod_level, 
    std::vector<NaniteMeshApp::Cluster> &clusters, 
    std::vector<NaniteMeshApp::ClusterGroup> &groups, 
    std::vector<glm::uvec3> &triangles, 
    std::vector<glm::vec3> &vertices)
{
    std::string clsr_path = save_path + "_" + std::to_string(lod_level) + ".clsr";
    std::ofstream clsr_file(clsr_path, std::ios::binary);
    assert(clsr_file.is_open());
    DiskClusterLevelHeader header;
    assert(clusters.size() != 0);
    header.cluster_count = uint32_t(clusters.size());
    header.group_count = uint32_t(groups.size());

    std::vector<DiskCluster> disk_clusters;
    disk_clusters.reserve(clusters.size());
    std::vector<glm::vec3> disk_vertices;
    disk_vertices.reserve(triangles.size() * 3);
    for (auto& cluster : clusters) {
        uint32_t cluster_vertex_begin = uint32_t(disk_vertices.size());
        for (uint32_t triangle_i : cluster.triangles) {
            glm::uvec3 triangle_verts = triangles[triangle_i];
            glm::vec3 v0 = vertices[triangle_verts[0]];
            glm::vec3 v1 = vertices[triangle_verts[1]];
            glm::vec3 v2 = vertices[triangle_verts[2]];
            disk_vertices.push_back(v0);
            disk_vertices.push_back(v1);
            disk_vertices.push_back(v2);
        }
        uint32_t cluster_vertex_size = uint32_t(disk_vertices.size()) - cluster_vertex_begin;
        disk_clusters.push_back(DiskCluster{
            .vertices_begin = cluster_vertex_begin,
            .vertices_count = cluster_vertex_size,
            .src_cluster_group = cluster.src_cluster_group,
            .dst_cluster_group = cluster.dst_cluster_group,
            .error = lod_level == 0 ? -1 : cluster.error,
            .bounding_sphere = cluster.bounding_sphere,
        });
    }

    header.vertices_count = uint32_t(disk_vertices.size());
    clsr_file.write(reinterpret_cast<const char *>(&header), sizeof(DiskClusterLevelHeader));
    clsr_file.write(reinterpret_cast<const char *>(disk_clusters.data()), sizeof(DiskCluster) * disk_clusters.size());
    clsr_file.write(reinterpret_cast<const char *>(disk_vertices.data()), sizeof(glm::vec3) * disk_vertices.size());

    clsr_file.close();
}

glm::vec4 calculate_bounding_sphere(const std::vector<glm::vec3> &vertices, uint32_t begin, uint32_t count)
{
    using namespace SEB_NAMESPACE;

    constexpr int dim = 3;  // We are working in 3D space

    // Convert glm::vec3 points to SEB Point format
    std::vector<Point<double>> seb_points;
    seb_points.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        glm::vec3 p = vertices[begin + i];
        seb_points.emplace_back(dim, std::vector<double>{p.x, p.y, p.z}.begin());
    }

    // Compute the smallest enclosing ball
    Smallest_enclosing_ball<double> miniball(dim, seb_points);

    auto center_it = miniball.center_begin();
    glm::vec3 center(center_it[0], center_it[1], center_it[2]);
    double radius = miniball.radius();
    return glm::vec4(center, radius);
}

glm::vec4 estimate_bounding_sphere_of_spheres(const std::vector<glm::vec4> &spheres)
{
    // this is an estimation based on https://stackoverflow.com/a/39683025 and Ritter's algorithm

    // collect min/max x,y,z of each sphere, and the corners of the inscribed cubes
    std::vector<glm::vec3> points;
    points.reserve(spheres.size() * 14);
    
    const float offset = float(sqrt(3.0)/3.0);
    const std::array<glm::vec3, 8> inscribed_cube = {
        glm::vec3(offset, offset, offset),
        glm::vec3(-offset, offset, offset),
        glm::vec3(offset, -offset, offset),
        glm::vec3(-offset, -offset, offset),
        glm::vec3(offset, offset, -offset),
        glm::vec3(-offset, offset, -offset),
        glm::vec3(offset, -offset, -offset),
        glm::vec3(-offset, -offset, -offset)
    };

    for (const glm::vec4& sphere : spheres) {
        glm::vec3 center = glm::vec3(sphere.x,sphere.y,sphere.z);
        float radius = sphere.w;
        
        points.push_back(center + glm::vec3(radius,0,0));
        points.push_back(center + glm::vec3(-radius,0,0));
        points.push_back(center + glm::vec3(0,radius,0));
        points.push_back(center + glm::vec3(0,-radius,0));
        points.push_back(center + glm::vec3(0,0,radius));
        points.push_back(center + glm::vec3(0,0,-radius));

        for (uint8_t i = 0; i < uint8_t(inscribed_cube.size()); ++i) {
            points.push_back(offset*radius + center);
        }
    }
    // use ritter's algo to update the radius
    glm::vec4 new_bounding_sphere = calculate_bounding_sphere(points, 0, uint32_t(points.size()));
    glm::vec3 new_center = glm::vec3(new_bounding_sphere);
    for (const glm::vec4& sphere : spheres) {
        glm::vec3 old_sphere_center = glm::vec3(sphere);
        glm::vec3 furthest_point = glm::normalize(old_sphere_center - new_center) * (sphere.w + 0.001f) + old_sphere_center;
        float distance = glm::distance(furthest_point, new_center);
        if (distance >= new_bounding_sphere.w) {
            new_bounding_sphere.w = distance;
        }
    }
    for (const glm::vec4& sphere : spheres) {
        assert(new_bounding_sphere.w - sphere.w > -0.001f);
    }
    return new_bounding_sphere;
}

void read_clsr(std::string file_path, RuntimeDAG* to, bool debug) {
    uint32_t LOD_level = 0;
    
    while (std::filesystem::exists(file_path + "_" + std::to_string(LOD_level) + ".clsr")) {
        std::string full_path = file_path + "_" + std::to_string(LOD_level) + ".clsr";
        std::ifstream cluster_file(full_path, std::ios::binary);
        if (!cluster_file.is_open()) {
            throw std::runtime_error("Failed to open the file: " +  full_path);
        }
        DiskClusterLevelHeader header;
        if (!cluster_file.read(reinterpret_cast< char * >(&header), sizeof(header))) {
            throw std::runtime_error("Failed to read clsr header");
        }
        if (std::string(header.clsr_header,4) != "clsr") {
            throw std::runtime_error("Unexpected magic number in clsr");
        }
        uint32_t group_count = header.group_count;
        uint32_t cluster_count = header.cluster_count;
        uint32_t vertices_count = header.vertices_count;
        to->clusters.push_back(std::vector<DiskCluster>(cluster_count));
        to->vertices.push_back(std::vector<glm::vec3>(vertices_count));
        to->groups.push_back(std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>>(group_count));
        to->color_index.push_back(std::vector<uint8_t>(cluster_count));
        if (!cluster_file.read(reinterpret_cast< char * >(to->clusters[LOD_level].data()), sizeof(DiskCluster) * cluster_count)) {
            throw std::runtime_error("Failed to read cluster data");
        }
        if (!cluster_file.read(reinterpret_cast< char * >(to->vertices[LOD_level].data()), sizeof(glm::vec3) * vertices_count)) {
            throw std::runtime_error("Failed to read vertices data");
        }

        for (uint32_t i = 0; i < uint32_t(to->clusters[LOD_level].size()); ++i) {
            const DiskCluster& disk_cluster = to->clusters[LOD_level][i];
            if (LOD_level != 0) {
                to->groups[LOD_level - 1][disk_cluster.src_cluster_group].second.push_back(i);
            }
            to->groups[LOD_level][disk_cluster.dst_cluster_group].first.push_back(i);
            to->color_index[LOD_level][i] = uint8_t(distrib(gen));
        }

        LOD_level ++;
    }
    std::cout<< "Done loading clusters, loaded " << LOD_level << " levels\n";
    return;
}
void dag_to_bvh(RuntimeDAG &dag, ClusterBVH *to)
{
    uint32_t cluster_count = 0;
    uint32_t group_count = 0;
    std::vector<uint32_t> level_offsets;
    assert(dag.clusters.size() == dag.groups.size());
    // figure out the offsets for each LOD level and reserve space for new clusters
    for (uint32_t i = 0; i < uint32_t(dag.clusters.size()); ++i) {
        const auto& clusters = dag.clusters[i];
        level_offsets.push_back(cluster_count);
        cluster_count += uint32_t(clusters.size());
        group_count += uint32_t(dag.groups[i].size()); 
    }

    to->clusters.reserve(cluster_count);
    // insert all clusters into one buffer
    uint32_t lod_level_vertices_offset = 0;
    for (uint32_t i = 0; i < uint32_t(dag.clusters.size()); ++i) {
        auto& clusters = dag.clusters[i];
        for (const DiskCluster& disk_cluster : clusters) {
            to->clusters.push_back(ClusterBVH::Node{
                // group and node index will be populated later
                .node_index = static_cast<uint32_t>(-1),
                .group_index = static_cast<uint32_t>(-1),
                .error = disk_cluster.error,
                .bounding_sphere = disk_cluster.bounding_sphere,
            });
            to->vertices.push_back(ClusterBVH::ClusterVertices{
                .vertices_begin = disk_cluster.vertices_begin + lod_level_vertices_offset,
                .vertices_count = disk_cluster.vertices_count,
            });
        }
        lod_level_vertices_offset += uint32_t(dag.vertices[i].size());
    }

    to->groups.reserve(group_count);
    // reorganize groups to one buffer 
    for (uint32_t i = 0; i < uint32_t(dag.groups.size()); ++i) {
        {// move vertices to the bvh
            to->source_vertices.insert(to->source_vertices.end(), dag.vertices[i].begin(), dag.vertices[i].end());
        }

        const auto& level_i_groups = dag.groups[i];
        uint32_t level_i_offset = level_offsets[i];
        uint32_t level_above_i_offset = i == dag.groups.size() - 1 ? 0 : level_offsets[i+1];
        for (uint32_t j = 0; j < uint32_t(level_i_groups.size()); ++j) {

            // for child clusters of the group, create BVH group object
            ClusterBVH::Group group;
            assert(level_i_groups[j].first.size() <= 8);
            for (uint32_t child_i = 0; child_i < 8; ++child_i) {
                if ( level_i_groups[j].first.size() <= child_i ) {
                    group.child_node_indices[child_i] = -1;
                }
                else {
                    uint32_t cluster_index = level_i_groups[j].first[child_i] + level_i_offset;
                    group.child_node_indices[child_i] = cluster_index;
                    if (i == dag.groups.size() - 1) {
                        to->root_nodes.push_back(cluster_index); // top level clusters
                    }
                }
            }
            to->groups.push_back(group);

            // for parent clusters of the group, assign the cluster index within the group and the group number
            assert(!(i == dag.groups.size() - 1 && level_i_groups[j].second.size() != 0)); // top level group shouldn't have any parents
            for (uint32_t parent_i = 0; parent_i < uint32_t(level_i_groups[j].second.size()); ++parent_i) {
                uint32_t cluster_index = level_i_groups[j].second[parent_i] + level_above_i_offset;
                to->clusters[cluster_index].group_index = uint32_t(to->groups.size() - 1); // we just pushed the current group
                to->clusters[cluster_index].node_index = parent_i; // index within the group
            }
        }
    }

    // check if the bvh and the dag is entirely corresponding
    uint32_t count = 0;
    uint32_t lod = 0;
    uint32_t offset = 0;
    for (auto& groups_at_level : dag.groups) {
        
        // ensure all the groups still have the same children
        for (auto& group : groups_at_level) {
            size_t rest = 0;
            for (size_t i = 0; i < group.first.size(); ++i) {
                if (int32_t(group.first[i] + offset) != to->groups[count].child_node_indices[i]) {
                    std::cout<<"failed at node "<<count<<" , lod "<< lod <<" , the child at index" << i <<" is different"<<std::endl;
                    std::cout<<group.first[i] + offset<<" , "<<to->groups[count].child_node_indices[i]<<std::endl;
                }
                assert(int32_t(group.first[i] + offset) == to->groups[count].child_node_indices[i]);
                rest = i + 1;
            }
            for (; rest < 8; ++rest) {
                if(to->groups[count].child_node_indices[rest] != -1) {
                    std::cout<<to->groups[count].child_node_indices[rest]<<std::endl;
                    assert(false);
                }
            }
            
            count += 1;
        }

        // ensure each cluster has the correct corresponding groups
        

        offset += uint32_t(dag.clusters[lod].size());
        lod++;
    }
    std::unordered_set<uint32_t> seen;

    count = 0;
    for (auto& node : to->clusters) {
        if(node.group_index == static_cast<uint32_t>(-1) && count>= level_offsets[1]){
            std::cout<<count<<std::endl;
            assert(false);
        }
        count++;
        if (count <= level_offsets[1]) {
            continue;
        }
        if (node.node_index == 0) {
            for (uint32_t child : to->groups[node.group_index].child_node_indices) {
                if (child == -1) break;
                auto res = seen.insert(child);
                if (!res.second) {
                    std::cout<<child<<std::endl;
                }
                assert(res.second);
            }
        }
    }
}