#!/bin/sh 

mpirun -n 8 ../mhd_harris \
    --mrc_domain_mx 64 --mrc_domain_my 128 --mrc_domain_mz 1 \
    --mrc_domain_npx 2 --mrc_domain_npy 4 \
    \
    --ggcm_mhd_d_i 0. \
    --ggcm_mhd_diffconstant 0. \
    --ggcm_mhd_magdiffu const \
    --ggcm_mhd_thx 0.8 \
    \
    --ggcm_mhd_step_type c3_double \
    --xggcm_mhd_step_debug_dump \
    --ggcm_mhd_step_do_nwst \
    \
    --ggcm_mhd_diag_fields rr:pp:v:b:divb:j \
    \
    --mrc_ts_max_time 40. \
    --mrc_ts_output_every_time 1. \
    --mrc_ts_dt 1e-3 \
    \
    2>&1 | tee log

#./plot.py
