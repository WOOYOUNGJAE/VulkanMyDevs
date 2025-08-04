# Vulkan Playground

## 📌 This project is a fork of [Sascha Willems Vulkan Demos](https://github.com/SaschaWillems/Vulkan).
You can view the original README [here](https://github.com/SaschaWillems/Vulkan#readme).

## Table of Contents
+ [My Devs](#my-devs)
    + [My Mesh Shader](#mesh-shader)
    + [Ray Tracing](#ray-tracing)

## My Devs

### Mesh Shader

- [My Mesh Shader](MyDevs/myMeshShader/)

    <img src="screenshots/myDevs/MyMeshShader_Meshlets.jpg" height="256px">


### Ray Tracing

- [Ray Tracing Basic](MyDevs/myRaytracingBasic/) - Basic Ray Tracing similar to Sascha's implementation

    <img src="screenshots/myDevs/RayTracingBasic_KHR.jpg" height="256px">
- [My Raytracing Little Advanced](MyDevs/myRayTracingLittleAdvanced//) - Multi BLAS, Dynamic AS, Indirect Build,,,

    <img src="screenshots/myDevs/MultiBLAS.jpg" height="256px">
    
- [NV Cluster Acceleration Structuer](MyDevs/myClusterAccelerationStructureNV/)



## About Coding Convention...
기존 sascha의 convention을 따르려 하였으나, 멤버 변수와 지역 변수의 혼동이 우려되는 경우에 한해 멤버 변수 앞에 "m_" prefix 붙임.