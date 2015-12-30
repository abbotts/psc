
#include <mrc_io_private.h>
#include <mrc_list.h>
#include <mrc_params.h>
#include <adios.h>

#include <stdlib.h>
#include <string.h>

#define CE assert(ierr == 0)

#define AERR(ierr); if (ierr != 0) { adios_err_print(ierr); }

static inline void
adios_err_print(int ierr)
{
  const char * errmsg = adios_get_last_errmsg();
  fprintf(stderr, "ADIOS Error # %d: %s\n", ierr, errmsg);
  assert(0);
}

// ======================================================================
// adios_size
// ---------------------
// A special subclass that only exists to calculate the size of an adios group

struct mrc_adios_size {
  uint64_t group_size;
};

#define to_size(io) mrc_to_subobj(io, struct mrc_adios_size)

static void
_mrc_adios_size_open(struct mrc_io *io, const char *mode)
{
  assert(strcmp(mode, "w") == 0);

  struct mrc_adios_size *ads = to_size(io);
  ads->group_size = 0;
}

static void
_mrc_adios_size_close(struct mrc_io *io)
{

}

static void
_mrc_adios_size_attr(struct mrc_io *io, const char *path, int type,
		const char *name, union param_u *pv)
{
  struct mrc_adios_size *ads = to_size(io);
  
  switch (type) {
  case PT_SELECT:
  case PT_INT:
  case MRC_VAR_INT:
    ads->group_size += sizeof(int);
    break;
  case PT_BOOL: 
  case MRC_VAR_BOOL: {
    ads->group_size += sizeof(int);
    break;
  }
  case PT_FLOAT:
  case MRC_VAR_FLOAT:
    ads->group_size += sizeof(float);
    break;
  case PT_DOUBLE:
  case MRC_VAR_DOUBLE:
    ads->group_size += sizeof(double);
    break;
  case PT_STRING:
    if (pv->u_string) {
      ads->group_size += sizeof(char) * strlen(pv->u_string);
    } else {
      ads->group_size += sizeof(char) * strlen("(NULL)");
    }
    break;
  case PT_INT3:
    ads->group_size += 3 * sizeof(int);
    break;
  case PT_FLOAT3:
    ads->group_size += 3 * sizeof(float);
    break;
  case PT_DOUBLE3:
    ads->group_size += 3 * sizeof(double);
    break;
  case PT_INT_ARRAY:
    ads->group_size += (pv->u_int_array.nr_vals + 1) * sizeof(int); // plus 1 for the variable defining nr_vals
    break;
  case PT_PTR:
    break;
  default:
    mprintf("mrc_io_adios_size: not adding attr '%s' (type %d)\n",
	    name, type);
    assert(0);
  }
}

static void 
_mrc_adios_size_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{
  // mrc_flds are dumped as a single block with ghost points (and god help our soul), 
  // so there pretty easy to calculate the size of.

  struct mrc_adios_size *ads = to_size(io);

  // We write three ints to define nr_patches, nr_global_patches, and patch off
  ads->group_size += sizeof(int) * 3;

  // The size of the bulk field that we're dumping
  ads->group_size += fld->_size_of_type * fld->_len;
}

static void
_mrc_adios_final_size(struct mrc_io *io, uint64_t *final_size)
{
  struct mrc_adios_size *ads = to_size(io);
  *final_size = ads->group_size;
}

static struct mrc_obj_method mrc_adios_size_methods[] = {
  MRC_OBJ_METHOD("final_size",   _mrc_adios_final_size),
  {}
};


struct mrc_io_ops mrc_io_adios_size_ops = {
  .name          = "adios_size",
  .size          = sizeof(struct mrc_adios_size),
  .methods       = mrc_adios_size_methods,
  .open          = _mrc_adios_size_open,
  .close         = _mrc_adios_size_close,
  .write_attr    = _mrc_adios_size_attr,
  .write_fld     = _mrc_adios_size_fld,
};
#undef to_size

struct mrc_adios_define {
  int64_t group_id;
  char *method; ///< the transport method for this group
};

#define VAR(x) (void *)offsetof(struct mrc_adios_define, x)
static struct param adios_define_descr[] = {
  { "method"              , VAR(method)               , PARAM_STRING("MPI")      },
  {},
};
#undef VAR

#define to_define(io) mrc_to_subobj(io, struct mrc_adios_define)

static void
_mrc_adios_define_open(struct mrc_io *io, const char *mode)
{
  assert(strcmp(mode, "w") == 0);

  struct mrc_adios_define *adef = to_define(io);
  int ierr;
  // Declare a group using the object name, without any stats of time index
  ierr = adios_declare_group(&adef->group_id, mrc_io_name(io), "", adios_flag_no); AERR(ierr);

  const char *tmethod, *outdir;
  mrc_io_get_param_string(io, "method", &tmethod);
  mrc_io_get_param_string(io, "outdir", &outdir);
  ierr = adios_select_method(adef->group_id, tmethod, "", outdir); AERR(ierr);
}

static void
_mrc_adios_define_close(struct mrc_io *io)
{
  struct mrc_adios_define *adef = to_define(io);
  adef->group_id = 0;
}

static void
_mrc_adios_define_attr(struct mrc_io *io, const char *path, int type,
    const char *name, union param_u *pv)
{
  struct mrc_adios_define *adef = to_define(io);

  int64_t gid = adef->group_id;

  // Use path/name for adios name, and leave path blank.
  char *adname;

  asprintf(&adname, "%s/%s", path, name);
  char *nrname;

  switch (type) {
  case PT_SELECT:
  case PT_INT:
  case MRC_VAR_INT:
    adios_define_var(gid, adname, "", adios_integer, "", "", "");
    break;
  case PT_BOOL: 
  case MRC_VAR_BOOL: {
    adios_define_var(gid, adname, "", adios_integer, "", "", "");
    break;
  }
  case PT_FLOAT:
  case MRC_VAR_FLOAT:
    adios_define_var(gid, adname, "", adios_real, "", "", "");
    break;
  case PT_DOUBLE:
  case MRC_VAR_DOUBLE:
    adios_define_var(gid, adname, "", adios_double, "", "", "");
    break;
  case PT_STRING:
    adios_define_var(gid, adname, "", adios_string, "", "", "");
    break;

  case PT_INT3:
    adios_define_var(gid, adname, "", adios_integer, "3", "", "");
    break;

  case PT_FLOAT3:
    adios_define_var(gid, adname, "", adios_real, "3", "", "");
    break;

  case PT_DOUBLE3:
    adios_define_var(gid, adname, "", adios_double, "3", "", "");
    break;
  case PT_INT_ARRAY:
    asprintf(&nrname, "%s-nrvals", adname);
    adios_define_var(gid, nrname, "", adios_integer, "", "", "");
    adios_define_var(gid, adname, "", adios_integer, nrname, "", "");
    free(nrname);
    break;
  case PT_PTR:
    break;
  default:
    mpi_printf(mrc_io_comm(io), "mrc_io_hdf5_parallel: not writing attr '%s' (type %d)\n",
      adname, type);
    assert(0);
  }
  free(adname);
}
static void 
_mrc_adios_define_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{
  // mrc_flds are dumped as a single block with ghost points (and god help our soul), 
  // so there pretty easy to calculate the size of.

  struct mrc_adios_define *adef = to_define(io);
  int64_t gid = adef->group_id;

  // define variables for the ghost_dims

  const char *name = mrc_fld_name(fld);
  char *adname;

  asprintf(&adname, "%s/%s", path, name);

  char *dimnames = (char *) malloc(sizeof(*dimnames) * (strlen(adname) + 100));

  char *offstr, *dimstr, *gdimstr;
  // define vars for local/global patch numbers and offset
  sprintf(dimnames, "%s/nr_global_patches", adname);
  adios_define_var(gid, dimnames, "", adios_integer, "", "", "");
  gdimstr = (char *) malloc(sizeof(*gdimstr) * (strlen(dimnames) + 100));
  strcpy(gdimstr, dimnames);

  sprintf(dimnames, "%s/nr_local_patches", adname);
  adios_define_var(gid, dimnames, "", adios_integer, "", "", "");
  dimstr = (char *) malloc(sizeof(*dimstr) * (strlen(dimnames) + 100));
  strcpy(dimstr, dimnames);

  sprintf(dimnames, "%s/patch_off", adname);
  adios_define_var(gid, dimnames, "", adios_integer, "", "", "");
  offstr = (char *) malloc(sizeof(*offstr) * (strlen(dimnames) + 100));
  strcpy(offstr, dimnames);


  // line up patches in file
  // I'm going to assume here that since we're calling ADIOS from C,
  // it wants its dimensions specified with C order (like HDF5 does).
  // Maybe this doesn't matter, but it's how I did it for phdf5, and it's actually
  // a little bit easier this way.
  int nr_spatial_dims = fld->_nr_spatial_dims;
  int nr_file_dims = nr_spatial_dims + 1;


  int fdims[nr_file_dims]; // 1 comp + 3d or 1d size of each patch;

  // Sanity check that we've picked the right dims
  int deflen = mrc_fld_nr_patches(fld);

  // FIXME: assuming that patch size (with offs) won't ever change,
  // but patch number may
  for (int d=0; d<nr_file_dims; d++) {
    char valstr[20];
    fdims[d] = fld->_ghost_dims[nr_file_dims - 1 - d]; // patch is always (_ghost)_dims[nr_file_dims]
    sprintf(valstr, ", %d", fdims[d]);
    strcat(gdimstr, valstr);
    strcat(dimstr, valstr);    
    strcat(offstr, ", 0");
    deflen *= fdims[d];
  }

  assert(deflen == fld->_len);

  // Define the field

  int dtype;
  switch (fld->_data_type) {
  case MRC_NT_FLOAT:
    dtype = adios_real;
    break;
  case MRC_NT_DOUBLE:
    dtype = adios_double;
    break;
  case MRC_NT_INT:
    dtype = adios_integer;
    break;
  default:
    assert(0);
  }

  sprintf(dimnames, "%s/data", adname);
  adios_define_var(gid, dimnames, "", dtype, dimstr, gdimstr, offstr);

  free(adname);
  free(dimnames);
  free(offstr);
  free(gdimstr);
  free(dimstr);

}


struct mrc_io_ops mrc_io_adios_define_ops = {
  .name          = "adios_define",
  .size          = sizeof(struct mrc_adios_define),
  .param_descr   =  adios_define_descr,
  .open          = _mrc_adios_define_open,
  .close         = _mrc_adios_define_close,
  .write_attr    = _mrc_adios_define_attr,
  .write_fld     = _mrc_adios_define_fld,
};
#undef to_define

struct mrc_adios_io {
  int64_t adios_file;
  uint64_t total_size; ///< the transport method for this group
};

#define to_adios(io) mrc_to_subobj(io, struct mrc_adios_io)



// --------------------------------------------------
// _mrc_adios_write_fld
// This just dumps the full mrc_fld into a global array that INCLUDES ghost points, lined
// up as a list of patches. Adapted from hdf5_parallel, because that's the easiest way
// to output to a shared file without collisions.
//
// FIXME: adios doesn't support the writing of hyperslabs from memory, which means each variable
// needs to be a contiguous block. That's why ghost points are dumped, even for regular fields.
// We could work around this by defining a bunch of extra variables, but that's not ideal.
// Note that it does support hyperslab definiation for visualization scheme reading off
// a pipeline...
static void
_mrc_adios_write_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{

  struct mrc_adios_io *aio = to_adios(io);
  int64_t fd_p = aio->adios_file;

  int ierr;

  const char *name = mrc_fld_name(fld);
  char *adname;

  asprintf(&adname, "%s/%s", path, name);

  char *dimnames = (char *) malloc(sizeof(*dimnames) * (strlen(adname) + 100));

  // ## AWRITE : nr_global_patches
  int nr_patches;
  mrc_domain_get_nr_global_patches(fld->_domain, &nr_patches);
  sprintf(dimnames, "%s/nr_global_patches", adname);
  ierr = adios_write(fd_p, dimnames, (void *) &nr_patches); AERR(ierr);

  // FIXME: This dumps everything together (best for aos). There's no single
  // write cached way to do separate the fields in either aos or soa, though
  // aos has worse data fragmentation.
  
  // ## AWRITE : nr_local_patches
  int nr_local_patches = mrc_fld_nr_patches(fld);
  sprintf(dimnames, "%s/nr_local_patches", adname);
  ierr = adios_write(fd_p, dimnames, (void *) &nr_local_patches);

  // ## AWRITE : patch_offset
  struct mrc_patch_info info;
  mrc_domain_get_local_patch_info(fld->_domain, 0, &info);
  sprintf(dimnames, "%s/patch_off", adname);
  ierr = adios_write(fd_p, dimnames, (void *) &info.global_patch); AERR(ierr);

  // Write the actual mrc_fld data (just a dump of the array)
  // FIXME: Assuming that the patch dimensions cannot change from the
  // define step (although we're allowing the number of patches to change)
  sprintf(dimnames, "%s/data", adname);
  ierr = adios_write(fd_p, dimnames, fld->_arr); AERR(ierr);

  free(adname);
  free(dimnames);

}

#undef to_adios
