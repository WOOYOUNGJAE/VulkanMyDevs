# My Cluster Acceleration Structure
---
## Table of Contents
+ [Cluster Acceleration Structure](#1.-Cluster-Acceleration-Structure-link)

Keyword : Cluster Acceleration Structure

# 1. Cluster Acceleration Structure [(link)](./myClusterAccelerationStructureNV.cpp)
<img src="../images/MyClusterAccelerationStructure.jpg" height="256px">



## Description

## 1. CLAS Transform
### 기존의 gltf Raytracing 같은 경우
1. node의 transfrom을 vertex position에 선반영할지
2. acceleration structure을 빌드할 때 input transform matrix에 입력할지

두 가지 옵션이 있었다. 그러나

### CLAS 로 구성되는 Clustered BLAS 같은 경우
Geometry의 node 계층을 고려하지 않고 임의의 cluster로 분할하였기 때문에 어떤 cluster에 어떤 node matrix를 적용할지 알 수 없다.\
더군다나 cluster build input 구조체 역시 trasnform 입력 변수가 없다.
```c++
typedef struct VkClusterAccelerationStructureBuildTriangleClusterInfoNV {
    uint32_t                                                         clusterID;
    VkClusterAccelerationStructureClusterFlagsNV                     clusterFlags;
    uint32_t                                                         triangleCount:9;
    uint32_t                                                         vertexCount:9;
    uint32_t                                                         positionTruncateBitCount:6;
    uint32_t                                                         indexType:4;
    uint32_t                                                         opacityMicromapIndexType:4;
    VkClusterAccelerationStructureGeometryIndexAndGeometryFlagsNV    baseGeometryIndexAndGeometryFlags;
    uint16_t                                                         indexBufferStride;
    uint16_t                                                         vertexBufferStride;
    uint16_t                                                         geometryIndexAndFlagsBufferStride;
    uint16_t                                                         opacityMicromapIndexBufferStride;
    VkDeviceAddress                                                  indexBuffer;
    VkDeviceAddress                                                  vertexBuffer;
    VkDeviceAddress                                                  geometryIndexAndFlagsBuffer;
    VkDeviceAddress                                                  opacityMicromapArray;
    VkDeviceAddress                                                  opacityMicromapIndexBuffer;
} VkClusterAccelerationStructureBuildTriangleClusterInfoNV;
```
따라서 각 vertex에 gltf node matrix를 선반영하는 것이 불가피하다.