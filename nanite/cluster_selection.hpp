#pragma once

#include "read_cluster.hpp"
#include <stdint.h>

bool cluster_renderable(const RuntimeDAG &, const DiskCluster& cluster, uint32_t LOD_level, const glm::vec3& camera_position);

bool cluster_within_tolerance(const DiskCluster& cluster, uint32_t LOD_level, const glm::vec3& camera_position);