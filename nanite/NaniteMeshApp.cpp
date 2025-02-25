#include "NaniteMeshApp.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "tiny_gltf.h"
#include <queue>

std::vector<NaniteMeshApp::Cluster> NaniteMeshApp::clusters = std::vector<Cluster>();
std::vector<NaniteMeshApp::ClusterGroup> NaniteMeshApp::current_cluster_group = std::vector<ClusterGroup>();

void NaniteMeshApp::Configuration::parse(int argc, char **argv) {
    
	for (int argi = 1; argi < argc; ++argi) {
        auto conv = [&](std::string const &what) {
            argi += 1;
            std::string val = argv[argi];
            for (size_t i = 0; i < val.size(); ++i) {
                if (val[i] < '0' || val[i] > '9') {
                    throw std::runtime_error("--drawing-size " + what + " should match [0-9]+, got '" + val + "'.");
                }
            }
            return std::stoul(val);
        };
		std::string arg = argv[argi];
		if (arg == "--path") {
			if (argi + 1 >= argc) throw std::runtime_error("--path requires a parameter (a .glTF path).\n");
			argi += 1;
			glTF_path = argv[argi];
		}
        else if (arg == "--cluster-limit") {
            if (argi + 1 >= argc) throw std::runtime_error("--cluster-limit requires a parameter (max triangle count).\n");
			
			per_cluster_triangle_limit = conv("max triangle count");
        }
        else if (arg == "--cluster-group") {
            if (argi + 1 >= argc) throw std::runtime_error("--cluster-group requires a parameter (max number of cluster per group).\n");
			
			per_merge_cluster_limit = conv("max cluster count per group");
        }
		else if (arg == "--force-loop") {
            if (argi + 1 >= argc) throw std::runtime_error("--force-loop requires a parameter (number of forced simplification loops)).\n");
			
			simplify_count = conv("number of simplification loops");
        }
        else if (arg == "--save-folder") {
            if (argi + 1 >= argc) throw std::runtime_error("--save-folder requires a parameter (folder name to store .clsr).\n");
        }
	}
	if (glTF_path == "") {
		throw std::runtime_error("must provide a glTF path with --path!\n");
	}
}

void NaniteMeshApp::Configuration::usage(std::function< void(const char *, const char *) > const &callback) {	
	//callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--path <p>", "Loads the glTF file at path <p>");
    callback("--save-folder <p>", "Saves result in folder <p>");
    callback("--cluster-limit <t>", "Limits the maximum number of triangles in a cluster to be <t>, default <t> = 128");
    callback("--cluster-group <c>", "Limits the maximum number of clusters when merging and splitting to <c>, default <c> = 4");
    callback("--force-loop <l>", "Force the number of simplification loops and output meshes");
}

NaniteMeshApp::NaniteMeshApp(Configuration & configuration_) :
	configuration(configuration_), triangle_to_cluster(0)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	loadGLTF(configuration.glTF_path, model, loader);
    clusters = cluster(triangles);
    for (uint32_t i = 0; i < configuration.simplify_count; ++i) {
        group();

        write_clsr(configuration.save_folder, i, clusters, current_cluster_group, triangles, vertices);
        simplify_cluster_groups();
        
        cluster_in_groups();
        // write_clusters_to_model(model);
        // save_groups_as_clusters(model, i);
        // save_model(model, std::string("../gltf/test_" + std::to_string(i)));
    }	
}

void NaniteMeshApp::loadGLTF(std::string gltfPath, tinygltf::Model& model, tinygltf::TinyGLTF& loader)
{
	
	std::string err;
	std::string warn;

	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, gltfPath);

	if (!warn.empty()) {
		printf("Warn: %s\n", warn.c_str());
	}

	if (!err.empty()) {
		printf("Err: %s\n", err.c_str());
	}

	if (!ret) {
		printf("Failed to parse glTF\n");
	}

	// assert(model.meshes.size() == 1 && "Currently only support single mesh");
	for (auto mesh : model.meshes) {

		for (auto primitive : mesh.primitives) {
			
			// referenced https://github.com/syoyo/tinygltf/wiki/Accessing-vertex-data
			const tinygltf::Accessor& accessor = model.accessors[primitive.attributes["POSITION"]];
			const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
			const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];
			// bufferView byteoffset + accessor byteoffset tells you where the actual position data is within the buffer. From there
			// you should already know how the data needs to be interpreted.
			// const float* positionsf = reinterpret_cast<const float*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
			const glm::vec3* positions = reinterpret_cast<const glm::vec3*>(&buffer.data[bufferView.byteOffset + accessor.byteOffset]);
			vertices.clear();
			// From here, you choose what you wish to do with this position data. In this case, we  will display it out.
			// for (size_t i = 0; i < accessor.count; ++i) {
			// 	// Positions are Vec3 components, so for each vec3 stride, offset for x, y, and z.
			// 	std::cout << "(" << positions[i * 3 + 0] << ", "// x
			// 					<< positions[i * 3 + 1] << ", " // y
            //                 << positions[i * 3 + 2] << ")" // z
            //                 << "\n";
			// }
			if (primitive.indices >= 0) { 
				const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
				const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
				const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

				// Pointer to index buffer
				const void* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
				
				// Read indices correctly based on componentType
				std::vector<uint32_t> indices;
				if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
					const uint8_t* indices8 = static_cast<const uint8_t*>(indexData);
					indices.assign(indices8, indices8 + indexAccessor.count);
				} 
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
					const uint16_t* indices16 = static_cast<const uint16_t*>(indexData);
					indices.assign(indices16, indices16 + indexAccessor.count);
				} 
				else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
					const uint32_t* indices32 = static_cast<const uint32_t*>(indexData);
					indices.assign(indices32, indices32 + indexAccessor.count);
				} 
				else {
					std::cerr << "Unsupported index type" << std::endl;
					continue;
				}
				
				//referenced https://github.com/15-466/15-466-f24-base5/blob/main/WalkMesh.cpp
				
				// Iterate over triangles
				assert(indices.size() % 3 == 0);
				std::unordered_map<glm::vec3, uint32_t> seen;
                std::unordered_map<glm::uvec2, uint32_t> next_vertex;
				for (size_t i = 0; i < indices.size(); i += 3) {
					uint32_t i0 = indices[i];
					uint32_t i1 = indices[i + 1];
					uint32_t i2 = indices[i + 2];
					auto do_next = [&](uint32_t a, uint32_t b, uint32_t c) {
						auto ret = next_vertex.insert(std::make_pair(glm::uvec2(a,b), c));
						assert(ret.second);
						// if (!ret.second) {
						// 	count ++;
						// 	std::cout<<"error on "<<i <<std::endl;
						// }
					};
					do_next(i0,i1,i2);
					do_next(i1,i2,i0);
					do_next(i2,i0,i1);
					auto get_vertex_index = [&](glm::vec3& v0) {
						// auto it = std::find_if(vertices.begin(), vertices.end(), [&](const glm::vec3& v) {
						// 	return glm::distance(v0,v) <= 0.0001f;
						// });
						
						// if (it != vertices.end()) return uint32_t(it - vertices.begin());
						// else {
						// 	vertices.emplace_back(v0);
						// 	return uint32_t(vertices.size() - 1);
						// }
						auto it = seen.find(v0);
						if (it != seen.end()) {
							return it->second;
						}
						else {
							vertices.emplace_back(v0);
							uint32_t index = uint32_t(vertices.size()-1);
							seen.emplace(v0,index);
							return index;
						}
					};
					glm::vec3 v0 = positions[i0];
					glm::vec3 v1 = positions[i1];
					glm::vec3 v2 = positions[i2];
					triangles.emplace_back(glm::uvec3(get_vertex_index(v0),get_vertex_index(v1),get_vertex_index(v2)));

						// std::cout << "  v0: (" << glm::to_string(positions[i0] ) << ")\n";
						// std::cout << "  v1: (" << glm::to_string(positions[i1] ) <<  ")\n";
						// std::cout << "  v2: (" << glm::to_string(positions[i2] ) <<  ")\n";
					

				}
			}
			std::cout<<"Finished Loading gltf, total number of vertices: "<<vertices.size()<<std::endl;
		}
	}
	
}

// step 1 of preprocessing
std::vector<NaniteMeshApp::Cluster> NaniteMeshApp::cluster(std::vector<glm::uvec3> source_triangles, uint32_t cluster_triangle_limit)
{
    if (cluster_triangle_limit == 0) {
        cluster_triangle_limit = configuration.per_cluster_triangle_limit;
    }
    std::vector<Cluster> result_clusters;
	result_clusters.clear();
    triangle_to_cluster = UnionFind(static_cast<uint32_t>(source_triangles.size()));

    // Initialize each triangle as its own cluster
    for (uint32_t i = 0; i < source_triangles.size(); ++i) {
        result_clusters.push_back({ { i }, {} });
		result_clusters[i].shared_edges.clear();
    }

    // Build shared edge counts
    std::unordered_map<glm::uvec2, uint32_t> edge_to_triangle;
    for (uint32_t i = 0; i < source_triangles.size(); ++i) {
        const glm::uvec3& triangle = source_triangles[i];
        for (int j = 0; j < 3; ++j) {
            glm::uvec2 edge = { triangle[j], triangle[(j + 1) % 3] };
            glm::uvec2 reversed_edge = { triangle[(j + 1) % 3], triangle[j] };

            if (edge_to_triangle.find(reversed_edge) != edge_to_triangle.end()) {
                uint32_t neighbor_cluster = edge_to_triangle[reversed_edge];
                result_clusters[i].shared_edges[neighbor_cluster]++;
                result_clusters[neighbor_cluster].shared_edges[i]++;
            } else {
                edge_to_triangle[edge] = i;
            }
        }
    }

    // Build priority queue of merge candidates
    std::priority_queue<MergeCandidate> merge_heap;
    for (uint32_t i = 0; i < result_clusters.size(); ++i) {
        for (const auto& [neighbor, edge_count] : result_clusters[i].shared_edges) {
            if (neighbor > i) {
                merge_heap.push({ i, neighbor, edge_count });
            }
        }
    }

    uint32_t loop_count = 0;
    std::cout << "Start Clustering... "<< "Total number of clusters to start: " << result_clusters.size()  << std::endl;
    while (!merge_heap.empty() && result_clusters.size() > 1) {
        if (loop_count % 10000 == 0) {
            std::cout << "\tClustering... "<< "loop: " << loop_count  << std::endl;
        }
        loop_count++;

        // Get best merge candidate
        MergeCandidate candidate = merge_heap.top();
        merge_heap.pop();

        // Validate candidate
        if (!is_valid_merge_candidate(candidate, result_clusters)) continue;

        // Ensure we don't exceed triangle limit
        if (result_clusters[candidate.cluster_a].triangles.size() + result_clusters[candidate.cluster_b].triangles.size() > cluster_triangle_limit) {
            continue;
        }
        // Merge clusters
        merge_clusters(candidate.cluster_a, candidate.cluster_b, result_clusters);

        // Update neighbors
        for (const auto& [neighbor, edge_count] : result_clusters[candidate.cluster_b].shared_edges) {
            if (neighbor == candidate.cluster_a) continue; // Skip self

            result_clusters[candidate.cluster_a].shared_edges[neighbor] += edge_count;
            result_clusters[neighbor].shared_edges[candidate.cluster_a] += edge_count;
            merge_heap.push({ candidate.cluster_a, neighbor, result_clusters[candidate.cluster_a].shared_edges[neighbor] });
        }
    }
	
	for (int32_t i = int32_t(result_clusters.size()) - 1; i > -1; --i) {
        if (i % 10000 == 0)
        std::cout<<"\tRemoving merged clusters... "<<i<<" clusters left to check.\n";
		if (!triangle_to_cluster.is_original(i)) {
			result_clusters.erase(result_clusters.begin() + i);
		}
	}
    std::cout << "Merging final loop count: " << loop_count << ", remaining clusters: " << result_clusters.size() << std::endl;
    return result_clusters;
}


void NaniteMeshApp::cluster_in_groups()
{
    std::vector<Cluster> new_clusters;
    new_clusters.clear();
    new_clusters.reserve(clusters.size());
    for (uint32_t group_i = 0; group_i < uint32_t(current_cluster_group.size()); ++group_i) {
        ClusterGroup& cluster_group = current_cluster_group[group_i];
        std::vector<uint32_t> new_cluster_indices_in_group;
        new_cluster_indices_in_group.reserve(cluster_group.clusters.size());
        uint32_t start_cluster_index_in_group = uint32_t(new_clusters.size());
        std::vector<glm::uvec3> group_triangles;
        std::vector<uint32_t> group_triangle_indices;
        group_triangles.reserve(triangles.size());
        group_triangle_indices.reserve(triangles.size());
        for (uint32_t cluster_i : cluster_group.clusters) {
            Cluster& cluster = clusters[cluster_i];
            for (uint32_t triangle_i : cluster.triangles) {
                group_triangles.push_back(triangles[triangle_i]);
                group_triangle_indices.push_back(triangle_i);
            }
            new_cluster_indices_in_group.push_back(start_cluster_index_in_group);
            start_cluster_index_in_group += 1;
        }
        std::vector<Cluster> group_clusters = cluster(group_triangles,
            std::max(int(group_triangles.size() / cluster_group.clusters.size() * 5),128));
        for (Cluster& cluster : group_clusters) {
            for (uint32_t& triangle_i : cluster.triangles) {
                triangle_i = group_triangle_indices[triangle_i];
            }
            cluster.src_cluster_group = group_i;
        }
        new_clusters.insert(new_clusters.end(),group_clusters.begin(), group_clusters.end());
        cluster_group.clusters = new_cluster_indices_in_group;
    }
    clusters = new_clusters;
}

// group merged clusters
void NaniteMeshApp::group(){
    std::priority_queue<GroupCandidate> group_heap;
    std::unordered_map<glm::uvec2, uint32_t> edge_to_cluster;
    for (uint32_t cluster_i = 0; cluster_i < clusters.size(); ++cluster_i) {
        clusters[cluster_i].shared_edges.clear();
        for (uint32_t i : clusters[cluster_i].triangles) {
            const glm::uvec3& triangle = triangles[i];

            for (int j = 0; j < 3; ++j) {
                glm::uvec2 edge = { triangle[j], triangle[(j + 1) % 3] };
                glm::uvec2 reversed_edge = { triangle[(j + 1) % 3], triangle[j] };

                if (edge_to_cluster.find(reversed_edge) != edge_to_cluster.end()) {
                    uint32_t neighbor_cluster = edge_to_cluster[reversed_edge];
                    if (neighbor_cluster == cluster_i) continue;
                    clusters[cluster_i].shared_edges[neighbor_cluster]++;
                    clusters[neighbor_cluster].shared_edges[cluster_i]++;
                } else {
                    edge_to_cluster[edge] = cluster_i;
                }
            }

        }
    }
    for (uint32_t i = 0; i < clusters.size(); ++i) {
        for (const auto& [neighbor, edge_count] : clusters[i].shared_edges) {
            if (neighbor > i && neighbor < clusters.size()) {
                group_heap.push({ i, neighbor, edge_count });
            }
        }
    }

    uint32_t loop_count = 0;

    current_cluster_group.clear();
    // Initialize each cluster as its own group
    for (uint32_t i = 0; i < clusters.size(); ++i) {
        current_cluster_group.push_back({ { i }, clusters[i].shared_edges });
    }
    UnionFind cluster_to_group = UnionFind(static_cast<uint32_t>(current_cluster_group.size()));


    while (!group_heap.empty() && current_cluster_group.size() > 1) {
        if (loop_count % 500 == 0) {
            std::cout << "\tGrouping clusters... "<< "loop: " << loop_count  << std::endl;
        }
        loop_count++;
        // Get best merge candidate
        GroupCandidate candidate = group_heap.top();
        group_heap.pop();
        // Validate candidate
        if (!is_valid_group_candidate(candidate, cluster_to_group)) continue;

        // Ensure we don't exceed group size limit
        if (current_cluster_group[candidate.group_a].clusters.size() +
            current_cluster_group[candidate.group_b].clusters.size() 
            > configuration.per_merge_cluster_limit) {
            continue;
        }

        {// Group clusters
            cluster_to_group.unite(candidate.group_a, candidate.group_b);
            // Move triangles from b to a
            current_cluster_group[candidate.group_a].clusters.insert(
                current_cluster_group[candidate.group_a].clusters.end(),
                current_cluster_group[candidate.group_b].clusters.begin(),
                current_cluster_group[candidate.group_b].clusters.end()
            );

            // Remove shared_edges[b] since b no longer exists
            current_cluster_group[candidate.group_a].shared_edges.erase(candidate.group_b);
        }


        // Update neighbors
        for (const auto& [neighbor, edge_count] : current_cluster_group[candidate.group_b].shared_edges) {
            if (neighbor == candidate.group_a) continue; // Skip self
            if (neighbor > current_cluster_group.size()) continue;
            current_cluster_group[candidate.group_a].shared_edges[neighbor] += edge_count;
            current_cluster_group[neighbor].shared_edges[candidate.group_a] += edge_count;

            group_heap.push({ candidate.group_a, neighbor, current_cluster_group[candidate.group_a].shared_edges[neighbor] });
        }
    }
    for (int32_t i = int32_t(current_cluster_group.size()) - 1; i > -1; --i) {
		if (!cluster_to_group.is_original(i)) {
			current_cluster_group.erase(current_cluster_group.begin() + i);
		}
	}
    for (uint32_t i = 0; i < uint32_t(current_cluster_group.size()); ++i) {
        // assign source cluster group for storage
        for (uint32_t cluster_index : current_cluster_group[i].clusters) {
            clusters[cluster_index].dst_cluster_group = i;
        }
    }
    
    std::cout << "Grouping final loop count: " << loop_count << ", total grouped clusters: " << current_cluster_group.size() << std::endl;
}

void NaniteMeshApp::save_groups_as_clusters(const tinygltf::Model& model, uint32_t level)
{
    std::vector<Cluster> clusters_from_groups;
    clusters_from_groups.reserve(clusters.size());
    for (ClusterGroup& group : current_cluster_group) {
        Cluster group_cluster;
        for (uint32_t cluster_i : group.clusters) {
            Cluster& cluster = clusters[cluster_i];
            group_cluster.triangles.insert(group_cluster.triangles.end(), cluster.triangles.begin(), cluster.triangles.end());
        }
        clusters_from_groups.push_back(group_cluster);
    }
    std::vector<Cluster> temp = clusters;
    clusters = clusters_from_groups;
    save_model(model, std::string("../gltf/test_" + std::to_string(level)));
    clusters = temp;
}

bool NaniteMeshApp::is_valid_merge_candidate(const MergeCandidate &candidate, std::vector<Cluster>& result_clusters)
{
    uint32_t root_a = triangle_to_cluster.find(candidate.cluster_a);
    uint32_t root_b = triangle_to_cluster.find(candidate.cluster_b);
    return (root_a == candidate.cluster_a && root_b == candidate.cluster_b &&
        result_clusters[root_a].shared_edges[root_b] == candidate.shared_edge_count &&
        result_clusters[root_b].shared_edges[root_a] == candidate.shared_edge_count);
}

void NaniteMeshApp::check_clusters_validity()
{
    std::unordered_map<glm::uvec2, uint32_t> next_vertex_in_cluster;
    auto do_next = [&](uint32_t a, uint32_t b, uint32_t c) {
        auto ret = next_vertex_in_cluster.insert(std::make_pair(glm::uvec2(a, b), c));
        assert(ret.second);
    };

    std::unordered_set<uint32_t> boundary_vertices;
    // Construct half-edge connectivity and detect boundaries
    for (Cluster& cluster : clusters) {
        for (uint32_t& triangle_index : cluster.triangles) {
            glm::uvec3 vertex_indices = triangles[triangle_index];
            uint32_t i0 = vertex_indices[0];
            uint32_t i1 = vertex_indices[1];
            uint32_t i2 = vertex_indices[2];

            do_next(i0, i1, i2);
            do_next(i1, i2, i0);
            do_next(i2, i0, i1);

        }
    }

}

bool NaniteMeshApp::is_valid_group_candidate(const GroupCandidate &candidate, UnionFind &cluster_to_group)
{
    uint32_t root_a = cluster_to_group.find(candidate.group_a);
    uint32_t root_b = cluster_to_group.find(candidate.group_b);
    return (root_a == candidate.group_a && root_b == candidate.group_b &&
            current_cluster_group[root_a].shared_edges[root_b] == candidate.shared_edge_count &&
			current_cluster_group[root_b].shared_edges[root_a] == candidate.shared_edge_count);
}

void NaniteMeshApp::merge_clusters(uint32_t a, uint32_t b, std::vector<Cluster>& result_clusters) {
    triangle_to_cluster.unite(a, b);
    // Move triangles from b to a
    result_clusters[a].triangles.insert(result_clusters[a].triangles.end(),
        result_clusters[b].triangles.begin(),
        result_clusters[b].triangles.end());

    // Remove shared_edges[b] since b no longer exists
    result_clusters[a].shared_edges.erase(b);

}

void NaniteMeshApp::write_clusters_to_model(tinygltf::Model& model)
{
    model = tinygltf::Model(); // Reset model

    std::vector<uint8_t> index_buffer;
    std::vector<tinygltf::Mesh> meshes;

    size_t global_index_offset = 0;

    // Convert global vertex data into a GLTF buffer
    std::vector<uint8_t> vertex_buffer(vertices.size() * sizeof(glm::vec3));
    memcpy(vertex_buffer.data(), vertices.data(), vertex_buffer.size());

    tinygltf::Accessor vertex_accessor;
    vertex_accessor.bufferView = 0;
    vertex_accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    vertex_accessor.type = TINYGLTF_TYPE_VEC3;
    vertex_accessor.count = vertices.size();
    model.accessors.push_back(vertex_accessor);
    for (size_t cluster_id = 0; cluster_id < clusters.size(); ++cluster_id) {
        Cluster& cluster = clusters[cluster_id];

        std::vector<uint32_t> cluster_indices;

        // Use global vertex indices directly
        for (uint32_t triangle_index : cluster.triangles) {
            glm::uvec3 triangle = triangles[triangle_index];
		
            cluster_indices.push_back(triangle.x);
            cluster_indices.push_back(triangle.y);
            cluster_indices.push_back(triangle.z);
        }

        if (cluster_indices.empty()) continue; // Skip empty clusters

        // Convert cluster indices to raw bytes
        size_t index_start = index_buffer.size();
        index_buffer.insert(index_buffer.end(),
                            reinterpret_cast<const uint8_t*>(cluster_indices.data()),
                            reinterpret_cast<const uint8_t*>(cluster_indices.data() + cluster_indices.size()));


        // Create a GLTF mesh for this cluster
        tinygltf::Mesh mesh;
        mesh.name = "Cluster_" + std::to_string(cluster_id);

        tinygltf::Primitive primitive;
        primitive.indices = static_cast<int>(model.accessors.size()); // Index accessor
        primitive.attributes["POSITION"] = 0; // All meshes share the same vertex buffer
        primitive.mode = TINYGLTF_MODE_TRIANGLES;

        mesh.primitives.push_back(primitive);
        meshes.push_back(mesh);

        // Create index accessor
        tinygltf::Accessor index_accessor;
        index_accessor.bufferView = 1;
        index_accessor.byteOffset = static_cast<int>(index_start);
        index_accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
        index_accessor.count = static_cast<int>(cluster_indices.size());
        index_accessor.type = TINYGLTF_TYPE_SCALAR;

        model.accessors.push_back(index_accessor);

        // Update global index offset
        global_index_offset += cluster_indices.size();
    }

    // Create a single buffer containing all cluster index data
    tinygltf::Buffer shared_vertex_buffer, shared_index_buffer;
    shared_vertex_buffer.data = vertex_buffer;
    shared_index_buffer.data = index_buffer;

    model.buffers.push_back(shared_vertex_buffer);
    model.buffers.push_back(shared_index_buffer);

    // Create buffer views for shared buffers
    tinygltf::BufferView vertex_view, index_view;
    vertex_view.buffer = 0;
    vertex_view.byteOffset = 0;
    vertex_view.byteLength = static_cast<int>(vertex_buffer.size());
    vertex_view.byteStride = sizeof(glm::vec3);
    vertex_view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
	vertex_view.name = "Vertex View";

    index_view.buffer = 1;
    index_view.byteOffset = 0;
    index_view.byteLength = static_cast<int>(index_buffer.size());

    index_view.byteStride = sizeof(uint32_t);
    index_view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
	index_view.name = "Index View";

    model.bufferViews.push_back(vertex_view);
    model.bufferViews.push_back(index_view);

    // Add meshes to model
    for (auto& mesh : meshes) {
        model.meshes.push_back(mesh);
    }

    // Assign meshes to a single scene
    tinygltf::Scene scene;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        tinygltf::Node node;
        node.translation = {0.0, 0.0, 0.0};
        node.rotation = {0.0, 0.0, 0.0, 1.0};
        node.scale = {1.0, 1.0, 1.0};
        node.mesh = static_cast<int>(i);
        model.nodes.push_back(node);
        scene.nodes.push_back(static_cast<int>(i));
    }
    model.scenes.push_back(scene);
    model.defaultScene = 0;
}

void NaniteMeshApp::simplify_cluster_groups()
{
    uint32_t cluster_count = 0;

    for (const ClusterGroup& cluster_group : current_cluster_group) {

        if (cluster_count % 100 == 0) {
            std::cout<<"\tsimplifying cluster groups: "<<cluster_count<<" / "<<current_cluster_group.size()<<std::endl;
        }
        cluster_count++;
        std::unordered_map<glm::uvec2, uint32_t> next_vertex_in_cluster;
        auto do_next = [&](uint32_t a, uint32_t b, uint32_t c) {
            auto ret = next_vertex_in_cluster.insert(std::make_pair(glm::uvec2(a, b), c));
            // assert(ret.second);
        };

        std::unordered_set<uint32_t> boundary_vertices;
        std::unordered_map<uint32_t, glm::mat4> quadrics;  // Stores error matrices for vertices
        std::vector<uint32_t> group_triangles;

        // Construct half-edge connectivity and detect boundaries
        for (uint32_t cluster_index : cluster_group.clusters) {
            Cluster& cluster = clusters[cluster_index];
            for (uint32_t& triangle_index : cluster.triangles) {
                const glm::uvec3& vertex_indices = triangles[triangle_index];
                uint32_t i0 = vertex_indices[0];
                uint32_t i1 = vertex_indices[1];
                uint32_t i2 = vertex_indices[2];
    
                do_next(i0, i1, i2);
                do_next(i1, i2, i0);
                do_next(i2, i0, i1);
    
            }
            group_triangles.insert(group_triangles.end(), 
                cluster.triangles.begin(), 
                cluster.triangles.end()
            );
        }

        for (uint32_t& triangle_index : group_triangles) {

            const glm::uvec3& vertex_indices = triangles[triangle_index];
            uint32_t i0 = vertex_indices[0];
            uint32_t i1 = vertex_indices[1];
            uint32_t i2 = vertex_indices[2];

            // Check boundary edges
            if (next_vertex_in_cluster.find(glm::uvec2(i1, i0)) == next_vertex_in_cluster.end()) boundary_vertices.insert(i0);
            if (next_vertex_in_cluster.find(glm::uvec2(i2, i1)) == next_vertex_in_cluster.end()) boundary_vertices.insert(i1);
            if (next_vertex_in_cluster.find(glm::uvec2(i0, i2)) == next_vertex_in_cluster.end()) boundary_vertices.insert(i2);

            {// Compute quadrics per vertex
                glm::vec3 v0 = vertices[i0];
                glm::vec3 v1 = vertices[i1];
                glm::vec3 v2 = vertices[i2];

                // Compute plane equation ax + by + cz + d = 0
                glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                float d = -glm::dot(normal, v0);
                glm::vec4 plane = glm::vec4(normal, d);

                // Compute quadric matrix Q = P * P^T
                glm::mat4 Q = glm::outerProduct(plane, plane);

                // Accumulate quadric for each vertex
                quadrics[i0] += Q;
                quadrics[i1] += Q;
                quadrics[i2] += Q;
            }
            
        }

        // Build priority queue of non-boundary vertex pairs
        struct QEMEntry {
            uint32_t v1, v2;
            glm::vec3 best_position;
            float error;
            bool operator<(const QEMEntry& other) const { return error > other.error; }
        };
        std::priority_queue<QEMEntry> heap;

        for (const auto& edge : next_vertex_in_cluster) {
            uint32_t v1 = edge.first.x, v2 = edge.first.y;
            if (boundary_vertices.count(v1) && boundary_vertices.count(v2)) continue; // Skip boundary edges

            // Compute contraction cost using quadrics
            glm::mat4 Qsum = quadrics[v1] + quadrics[v2];
            // Extract the upper-left 3x3 block for A
            glm::mat3 A = glm::mat3(Qsum);

            // Extract b as the negation of the upper-right 3x1 column
            glm::vec3 b = -glm::vec3(Qsum[3]); 

            glm::vec3 x;
            if (boundary_vertices.count(v1)) {
                x = vertices[v1];
            }
            else if (boundary_vertices.count(v2)) {
                x = vertices[v2];
            }
            else {
                // Solve for x: A * x = b or get the best position if A is singular
                x = get_best_vertex_after_collapse(Qsum, vertices[v1], vertices[v2]);
            }

            // Extract c (bottom-right scalar)
            float c = Qsum[3][3];

            // Compute the error: Q(v) = -b^T * A^-1 * b + c
            float error = -glm::dot(b, x) + c;

            heap.push({v1, v2, x, error});
        }

        // Edge collapse
        while (!heap.empty()) {
            QEMEntry entry = heap.top();
            heap.pop();

            uint32_t v1 = entry.v1, v2 = entry.v2;
            assert (!boundary_vertices.count(v1) || !boundary_vertices.count(v2));

            
            bool normal_flip_detected = false;
            std::vector<QEMEntry> flip_normal_entries;
            do { // Check normals of adjacent triangles
                normal_flip_detected = false;
                for (uint32_t tri_idx : group_triangles) {
                    const glm::uvec3& t = triangles[tri_idx];

                    if (t.x == v1 || t.y == v1 || t.z == v1 || t.x == v2 || t.y == v2 || t.z == v2) {
                        glm::vec3 v0 = vertices[t.x];
                        glm::vec3 v1_pos = vertices[t.y];
                        glm::vec3 v2_pos = vertices[t.z];

                        glm::vec3 original_normal = compute_normal(v0, v1_pos, v2_pos);

                        // Temporarily replace v2 with best_position to simulate the collapse
                        glm::vec3 collapsed_v1_pos = (t.x == v2) ? entry.best_position : vertices[t.x];
                        glm::vec3 collapsed_v2_pos = (t.y == v2) ? entry.best_position : vertices[t.y];
                        glm::vec3 collapsed_v3_pos = (t.z == v2) ? entry.best_position : vertices[t.z];

                        glm::vec3 new_normal = compute_normal(collapsed_v1_pos, collapsed_v2_pos, collapsed_v3_pos);

                        // If dot product is negative, normal flips
                        if (glm::dot(original_normal, new_normal) <= 0) {
                            normal_flip_detected = true;
                            flip_normal_entries.push_back(entry);

                            break;
                        }
                    }
                }
                if (normal_flip_detected) {

                    entry = heap.top();
                    heap.pop();
                }
            } while (normal_flip_detected && !heap.empty());
            if (normal_flip_detected && heap.empty()) {
                // we ran out of edges to collapse without flipping normals
                break;
            }

            // push back all the entries
            for (QEMEntry const popped_entry : flip_normal_entries) {
                heap.push(popped_entry);
            }

            v1 = entry.v1, v2 = entry.v2;
            if (boundary_vertices.count(v1) && boundary_vertices.count(v2)) continue; 
            // Update vertex positions
            vertices[v1] = entry.best_position;
            vertices[v2] = entry.best_position;
            
            quadrics[v1] += quadrics[v2]; // Merge quadrics

            // Update triangles: replace v2 with v1 where applicable
            for (uint32_t& tri_idx : group_triangles) {
                glm::uvec3& t = triangles[tri_idx];
                if (t.x == v2) t.x = v1;
                if (t.y == v2) t.y = v1;
                if (t.z == v2) t.z = v1;
            }

            // Remove degenerate triangles (where two or more vertices are the same)
            group_triangles.erase(std::remove_if(group_triangles.begin(), group_triangles.end(),
                [&](uint32_t tri_idx) {
                    const glm::uvec3& t = triangles[tri_idx];
                    return (t.x == t.y || t.y == t.z || t.z == t.x);
                }),
                group_triangles.end());
            
            for (auto it = next_vertex_in_cluster.begin(); it != next_vertex_in_cluster.end(); ) {
                uint32_t a = it->first.x, b = it->first.y;
                if (a == v2) a = v1;
                if (b == v2) b = v1;
                
                if (a != b) {  // Avoid self-loops
                    next_vertex_in_cluster[glm::uvec2(a, b)] = it->second;
                }
                it = next_vertex_in_cluster.erase(it);
            }

            // Remove elements in the heap that involve v1 or v2

            std::priority_queue<QEMEntry> new_heap;
            while (!heap.empty()) {
                QEMEntry edge = heap.top();
                heap.pop();
                
                // If edge involves v1 or v2, skip it
                if (edge.v1 == v1 || edge.v2 == v1 || edge.v1 == v2 || edge.v2 == v2) {
                    continue;
                }
                
                // Otherwise, keep it
                new_heap.push(edge);
            }

            // Replace old heap with filtered heap
            heap = std::move(new_heap);

            // Rebuild heap with updated quadrics
            for (const auto& edge : next_vertex_in_cluster) {
                uint32_t v3 = edge.first.x, v4 = edge.first.y;
                if (v3 != v1 && v4 != v1) continue; // Only update edges involving v1

                if (boundary_vertices.count(v3) || boundary_vertices.count(v4)) continue; // Ignore boundary

                glm::mat4 Qsum = quadrics[v3] + quadrics[v4];
                // Extract the upper-left 3x3 block for A
                glm::mat3 A = glm::mat3(Qsum);

                // Extract b as the negation of the upper-right 3x1 column
                glm::vec3 b = -glm::vec3(Qsum[3]); 

                // Solve for x: A * x = b or get the best position if A is singular
                glm::vec3 x = get_best_vertex_after_collapse(Qsum, vertices[v3], vertices[v4]);

                // Extract c (bottom-right scalar)
                float c = Qsum[3][3];

                // Compute the error: Q(v) = -b^T * A^-1 * b + c
                float error = -glm::dot(b, x) + c;

                heap.push({v3, v4, x, error});

            }
        }
        
    }

    { // clean up degenerate triangles and unused triangles from the triangle list
        std::vector<glm::uvec3> new_triangles;
        new_triangles.reserve(triangles.size());
        for (uint32_t i = 0; i < clusters.size(); ++i) {
            std::vector<uint32_t> new_cluster_triangles;
            new_cluster_triangles.reserve(clusters[i].triangles.size());
            for (uint32_t triangle_index : clusters[i].triangles) {
                const glm::uvec3& triangle = triangles[triangle_index];
                const glm::vec3& v0 = vertices[triangle[0]];
                const glm::vec3& v1 = vertices[triangle[1]];
                const glm::vec3& v2 = vertices[triangle[2]];
                if (v0 == v1 || v1 == v2 || v0 == v2) continue; //skip degenerate triangles
                new_cluster_triangles.push_back(uint32_t(new_triangles.size()));
                new_triangles.push_back(triangle);
            }
            clusters[i].triangles = new_cluster_triangles;
        }
        triangles = new_triangles;
    }

    std::cout<<"Finished simplifying "<<cluster_count<<" clusters"<<std::endl;
}

glm::vec3 NaniteMeshApp::get_best_vertex_after_collapse(const glm::mat4 &Qsum, glm::vec3 v1, glm::vec3 v2)
{

    glm::mat3 A = glm::mat3(Qsum);
    glm::vec3 b = -glm::vec3(Qsum[3]);

    if (glm::determinant(A) > 1e-3f) { // A isn't singular
        return glm::inverse(A) * b; 
    }

    // Best position on (v1, v2)
    float error_v1 = glm::dot(glm::vec4(v1, 1.0f), Qsum * glm::vec4(v1, 1.0f));
    float error_v2 = glm::dot(glm::vec4(v2, 1.0f), Qsum * glm::vec4(v2, 1.0f));
    
    glm::vec3 best_v = (error_v1 < error_v2) ? v1 : v2;
    float min_error = std::min(error_v1, error_v2);

    // Check midpoint
    glm::vec3 v_mid = 0.5f * (v1 + v2);
    float error_mid = glm::dot(glm::vec4(v_mid, 1.0f), Qsum * glm::vec4(v_mid, 1.0f));

    if (error_mid < min_error) best_v = v_mid;
    
    return best_v;

}

// Function to deep copy a buffer and apply an offset to the vertex positions
void NaniteMeshApp::copy_offset_mesh_to_model(tinygltf::Model& model, tinygltf::Mesh& mesh, const glm::vec3& offset) {
    for (auto& primitive : mesh.primitives) {
        auto it = primitive.attributes.find("POSITION");
        if (it != primitive.attributes.end()) {
            int accessorIndex = it->second;
            tinygltf::Accessor& accessor = model.accessors[accessorIndex];
            tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
            tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

            // Create a new buffer for the copied data
            tinygltf::Buffer newBuffer;
            newBuffer.data = buffer.data; // Copy the original buffer data

            // Create a new buffer view
            tinygltf::BufferView newBufferView = bufferView;
            newBufferView.buffer = int(model.buffers.size()); // Point to the new buffer

            // Create a new accessor
            tinygltf::Accessor newAccessor = accessor;
            newAccessor.bufferView = int(model.bufferViews.size()); // Point to the new buffer view

            // Apply the offset to the new buffer's vertex positions
            float* positions = reinterpret_cast<float*>(&newBuffer.data[bufferView.byteOffset + accessor.byteOffset]);
            for (size_t i = 0; i < accessor.count; ++i) {
                positions[i * 3 + 0] += offset.x; // X
                positions[i * 3 + 1] += offset.y; // Y
                positions[i * 3 + 2] += offset.z; // Z
            }

            // Add the new buffer, buffer view, and accessor to the model
            model.buffers.push_back(newBuffer);
            model.bufferViews.push_back(newBufferView);
            model.accessors.push_back(newAccessor);

        }
    }
	tinygltf::Mesh new_mesh = mesh;
	new_mesh.name += "_new";
	new_mesh.primitives[0].attributes["POSITION"] = int(model.accessors.size()) - 1;
	model.meshes.emplace_back(new_mesh);
	model.nodes.emplace_back(model.nodes[0]);
	model.nodes[1].mesh = int(model.meshes.size())-1;
}


bool NaniteMeshApp::save_model(const tinygltf::Model& model, std::string filename) {
    tinygltf::TinyGLTF loader;
	assert(filename != "");
	if (filename.size() < 5 || filename.substr(filename.size()-5,5) != ".gltf") {
		filename = filename + ".gltf";
	}

    bool ret = loader.WriteGltfSceneToFile(&model, filename);

    if (!ret) {
        std::cout << "Failed to save glTF: " << filename << std::endl;
    }
	else {
		std::cout << "Saved glTF: " << filename << std::endl;
	}
    return ret;
}

