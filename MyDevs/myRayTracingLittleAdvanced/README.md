# My Raytracing Little Advanced
---
## Table of Contents
+ [Multi BLAS](#1-multi-blas-link)
+ [Dynamic Acceleration Structure](#2-dynamic-acceleration-structure-link)
+ [Skeletal Mesh Animation RT](#3-Skeletal-Mesh-Animation-Raytracing-link)
+ [Build Acceleration Structure Indirect(deprecated)](#4-build-acceleration-structure-indirectdeprecated)
+ [Others](#others)


# 1. Multi BLAS [(link)](./myMultiBLAS.cpp)
<img src="../images/MultiBLAS.jpg" height="256px">

하나의 gltf model을 하나의 BLAS로 생성하는 기존의 구조 대신 Mesh마다 BLAS 생성

Keyword : goemetry node in RT

## Description

<img src="../images/MultiBLAS_model.jpg" height="256px">

### ray와 교차한 삼각형이 buffer을 찾아가는 과정
```glsl
// simplified code
struct GeometryNode {
	uint32_t vertexStartOffset;
	uint32_t indexStartOffset;
	uint32_t primitiveStartOffset;
};

struct Primitive
{
	uint32_t vertexStartOffsetInMesh;
	uint32_t IndexStartOffsetInMesh;
	// material infos per primitive
	// ..
};

void findTriangle()
{
	// Get GeometryNode(Mesh's) via hit BLAS instance (gl_InstanceID)
	GeometryNode geometryNode = sceneNodes[gl_InstanceID];
	// gl_GeometryIndexEXT represents current gltf primitive from mesh
	Primitive meshPrimitive = scenePrimitives[geometryNode.primitiveStartOffset + gl_GeometryIndexEXT];
	
	uint64_t vertexAddress = sceneDeviceAddress.vertexBufferAddress; // vertex buffer is combinded single buffer
	uint64_t triangleIndexOffsetInBytes = 
		INDEX_TYPE_SIZE * (geometryNode.indexStartOffset + meshPrimitive.IndexStartOffsetInMesh + (gl_PrimitiveID * 3));
	uint64_t currentTriangleAddress = sceneDeviceAddress.indexBufferAddress + triangleIndexOffsetInBytes;

	Vertices   vertices = Vertices(vertexAddress);
	Indices    indices = Indices(currentTriangleAddress);	

	//..
}

```

**ray와 삼각형이 충돌하였을 때 알 수 있는 것**

    1. 어떤 BLAS instance인지 (gl_InstanceID)
    2. BLAS instance를 구성하는 geometry 중 어떤 것인지 (gl_GeometryIndexEXT)
    3. 2번의 geometry 중 몇 번째 삼각형인지 (gl_PrimitiveID)

1. blas instance와 mesh 는 1대1 대응이기 때문에 mesh가 갖고 있는 geometryNode 데이터를 가져온다.
2. geometryNode의 primitiveStartOffset을 통해 mesh가 갖고 있는 meshPrimitive의 시작 지점을 받아낸 후 gl_GeometryIndexEXT을 추가적으로 더하여 현 삼각형이 속한 meshPrimitive를 찾는다.
3. geometryNode의 indexStartOffset, meshPrimitive의 IndexStartOffsetInMesh을 활용해 scene의 전체 index buffer 중 현 삼각형의 첫 index 지점(device address) 를 찾아낸다.

---

# 2. Dynamic Acceleration Structure [(link)](./myDynamicAccelerationStructure.cpp)

[MyMultiBLAS](#1-multi-blas-link)  베이스에서 확장

Keyword : dynamic acceleration structure
## Description

### 흐름
- prepare(init) 에서 한 번만 blas 에 대해서만 initBLAS()
	- blas 를 위한 geometry 정보 입력, blas buffer 생성.
- 매 프레임 buildBLASes(), buildTLAS() 호출
	- first build일 경우 vkCreateAccelerationStructureKHR와 vkCmdBuildAccelerationStructuresKHR 모두.
	- update일 경우 vkCmdBuildAccelerationStructuresKHR만


# 3. Skeletal Mesh Animation Raytracing [(link)](./mySkeletalAnimationRT.cpp)
<img src="../images/SkeletalAnimationRT.jpg" height="256px">

Keyword : skeletal mesh, skinning, animation, compute shader
## Description
### compute skinning [(anim.comp)](../../shaders/glsl/myRayTracingLittleAdvanced/anim.comp)
compute shader로 animation 변환을 하는 것은 실제 vertex data에 write를 하기 때문에 최초의 상태(T pose)가 유지되어야 한다.\
따라서 다음 두 가지 vertex buffer을 사용한다.
1. compute shader의 input으로 활용할 "T pose Vertex Buffer"
2. compute shader의 output으로 활용할 "Deforming Vertex Buffer"

이후 변환된 Deforming Vertex Buffer을 acceleration sturcture build의 input에 입력한다.
```c++
if (isDeformable)
	vertexBuffer = model.deformingVertices.buffer;
else
	vertexBuffer = model.vertices.buffer;
// ....
asGeometry.geometry.triangles.vertexData = getBufferDeviceAddress(vertexBuffer);
```






# 4. Build Acceleration Structure Indirect(deprecated)
[MyDynamicAccelerationStructure](#2.-My-Dynamic-Acceleration-Structure) 베이스에서 확장

keyword : vkCmdBuildAccelerationStructuresIndirectKHR

## Description
nvidia gpu는 asIndirectBuild를 지원하지 않는다는 것을 알게 되어 중단하였다.\
Legacy code - [myBuildASIndirect.cpp](myBuildASIndirect.cpp)


# Others
## 1. GPU Timer [(code)](../myBase/myVulkanRTBase.h)
```c++
/**
 * @example
 * gpuTimer.reset()
 * gpuTimer.record(, 0)
 * "Record On CommandBuffer Things"
 * gpuTimer.record(, 1)
 * float deltaTime = gpuTimer.timerResult()
 */	
class GPUTimer // in MyVulkanRTBase.h
{
	float timerResult()
	{
		float result = -1.f;
		uint64_t timeStampResult[4]{}; // query0(result, availability), query1(result, availability)
		vkGetQueryPoolResults(device, timeStampQueryPool, 0, queryCount, sizeof(timeStampResult),
			timeStampResult, sizeof(uint64_t) * 2, queryFlag);

		if (timeStampResult[1] && timeStampResult[3]) // availability
		{
			result = float(timeStampResult[2] - timeStampResult[0]) * timestampPeriodDeviceLimit / (1000000.0f);
		}

		return result;
	}
};
```