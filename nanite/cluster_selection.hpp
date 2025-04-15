#pragma once

#include "read_cluster.hpp"
#include <stdint.h>

// used to traverse through all the clusters and check if the cluster should be rendered based on the parent
bool cluster_renderable(const RuntimeDAG& dag, const DiskCluster &cluster, uint32_t LOD_level,
    const glm::vec3& camera_position, glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world,
    uint32_t width, uint32_t height);

// used to traverse through the BVH and returns a vector of clusters that should be rendered, the second uint32 is the LOD of the cluster
std::vector<std::pair<uint32_t, uint32_t>> get_nodes_renderable(const ClusterBVH& bvh, 
    glm::mat4& world_from_local, const glm::vec3 &camera_position,
    glm::mat4x4 &clip_from_view, glm::mat4x4 &view_from_world, uint32_t width, uint32_t height);
 
bool cluster_within_tolerance(const DiskCluster &cluster, const glm::vec3 &camera_position, 
    glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world, uint32_t width, uint32_t height);

bool cluster_within_tolerance(const ClusterBVH::Node &cluster, glm::mat4& world_from_local, const glm::vec3 &camera_position, 
    glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world, uint32_t width, uint32_t height);