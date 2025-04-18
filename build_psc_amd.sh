#! /bin/bash

rm -rf psc_build

root_dir=${PWD}
source ${root_dir}/host-configs/environments/pinoak-psc-amd.sh
mkdir -p psc_build
pushd psc_build

cmake -C ${root_dir}/host-configs/psc/pinoak-amd.cmake ${root_dir}/psc/

make -j