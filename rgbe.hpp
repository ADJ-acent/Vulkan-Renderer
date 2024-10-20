#pragma once

#include "GLM.hpp"
#include <algorithm>
#include <iostream>

// referenced https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-RGB-sexp and https://github.com/ixchow/15-466-ibl/blob/master/rgbe.hpp

inline uint32_t rgbe_to_E5B9G9R9(glm::u8vec4 col) {
	//map pure black to pure black
	if (col == glm::u8vec4(0,0,0,0)) return 0;
	constexpr int32_t N = 9;
	constexpr int32_t B = 15;
	constexpr int32_t E_max = 31;
	constexpr float shared_exp_max = (float(2<<N)-1.0f)/float(2<<N)*float(2<<(E_max-B));
	int exp = int(col.a) - 128;

	glm::vec3 color = glm::vec3(
		std::max(0.0f, std::min(std::ldexp((col.r + 0.5f) / 256.0f, exp), shared_exp_max)),
		std::max(0.0f, std::min(std::ldexp((col.g + 0.5f) / 256.0f, exp), shared_exp_max)),
		std::max(0.0f, std::min(std::ldexp((col.b + 0.5f) / 256.0f, exp), shared_exp_max)));
	float temp = color.y > color.z ? color.y : color.z;
	float max = color.x > temp ? color.x : temp;
	static const float exp_threshold = float(exp2(-(B+1)));
	int32_t exp_prime = 0;
	if (max > exp_threshold) {
		exp_prime = int32_t(log2(max)) + B + 1;
	}

	static const int32_t exp_shared_threshold = 2 << 9;
	int32_t max_shared = int32_t(max / float(exp2(exp_prime-B-N)) + 0.5f);
	int32_t exp_shared = (max_shared == exp_shared_threshold) ? exp_prime + 1 : exp_prime;
	float divisor = float(exp2(exp_shared-B-N));

	uint16_t r = uint16_t(color.x / divisor + 0.5f);
	uint16_t g = uint16_t(color.y / divisor + 0.5f);
	uint16_t b = uint16_t(color.z / divisor + 0.5f);

	return ((exp_shared & 0b11111) << 27) | ((b & 0b111111111) << 18) | ((g & 0b111111111) << 9) | (r & 0b111111111);
}

inline glm::u8vec4 float_to_rgbe(glm::vec3 col) {

	float d = std::max(col.r, std::max(col.g, col.b));

	//1e-32 is from the radiance code, and is probably larger than strictly necessary:
	if (d <= 1e-32f) {
		return glm::u8vec4(0,0,0,0);
	}

	int e;
	float fac = 255.999f * (std::frexp(d, &e) / d);

	//value is too large to represent, clamp to bright white:
	if (e > 127) {
		return glm::u8vec4(0xff, 0xff, 0xff, 0xff);
	}

	//scale and store:
	return glm::u8vec4(
		std::max(0, int32_t(col.r * fac)),
		std::max(0, int32_t(col.g * fac)),
		std::max(0, int32_t(col.b * fac)),
		e + 128
	);
}