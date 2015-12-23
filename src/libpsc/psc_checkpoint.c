
#include "psc.h"
#include "psc_glue.h"

#include "mrc_io.h"

#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

// ----------------------------------------------------------------------
// psc_read_checkpoint

struct psc *
psc_read_checkpoint(MPI_Comm comm, int n)
{
  mpi_printf(MPI_COMM_WORLD, "INFO: Reading checkpoint.\n");

  char dir[30];
  sprintf(dir, "checkpoint.%08d", n);

  struct mrc_io *io = mrc_io_create(comm);
  mrc_io_set_type(io, "xdmf_serial");
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

#ifdef HAVE_ADIOS
#include "psc_adios.h"

static bool
psc_adios_define(struct psc *psc)
{
  int ierr;
  int64_t m_adios_group;
  
  // Declare the particles group, without any statistics
  ierr = adios_declare_group(&m_adios_group, "mparticles", "", adios_flag_no); AERR(ierr);
  ierr = adios_select_method(m_adios_group, "MPI", "", ""); AERR(ierr);

  psc_foreach_patch(psc, p) {
    struct psc_particles *prts = psc_mparticles_get_patch(psc->particles, p);
    psc_particles_define_vars_adios(prts, psc->mrc_domain, m_adios_group);
  }
  return true;
}

static void
psc_adios_write(struct psc *psc)
{
  int ierr;
  // the adios group for writing (collective call)
  char filename[256];
  sprintf(filename, "checkpoint_particles.%08d.bp", psc->timestep); // adios particle file
  int64_t fd_p; // adios file pointer
  ierr = adios_open(&fd_p, "mparticles", filename, "w", psc_comm(psc)); AERR(ierr);

  // calculate and set the payload size
  uint64_t group_size = 0;
  psc_foreach_patch(psc, p) {
    struct psc_particles *prts = psc_mparticles_get_patch(psc->particles, p);
    group_size += psc_particles_calc_size_adios(prts);
  }
  uint64_t total_size;
  ierr = adios_group_size(fd_p, group_size, &total_size); AERR(ierr);

  // Issue the write calls for each patch
  psc_foreach_patch(psc, p) {
    struct psc_particles *prts = psc_mparticles_get_patch(psc->particles, p);
    psc_particles_write_adios(prts, psc->mrc_domain, fd_p);
  }

  ierr = adios_close(fd_p); AERR(ierr);
}
#endif

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

#ifdef HAVE_ADIOS

  static bool adios_defined;

  if (psc->prm.adios_checkpoint) {
    if (!adios_defined) {
      adios_defined = psc_adios_define(psc);
    }
    psc_adios_write(psc);
  }

#endif

}

