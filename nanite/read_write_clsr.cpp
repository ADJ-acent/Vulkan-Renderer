#include "NaniteMeshApp.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>

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
        });
    }

    header.vertices_count = uint32_t(disk_vertices.size());
    clsr_file.write(reinterpret_cast<const char *>(&header), sizeof(DiskClusterLevelHeader));
    clsr_file.write(reinterpret_cast<const char *>(disk_clusters.data()), sizeof(DiskCluster) * disk_clusters.size());
    clsr_file.write(reinterpret_cast<const char *>(disk_vertices.data()), sizeof(glm::vec3) * disk_vertices.size());

    clsr_file.close();
}

glm::vec4 RitterBoundingSphere(const std::vector<glm::vec3> &vertices, size_t begin, size_t end)
{
    if (begin >= end) return glm::vec4(0.0f); // Empty case

    // Pick an arbitrary point (first vertex in range)
    glm::vec3 p1 = vertices[begin];

    // Find farthest point from p1
    float maxDistSq = 0.0f;
    glm::vec3 p2 = p1;
    for (size_t i = begin; i < end; ++i) {
        float distSq = glm::length2(vertices[i] - p1);
        if (distSq > maxDistSq) {
            maxDistSq = distSq;
            p2 = vertices[i];
        }
    }

    // Find farthest point from p2
    maxDistSq = 0.0f;
    glm::vec3 p3 = p2;
    for (size_t i = begin; i < end; ++i) {
        float distSq = glm::length2(vertices[i] - p2);
        if (distSq > maxDistSq) {
            maxDistSq = distSq;
            p3 = vertices[i];
        }
    }

    // Compute initial bounding sphere from p2 and p3
    glm::vec3 center = (p2 + p3) * 0.5f;
    float radius = glm::distance(p2, p3) * 0.5f;
    float radiusSq = radius * radius;

    // Expand the sphere if necessary
    for (size_t i = begin; i < end; ++i) {
        glm::vec3 point = vertices[i];
        float distSq = glm::length2(point - center);

        if (distSq > radiusSq) {
            float dist = sqrt(distSq);
            float newRadius = (radius + dist) * 0.5f;
            float expandFactor = (newRadius - radius) / dist;
            center += (point - center) * expandFactor;
            radius = newRadius;
            radiusSq = radius * radius;
        }
    }

    return glm::vec4(center, radius);
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