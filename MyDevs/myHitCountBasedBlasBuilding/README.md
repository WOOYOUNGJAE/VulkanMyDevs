# My Hit Count Based BLAS Building
---
## Table of Contents
+ [Hit Count Based BLAS Building - Clustered Triangle BLAS](#1-Hit-Count-Based-BLAS-Building---Clustered-Triangle-BLAS-link)

Keyword : Cluster Acceleration Structure, Skinning, Raytracing, Hit Count Based Building

**핵심 아이디어**:
오브젝트를 Clustering 후 이전 프레임의 Ray-Hit이 되지 않은 Cluster은 다음 BLAS 빌드(업데이트)를 생략한다.
- idea from GDC2025 - RTX Mega Geometry(https://youtu.be/KblmxDkaUfc?t=2807)

# 1. HCB(Hit Count Based BLAS Building) - Clustered Triangle BLAS [(link)](./myClusteredSkeletalMesh.cpp)
<img src="../images/ClusteredTriangleBLAS.jpg" height="256px">

## Description
클러스터를 전통 BLAS(Triangle BLAS)로 생성하여 CLAS에서 활용할 수 있는 "Hit Count Based Building" 기법을 활용해본다.

## 1.1 Write Hit Info into Cluster Node
### [closestHit code](../../shaders/glsl/myHitCountBasedBlasBuilding/closesthit.rchit)
```c++
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
## 1.2 Compare AS Building Performance (8 Models)
| 항목 | Traditional BLAS |  Clustered Triangle BLAS(HCB) |
| :--- | :---: | :---: |
| **BLAS Build Time** | 0.681594 (ms) | 0.147033 (ms) |
| **TLAS Build Time** | 0.0135525 (ms) | 0.0260964 (ms) |
| **Total AS Build Time** | 0.695147 (ms) | 0.173129 (ms) |
| **Tracing Time** | 0.573494 (ms) | 0.683351 (ms) |
| **FPS** | 145.803fps (6.85856 ms) | 120.647fps (8.28866 ms) |


<small>**Thread Block**: **(64, 1, 1)**</small>\
<small>**Num Vertices**: **126,150**</small>\
<small>**Num Triangles**: **234,277**</small>\
<small>**Num Joints**: **640**</small>\
<small>**Measured Frame Count**: **1000**</small>

> HCB 방법이 기존 방법보다 AS GPU Build Time은 무려 약 75% 빨라졌지만 총 FPS는 17% 감소하였다.\
현재 방식의 HCB는 매 프레임 BLAS Build 커맨드를 만들어야 하는데, 이 구간이 CPU측에서 1.7ms 정도로 상당하게 소요된다.


GDC 2025에서 alan wake 2 팀이 얘기하길 hit-count를 CPU에 보낼 필요가 없음을 강조했었다.\
당시 발표 영상을 볼 때는 그 이점이 와닿지 않았는데 BLAS로 HCB를 간접 구현해보니 연결이 되었다.

이번에 간접 구현해 본 방법은 
1. 클러스터 노드에 비트 마스킹으로 Hit됨을 기록
2. CPU에서 읽어서 Hit된 클러스터들만 모아서 Build에 사용되는 Geometry들을 다시 모은다.
3. 모은 Geometry들을 바탕으로 Build BLAS 커맨드를 다시 생성한다.
    * 이 커맨드를 생성할 때의 CPU 시간조차 1ms 이상이 소요된다. (BLAS 약 2000개 기준)

반면 CLAS Extension을 사용할 때는 CLAS Build가 Indirect 명령이기 때문에, argument buffer와 hit-count 정보만 GPU 측에서 잘 연결해주면 된다.\
렌더러 구조마다 다르겠지만 매 프레임 명령을 재빌드 생략할 수 있기 때문에 이점이 있다.

원래 CLAS대신 BLAS로 Hit Count Based Building을 적용해보려고 했지만 보류하기로 했다.