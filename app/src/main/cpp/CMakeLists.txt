cmake_minimum_required(VERSION 3.22.1)
project(realcugan_ncnn)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ncnn prebuilt is compiled with OpenMP threading.
# -static-openmp embeds the OpenMP runtime so no libomp.so is needed at runtime.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fopenmp -static-openmp")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fopenmp -static-openmp")

# ncnn headers
include_directories(${CMAKE_SOURCE_DIR}/ncnn)

set(NCNN_LIB_DIR ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI})

# Import static ncnn library (Vulkan enabled build)
add_library(ncnn STATIC IMPORTED)
set_target_properties(ncnn PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libncnn.a)

# Vulkan-enabled libncnn.a also requires the glslang/SPIRV static libs
# shipped alongside it (shader compilation for GPU pipelines).
add_library(glslang STATIC IMPORTED)
set_target_properties(glslang PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libglslang.a)
add_library(spirv STATIC IMPORTED)
set_target_properties(spirv PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libSPIRV.a)
add_library(glslang_mi STATIC IMPORTED)
set_target_properties(glslang_mi PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libMachineIndependent.a)
add_library(glslang_gc STATIC IMPORTED)
set_target_properties(glslang_gc PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libGenericCodeGen.a)
add_library(glslang_ogl STATIC IMPORTED)
set_target_properties(glslang_ogl PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libOGLCompiler.a)
add_library(glslang_osd STATIC IMPORTED)
set_target_properties(glslang_osd PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libOSDependent.a)
add_library(glslang_drl STATIC IMPORTED)
set_target_properties(glslang_drl PROPERTIES IMPORTED_LOCATION ${NCNN_LIB_DIR}/libglslang-default-resource-limits.a)

# System Vulkan (ncnn auto-detects at runtime; unavailable devices fall back to CPU)
find_library(vulkan-lib vulkan)

# JNI shared library (name kept as realcugan_ncnn)
add_library(realcugan_ncnn SHARED realcugan_jni.cpp)

# Static libs are grouped so circular ncnn<->glslang references always resolve.
target_link_libraries(realcugan_ncnn
    -Wl,--start-group
    ncnn
    spirv
    glslang
    glslang_mi
    glslang_gc
    glslang_ogl
    glslang_osd
    glslang_drl
    -Wl,--end-group
    ${vulkan-lib}
    android
    jnigraphics
    dl
    log)
