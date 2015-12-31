
#include "psc.h"
#include "psc_particles_c.h"

#include <mrc_io.h>
#include <stdlib.h>
#include <assert.h>

// ======================================================================
// psc_particles "c"

static void
psc_particles_c_setup(struct psc_particles *prts)
{
  struct psc_particles_c *c = psc_particles_c(prts);

  c->n_alloced = prts->n_part * 1.2;
  c->particles = calloc(c->n_alloced, sizeof(*c->particles));
}

static void
psc_particles_c_destroy(struct psc_particles *prts)
{
  struct psc_particles_c *c = psc_particles_c(prts);

  free(c->particles);
}

void
particles_c_realloc(struct psc_particles *prts, int new_n_part)
{
  struct psc_particles_c *c = psc_particles_c(prts);
  if (new_n_part <= c->n_alloced)
    return;

  c->n_alloced = new_n_part * 1.2;
  c->particles = realloc(c->particles, c->n_alloced * sizeof(*c->particles));
}

// ======================================================================

#ifdef HAVE_LIBHDF5_HL

// FIXME. This is a rather bad break of proper layering, HDF5 should be all
// mrc_io business. OTOH, it could be called flexibility...

#include <hdf5.h>
#include <hdf5_hl.h>

#define H5_CHK(ierr) assert(ierr >= 0)
#define CE assert(ierr == 0)

#ifdef HAVE_ADIOS
#include <psc_adios.h>

// Define the adios group for particle output of this type
// FIXME: this being hard-coded at the particle type level bothers me,
// but that's a bigger problem with how the particle i/o works

// Calculate this patches contribution to the ADIOS payload
static uint64_t
psc_particles_c_adios_size(struct psc_particles *prts)
{
  // FIXME: obviously this could be made more general, if we need it to do float particles
  assert(sizeof(particle_c_t) / sizeof(particle_c_real_t) == 10);
  assert(sizeof(particle_c_real_t) == sizeof(double));
  // sizeof(nparts) + nparts * 10 * sizeof(double)
  return 3 * sizeof(int) + prts->n_part * 10 * sizeof(double);
}

static void
psc_particles_c_define_adios_vars(struct psc_particles *prts, const char *path, int64_t m_adios_group)
{
  assert(sizeof(particle_c_t) / sizeof(particle_c_real_t) == 10);
  assert(sizeof(particle_c_real_t) == sizeof(double));
  
  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));
  sprintf(varnames, "%s/p", path);
  adios_define_var(m_adios_group, varnames, "", adios_integer, "","","");

  sprintf(varnames, "%s/flags", path);
  adios_define_var(m_adios_group, varnames, "", adios_unsigned_integer, "","","");


  sprintf(varnnames, "%s/n_part", path);
  adios_define_var(m_adios_group, varnames, "", adios_integer, "","","");

  char *vardata;
  asprintf(&vardata, "%s/particles", path);
  strcat(varnames, ", 10");

  adios_define_var(m_adios_group, vardata, "", adios_double, varnames, "", "");

  free(vardata);
  free(varnames);
}

// write the particles using adios
static void
psc_particles_c_write_adios(struct psc_particles *prts, const char *path, int64_t fd_p)
{
  int ierr;

  assert(sizeof(particle_c_t) / sizeof(particle_c_real_t) == 10);
  assert(sizeof(particle_c_real_t) == sizeof(double));

  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));

  sprintf(varnames, "%s/p", path);
  ierr = adios_write(fd_p, varnames, (void *) &prts->p);

  sprintf(varnames, "%s/flags", path);
  ierr = adios_write(fd_p, varnames, (void *) &prts->flags);

  sprintf(varnnames, "%s/n_part", path);
  ierr = adios_write(fd_p, varnames, (void *) &prts->n_part);

  sprintf(varnames, "%s/particles_c", path);
  ierr = adios_write(fd_p, varnames, (void *) particles_c_get_one(prts, 0)); AERR(ierr);

  free(varnames);
}

// write the particles using adios
static void
psc_particles_c_read_adios(struct psc_particles *prts, const char *path, const ADIOS_FILE * afp)
{
  int ierr;

  assert(sizeof(particle_c_t) / sizeof(particle_c_real_t) == 10);
  assert(sizeof(particle_c_real_t) == sizeof(double));

  // FIXME: I'm going to assume that the uids for particles patches
  // are GLOBALLY unique, and use those as the read/write path.  If
  // the loaded checkpoint fails to reproduce the original result,
  // this is probably why
  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));

  sprintf(varnames, "%s/p", path);
  ADIOS_VARINFO *info = adios_inq_var(afp, varnames); assert(info);
  prts->p = *(int *)info->value;
  adios_free_varinfo(info);

  sprintf(varnames, "%s/flags", path);
  ADIOS_VARINFO *info = adios_inq_var(afp, varnames); assert(info);
  prts->flags = *(unsigned int *)info->value;
  adios_free_varinfo(info);

  sprintf(varnames, "%s/n_part", path);
  ADIOS_VARINFO *info = adios_inq_var(afp, varnames); assert(info);
  prts->n_part = *(int *)info->value;
  adios_free_varinfo(info);

  psc_particles_setup(prts);

  ierr = adios_schedule_read(afp, NULL, vardata, 0, 1, 
                            (void *) particles_c_get_one(prts, 0)); AERR(ierr);

  ierr = adios_perform_reads(afp, 1); AERR(ierr);

  free(varnames);

}

#endif 

// ----------------------------------------------------------------------
// psc_particles_c_write

static void
psc_particles_c_write(struct psc_particles *prts, struct mrc_io *io)
{
  int ierr;  assert(sizeof(particle_c_t) / sizeof(particle_c_real_t) == 10);
  assert(sizeof(particle_c_real_t) == sizeof(double));

  const char *path = mrc_io_obj_path(io, prts);
#ifdef HAVE_ADIOS
  if (strcmp(mrc_io_type(io),"adios_define") == 0) {
    int64_t gid;
    a_get_gid_t get_gid = (a_get_gid_t) mrc_io_get_method(io, "get_group_id");
    get_gid(io, &gid);
    psc_particles_c_define_adios_vars(prts, path, gid);
    return;
  }

  if (strcmp(mrc_io_type(io),"adios_size") == 0) {
    uint64_t patch_size = psc_particles_c_adios_size(prts);
    a_add_to_size_t add_size = (a_add_to_size_t) mrc_io_get_method(io, "add_to_size");
    add_size(io, patch_size);
    return;
  }

  if (strcmp(mrc_io_type(io),"adios") == 0) {
    int64_t fd_p;
    a_get_write_t get_write_file = (a_get_write_t) mrc_io_get_method(io, "get_write_file");
    get_write_file(io, &fd_p);
    psc_particles_c_write_adios(prts, path, fd_p);
    return;
  }
#endif

  long h5_file;
  mrc_io_get_h5_file(io, &h5_file);

  hid_t group = H5Gopen(h5_file, mrc_io_obj_path(io, prts), H5P_DEFAULT); H5_CHK(group);
  // save/restore n_alloced, too?
  ierr = H5LTset_attribute_int(group, ".", "p", &prts->p, 1); CE;
  ierr = H5LTset_attribute_int(group, ".", "n_part", &prts->n_part, 1); CE;
  ierr = H5LTset_attribute_uint(group, ".", "flags", &prts->flags, 1); CE;

  if (prts->n_part > 0) {
    // in a rather ugly way, we write the long "kind" member as a double
    hsize_t hdims[2] = { prts->n_part, 10 };
    ierr = H5LTmake_dataset_double(group, "particles_c", 2, hdims,
				   (double *) particles_c_get_one(prts, 0)); CE;
  }
  ierr = H5Gclose(group); CE;
}

// ----------------------------------------------------------------------
// psc_particles_c_read

static void
psc_particles_c_read(struct psc_particles *prts, struct mrc_io *io)
{
#ifdef HAVE_ADIOS
  const char *path = mrc_io_obj_path(io, prts);
  if (strcmp(mrc_io_type(io),"adios") == 0) {
    ADIOS_FILE *rfp;
    a_get_read_t get_read_file = (a_get_read_t) mrc_io_get_method(io, "get_read_file");
    get_read_file(io, &rfp);
    psc_particles_c_read_adios(prts, path, rfp);
    return;
  }
#endif

  int ierr;
  long h5_file;
  mrc_io_get_h5_file(io, &h5_file);

  hid_t group = H5Gopen(h5_file, mrc_io_obj_path(io, prts), H5P_DEFAULT); H5_CHK(group);
  ierr = H5LTget_attribute_int(group, ".", "p", &prts->p); CE;
  ierr = H5LTget_attribute_int(group, ".", "n_part", &prts->n_part); CE;
  ierr = H5LTget_attribute_uint(group, ".", "flags", &prts->flags); CE;
  psc_particles_setup(prts);

  if (prts->n_part > 0) {
    ierr = H5LTread_dataset_double(group, "particles_c",
				   (double *) particles_c_get_one(prts, 0)); CE;
  }
  ierr = H5Gclose(group); CE;
}

#endif

// ======================================================================
// psc_mparticles: subclass "c"
  
struct psc_mparticles_ops psc_mparticles_c_ops = {
  .name                    = "c",
};

// ======================================================================
// psc_particles: subclass "c"

struct psc_particles_ops psc_particles_c_ops = {
  .name                    = "c",
  .size                    = sizeof(struct psc_particles_c),
  .setup                   = psc_particles_c_setup,
  .destroy                 = psc_particles_c_destroy,
#ifdef HAVE_LIBHDF5_HL
  .write                   = psc_particles_c_write,
  .read                    = psc_particles_c_read,
#endif
};
