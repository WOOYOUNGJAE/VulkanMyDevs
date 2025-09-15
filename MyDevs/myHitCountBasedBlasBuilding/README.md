# My Hit Count Based BLAS Building
---
## Table of Contents
+ [Hit Count Based BLAS Building - Clustered Triangle BLAS](#1-Hit-Count-Based-BLAS-Building---Clustered-Triangle-BLAS-link)
+ [Hit Count Based BLAS Building - Clustered BLAS](#2-compare-as-building-performance)

Keyword : Cluster Acceleration Structure, Skinning, Raytracing, Hit Count Based Building

**핵심 아이디어**:
오브젝트를 Clustering 후 이전 프레임의 Ray-Hit이 되지 않은 Cluster은 다음 BLAS 빌드(업데이트)에서 제외한다.
- idea from GDC2025 - RTX Mega Geometry(https://youtu.be/KblmxDkaUfc?t=2807)

# 1. Hit Count Based BLAS Building - Clustered Triangle BLAS [(link)](./myClusteredSkeletalMesh.cpp)
<img src="../images/ClusteredTriangleBLAS.jpg" height="256px">

## Description
클러스터를 전통 BLAS(Triangle BLAS)로 생성하여 CLAS에서 활용할 수 있는 "Hit Count Based Building" 기법을 활용해본다.

## 1.1 Write Hit Info onto Cluster Node
### [closestHit code](../../shaders/glsl/myHitCountBasedBlasBuilding/closesthit.rchit)
```glsl
struct ClusterNode
{
    uint32_t numVertices; // num of cluster's vertices
    uint32_t numTriangles; // num of cluster's vertices
    uint32_t firstTriangle; // first triangle offset from global(mesh's) triangles
    uint32_t firstLocalVertex;
    uint32_t firstLocalIndex; // first local triangle's "INDEX" from total local triangles
    uint64_t triangleHitMask;
    uint32_t padding0;
};
sceneClusters.clusters[geometryNode.clusterStartOffset + clusterID].triangleHitMask |= (1 << primitiveID);
```



## 2. Compare AS Building Performance
