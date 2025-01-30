#include "../GLM.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
struct UnionFind {// modified from https://www.geeksforgeeks.org/introduction-to-disjoint-set-data-structure-or-union-find-algorithm/
    std::vector<int> parent;
    UnionFind(int size) {
        parent.resize(size);
        for (int i = 0; i < size; i++) {
            parent[i] = i;
        }
    }

    int find(int i) {
        if (parent[i] == i) {
            return i;
        }
      
        parent[i] = find(parent[i]);
        return parent[i];
    }

    void unite(int i, int j) {

        int irep = find(i);
        int jrep = find(j);
        parent[irep] = jrep;
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
    std::unordered_map<uint32_t, uint32_t> triangle_to_cluster;

    NaniteMeshApp(Configuration &);
    void loadGLTF(std::string gltfPath);
    void cluster(uint32_t cluster_triangle_limit);
};