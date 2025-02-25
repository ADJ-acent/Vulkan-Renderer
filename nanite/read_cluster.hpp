#pragma once

#include "../GLM.hpp"
#include <stdint.h>
#include <string>
#include <vector>

/** Disk cluster data
 *  One file per cluster level, each with format name-level.clsr
 *  In the file, we have in order:
 *      1. a 4 byte header "clsr",
 *      2. a 4 byte group count and a 4 byte cluster count
 *          (1 and 2 combines into the DiskClusterLevelHeader struct),
 *      3. vector of DiskCluster containing individual cluster information
 *          (which contains information on how to index into the child cluster 
 *           indices and vertices information, etc, see definition below),
 *      4. vector of children cluster indices,
 *      5. vector of vertex positions,
 * 
 */


 struct DiskClusterLevelHeader {
    char clsr_header[4] = {'c','l','s','r'};
    uint32_t cluster_count;
    uint32_t group_count;
    uint32_t vertices_count;
};
static_assert(sizeof(DiskClusterLevelHeader) == 16);

struct DiskCluster {
    uint32_t vertices_begin; // glm::vec3
    uint32_t vertices_count;
    int32_t src_cluster_group; // cluster group where simplification happened to generate the current cluster, -1 if base layer
    int32_t dst_cluster_group; // cluster group made partly from the current cluster, then simplified to generate next level, -1 if top layer
    glm::vec4 bounding_sphere; // xyz, radius
};


struct RuntimeDAG {
    /** Note:
     *  Notion of parent for a given cluster are the clusters that were simplied to derive the current clusters
     */
    // at each LOD we have vector of < vector of parents of the group, vector of children of the group> for every group
    std::vector<std::vector<std::pair<std::vector<uint32_t>, std::vector<uint32_t>>>> groups; 
    // 0 is the base level
    std::vector<std::vector<DiskCluster>> clusters;
    std::vector<std::vector<glm::vec3>> vertices;
    std::vector<std::vector<uint8_t>> color_index;
};

/**
 * clsr files should be saved in order, for example: result_0.clsr, result_1.clsr, etc. where 0 is LOD level 0
 * the input file path for the given example above would be "result"
 */
void read_clsr(std::string file_path, RuntimeDAG* to, bool debug = false);