/* Copyright (c) 2023, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct Vertex
{
  vec3 pos;
  vec3 normal;
  vec2 uv;
};

struct Triangle {
	Vertex vertices[3];
	vec3 normal;
	vec2 uv;
};

// Todo change to specialization
#ifdef USE_SKINNING
#define NUM_VEC4_FROM_VERTEX_SIZE 6
#else
#define NUM_VEC4_FROM_VERTEX_SIZE 4
#endif


#define INDEX_TYPE_SIZE 4

// This function will unpack our vertex buffer data into a single triangle and calculates uv coordinates
Triangle unpackTriangle(uint primitiveID, int vertexSize) {
	Triangle tri;
	const uint triIndex = primitiveID * 3;

	GeometryNode geometryNode = geometryNodes.nodes[gl_InstanceID];
	MeshPrimitive meshPrimitive = meshPrimitives.primitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];

	
	// move to start of this node(mesh)'s MeshPrimitive
	uint64_t nodeVertexAddress = sceneDeviceAddress.vertexBufferAddress;// +vertexSize * (geometryNode.vertexStartOffset + meshPrimitive.vertexStartOffsetInMesh + primitiveID);
	uint64_t nodeIndexAddress = sceneDeviceAddress.indexBufferAddress + INDEX_TYPE_SIZE * (geometryNode.indexStartOffset + meshPrimitive.IndexStartOffsetInMesh + (primitiveID * 3)); // index size

	Vertices   vertices = Vertices(nodeVertexAddress);
	Indices    indices = Indices(nodeIndexAddress);

		
	// Unpack vertices
	// Data is packed as vec4 so we can map to the glTF vertex structure from the host side
	// We match vkglTF::Vertex: pos.xyz+normal.x, normalyz+uv.xy
	// glm::vec3 pos;
	// glm::vec3 normal;
	// glm::vec2 uv;
	// ...
	for (uint i = 0; i < 3; i++) {
		const uint offset = indices.i[i] * NUM_VEC4_FROM_VERTEX_SIZE; // vertex stride
		vec4 d0 = vertices.v[offset + 0]; // pos.xyz, n.x
		vec4 d1 = vertices.v[offset + 1]; // n.yz, uv.xy
		tri.vertices[i].pos = d0.xyz;
		tri.vertices[i].normal = vec3(d0.w, d1.xy);
		tri.vertices[i].uv = d1.zw;
	}
	// Calculate values at barycentric coordinates
	vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	tri.uv = tri.vertices[0].uv * barycentricCoords.x + tri.vertices[1].uv * barycentricCoords.y + tri.vertices[2].uv * barycentricCoords.z;
	tri.normal = tri.vertices[0].normal * barycentricCoords.x + tri.vertices[1].normal * barycentricCoords.y + tri.vertices[2].normal * barycentricCoords.z;
	return tri;
}