#include "cluster_selection.hpp"
#include <iostream>
#include <deque>

bool cluster_renderable(const RuntimeDAG& dag, const DiskCluster &cluster, uint32_t LOD_level,
     const glm::vec3& camera_position,glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world,
    uint32_t width, uint32_t height)
{
    if (LOD_level == uint32_t(dag.groups.size()) - 1) return cluster_within_tolerance(cluster, camera_position, clip_from_view, view_from_world, width, height);
    if (!cluster_within_tolerance(cluster, camera_position, clip_from_view, view_from_world, width, height)) return false;

    auto& group = dag.groups[LOD_level][cluster.dst_cluster_group];
        
    // std::cout<<group.second.size()<<std::endl;
    for (uint32_t child_cluster_i : group.second) {
        // std::cout<<"lod: "<<LOD_level<< ", "<<cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], LOD_level+1, camera_position)<<std::endl;
        if (cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], camera_position, clip_from_view, view_from_world, width, height)) return false;
    }
    
    return true;
}

std::vector<std::pair<uint32_t, uint32_t>> get_nodes_renderable(const ClusterBVH& bvh, 
    glm::mat4& world_from_local, const glm::vec3 &camera_position,
    glm::mat4x4 &clip_from_view, glm::mat4x4 &view_from_world, uint32_t width, uint32_t height)
{
    std::deque<std::pair<uint32_t, uint32_t>> node_queue;
    for (auto& root_node : bvh.root_nodes) {
        node_queue.push_back({root_node, 0});
    }
    std::vector<std::pair<uint32_t, uint32_t>> result;
    result.reserve(bvh.clusters.size());
    while (!node_queue.empty()) {
        uint32_t node_i = node_queue.front().first;
        uint32_t lod_level = node_queue.front().second;
        node_queue.pop_front();
        // if a node is in the queue, its parent's error is too high
        // we just need to check if its own error is too high
        const ClusterBVH::Node& node = bvh.clusters[node_i];
        bool within_tolerance = cluster_within_tolerance(node, world_from_local, camera_position, clip_from_view, view_from_world, width, height);

        if (within_tolerance) {
            result.push_back({node_i, lod_level});
        }
        else if (node.node_index == 0) { // only push if the node is the first in the group
            for (uint32_t i = 0; i < 8; ++i) {
                int32_t child_node_i = bvh.groups[node.group_index].child_node_indices[i];
                if (child_node_i == -1) break;
                node_queue.push_back({child_node_i, lod_level+1});
            }
        }
    }
    return result;
}

bool cluster_within_tolerance(const DiskCluster &cluster, const glm::vec3 &camera_position,
                              glm::mat4x4 &clip_from_view, glm::mat4x4 &view_from_world, uint32_t width, uint32_t height)
{

    glm::vec3 sphere_center = glm::vec3(view_from_world * glm::vec4(glm::vec3(cluster.bounding_sphere),1));
    float sphere_radius = cluster.bounding_sphere.w;
    float d2 = glm::dot(sphere_center, sphere_center);
    float r2 = sphere_radius * sphere_radius;
    float sphere_diameter_uv = clip_from_view[0][0] * sphere_radius / sqrt(d2 - r2);
    float view_size = float(std::max(width, height));
    float sphere_diameter_pixels = sphere_diameter_uv * view_size;
    // std::cout<<LOD_level<<"," <<glm::to_string(cluster.bounding_sphere)<<std::endl;
    // std::cout<<LOD_level<<","<<sphere_diameter_pixels<<std::endl;
    return sphere_diameter_pixels * cluster.error < 25.0f;
}


bool cluster_within_tolerance(const ClusterBVH::Node &cluster, glm::mat4& world_from_local, const glm::vec3 &camera_position,
    glm::mat4x4 &clip_from_view, glm::mat4x4 &view_from_world, uint32_t width, uint32_t height)
{
    glm::vec3 sphere_center = glm::vec3(view_from_world * world_from_local * glm::vec4(glm::vec3(cluster.bounding_sphere),1));
    float sphere_radius = world_from_local[0][0] * cluster.bounding_sphere.w; // assuming the scaling is uniform
    float d2 = glm::dot(sphere_center, sphere_center);
    float r2 = sphere_radius * sphere_radius;
    float sphere_diameter_uv = clip_from_view[0][0] * sphere_radius / sqrt(d2 - r2);
    float view_size = float(std::max(width, height));
    float sphere_diameter_pixels = sphere_diameter_uv * view_size;
    // std::cout<<LOD_level<<"," <<glm::to_string(cluster.bounding_sphere)<<std::endl;
    // std::cout<<LOD_level<<","<<sphere_diameter_pixels<<std::endl;
    return sphere_diameter_pixels * cluster.error < 25.0f;
}