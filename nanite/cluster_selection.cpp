#include "cluster_selection.hpp"
#include <iostream>

bool cluster_renderable(const RuntimeDAG& dag, const DiskCluster &cluster, uint32_t LOD_level,
     const glm::vec3& camera_position,glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world,
    uint32_t width, uint32_t height)
{
    if (LOD_level == uint32_t(dag.groups.size()) - 1) return cluster_within_tolerance(cluster, LOD_level, camera_position, clip_from_view, view_from_world, width, height);
    if (!cluster_within_tolerance(cluster, LOD_level, camera_position, clip_from_view, view_from_world, width, height)) return false;

    auto& group = dag.groups[LOD_level][cluster.dst_cluster_group];
        
    // std::cout<<group.second.size()<<std::endl;
    for (uint32_t child_cluster_i : group.second) {
        // std::cout<<"lod: "<<LOD_level<< ", "<<cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], LOD_level+1, camera_position)<<std::endl;
        if (cluster_within_tolerance(dag.clusters[LOD_level+1][child_cluster_i], LOD_level+1, camera_position, clip_from_view, view_from_world, width, height)) return false;
    }
    
    return true;
}

bool cluster_within_tolerance(const DiskCluster &cluster, uint32_t LOD_level, const glm::vec3 &camera_position, 
    glm::mat4x4& clip_from_view, glm::mat4x4&view_from_world, uint32_t width, uint32_t height)
    {
    if (LOD_level == 0) return true;
    glm::vec3 sphere_center = glm::vec3(view_from_world * glm::vec4(glm::vec3(cluster.bounding_sphere),1));
    float sphere_radius = cluster.bounding_sphere.w;
    float d2 = glm::dot(sphere_center, sphere_center);
    float r2 = sphere_radius * sphere_radius;
    float sphere_diameter_uv = clip_from_view[0][0] * sphere_radius / sqrt(d2 - r2);
    float view_size = float(std::max(width, height));
    float sphere_diameter_pixels = sphere_diameter_uv * view_size;
    // std::cout<<LOD_level<<"," <<glm::to_string(cluster.bounding_sphere)<<std::endl;
    // std::cout<<LOD_level<<","<<sphere_diameter_pixels<<std::endl;
    return sphere_diameter_pixels * LOD_level < 100.0f;
}
