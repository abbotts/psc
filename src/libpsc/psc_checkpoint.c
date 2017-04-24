
#include "psc.h"
#include "psc_glue.h"

#include "mrc_io.h"

#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

// ----------------------------------------------------------------------
// psc_read_checkpoint

#ifdef HAVE_ADIOS
#include "psc_adios.h"
#include <adios_read.h>

#endif


struct psc *
psc_read_checkpoint(MPI_Comm comm, int n)
{
  mpi_printf(MPI_COMM_WORLD, "INFO: Reading checkpoint.\n");

  char dir[30];
  sprintf(dir, "checkpoint.%08d", n);

  struct mrc_io *io = mrc_io_create(comm);
  int adios_checkpoint = 0;
  mrc_params_get_option_int("adios_checkpoint", &adios_checkpoint);
  if (adios_checkpoint) {
    mrc_io_set_type(io, "adios");
  } else {
    mrc_io_set_type(io, "xdmf_serial");
  }
  mrc_io_set_name(io, "checkpoint");
  mrc_io_set_param_string(io, "basename", "checkpoint");
  mrc_io_set_param_string(io, "outdir", dir);
  mrc_io_set_from_options(io);
  mrc_io_setup(io);
  mrc_io_open(io, "r", n, 0.);
  struct psc *psc = mrc_io_read_path(io, "checkpoint", "psc", psc);
  mrc_io_close(io);
  mrc_io_destroy(io);

  ppsc = psc;
  return psc;
}

// ----------------------------------------------------------------------
// psc_write_checkpoint

void
psc_write_checkpoint(struct psc *psc)
{
  mpi_printf(psc_comm(psc), "INFO: Writing checkpoint.\n");

  char dir[30];
  sprintf(dir, "checkpoint.%08d", psc->timestep);
  int rank;
  MPI_Comm_rank(psc_comm(psc), &rank);
  if (rank == 0) {
    if (mkdir(dir, 0777)) {
      if (errno != EEXIST) {
	perror("ERROR: mkdir");
	abort();
      }
    }
  }
  MPI_Barrier(psc_comm(psc));

#ifdef HAVE_ADIOS
  static bool adios_group_defined;
  if (psc->prm.adios_checkpoint) {
    const char *adios_steps[] = { "adios_define", "adios_size", "adios_write"};

    uint64_t payload_size = 0;
    for (int step = 0; step < 3; step++) {

      if (adios_group_defined && (step == 0)) continue;

      struct mrc_io *io = mrc_io_create(psc_comm(psc));
      mrc_io_set_type(io, adios_steps[step]);
      mrc_io_set_name(io, "checkpoint");
      mrc_io_set_param_string(io, "basename", "checkpoint");
      mrc_io_set_param_string(io, "outdir", dir);
      mrc_io_set_from_options(io);
      mrc_io_setup(io);

      if (step == 2) {
        a_set_size_t set_payload_size = (a_set_size_t) mrc_io_get_method(io, "set_group_size");
        set_payload_size(io, payload_size);
      }

      mrc_io_open(io, "w", psc->timestep, psc->timestep * psc->dt);
      mrc_io_write_path(io, "checkpoint", "psc", psc);
      mrc_io_close(io);

      if (step == 1) {
        a_get_size_t get_payload_size = (a_get_size_t) mrc_io_get_method(io, "final_size");
        get_payload_size(io, &payload_size);
      }

      mrc_io_destroy(io);

      if (step == 0) {
        adios_group_defined = true;
      }
    }
  } else {

#else

  {

#endif

  struct mrc_io *io = mrc_io_create(psc_comm(psc));
  mrc_io_set_type(io, "xdmf_serial");
  mrc_io_set_name(io, "checkpoint");
  mrc_io_set_param_string(io, "basename", "checkpoint");
  mrc_io_set_param_string(io, "outdir", dir);
  mrc_io_set_from_options(io);
  mrc_io_setup(io);
  mrc_io_open(io, "w", psc->timestep, psc->timestep * psc->dt);
  mrc_io_write_path(io, "checkpoint", "psc", psc);
  mrc_io_close(io);
  mrc_io_destroy(io);
  }
}

