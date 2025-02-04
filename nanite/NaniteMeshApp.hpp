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

    void swap(uint32_t i, uint32_t j) {

        for (uint32_t& p : parent) {
            if (p == i) p = j;
            else if (p == j) p = i;
        }
    }
};

struct NaniteMeshApp {
    struct Configuration {
        std::string glTF_path;
        void parse(int argc, char **argv);
        static void usage(std::function< void(const char *, const char *) > const &callback);
    } configuration;



    std::unordered_map<glm::uvec2, uint32_t> next_vertex;
    std::vector<glm::uvec3> triangles;
    std::vector<glm::vec3> vertices; // position of vertices


    struct Cluster {
        std::unordered_set<uint32_t> triangles;  // Indices of triangles in this cluster
        std::unordered_map<uint32_t, uint32_t> shared_edges; // Neighboring clusters and shared edge count
    };
    std::vector<Cluster> clusters;
    UnionFind triangle_to_cluster;
    
    // std::unordered_map<uint32_t, uint32_t> triangle_to_cluster;

    NaniteMeshApp(Configuration &);
    void loadGLTF(std::string gltfPath, tinygltf::Model& model, tinygltf::TinyGLTF& loader);
    void cluster(uint32_t cluster_triangle_limit);
    void copy_offset_mesh_to_model(tinygltf::Model& model, tinygltf::Mesh& mesh, const glm::vec3& offset);
    void write_mesh_to_model(tinygltf::Model& model, std::vector<int> indices); 
    bool save_model(const tinygltf::Model& model, std::string filename);
};