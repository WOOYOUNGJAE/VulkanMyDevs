# My Dynamic Acceleration Structure

## Synopsis
매 프레임 Acceleration Structure 업데이트.

Keyword : dynamic acceleration structure

## Description

[MyMultiBLAS](../myMultiBLAS/) 프로젝트 베이스에서 확장

### 흐름
- prepare(init) 에서 한 번만 blas 에 대해서만 initBLAS()
	- blas 를 위한 geometry 정보 입력, blas buffer 생성.
- 매 프레임 buildBLASes(), buildTLAS() 호출
	- first build일 경우 vkCreateAccelerationStructureKHR와 vkCmdBuildAccelerationStructuresKHR 모두.
	- update일 경우 vkCmdBuildAccelerationStructuresKHR만
