#include "NaniteMeshApp.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "tiny_gltf.h"
#include <iostream>

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
	configuration(configuration_)
{
	loadGLTF(configuration.glTF_path);
	cluster(12);
}

void NaniteMeshApp::loadGLTF(std::string gltfPath)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
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
	// initialize clusters, one cluster per triangle
	clusters.clear();
	for (uint32_t i = 0; i < triangles.size(); ++i) {
		clusters.push_back({ {i}, {} });
		triangle_to_cluster[i] = i;
	}

	// build shared edge count

	std::unordered_map<glm::uvec2, uint32_t> edge_to_triangle;
	edge_to_triangle.clear();
	for (uint32_t i = 0; i < triangles.size(); ++i) {
		const glm::uvec3& triangle = triangles[i];
		for (int j = 0; j < 3; ++j) {
			glm::uvec2 edge = { triangle[j], triangle[(j + 1) % 3] };
			glm::uvec2 reversed_edge = { triangle[(j + 1) % 3], triangle[j] };

			if (edge_to_triangle.find(reversed_edge) != edge_to_triangle.end()) {

				uint32_t neighborIdx = edge_to_triangle[reversed_edge];
				uint32_t neighbor_cluster = triangle_to_cluster[neighborIdx];
				uint32_t current_cluster = triangle_to_cluster[i];

				// Increase the shared edge count between clusters
				clusters[current_cluster].shared_edges[neighbor_cluster]++;
				clusters[neighbor_cluster].shared_edges[current_cluster]++;
			} else {
				edge_to_triangle[edge] = i; // Store triangle index
			}
		}
	}

	uint32_t loop_count = 0;
	try {

	while (clusters.size() > 1) {
		if (loop_count % 100 == 0) {
			std::cout<<"loop:"<< loop_count<<" , cluster count: "<< clusters.size()<<std::endl;
		}
		loop_count++;
		// Find the two clusters with the max shared edges
		uint32_t max_a = 0, max_b = 0, max_edges = 0;
		for (uint32_t i = 0; i < clusters.size(); ++i) {
			for (const auto& [neighbor, edge_count] : clusters[i].shared_edges) {
				assert(i != neighbor);
				if (neighbor < i) continue;
				if (edge_count > max_edges && clusters[i].triangles.size() + clusters[neighbor].triangles.size() <= cluster_triangle_limit) {
					max_edges = edge_count;
					max_a = i;
					max_b = neighbor;
				}
			}
		}

		if (max_edges == 0) {
			break; // Stop if no valid merges
		}
		// Merge cluster max_b into max_a
		for (uint32_t tri : clusters[max_b].triangles) {
			clusters[max_a].triangles.insert(tri);
			triangle_to_cluster[tri] = max_a;
		}

		// Update shared edges
		for (const auto& [neighbor, edge_count] : clusters[max_b].shared_edges) {
			if (neighbor == max_a) continue; // Skip self

			clusters[max_a].shared_edges[neighbor] += edge_count;
			clusters[neighbor].shared_edges[max_a] += edge_count;
			clusters[neighbor].shared_edges.erase(max_b);
		}


		// Swap max_b with the last cluster, update indices, and pop the last element
		if (max_b != uint32_t(clusters.size() - 1)) {
			std::swap(clusters[max_b], clusters.back());

			// Update triangle_to_cluster for swapped cluster
			for (uint32_t tri : clusters[max_b].triangles) {
				triangle_to_cluster[tri] = max_b;
			}

			// Update shared_edges references to swapped index
			for (auto& [neighbor, edges] : clusters[max_b].shared_edges) {
				clusters[neighbor].shared_edges.erase(uint32_t(clusters.size()) - 1);
				clusters[neighbor].shared_edges[max_b] = edges;
			}
		}

		clusters.pop_back();
	}
	}
	catch (std::exception &e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return;
	}
	std::cout<<"loop:"<< loop_count<<" , cluster count: "<< clusters.size()<<std::endl;
	std::cout<<"end";
}

