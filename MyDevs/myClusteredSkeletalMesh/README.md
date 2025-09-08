# My Clustered Skeletal Mesh
---
## Table of Contents
+ [Clustered Skeletal Mesh](#1-Clustered-Skeletal-Mesh-link)
+ [Compare AS Building Performance](#2-compare-as-building-performance)

Keyword : Cluster Acceleration Structure, Skeletal Mesh, Skinning, Raytracing

# 1. Clustered Skeletal Mesh [(link)](./myClusteredSkeletalMesh.cpp)
<img src="../images/ClusteredSkeletalAnimationRT.jpg" height="256px">

## Description
[myClusterAccelerationStructureNV](../myClusterAccelerationStructureNV/myClusterAccelerationStructureNV.cpp) 와 [mySkeletalAnimationRT](../myRayTracingLittleAdvanced/mySkeletalAnimationRT.cpp) 의 결합 프로젝트.\
skeletal mesh의 gltf 모델을 Cluster Acceleration Structure 기반 가속 구조로 빌드를 하고 Compute Shader로 애니메이션을 수행한다.

## 1. Applying Node Transform
[myClusterAccelerationStructureNV README](../myClusterAccelerationStructureNV/README.md#1-clas-transform) 에서 언급한 바와 같이 CLAS를 빌드할 때 transform을 입력할 수 없기 때문에 node-transform을 vertex에 선반영 시켜야 한다.

Static Object 같은 경우는 최초 gltf 로딩을 할 때 vertex에 node-transform을 반영하면 되지만,\
매 프레임 vertex의 정보가 변경되어야 하는 애니메이션 Object같은 경우 Compute Shader내에서 node-transform을 곱하여 적용한다.

### [anim.comp code](../../shaders/glsl/myClusteredSkeletalMesh/anim.comp)
```c++
mat4 skinMat = 
    jointData.matrix * (
    vertexWeight0.x * jointData.jointMatrices[int(vertexJoint0.x)] +
    vertexWeight0.y * jointData.jointMatrices[int(vertexJoint0.y)] +
    vertexWeight0.z * jointData.jointMatrices[int(vertexJoint0.z)] +
    vertexWeight0.w * jointData.jointMatrices[int(vertexJoint0.w)]);

vec3 deformedPos = (worldMat * skinMat * vertexPos).xyz;
```
기존의 BLAS의 경우 BLAS 빌드 할 때 Transform buffer을 입력하여 world space 변환이 가능했으나, Clustered BLAS는 Transform 입력을 지원하지 않기 때문에 compute shader에서 world 까지 변환을 마친 후 BVH 빌드를 해야 한다.

## 2. Compare AS Building Performance
| 항목 | Traditional AS | With CLAS |
| :--- | :--- | :--- |
| **Average CLAS Build Time** | | 0.0749331 (ms) |
| **Average BLAS Build Time** | 0.243804 (ms) | 0.0581301 (ms) |
| **Average TLAS Build Time** | 0.013284 (ms) | 0.0132442 (ms) |
| **Average Total AS Build Time** | 0.257088 (ms) | 0.146307 (ms) |
| **Average Tracing Time** | 0.24183 (ms) | 0.231211 (ms) |
| **Average FPS** | 42.53 fps (23.51 ms)  | 44.83 fps (22.31 ms) |

<small>**Num Vertices**: 15,716(Model_1) + 9,285(Model_2) + 6,109(Model_3) = **31,110**</small>\
<small>**Num Triangles**: 31,292(Model_1) + 17,916(Model_2) + 12,174(Model_3) = **61,382**</small>\
<small>**Measured Frame Count**: **3000**</small>

- [myRayTracingLittleAdvanced](../myRayTracingLittleAdvanced) 에 추가한 [GPU Timer](../myRayTracingLittleAdvanced//README.md#1-gpu-timer-code)을 활용하여 Acceleration Structure 빌드 커맨드 수행 시간 측정