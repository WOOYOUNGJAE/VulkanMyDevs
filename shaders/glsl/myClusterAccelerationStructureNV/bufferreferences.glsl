/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

layout(push_constant) uniform SceneDeviceAddress {
	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;
} sceneDeviceAddress;

layout(buffer_reference, scalar) buffer Vertices {vec4 v[]; };
layout(buffer_reference, scalar) buffer Indices {uint i[]; };
layout(buffer_reference, scalar) buffer Data {vec4 f[]; };