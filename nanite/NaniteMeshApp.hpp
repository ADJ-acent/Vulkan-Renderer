#include "../GLM.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iostream>

namespace tinygltf {
    struct Mesh;
    class Model;
    class TinyGLTF;
}

struct UnionFind {// modified from https://www.geeksforgeeks.org/introduction-to-disjoint-set-data-structure-or-union-find-algorithm/
    std::vector<uint32_t> parent;
    UnionFind(uint32_t size) {
        parent.resize(size);
        for (uint32_t i = 0; i < uint32_t(size); i++) {
            parent[i] = i;
        }
    }

    uint32_t find(uint32_t i) {
        if (parent[i] == i) {
            return i;
        }
        parent[i] = find(parent[i]);
        return parent[i];
    }

    void unite(uint32_t i, uint32_t j) {
        uint32_t irep = find(i);
        uint32_t jrep = find(j);
        parent[jrep] = irep;
    }

    bool is_original(uint32_t i) {
        return (parent[i] == i);
    }
};

struct NaniteMeshApp {
    struct Configuration {
        std::string glTF_path;
        uint32_t per_cluster_triangle_limit = 128;
        uint32_t per_merge_cluster_limit = 4;
        void parse(int argc, char **argv);
        static void usage(std::function< void(const char *, const char *) > const &callback);
    } configuration;

    struct Cluster {
        std::vector<uint32_t> triangles;  // Indices of triangles in this cluster
        std::unordered_map<uint32_t, uint32_t> shared_edges; // Neighboring clusters and shared edge count
    };

    struct MergeCandidate {
        uint32_t cluster_a;
        uint32_t cluster_b;
        uint32_t shared_edge_count;
        // Custom comparator for max heap:
        bool operator<(const MergeCandidate &other) const {
            return shared_edge_count < other.shared_edge_count;
        }
    };
    std::vector<glm::uvec3> triangles;
    std::vector<glm::vec3> vertices; // position of vertices

    std::vector<Cluster> clusters;
    UnionFind triangle_to_cluster;
    
    // std::unordered_map<uint32_t, uint32_t> triangle_to_cluster;

    NaniteMeshApp(Configuration &);
    void loadGLTF(std::string gltfPath, tinygltf::Model& model, tinygltf::TinyGLTF& loader);
    void cluster(uint32_t cluster_triangle_limit = 0);
    bool is_valid_candidate(const MergeCandidate &);
    void merge_clusters(uint32_t a, uint32_t b);
    void write_clusters_to_model(tinygltf::Model& model);
    void simplify_clusters();
    inline glm::vec3 compute_normal(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
        return glm::normalize(glm::cross(v1 - v0, v2 - v0));
    }
    glm::vec3 get_best_vertex_after_collapse(const glm::mat4& Qsum, glm::vec3 v1, glm::vec3 v2);
    void copy_offset_mesh_to_model(tinygltf::Model& model, tinygltf::Mesh& mesh, const glm::vec3& offset);
    void write_mesh_to_model(tinygltf::Model& model, std::vector<int> indices); 
    bool save_model(const tinygltf::Model& model, std::string filename);
};