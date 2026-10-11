# NVIDIA NRD (Real-time Denoisers) for the Vulkan path tracer's "nrd"
# denoiser (pc/docs/Vulkan_Renderer.md, "NRD"). NRD is under the NVIDIA RTX
# SDKs license, so it is never vendored into this (CC0) repository: this
# project downloads it at a pinned commit into the build directory and
# builds it as a static library with its SPIR-V shaders embedded. NRD's own
# CMake then downloads ShaderMake, MathLib and DXC (the HLSL compiler for
# its shaders), each pinned by NRD.
#
# Driven by pc/tools/nrd.py (called from pc/tools/build_boot.py --arch 64):
#     cmake -S pc/tools/nrd -B pc/build/nrd && cmake --build pc/build/nrd
cmake_minimum_required(VERSION 3.22)
project(rood_nrd LANGUAGES CXX)
include(FetchContent)

set(ROOD_NRD_COMMIT "7033ffbc48dbd74713194555abc13da8d7de4bcb" CACHE STRING "NRD commit")
set(ROOD_NRD_SHA256 "fb11d76cd626610b4423ecfcfbefe0137d0c2cd92b47489f67b590b2eaab6b52" CACHE STRING
    "SHA-256 of the NRD archive at ROOD_NRD_COMMIT")

# Configuration: a static
# library, oct-packed normals (R10G10B10A2 encoding), linear roughness, no
# quad intrinsics (no VK_KHR_compute_shader_derivatives needed), SPIR-V only.
set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRD_NORMAL_ENCODING 2 CACHE STRING "" FORCE)
set(NRD_ROUGHNESS_ENCODING 1 CACHE STRING "" FORCE)
set(NRD_SUPPORTS_QUAD_INTRINSICS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS ON CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_NRI OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(nrd
    URL "https://github.com/NVIDIA-RTX/NRD/archive/${ROOD_NRD_COMMIT}.zip"
    URL_HASH "SHA256=${ROOD_NRD_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP 1
    DOWNLOAD_NO_PROGRESS 1)
FetchContent_MakeAvailable(nrd)

# Where pc/tools/nrd.py finds the results.
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/rood-nrd-paths.txt" CONTENT
"include $<TARGET_PROPERTY:NRD,SOURCE_DIR>/Include
library $<TARGET_FILE:NRD>
library $<TARGET_FILE:ShaderMakeBlob>
")
