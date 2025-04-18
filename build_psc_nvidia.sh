#! /bin/bash

rm -rf psc_build

root_dir=${PWD}
source ${root_dir}/host-configs/environments/pinoak-psc.sh
mkdir -p psc_build
pushd psc_build

cmake -C ${root_dir}/host-configs/psc/pinoak-nvidia.cmake ${root_dir}/psc/

