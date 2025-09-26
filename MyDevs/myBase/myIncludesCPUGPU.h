/*
* Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.
* SPDX-License-Identifier: Apache-2.0
*/

// Includes for both CPU, GPU
#ifndef MY_INCLUDES_CPU_GPU
#define MY_INCLUDES_CPU_GPU

#define WAVE_SIZE 32 // warp sizes
#define WORK_GROUP_SIZE 128 // thread block size
#define MAX_JOINTS 256

#ifdef __cplusplus
#include <glm/glm.hpp>
#define BUFFER_REF(typ) uint64_t
#define BUFFER_REF_DECLARE_ARRAY(refname, typ, keywords, alignment)                                                    \
  static_assert(alignof(typ) == alignment || (alignment > alignof(typ) && ((alignment % alignof(typ)) == 0)),          \
                "Alignment incompatible: " #refname)
#else
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int32 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_buffer_reference2 : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_atomic_int64 : enable

#define BUFFER_REF(refname) refname
#define BUFFER_REF_DECLARE_ARRAY(refname, typ, keywords, alignment)                                                    \
  layout(buffer_reference, buffer_reference_align = alignment, scalar) keywords buffer refname                         \
  {                                                                                                                    \
    typ d[];                                                                                                           \
  };

BUFFER_REF_DECLARE_ARRAY(uint8s_in, uint8_t, readonly, 4);
BUFFER_REF_DECLARE_ARRAY(uints_in, uint32_t, readonly, 4);
BUFFER_REF_DECLARE_ARRAY(uint64s_inout, uint64_t, , 8);
BUFFER_REF_DECLARE_ARRAY(uvec3s_in, uvec3, readonly, 4);
BUFFER_REF_DECLARE_ARRAY(vec3s_in, vec3, readonly, 4);
BUFFER_REF_DECLARE_ARRAY(vec3s_inout, vec3, , 4);

#endif


struct ClusteredGeometryData
{
#ifdef __cplusplus
    using mat4 = glm::mat4;
#endif

    mat4 worldMatrix;

    // all scene's vertex/index buff address push as pushconstant for bindless
    uint64_t vertexBufferDeviceAddress;
    uint64_t indexBufferDeviceAddress;

    //uint64_t clusters; // index of cluster array
    //uint64_t clusterLocalVertices;
    //uint64_t clusterLocalTriangles;
    //uint64_t clusterBboxes;

    uint32_t numTriangles;
    uint32_t numVertices;
    uint32_t numClusters;
    uint32_t geometryID;

    uint64_t blasReference; // for building blas instance. update this in comp shader TODO: DLELETE?
    // TODO: testing
    uint32_t triangleStartOffset; // from all scene's triangles
    uint32_t clusterStartOffset; // from all scene's clusters, use as allClusters[clsuterStartOffset + clusterID]
};

struct ClusterRT
{
    uint32_t numVertices; // num of cluster's vertices
    uint32_t numTriangles; // num of cluster's vertices
    uint32_t firstTriangle; // first triangle offset from global(mesh's) triangles

    /**
    * Offset of first vertex/index from total local vertices/triangles
    * example:
    *|  cluster0           |  cluster1           |  cluster2           |
    *| localVertices(uint) | localVertices(uint) | localVertices(uint) |
    *
    */
    uint32_t firstLocalVertex;
    uint32_t firstLocalIndex; // first local triangle's "INDEX" from total local triangles
    uint64_t triangleHitMask;
    uint32_t padding0;
};

struct ClusteredMeshPrimitive
{
    /*uint32_t vertexStartOffsetInMesh;
    uint32_t IndexStartOffsetInMesh;*/
    int32_t textureIndexBaseColor;
    int32_t textureIndexOcclusion;
    uint32_t triangleStartOffsetGlobal; // start offset in scene's all triangles
    int32_t padding;
};

/** same as
 * typedef struct VkAccelerationStructureInstanceKHR {
    VkTransformMatrixKHR          transform;
    uint32_t                      instanceCustomIndex:24;
    uint32_t                      mask:8;
    uint32_t                      instanceShaderBindingTableRecordOffset:24;
    VkGeometryInstanceFlagsKHR    flags:8;
    uint64_t                      accelerationStructureReference;
} VkAccelerationStructureInstanceKHR;
 */
struct ASInstance
{
#ifdef __cplusplus
    using mat3x4 = glm::mat3x4;
#endif
    // same as VkAccelerationStructureInstanceKHR
    mat3x4   transform;
    uint32_t instanceCustomIndex24_mask8; // 2 in 1
    uint32_t instanceSbtOffset24_flags8; // 2 in 1
    uint64_t accelerationStructureReference;
};

#ifndef __cplusplus
BUFFER_REF_DECLARE_ARRAY(ASInstance_inout, ASInstance, , 16);
BUFFER_REF_DECLARE_ARRAY(ClusteredGeometryData_in, ClusteredGeometryData, readonly , 16);
#endif
struct ClusteredBlasPushConstantData
{
    uint32_t instanceCount;
    uint32_t sumCount;
    uint32_t animated;
    uint32_t _pad;

    // ASInstance_inout {typ d[];}
    BUFFER_REF(ASInstance_inout) asInstances;
    // ClusteredGeometryData_in {typ d[];}
    BUFFER_REF(ClusteredGeometryData_in) clusteredGeometryDatas;
    // uint64s_inout { uint64_t d[]; }
    BUFFER_REF(uint64s_inout) blasAddresses;
};


struct Payload_MeshShader
{
	uint32_t meshletIndices[WAVE_SIZE];
};

struct MainRendererPushConstantData
{
    uint64_t vertexBufferDeviceAddress;
    uint64_t indexBufferDeviceAddress;
    uint32_t renderMode; // 0:Texture 1:Triangle 2:Cluster
};

#endif