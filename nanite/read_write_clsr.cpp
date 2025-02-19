#include "NaniteMeshApp.hpp"
#include <iostream>
#include <fstream>

void write_clsr(std::string save_path, uint32_t lod_level, 
    std::vector<NaniteMeshApp::Cluster> &clusters, 
    std::vector<NaniteMeshApp::ClusterGroup> &groups, 
    std::vector<glm::uvec3> &triangles, 
    std::vector<glm::vec3> &vertices)
{
    std::string clsr_path = save_path + "-" + std::to_string(lod_level) + ".clsr";
    std::ofstream clsr_file(clsr_path, std::ios::binary);
    assert(clsr_file.is_open());
    DiskClusterLevelHeader header;
    assert(clusters.size() != 0);
    header.cluster_count = uint32_t(clusters.size());
    clsr_file.write(reinterpret_cast<const char *>(&header), sizeof(DiskClusterLevelHeader));

    //no groups, so must be base level
    if (groups.size() == 0) {
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
            });
        }
        clsr_file.write(reinterpret_cast<const char *>(disk_clusters.data()), sizeof(DiskCluster) * disk_clusters.size());
        clsr_file.write(reinterpret_cast<const char *>(disk_vertices.data()), sizeof(glm::vec3) * disk_vertices.size());
    }
    // for (auto& group : groups) {
        
    // }

    clsr_file.close();
}