set(CMAKE_C_COMPILER  "cc" CACHE PATH "")
set(CMAKE_CXX_COMPILER  "hipcc" CACHE PATH "")
set(CMAKE_Fortran_COMPILER "ftn" CACHE PATH "")

# gtensor refuses to compile without CXX compiler as hipcc, which is really a pain
# in the ass when combined with CMake not scraping flags out of cc. Really, the entire
# hip toolchain is busted here, which is honestly kind of what I expect out of gtensor.
#set(HDF5_INCLUDE_DIRS "/opt/cray/pe/hdf5-parallel/1.14.3.5/amd/6.0/include" CACHE PATH "")
set(HDF5_DIR "/opt/cray/pe/hdf5-parallel/1.14.3.5/amd/6.0/" CACHE PATH "")
set(HDF5_INCLUDE_DIRS "${HDF5_DIR}/include" CACHE PATH "")
set(HDF5_HL_LIBRARIES "${HDF5_DIR}/libhdf5_hl_parallel.so")

set(CMAKE_CXX_FLAGS "-I${HDF5_INCLUDE_DIRS}" CACHE PATH "")

set(PSC_GPU "hip" CACHE STRING "")
set(USE_CUDA "True" CACHE BOOL "")
set(CMAKE_HIP_ARCHITECTURES "gfx90a")
set(AMDGPU_TARGETS "gfx90a" CACHE STRING "")

set(HDF5_FIND_DEBUG "true")
set(HDF5_NO_FIND_PACKAGE_CONFIG_FILE "true")

#set(HDF5_INCLUDE_DIRS "/opt/cray/pe/hdf5-parallel/1.14.3.5/amd/6.0/include" CACHE PATH "")