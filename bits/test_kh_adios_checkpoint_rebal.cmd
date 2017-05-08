
# sample kelvin-helmholtz run, with checkpointing

ADIOS=0

mpirun -n 8 ../src/psc_kelvin_helmholtz \
    --gdims_y 80 --gdims_z 80 \
    --np_y 10 --np_z 10 \
    --nmax 101 \
    --output_fields e,h,j,n,v \
    --write_tfield no \
    --write_pfield yes --pfield_step 5 \
    --write_checkpoint --write_checkpoint_every_step 10 --psc_adios_checkpoint $ADIOS \
    --psc_push_particles_type 1vb_double --checkpoint_method "MPI_AMR" \
    --checkpoint_transport_options "have_metadata_file=0,num_aggregators=4" \
    --psc_push_fields_type c --psc_detailed_profiling 0 \
    --psc_bnd_fields_type c \
    --psc_bnd_type c \
    --psc_bnd_particles_type c \
    --psc_balance_every 1

mkdir -p first_run
mv *.xdmf *.h5 first_run

# xterm -e lldb -- 
if [ "$ADIOS" ]; then
   cd checkpoint.00000050
   bpmeta checkpoint.000050.bp
   cd ..
fi

mpirun -n 8 ../src/psc_kelvin_helmholtz \
    --from_checkpoint 50 --checkpoint_nmax 101 \
    --adios_checkpoint $ADIOS

if [ $? -eq 0 ]; then
   h5dump first_run/pfd.000100_p000000.h5 > original.dump
   h5dump pfd.000100_p000000.h5 > restart.dump

   diff original.dump restart.dump
else
  echo "Restarted run failed!"
fi
