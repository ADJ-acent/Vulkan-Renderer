#include "cluster_selection.hpp"

bool cluster_renderable(const RuntimeDAG& dag, const DiskCluster &cluster, uint32_t LOD_level, const glm::vec3& camera_position)
{
    if (LOD_level == uint32_t(dag.groups.size()) - 1) return cluster_within_tolerance(cluster, LOD_level, camera_position);
    if (!cluster_within_tolerance(cluster, LOD_level, camera_position)) return false;
    for (uint32_t group_i = 0; group_i < uint32_t(dag.groups[LOD_level].size()); ++group_i) {
        auto& group = dag.groups[LOD_level][group_i];
        // std::cout<<group.second.size()<<std::endl;
        for (uint32_t child_cluster_i : group.second) {
            // std::cout<<"lod: "<<LOD_level<< ", "<<cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], LOD_level+1, camera_position)<<std::endl;
            if (cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], LOD_level+1, camera_position)) return false;
        }
    }
    return true;
}

bool cluster_within_tolerance(const DiskCluster &cluster, uint32_t LOD_level, const glm::vec3 &camera_position)
{
    return glm::length(camera_position) > float (LOD_level) * 20;
}
