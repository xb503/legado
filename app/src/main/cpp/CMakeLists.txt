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

# Import static ncnn library
add_library(ncnn STATIC IMPORTED)
set_target_properties(ncnn PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libncnn.a)

# Real-CUGAN JNI shared library
add_library(realcugan_ncnn SHARED realcugan_jni.cpp)

target_link_libraries(realcugan_ncnn
    ncnn
    android
    log)
