
#include "psc.h"
#include "psc_fields_c.h"

#include <mrc_params.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void
psc_fields_c_setup(struct psc_fields *pf)
{
  unsigned int size = 1;
  for (int d = 0; d < 3; d++) {
    size *= pf->im[d];
  }
#ifdef USE_CBE
  // The Cell processor translation can use the C fields with one modification:
  // the data needs to be 128 byte aligned (to speed off-loading to spes). This
  // change is roughly put in below.
  void *m;
  int ierr = posix_memalign(&m, 128, nr_comp * size * sizeof(*pf->flds));
  pf->flds =  m; 
  assert(ierr == 0);
#else
  pf->data = calloc(pf->nr_comp * size, sizeof(fields_c_real_t));
#endif
}

static void
psc_fields_c_destroy(struct psc_fields *pf)
{
  free(pf->data);
}

static void
psc_fields_c_zero_comp(struct psc_fields *pf, int m)
{
  memset(&F3_C(pf, m, pf->ib[0], pf->ib[1], pf->ib[2]), 0,
	 pf->im[0] * pf->im[1] * pf->im[2] * sizeof(fields_c_real_t));
}

static void
psc_fields_c_set_comp(struct psc_fields *pf, int m, double _val)
{
  fields_c_real_t val = _val;

  for (int jz = pf->ib[2]; jz < pf->ib[2] + pf->im[2]; jz++) {
    for (int jy = pf->ib[1]; jy < pf->ib[1] + pf->im[1]; jy++) {
      for (int jx = pf->ib[0]; jx < pf->ib[0] + pf->im[0]; jx++) {
	F3_C(pf, m, jx, jy, jz) = val;
      }
    }
  }
}

static void
psc_fields_c_scale_comp(struct psc_fields *pf, int m, double _val)
{
  fields_c_real_t val = _val;

  for (int jz = pf->ib[2]; jz < pf->ib[2] + pf->im[2]; jz++) {
    for (int jy = pf->ib[1]; jy < pf->ib[1] + pf->im[1]; jy++) {
      for (int jx = pf->ib[0]; jx < pf->ib[0] + pf->im[0]; jx++) {
	F3_C(pf, m, jx, jy, jz) *= val;
      }
    }
  }
}

static void
psc_fields_c_copy_comp(struct psc_fields *pto, int m_to, struct psc_fields *pfrom, int m_from)
{
  for (int jz = pto->ib[2]; jz < pto->ib[2] + pto->im[2]; jz++) {
    for (int jy = pto->ib[1]; jy < pto->ib[1] + pto->im[1]; jy++) {
      for (int jx = pto->ib[0]; jx < pto->ib[0] + pto->im[0]; jx++) {
	F3_C(pto, m_to, jx, jy, jz) = F3_C(pfrom, m_from, jx, jy, jz);
      }
    }
  }
}

static void
psc_fields_c_axpy_comp(struct psc_fields *y, int ym, double _a, struct psc_fields *x, int xm)
{
  fields_c_real_t a = _a;

  for (int jz = y->ib[2]; jz < y->ib[2] + y->im[2]; jz++) {
    for (int jy = y->ib[1]; jy < y->ib[1] + y->im[1]; jy++) {
      for (int jx = y->ib[0]; jx < y->ib[0] + y->im[0]; jx++) {
	F3_C(y, ym, jx, jy, jz) += a * F3_C(x, xm, jx, jy, jz);
      }
    }
  }
}

// ======================================================================

#ifdef HAVE_LIBHDF5_HL

#include <mrc_io.h>

// FIXME. This is a rather bad break of proper layering, HDF5 should be all
// mrc_io business. OTOH, it could be called flexibility...

#include <hdf5.h>
#include <hdf5_hl.h>

#define H5_CHK(ierr) assert(ierr >= 0)
#define CE assert(ierr == 0)

// FIXME : adios is nested inside HDF5 at the moment, even though it 
// doesn't need to be.
#ifdef HAVE_ADIOS
#include <psc_adios.h>

// Define the adios group for particle output of this type
// FIXME: this being hard-coded at the particle type level bothers me,
// but that's a bigger problem with how the particle i/o works

// Calculate this patches contribution to the ADIOS payload
static uint64_t
psc_fields_c_adios_size(struct psc_fields *fields)
{
  // sizeof(nparts) + nparts * 10 * sizeof(double)
  uint64_t fldsize = sizeof(double) * fields->nr_comp;

  for (int d = 0; d < 3; d++) {
    fldsize *= fields->im[d];
  }
  return 1 * sizeof(int) + fldsize;
}

static void
psc_fields_c_define_adios_vars(struct psc_fields *fields, const char *path, int64_t m_adios_group)
{
  
  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));
  // ------------
  // This all gets written from the psc_fields class param definition, and isn't needed here.
  // ------------
  // sprintf(varnames, "%s/p", path);
  // adios_define_var(m_adios_group, varnames, "", adios_integer, "","","");

  // sprintf(varnames, "%s/ib", path);
  // adios_define_var(m_adios_group, varnames, "", adios_integer, "3","","");

  // // Because of our wacky backwards memory ordering we have to 
  // // allocate a bigass string to hold the 3 individualy written
  // // im variables + nr_comp
  // char *varcomp = malloc(sizeof(*varcomp) * 4*(strlen(path) + 10));

  // sprintf(varcomp, "%s/nr_comp", path);
  // adios_define_var(m_adios_group, varcomp, "", adios_integer, "","","");

  // for (int d = 2; d >= 0; d--) {
  //   sprintf(varnames, "%s/im%d", path, d);
  //   adios_define_var(m_adios_group, varnames, "", adios_integer, "", "", "");
  //   strcat(varcomp, ", ");
  //   strcat(varcomp, varnames);
  // }

  // FIXME : For some reason adios complains when I try to define the dimensions
  // of field_c using the varcomp string "nr_comp, im2, im1, im0". But it seems
  // to be okay if I just calculate the bulk length and use that as the dim, so 
  // that's what we'll do for now.
  sprintf(varnames, "%s/len", path);
  adios_define_var(m_adios_group, varnames, "", adios_integer, "", "", "");

  char *vardata = malloc(sizeof(*vardata) * (strlen(path) + 12));
  sprintf(vardata, "%s/fields_c", path);
  adios_define_var(m_adios_group, vardata, "", adios_double, varnames, "", "");

  free(vardata);
  free(varnames);
  //free(varcomp);
}

// write the particles using adios
static void
psc_fields_c_write_adios(struct psc_fields *fields, const char *path, int64_t fd_p)
{
  int ierr;
  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));
  // ------------
  // This all gets written from the psc_fields class param definition, and isn't needed here.
  // ------------

  // sprintf(varnames, "%s/p", path);
  // ierr = adios_write(fd_p, varnames, (void *) &fields->p); AERR(ierr);

  // sprintf(varnames, "%s/ib", path);
  // ierr = adios_write(fd_p, varnames, (void *) fields->ib); AERR(ierr);

  // sprintf(varnames, "%s/nr_comp", path);
  // ierr = adios_write(fd_p, varnames, (void *) &fields->nr_comp); AERR(ierr);

  // for (int d = 2; d >= 0; d--) {
  //   sprintf(varnames, "%s/im%d", path, d);
  //   ierr = adios_write(fd_p, varnames, (void *) &fields->im[d]); AERR(ierr);
  // }

  int len = fields->nr_comp;

  for (int d = 0; d < 3; d++) {
    len *= fields->im[d];
  }
  sprintf(varnames, "%s/len", path);
  ierr = adios_write(fd_p, varnames, (void *) &len); AERR(ierr);

  sprintf(varnames, "%s/fields_c", path);
  ierr = adios_write(fd_p, varnames, (void *) fields->data); AERR(ierr);

  free(varnames);

}

// write the particles using adios
static void
psc_fields_c_read_adios(struct psc_fields *fields, const char *path, ADIOS_FILE * afp)
{
  int ierr;

  char *varnames = malloc(sizeof(*varnames) * (strlen(path) + 50));

  // ------------
  // This all gets read from the psc_fields class param definition, and isn't needed here.
  // ------------

  // sprintf(varnames, "%s/p", path);
  // ADIOS_VARINFO *info = adios_inq_var(afp, varnames); assert(info);
  // fields->p = *(int *)info->value;
  // adios_free_varinfo(info);

  // int ib[3];
  // sprintf(varnames, "%s/ib", path);
  // ierr = adios_schedule_read(afp, NULL, varnames, 0, 1, (void *) ib); AERR(ierr);
  // ierr = adios_perform_reads(afp, 1); AERR(ierr);

  // sprintf(varnames, "%s/nr_comp", path);
  // info = adios_inq_var(afp, varnames); assert(info);
  // assert(fields->nr_comp == *(int *)info->value);
  // adios_free_varinfo(info);


  // for (int d = 2; d >= 0; d--) {
  //   sprintf(varnames, "%s/im%d", path, d);
  //   info = adios_inq_var(afp, varnames); assert(info);
  //   assert(fields->im[d] == *(int *)info->value);
  //   adios_free_varinfo(info);
  //   assert(ib[d] == fields->ib[d]);
  // }

  psc_fields_setup(fields);

  // Safety check
  int len = fields->nr_comp;

  for (int d = 0; d < 3; d++) {
    len *= fields->im[d];
  }
  sprintf(varnames, "%s/len", path);
  int filelen;
  ierr = adios_schedule_read(afp, NULL, varnames, 0, 1, 
                            (void *) &filelen); AERR(ierr);

  ierr = adios_perform_reads(afp, 1); AERR(ierr);

  assert(len == filelen);

  sprintf(varnames, "%s/fields_c", path);

  ierr = adios_schedule_read(afp, NULL, varnames, 0, 1, 
                            (void *) fields->data); AERR(ierr);

  ierr = adios_perform_reads(afp, 1); AERR(ierr);

  free(varnames);

}

#endif 

// ----------------------------------------------------------------------
// psc_fields_c_write

static void
psc_fields_c_write(struct psc_fields *flds, struct mrc_io *io)
{
  const char *path = mrc_io_obj_path(io, flds);
#ifdef HAVE_ADIOS
  if (strcmp(mrc_io_type(io),"adios_define") == 0) {
    int64_t gid;
    a_get_gid_t get_gid = (a_get_gid_t) mrc_io_get_method(io, "get_group_id");
    get_gid(io, &gid);
    psc_fields_c_define_adios_vars(flds, path, gid);
    return;
  }

  if (strcmp(mrc_io_type(io),"adios_size") == 0) {
    uint64_t patch_size = psc_fields_c_adios_size(flds);
    a_add_to_size_t add_size = (a_add_to_size_t) mrc_io_get_method(io, "add_to_size");
    add_size(io, patch_size);
    return;
  }

  if (strcmp(mrc_io_type(io),"adios_write") == 0) {
    int64_t fd_p;
    a_get_write_t get_write_file = (a_get_write_t) mrc_io_get_method(io, "get_write_file");
    get_write_file(io, &fd_p);
    psc_fields_c_write_adios(flds, path, fd_p);
    return;
  }
#endif

  int ierr;
  long h5_file;
  mrc_io_get_h5_file(io, &h5_file);
  hid_t group = H5Gopen(h5_file, mrc_io_obj_path(io, flds), H5P_DEFAULT); H5_CHK(group);
  ierr = H5LTset_attribute_int(group, ".", "p", &flds->p, 1); CE;
  ierr = H5LTset_attribute_int(group, ".", "ib", flds->ib, 3); CE;
  ierr = H5LTset_attribute_int(group, ".", "im", flds->im, 3); CE;
  ierr = H5LTset_attribute_int(group, ".", "nr_comp", &flds->nr_comp, 1); CE;
  // write components separately instead?
  hsize_t hdims[4] = { flds->nr_comp, flds->im[2], flds->im[1], flds->im[0] };
  ierr = H5LTmake_dataset_double(group, "fields_c", 4, hdims, flds->data); CE;
  ierr = H5Gclose(group); CE;
}

// ----------------------------------------------------------------------
// psc_fields_c_read

static void
psc_fields_c_read(struct psc_fields *flds, struct mrc_io *io)
{
#ifdef HAVE_ADIOS
  const char *path = mrc_io_obj_path(io, flds);
  if (strcmp(mrc_io_type(io),"adios") == 0) {
    ADIOS_FILE *rfp;
    a_get_read_t get_read_file = (a_get_read_t) mrc_io_get_method(io, "get_read_file");
    get_read_file(io, &rfp);
    psc_fields_c_read_adios(flds, path, rfp);
    return;
  }
#endif

  int ierr;
  long h5_file;
  mrc_io_get_h5_file(io, &h5_file);
  hid_t group = H5Gopen(h5_file, mrc_io_obj_path(io, flds), H5P_DEFAULT); H5_CHK(group);
  int ib[3], im[3], nr_comp;
  ierr = H5LTget_attribute_int(group, ".", "p", &flds->p); CE;
  ierr = H5LTget_attribute_int(group, ".", "ib", ib); CE;
  ierr = H5LTget_attribute_int(group, ".", "im", im); CE;
  ierr = H5LTget_attribute_int(group, ".", "nr_comp", &nr_comp); CE;
  for (int d = 0; d < 3; d++) {
    assert(ib[d] == flds->ib[d]);
    assert(im[d] == flds->im[d]);
  }
  assert(nr_comp == flds->nr_comp);
  psc_fields_setup(flds);
  ierr = H5LTread_dataset_double(group, "fields_c", flds->data); CE;
  ierr = H5Gclose(group); CE;
}

#endif

// ======================================================================
// psc_mfields: subclass "c"
  
struct psc_mfields_ops psc_mfields_c_ops = {
  .name                  = "c",
};

// ======================================================================
// psc_fields: subclass "c"
  
struct psc_fields_ops psc_fields_c_ops = {
  .name                  = "c",
  .setup                 = psc_fields_c_setup,
  .destroy               = psc_fields_c_destroy,
#ifdef HAVE_LIBHDF5_HL
  .read                  = psc_fields_c_read,
  .write                 = psc_fields_c_write,
#endif
  .zero_comp             = psc_fields_c_zero_comp,
  .set_comp              = psc_fields_c_set_comp,
  .scale_comp            = psc_fields_c_scale_comp,
  .copy_comp             = psc_fields_c_copy_comp,
  .axpy_comp             = psc_fields_c_axpy_comp,
};

