#pragma once

#include "read_cluster.hpp"
#include <stdint.h>

bool cluster_renderable(const RuntimeDAG& dag, const DiskCluster &cluster, uint32_t LOD_level,
    const glm::vec3& camera_position, glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world,
    uint32_t width, uint32_t height);

bool cluster_within_tolerance(const DiskCluster &cluster, uint32_t LOD_level, const glm::vec3 &camera_position, 
    glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world, uint32_t width, uint32_t height);