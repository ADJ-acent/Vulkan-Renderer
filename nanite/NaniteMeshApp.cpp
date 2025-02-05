#include "NaniteMeshApp.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "tiny_gltf.h"
#include <queue>

void NaniteMeshApp::Configuration::parse(int argc, char **argv) {
	for (int argi = 1; argi < argc; ++argi) {
		std::string arg = argv[argi];
		if (arg == "--path") {
			if (argi + 1 >= argc) throw std::runtime_error("--path requires a parameter (a .glTF path).\n");
			argi += 1;
			glTF_path = argv[argi];
		}
		
	}
	if (glTF_path == "") {
		throw std::runtime_error("must provide a glTF path with --path!\n");
	}
}

void NaniteMeshApp::Configuration::usage(std::function< void(const char *, const char *) > const &callback) {	
	//callback("--debug, --no-debug", "Turn on/off debug and validation layers.");
	callback("--path <p>", "Loads the glTF file at path <p>");
}

NaniteMeshApp::NaniteMeshApp(Configuration & configuration_) :
	configuration(configuration_), triangle_to_cluster(0)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	loadGLTF(configuration.glTF_path, model, loader);
	// copy_offset_mesh_to_model(model, model.meshes[0],glm::vec3(20));
	cluster(12);
	write_clusters_to_model(model);
	save_model(model, std::string("../gltf/test"));
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

	assert(model.meshes.size() == 1 && "Currently only support single mesh");
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
				for (size_t i = 0; i < indices.size(); i += 3) {
					uint32_t i0 = indices[i];
					uint32_t i1 = indices[i + 1];
					uint32_t i2 = indices[i + 2];
					// auto do_next = [&](uint32_t a, uint32_t b, uint32_t c) {
					// 	auto ret = next_vertex.insert(std::make_pair(glm::uvec2(a,b), c));
					// 	// assert(ret.second);
					// 	if (!ret.second) {
					// 		count ++;
					// 		std::cout<<"error on "<<i <<std::endl;
					// 	}
					// };
					// do_next(i0,i1,i2);
					// do_next(i1,i2,i0);
					// do_next(i2,i0,i1);
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
					if (i % 10000 == 0) {
						std::cout<<"current index: "<<i<<" / "<<indices.size()<<std::endl;
					}
					// if (i % 10 == 0 && i > 2990000) {
					// 	std::cout<<glm::to_string(vertices[i0])<<std::endl;
					// 	std::cout<<glm::to_string(positions[i0])<<std::endl;
					// 	std::cout << "  v0: (" << positionsf[i0 * 3] << ", " << positionsf[i0 * 3 + 1] << ", " << positionsf[i0 * 3 + 2] << ")\n";
					// 	std::cout << "  v1: (" << positionsf[i1 * 3] << ", " << positionsf[i1 * 3 + 1] << ", " << positionsf[i1 * 3 + 2] << ")\n";
					// 	std::cout << "  v2: (" << positionsf[i2 * 3] << ", " << positionsf[i2 * 3 + 1] << ", " << positionsf[i2 * 3 + 2] << ")\n";
					// }

				}
			}
			std::cout<<accessor.count<<std::endl;
			std::cout<<vertices.size()<<std::endl;
			std::cout<<"done!\n";
		}
	}
	
}

// step 1 of preprocessing
void NaniteMeshApp::cluster(uint32_t cluster_triangle_limit)
{
	clusters.clear();
    triangle_to_cluster = UnionFind(static_cast<uint32_t>(triangles.size()));

    // Initialize each triangle as its own cluster
    for (uint32_t i = 0; i < triangles.size(); ++i) {
        clusters.push_back({ { i }, {} });
		clusters[i].shared_edges.clear();
    }

    // Build shared edge counts
    std::unordered_map<glm::uvec2, uint32_t> edge_to_triangle;
    for (uint32_t i = 0; i < triangles.size(); ++i) {
        const glm::uvec3& triangle = triangles[i];
        for (int j = 0; j < 3; ++j) {
            glm::uvec2 edge = { triangle[j], triangle[(j + 1) % 3] };
            glm::uvec2 reversed_edge = { triangle[(j + 1) % 3], triangle[j] };

            if (edge_to_triangle.find(reversed_edge) != edge_to_triangle.end()) {
                uint32_t neighbor_cluster = edge_to_triangle[reversed_edge];
                clusters[i].shared_edges[neighbor_cluster]++;
                clusters[neighbor_cluster].shared_edges[i]++;
            } else {
                edge_to_triangle[edge] = i;
            }
        }
    }

    // Build priority queue of merge candidates
    std::priority_queue<MergeCandidate> merge_heap;
    for (uint32_t i = 0; i < clusters.size(); ++i) {
        for (const auto& [neighbor, edge_count] : clusters[i].shared_edges) {
            if (neighbor > i) {
                merge_heap.push({ i, neighbor, edge_count });
            }
        }
    }

    uint32_t loop_count = 0;

    while (!merge_heap.empty() && clusters.size() > 1) {
        // if (loop_count % 100 == 0) {
            std::cout << "loop: " << loop_count << " , cluster count: " << clusters.size() << std::endl;
        // }
        loop_count++;

        // Get best merge candidate
        MergeCandidate candidate = merge_heap.top();
        merge_heap.pop();

        // Validate candidate
        if (!is_valid_candidate(candidate)) continue;

        // Ensure we don't exceed triangle limit
        if (clusters[candidate.cluster_a].triangles.size() + clusters[candidate.cluster_b].triangles.size() > cluster_triangle_limit) {
            continue;
        }

        // Merge clusters
        merge_clusters(candidate.cluster_a, candidate.cluster_b);

        // Update neighbors
        for (const auto& [neighbor, edge_count] : clusters[candidate.cluster_b].shared_edges) {
            if (neighbor == candidate.cluster_a) continue; // Skip self

            clusters[candidate.cluster_a].shared_edges[neighbor] += edge_count;
            clusters[neighbor].shared_edges[candidate.cluster_a] += edge_count;
            merge_heap.push({ candidate.cluster_a, neighbor, clusters[candidate.cluster_a].shared_edges[neighbor] });
        }
    }
	
	for (int32_t i = int32_t(clusters.size()) - 1; i > -1; --i) {
		if (!triangle_to_cluster.is_original(i)) {
			clusters.erase(clusters.begin() + i);
		}
	}
    std::cout << "Final loop count: " << loop_count << ", remaining clusters: " << clusters.size() << std::endl;
}

bool NaniteMeshApp::is_valid_candidate(const MergeCandidate &candidate)
{
    uint32_t root_a = triangle_to_cluster.find(candidate.cluster_a);
    uint32_t root_b = triangle_to_cluster.find(candidate.cluster_b);
    return (root_a == candidate.cluster_a && root_b == candidate.cluster_b &&
            clusters[root_a].shared_edges[root_b] == candidate.shared_edge_count &&
			clusters[root_b].shared_edges[root_a] == candidate.shared_edge_count);
}

void NaniteMeshApp::merge_clusters(uint32_t a, uint32_t b) {
    triangle_to_cluster.unite(a, b);
    // Move triangles from b to a
    clusters[a].triangles.insert(clusters[a].triangles.end(),
                                 clusters[b].triangles.begin(),
                                 clusters[b].triangles.end());

    // Remove shared_edges[b] since b no longer exists
    clusters[a].shared_edges.erase(b);

}

void NaniteMeshApp::write_clusters_to_model(tinygltf::Model& model)
{
    model = tinygltf::Model(); // Reset model

    std::vector<uint8_t> index_buffer;
    std::vector<tinygltf::Mesh> meshes;

    size_t global_index_offset = 0;

    // Convert global vertex data into a GLTF buffer
    std::vector<uint8_t> vertex_buffer(reinterpret_cast<uint8_t*>(vertices.data()),
                                       reinterpret_cast<uint8_t*>(vertices.data() + vertices.size()));

    for (size_t cluster_id = 0; cluster_id < clusters.size(); ++cluster_id) {
        Cluster& cluster = clusters[cluster_id];

        std::vector<uint32_t> cluster_indices;

        // Use global vertex indices directly
        for (uint32_t triangle_index : cluster.triangles) {
            glm::uvec3 triangle = triangles[triangle_index];
			assert(triangle.x < vertices.size());
			assert(triangle.y < vertices.size());
			assert(triangle.z < vertices.size());
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
        primitive.indices = static_cast<int>(model.accessors.size() + 1); // Index accessor
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
    vertex_view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
	vertex_view.name = "Vertex View";

    index_view.buffer = 1;
    index_view.byteOffset = 0;
    index_view.byteLength = static_cast<int>(index_buffer.size());
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
        node.mesh = static_cast<int>(i);
        model.nodes.push_back(node);
        scene.nodes.push_back(static_cast<int>(i));
    }
    model.scenes.push_back(scene);
    model.defaultScene = 0;
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

