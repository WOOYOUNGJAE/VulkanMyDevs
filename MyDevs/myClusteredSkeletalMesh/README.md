# My Clustered Skeletal Mesh
---
## Table of Contents
+ [Clustered Skeletal Mesh](#1.-Clustered-Skeletal-Mesh-link)

Keyword : Cluster Acceleration Structure, Skeletal Mesh, Skinning, Raytracing

# 1. Clustered Skeletal Mesh [(link)](./myClusteredSkeletalMesh.cpp)
<img src="../images/ClusteredSkeletalAnimationRT.jpg" height="256px">

## Description
[myClusterAccelerationStructureNV](../myClusterAccelerationStructureNV/myClusterAccelerationStructureNV.cpp) 와 [mySkeletalAnimationRT](../myRayTracingLittleAdvanced/mySkeletalAnimationRT.cpp) 의 결합 프로젝트.\
skeletal mesh의 gltf 모델을 Cluster Acceleration Structure 기반 가속 구조로 빌드를 하고 Compute Shader로 애니메이션을 수행한다.

## 1. Applying Node Transform
[myClusterAccelerationStructureNV README](../myClusterAccelerationStructureNV/README.md) 에서 언급한 바와 같이 CLAS를 빌드할 때 transform을 입력할 수 없기 때문에 node-transform을 vertex에 선반영 시켜야 한다.

Static Object 같은 경우는 최초 gltf 로딩을 할 때 vertex에 node-transform을 반영하면 되지만,\
매 프레임 vertex의 정보가 변경되어야 하는 애니메이션 Object같은 경우 Compute Shader내에서 node-transform을 곱하여 적용한다.

### [anim.comp code](../../shaders/glsl/myClusteredSkeletalMesh/anim.comp)
```c++
mat4 skinMat = 
    jointData.matrix * (vertexWeight0.x * jointData.jointMatrices[int(vertexJoint0.x)] +
    vertexWeight0.y * jointData.jointMatrices[int(vertexJoint0.y)] +
    vertexWeight0.z * jointData.jointMatrices[int(vertexJoint0.z)] +
    vertexWeight0.w * jointData.jointMatrices[int(vertexJoint0.w)]);

vec3 deformedPos = (skinMat * vertexPos).xyz;
```