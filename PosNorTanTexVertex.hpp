#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

struct PosNorTanTexVertex {
	struct { float x,y,z; } Position;
	struct { float x,y,z; } Normal;
	struct { float x,y,z; } Tangent;
	struct { float s,t; } TexCoord;
    //a pipeline vertex input state that works with a buffer holding a PosNorTanTexVertex[] array:
	static const VkPipelineVertexInputStateCreateInfo array_input_state;
};

static_assert(sizeof(PosNorTanTexVertex) == 3*4 + 3*4 + 3*4 + 2*4, "PosNorTexVertex is packed.");