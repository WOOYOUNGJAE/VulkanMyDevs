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

  //vec4 joint0;
  //vec4 weight0;
#if CUSTOM_VERTEX
  uvec4 customData4; // [meshID, primitiveIdInMesh, 0, 0]
#endif
};

struct Triangle {
	Vertex vertices[3];
	vec3 normal;
	vec2 uv;
};

// Todo change to specialization
#ifdef USE_SKINNING
#if CUSTOM_VERTEX
#define VERTEX_SIZE 112
#else
#define VERTEX_SIZE 96
#endif
#else
#define VERTEX_SIZE 64
#endif
#define NUM_VEC4_FROM_VERTEX_SIZE (VERTEX_SIZE / 16)


#define INDEX_TYPE_SIZE 4

Triangle getTriangle(uint64_t verticesAddress, uint64_t dstTriangleAddress)
{
	Triangle tri;

	Vertices   vertices = Vertices(verticesAddress);
	Indices    indices = Indices(dstTriangleAddress);

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

		//tri.vertices[i].joint0 = vertices.v[offset + 4];
		//tri.vertices[i].weight0 = vertices.v[offset + 5];
#if CUSTOM_VERTEX
		tri.vertices[i].customData4.xy = uint(vertices.v[offset + 6].xy);
#endif 
	}

	// Calculate values at barycentric coordinates
	vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	tri.uv = tri.vertices[0].uv * barycentricCoords.x + tri.vertices[1].uv * barycentricCoords.y + tri.vertices[2].uv * barycentricCoords.z;
	tri.normal = tri.vertices[0].normal * barycentricCoords.x + tri.vertices[1].normal * barycentricCoords.y + tri.vertices[2].normal * barycentricCoords.z;


	return tri;
}


// This function will unpack our vertex buffer data into a single triangle and calculates uv coordinates
#if CLUSTER_BLAS

Triangle unpackTriangle(GeometryNode geometryNode, ClusterRT cluster, uint primitiveID)
{
	const uint indexOffset = (geometryNode.triangleStartOffset + cluster.firstTriangle + primitiveID) * 3;
	// move to start of this node(mesh)'s MeshPrimitive
	uint64_t verticesAddress = pushData.baseData.vertexBufferDeviceAddress;
	uint64_t dstTriangleAddress = pushData.baseData.indexBufferDeviceAddress + INDEX_TYPE_SIZE * indexOffset; // index size

	return getTriangle(verticesAddress, dstTriangleAddress);
}
#else
Triangle unpackTriangle(GeometryNode geometryNode, MeshPrimitive meshPrimitive, uint primitiveID)
{
	Triangle tri;
	const uint indexOffset = (geometryNode.indexStartOffset + meshPrimitive.IndexStartOffsetInMesh) + primitiveID * 3;

	// move to start of this node(mesh)'s MeshPrimitive
	uint64_t nodeVertexAddress = pushData.baseData.vertexBufferDeviceAddress;// +vertexSize * (geometryNode.vertexStartOffset + meshPrimitive.vertexStartOffsetInMesh + primitiveID);
	uint64_t nodeIndexAddress = pushData.baseData.indexBufferDeviceAddress + INDEX_TYPE_SIZE * indexOffset; // index size

	return getTriangle(nodeVertexAddress, nodeIndexAddress);
}

#endif
