
#include <mrc_io_private.h>
#include <mrc_list.h>
#include <mrc_params.h>
#include <mrc_fld.h>
#include <mrc_ndarray.h>
#include <adios.h>
#include <adios_read.h>

#include <stdlib.h>
#include <string.h>

#define CE assert(ierr == 0)

#define AERR(ierr) {if (ierr != 0) { adios_err_print(ierr); } }

struct defined_group
{
  list_t group_list;
  char *name;
  int64_t gid;
};

LIST_HEAD(defined_adios_groups);

static inline size_t
sizeof_mrc_type(int mtype)
{
  switch(mtype) {
    case MRC_NT_FLOAT:
      return sizeof(int);
    case MRC_NT_DOUBLE:
      return sizeof(double);
    case MRC_NT_INT:
      return sizeof(int);
    default:
      // Bad data type
      assert(0);
  }
}

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
    ads->group_size += sizeof(int); // For the length of the string
    break;
  case PT_INT3:
    ads->group_size += 3 * sizeof(int);
    break;
  case PT_FLOAT3:
    ads->group_size += 3 * sizeof(float);
    break;
  case PT_DOUBLE3:
  case MRC_VAR_DOUBLE3:
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
  ads->group_size += sizeof(int) * 4; // and with +1 for the len hack

  // The size of the bulk field that we're dumping
  ads->group_size += sizeof_mrc_type(mrc_fld_data_type(fld)) * mrc_fld_len(fld);
}

static void
_mrc_adios_size_ndarray(struct mrc_io *io, const char *path, struct mrc_ndarray *nd)
{
  assert(nd->n_dims);
  assert(mrc_ndarray_f_contiguous(nd));

  struct mrc_adios_size *ads = to_size(io);
  // Mimic the hdf5_serial method for this, so lets hope we're not missing
  // something important here

  // We write one int to define how many dims, then n_dims ints for the size
  // This might not be neccessary: since dims is a param it should already be
  // written and we could just point to it. But that might be an optimization
  // for later on...
  // ads->group_size += sizeof(int) * (1 + nd->n_dims);

  // The bulk size of the data we're dumping
  ads->group_size += nd->size_of_type * nd->len;
}

static void
_mrc_adios_final_size(struct mrc_io *io, uint64_t *final_size)
{
  struct mrc_adios_size *ads = to_size(io);
  *final_size = ads->group_size;
}

// For things like PSC checkpointing that want to write things
// that don't live in mrc objects
static void
_mrc_adios_add_to_size(struct mrc_io *io, uint64_t add_size)
{
  struct mrc_adios_size *ads = to_size(io);
  ads->group_size += add_size;  
}

static struct mrc_obj_method mrc_adios_size_methods[] = {
  MRC_OBJ_METHOD("final_size",   _mrc_adios_final_size),
  MRC_OBJ_METHOD("add_to_size",  _mrc_adios_add_to_size),  
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
  .write_ndarray = _mrc_adios_size_ndarray,
};
#undef to_size

struct mrc_adios_define {
  int64_t group_id;
  char *method; ///< the transport method for this group
  char *trans_opts; ///< a comma separated string of options fof the select transport method
};

#define VAR(x) (void *)offsetof(struct mrc_adios_define, x)
static struct param adios_define_descr[] = {
  { "method"              , VAR(method)               , PARAM_STRING("MPI")      },
  { "transport_options"   , VAR(trans_opts)           , PARAM_STRING(NULL)       },
  {},
};
#undef VAR

#define to_define(io) mrc_to_subobj(io, struct mrc_adios_define)

static void
_mrc_adios_define_open(struct mrc_io *io, const char *mode)
{
  assert(strcmp(mode, "w") == 0);
  // Calling this method will generate the initial variable
  // definition of the adios group, and will force regeneration
  // if the group is already defined
  struct mrc_adios_define *adef = to_define(io);
  int ierr;
  struct defined_group *gptr;
  adef->group_id = 0;
  // See if we already have a group setup for this io name
  if (!list_empty(&defined_adios_groups)) {
    list_for_each_entry(gptr, &defined_adios_groups, group_list) {
      if(strcmp(gptr->name, mrc_io_name(io)) == 0) {
        adef->group_id = gptr->gid;
        break;
      }
    }
  }

  if (adef->group_id) {
    // If we have this io name already, delete the old definition
    ierr = adios_delete_vardefs(adef->group_id); AERR(ierr);
  } else {
    // Declare a group using the object name, without any stats or time index
    ierr = adios_declare_group(&adef->group_id, mrc_io_name(io), "", adios_flag_no); AERR(ierr);
    gptr = calloc(1, sizeof(*gptr));
    gptr->name = strdup(mrc_io_name(io));
    gptr->gid = adef->group_id;
    list_add_tail(&(gptr->group_list), &defined_adios_groups);
  }

  const char *tmethod, *outdir, *topts;
  mrc_io_get_param_string(io, "method", &tmethod);
  mrc_io_get_param_string(io, "transport_options", &topts);  
  if (!topts) {
    topts = "";
  }
  mrc_io_get_param_string(io, "outdir", &outdir);
  ierr = adios_select_method(adef->group_id, tmethod, topts, ""); AERR(ierr);
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
  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));
  assert(adname);
  sprintf(adname, "%s/%s", path, name);
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
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-sz", adname);
    adios_define_var(gid, nrname, "", adios_integer, "", "", "");
    adios_define_var(gid, adname, "", adios_string, "", "", "");
    free(nrname);
    break;

  case PT_INT3:
    adios_define_var(gid, adname, "", adios_integer, "3", "", "");
    break;

  case PT_FLOAT3:
    adios_define_var(gid, adname, "", adios_real, "3", "", "");
    break;

  case PT_DOUBLE3:
  case MRC_VAR_DOUBLE3:
    adios_define_var(gid, adname, "", adios_double, "3", "", "");
    break;
  case PT_INT_ARRAY:
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-nrvals", adname);
    adios_define_var(gid, nrname, "", adios_integer, "", "", "");
    adios_define_var(gid, adname, "", adios_integer, nrname, "", "");
    free(nrname);
    break;
  case PT_PTR:
    break;
  default:
    mpi_printf(mrc_io_comm(io), "mrc_io_adois: not writing attr '%s' (type %d)\n",
      adname, type);
    assert(0);
  }
  free(adname);
}

static void
_mrc_adios_define_ndarray(struct mrc_io *io, const char *path, struct mrc_ndarray *nd)
{
  assert(nd->n_dims);
  assert(mrc_ndarray_f_contiguous(nd));

  struct mrc_adios_define *adef = to_define(io);
  int64_t gid = adef->group_id;

  // I'm not 100% sure that adios can take a single variable name pointing
  // to an array on integers (dims, in this case) and make a sensible array
  // descriptor out of it. Either way, the order will be wrong, since adios will
  // assume C order and we're using F order. So just dump is as a block, like we
  // do with mrc_flds.


  int dtype;
  switch (nd->data_type) {
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

  // This should be written as a param from the class descriptor write (I hope)
  char *lenname = (char *) malloc(sizeof(*lenname) * (strlen(path) + 10));
  sprintf(lenname, "%s/len", path);

  char *datname = (char *) malloc(sizeof(*datname) * (strlen(path) + 10));
  sprintf(datname, "%s/data", path);

  adios_define_var(gid, datname, "", dtype, lenname, "", "");

  free(lenname);
  free(datname);
}

static void 
_mrc_adios_define_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{
  // mrc_flds are dumped as a single block with ghost points (and god help our soul), 
  // so there pretty easy to calculate the size of.
  int ierr;
  struct mrc_adios_define *adef = to_define(io);
  int64_t gid = adef->group_id;

  // define variables for the ghost_dims

  const char *name = mrc_fld_name(fld);
  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));

  sprintf(adname, "%s/%s", path, name);

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
  int nr_spatial_dims;
  ierr = mrc_fld_get_param_int(fld, "nr_spatial_dims", &nr_spatial_dims); CE;
  int nr_file_dims = nr_spatial_dims + 1;


  int fdims[nr_file_dims]; // 1 comp + 3d or 1d size of each patch;

  // Sanity check that we've picked the right dims
  int deflen = mrc_fld_nr_patches(fld);

  // FIXME: assuming that patch size (with offs) won't ever change,
  // but patch number may
  for (int d=0; d<nr_file_dims; d++) {
    char valstr[20];
    fdims[d] = mrc_fld_ghost_dims(fld)[nr_file_dims - 1 - d]; // patch is always (_ghost)_dims[nr_file_dims]
    sprintf(valstr, ", %d", fdims[d]);
    strcat(gdimstr, valstr);
    strcat(dimstr, valstr);    
    strcat(offstr, ", 0");
    deflen *= fdims[d];
  }

  assert(deflen == mrc_fld_len(fld));

  // Define the field

  int dtype;
  switch (mrc_fld_data_type(fld)) {
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

  sprintf(dimstr, "%s/len", adname);
  adios_define_var(gid, dimstr, "", adios_integer, "", "", "");

  sprintf(dimnames, "%s/data", adname);
  adios_define_var(gid, dimnames, "", dtype, dimstr, "", "");

  free(adname);
  free(dimnames);
  free(offstr);
  free(gdimstr);
  free(dimstr);

}
static void
_mrc_adios_get_group_id(struct mrc_io *io, int64_t *gid)
{
  struct mrc_adios_define *aio = to_define(io);
  *gid = aio->group_id;
}

static struct mrc_obj_method mrc_adios_define_methods[] = {
  MRC_OBJ_METHOD("get_group_id",   _mrc_adios_get_group_id),    
  {}
};

struct mrc_io_ops mrc_io_adios_define_ops = {
  .name          = "adios_define",
  .size          = sizeof(struct mrc_adios_define),
  .methods       = mrc_adios_define_methods,
  .param_descr   =  adios_define_descr,
  .open          = _mrc_adios_define_open,
  .close         = _mrc_adios_define_close,
  .write_attr    = _mrc_adios_define_attr,
  .write_fld     = _mrc_adios_define_fld,
  .write_ndarray = _mrc_adios_define_ndarray,
};
#undef to_define

struct mrc_adios_io {
  int64_t write_file;
  ADIOS_FILE *read_file;
  ADIOS_SELECTION *selection;
  uint64_t group_size; 
  char *mode;
};

#define to_adios(io) mrc_to_subobj(io, struct mrc_adios_io)

static void
_mrc_adios_open(struct mrc_io *io, const char *mode)
{
  struct mrc_adios_io *aio = to_adios(io);

  aio->mode = strdup(mode);
  char filename[strlen(io->par.outdir) + strlen(io->par.basename) + 20];
  sprintf(filename, "%s/%s.%06d.bp", io->par.outdir, io->par.basename,
    io->step);

  int ierr;

  // This is now a pure writing method
  assert(strcmp(mode, "w") == 0);

  ierr = adios_open(&aio->write_file, mrc_io_name(io), filename, "w", mrc_io_comm(io)); AERR(ierr);
  assert(aio->group_size > 0);
  uint64_t total_size;
  ierr = adios_group_size(aio->write_file, aio->group_size, &total_size); AERR(ierr);

}

static void
_mrc_adios_close(struct mrc_io *io)
{
  struct mrc_adios_io *aio = to_adios(io);
  int ierr;

  ierr = adios_close(aio->write_file); AERR(ierr);
  aio->write_file = 0;
  aio->group_size = 0;

  free(aio->mode);
}

static void
_mrc_adios_write_attr(struct mrc_io *io, const char *path, int type,
    const char *name, union param_u *pv)
{
  struct mrc_adios_io *aio = to_adios(io);

  int64_t fd_p = aio->write_file;

  // Use path/name for adios name, and leave path blank.
  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));
  assert(adname);
  sprintf(adname, "%s/%s", path, name);

  char *nrname;
  int ierr;

  switch (type) {
  case PT_SELECT:
  case PT_INT:
  case MRC_VAR_INT:
  case PT_BOOL: 
  case MRC_VAR_BOOL:
  case PT_FLOAT:
  case MRC_VAR_FLOAT:
  case PT_DOUBLE:
  case MRC_VAR_DOUBLE:
  case PT_INT3:
  case PT_FLOAT3:
  case PT_DOUBLE3:
  case MRC_VAR_DOUBLE3:
    ierr = adios_write(fd_p, adname, pv); AERR(ierr);
    break;
  case PT_STRING:
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-sz", adname);
    const char *wstring = pv->u_string ? pv->u_string : "(NULL)";
    int size = strlen(wstring);
    ierr = adios_write(fd_p, nrname, &size); AERR(ierr);
    ierr = adios_write(fd_p, adname, wstring); AERR(ierr);
    free(nrname);
    break;
  case PT_INT_ARRAY:
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-nrvals", adname);
    ierr = adios_write(fd_p, nrname, &pv->u_int_array.nr_vals); AERR(ierr);
    ierr = adios_write(fd_p, adname, pv->u_int_array.vals); AERR(ierr);
    free(nrname);
    break;
  case PT_PTR:
    break;
  default:
    mpi_printf(mrc_io_comm(io), "mrc_io_adios: not writing attr '%s' (type %d)\n",
      adname, type);
    assert(0);
  }
  free(adname);
}


static void
_mrc_adios_write_ndarray(struct mrc_io *io, const char *path, struct mrc_ndarray *nd)
{
  assert(nd->n_dims);
  assert(mrc_ndarray_f_contiguous(nd));
  struct mrc_adios_io *aio = to_adios(io);
  int64_t fd_p = aio->write_file;

  int ierr;

  // This should be written as a param from the class descriptor write (I hope)
  char *datname = (char *) malloc(sizeof(*datname) * (strlen(path) + 10));
  sprintf(datname, "%s/data", path);

  ierr = adios_write(fd_p, datname, nd->arr); AERR(ierr);

  free(datname);
}

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
  int64_t fd_p = aio->write_file;

  int ierr;

  const char *name = mrc_fld_name(fld);

  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));
  assert(adname);
  sprintf(adname, "%s/%s", path, name);


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
  // FIXME : Adios on chester seems to choke with the offset lists I'm giving it,
  // even though it works on my laptop. But it seems to be okay with flat
  // dumps when treating it as a local array with length "_len", so that's what
  // we'll do.
  sprintf(dimnames, "%s/len", adname);
  int fld_len = mrc_fld_len(fld);
  ierr = adios_write(fd_p, dimnames, (void *) &fld_len); AERR(ierr);

  // FIXME: Assuming that the patch dimensions cannot change from the
  // define step (although we're allowing the number of patches to change)
  sprintf(dimnames, "%s/data", adname);
  ierr = adios_write(fd_p, dimnames, fld->_nd->arr); AERR(ierr);

  free(adname);
  free(dimnames);

}


static void
_mrc_adios_set_size(struct mrc_io *io, uint64_t group_size)
{
  struct mrc_adios_io *aio = to_adios(io);
  aio->group_size = group_size;
}

static void
_mrc_adios_get_write_file(struct mrc_io *io, int64_t *wfp)
{
  struct mrc_adios_io *aio = to_adios(io);
  *wfp = aio->write_file;
}

static struct mrc_obj_method mrc_adios_methods[] = {
  MRC_OBJ_METHOD("set_group_size",   _mrc_adios_set_size),
  MRC_OBJ_METHOD("get_write_file",   _mrc_adios_get_write_file),    
  {}
};


struct mrc_io_ops mrc_io_adios_ops = {
  .name          = "adios_write",
  .parallel      = false, // This is kind of a lie, but it should function like serial io
  .size          = sizeof(struct mrc_adios_io),
  .methods       = mrc_adios_methods,
  .open          = _mrc_adios_open,
  .close         = _mrc_adios_close,
  .write_attr    = _mrc_adios_write_attr,
  .write_fld     = _mrc_adios_write_fld,
  .write_ndarray = _mrc_adios_write_ndarray,
};

#undef to_adios

// Add a "meta" wrapper class that will take the name "adios"
// This class will buffer all the mrc_io calls made between
// "open" and "close", then replay them using the correct helper
// classes. If you want better performance, it would be work looking
// at how psc_checkpointing works and explicitly calling the internal
// types yourself.

// NOTE : The reads only exist on this top-level object

enum {
  AW_ATTR,
  AW_FLD,
  AW_NDARRAY,
  NR_AW_OPS,
};


struct buffered_op {
  list_t buffer;
  int op_type; ///< what sort of op we need to replay
  char *path; ///< the path that the op got called with
  char *name; ///< the name the op got called with
  void *address; ///< the address the op was passes(for fld write, etc), or any extra memory we alloced (for attr_write)
  union param_u u; ///< the value of the parameter the op was called with (for attr_write)
  int param_type; ///< the type of the param (for attr_write)
};

struct mrc_adios_meta
{
  list_t buffered_writes;
  bool run_define;
  ADIOS_FILE *read_file;
  ADIOS_SELECTION *selection;
  char *mode;
  int max_buffer;
  char *method; ///< the transport method for this group
  char *trans_opts; ///< a comma separated string of options fof the select transport method
};

#define to_meta(io) mrc_to_subobj(io, struct mrc_adios_meta)

static void
_meta_open(struct mrc_io *io, const char *mode)
{

  struct mrc_adios_meta *aio = to_meta(io);
  int ierr;

  if (!adios_is_initialized()) {
    ierr = adios_init_noxml(mrc_io_comm(io)); AERR(ierr);
    adios_set_max_buffer_size(aio->max_buffer);
  }
  aio->mode = strdup(mode);

  if (strcmp(mode, "w") == 0) {

    INIT_LIST_HEAD(&(aio->buffered_writes));

  } else if (strcmp(mode, "r") == 0) {

    char filename[strlen(io->par.outdir) + strlen(io->par.basename) + 20];
    sprintf(filename, "%s/%s.%06d.bp", io->par.outdir, io->par.basename,
      io->step);

    // FIXME: hardcoding open method here
    ierr = adios_read_init_method(ADIOS_READ_METHOD_BP, mrc_io_comm(io), "abort_on_error"); AERR(ierr);
    aio->read_file = adios_read_open_file(filename, ADIOS_READ_METHOD_BP, mrc_io_comm(io));
    // FIXME: This only allows reading back on the same number of processors!
    aio->selection = adios_selection_writeblock(io->rank);
    assert(aio->read_file);

  } else {
    assert(0);
  }

}

static void
_mrc_adios_get_read_file(struct mrc_io *io, ADIOS_FILE **rfp)
{
  struct mrc_adios_meta *aio = to_meta(io);
  *rfp = aio->read_file;
}

static void
_meta_write_attr(struct mrc_io *io, const char *path, int type,
    const char *name, union param_u *pv)
{
  struct mrc_adios_meta *aio = to_meta(io);
  struct buffered_op *newop = calloc(1, sizeof(*newop));
  newop->op_type = AW_ATTR;
  newop->path = strdup(path);
  newop->param_type = type;
  newop->name = strdup(name);
  memcpy(&(newop->u), pv, sizeof(*pv));

  // Because it's possible that this can be called with the union pointing
  // to a string or array on the stack, we also need to duplicate the memory
  // that the param union points too.
  switch (type) {
  case PT_SELECT:
  case PT_INT:
  case MRC_VAR_INT:
  case PT_BOOL: 
  case MRC_VAR_BOOL:
  case PT_FLOAT:
  case MRC_VAR_FLOAT:
  case PT_DOUBLE:
  case MRC_VAR_DOUBLE:
  case PT_INT3:
  case PT_FLOAT3:
  case PT_DOUBLE3:
  case MRC_VAR_DOUBLE3:
  // nothing to do for all of these, because param_u is large
  // enough to hold their actual values
    break;
  case PT_STRING:
    // Copy the string itself, in case it lives on the stack.
    if (pv->u_string) {
      newop->u.u_string =  strdup(pv->u_string);
    }
    break;
  case PT_INT_ARRAY:
    if (pv->u_int_array.nr_vals > 0) {
      newop->u.u_int_array.vals = (int *) calloc(pv->u_int_array.nr_vals, sizeof(int));
      memcpy((void *) newop->u.u_int_array.vals, 
             (void *) pv->u_int_array.vals,
             sizeof(int) * pv->u_int_array.nr_vals);
    }
    break;
  case PT_PTR:
    break;
  default:
    mpi_printf(mrc_io_comm(io), "mrc_io_adios: not writing attr '%s' (type %d)\n",
      name, type);
    assert(0);
  }

  list_add_tail(&(newop->buffer), &(aio->buffered_writes));
}

static void
_meta_write_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{
  struct mrc_adios_meta *aio = to_meta(io);
  struct buffered_op *newop = calloc(1, sizeof(*newop));
  newop->op_type = AW_FLD;
  newop->path = strdup(path);
  newop->address = (void *) fld;
  list_add_tail(&(newop->buffer), &(aio->buffered_writes));
}

static void
_meta_write_ndarray(struct mrc_io *io, const char *path, struct mrc_ndarray *nd)
{
  struct mrc_adios_meta *aio = to_meta(io);
  struct buffered_op *newop = calloc(1, sizeof(*newop));
  newop->op_type = AW_NDARRAY;
  newop->path = strdup(path);
  newop->address = (void *) nd;
  list_add_tail(&(newop->buffer), &(aio->buffered_writes)); 
}

void __attribute__((noinline))
_meta_close(struct mrc_io *io)
{
  struct mrc_adios_meta *aio = to_meta(io);
  char *mode = aio->mode;
  int ierr;

  if (strcmp(mode, "r") == 0) {

    ierr = adios_read_close(aio->read_file); AERR(ierr);
    // FIXME : this may cause problems if we read more than once...
    ierr = adios_read_finalize_method(ADIOS_READ_METHOD_BP); AERR(ierr);
    adios_selection_delete(aio->selection);
    aio->read_file = NULL;

  } else if (strcmp(mode, "w") == 0) {

    // Time to replay the buffered operations using the three stages
    // First, see if we need to regenerate the definitions
    bool adios_group_defined = false;

    if (!list_empty(&defined_adios_groups)) {
      struct defined_group *gptr;
      list_for_each_entry(gptr, &defined_adios_groups, group_list) {
        if(strcmp(gptr->name, mrc_io_name(io)) == 0) {
          adios_group_defined = true;
          break;
        }
      }
    }

    const char *adios_steps[] = { "adios_define", "adios_size", "adios_write"};

    uint64_t payload_size = 0;

    const char *basename, *outdir;
    mrc_io_get_param_string(io, "basename", &basename);
    mrc_io_get_param_string(io, "outdir", &outdir);

    // start at 0 if we need to run define, otherwise start at 1
    for (int step = (!(aio->run_define) && adios_group_defined); 
             step < 3; step++) {

      struct mrc_io *io2 = mrc_io_create(mrc_io_comm(io));
      // FIXME: right now I copy stuff from the meta class into the
      // internal classes. Maybe it would be better to use set from options,
      // and override the type setting? I'm not really sure..
      //mrc_io_set_from_options(io2);

      mrc_io_set_type(io2, adios_steps[step]);
      mrc_io_set_name(io2, mrc_io_name(io));
      mrc_io_set_param_string(io2, "basename", basename);
      mrc_io_set_param_string(io2, "outdir", outdir);
      if ( step == 0) {
        const char *transopt, *transmeth;
        mrc_io_get_param_string(io, "transport_options", &transopt);
        if (transopt) {
          mrc_io_set_param_string(io2, "transport_options", transopt);
        }
        mrc_io_get_param_string(io, "method", &transmeth);
        mrc_io_set_param_string(io2, "method", transmeth);
      }

      mrc_io_setup(io2);

      if (step == 2) {
        // call the method directrly
        _mrc_adios_set_size(io2, payload_size);
      }

      mrc_io_open(io2, "w", io->step, io->time);

      // Time to replay the buffered operations
      struct buffered_op *bop;
      list_for_each_entry(bop, &(aio->buffered_writes), buffer) {
        switch (bop->op_type) {
          case AW_ATTR:
            mrc_io_write_attr(io2, bop->path, bop->param_type, bop->name, &(bop->u));
            break;
          case AW_FLD:
            mrc_io_write_fld(io2, bop->path, (struct mrc_fld *) bop->address);
            break;
          case AW_NDARRAY:
            mrc_io_write_ndarray(io2, bop->path,
                                     (struct mrc_ndarray *) bop->address);
            break;
          default:
            assert(0);
        }
      }

      mrc_io_close(io2);

      if (step == 1) {
        // call the method directly
        _mrc_adios_final_size(io2, &payload_size);
      }

      mrc_io_destroy(io2);

      if (step == 0) {
        aio->run_define = false;
      }
    }

    list_t *curr, *next;
    // Clean up the buffer

    list_for_each_safe(curr, next, &(aio->buffered_writes)) {
      struct buffered_op *bop = container_of(curr, struct buffered_op, buffer);
      switch (bop->op_type) {
        case AW_ATTR:
          free(bop->path);
          free(bop->name);
          // these two types have extra allocations, so we need to clean it up
          if ((bop->param_type == PT_STRING) && bop->u.u_ptr) {
            free(bop->u.u_ptr);
          } else if ((bop->param_type == PT_INT_ARRAY) && (bop->u.u_int_array.nr_vals > 0)) {
            free(bop->u.u_int_array.vals);
          }
          break;
        case AW_FLD:
          free(bop->path);
          break;
        case AW_NDARRAY:
          free(bop->path);
          break;
        default:
          assert(0);
      }
      list_del(curr);
      free(bop);
    }

  }
  
  free(aio->mode);
}

static bool
check_writeblock(struct mrc_io *io, ADIOS_FILE *fd_p, const char *adname, ADIOS_SELECTION **select) 
{
  // FIXME : Some things are apparently still written from a single proc in psc (field
  // and particle patches are local objects) so the writeblock selection messes up.
  // We need to add a check for how many writeblocks we have, and if there's only one then 
  // use that. Also, we might as well add in a check that the number of writeblocks are the
  // same as the io size on this communicator.

  ADIOS_VARINFO *info = adios_inq_var(fd_p, adname); assert(info);
  if (info->nblocks[0] == 1) {
    *select = NULL;
    adios_free_varinfo(info);
    return false;
  } 
  else if (info->nblocks[0] == io->size) {
    adios_free_varinfo(info);
    return false;
  } 
  else {
    int ierr = adios_inq_var_blockinfo(fd_p, info); AERR(ierr);
    *select = NULL;
    for (int blk = 0; blk < info->nblocks[0]; blk++) {
      if (info->blockinfo[blk].process_id == io->rank) {
        *select = adios_selection_writeblock(blk);        
         adios_free_varinfo(info);
         return true;
      }
    }
    mprintf("Error! %s: cannot associate rank %d to one of %d writeblocks\n", adname, io->rank, info->nblocks[0]);
    for (int blk = 0; blk < info->nblocks[0]; blk++) {    
      mprintf("Block %d - pid %d\n", blk, info->blockinfo[blk].process_id);
    }
    adios_free_varinfo(info);
    assert(0);
  }
}

static void
_mrc_adios_read_attr(struct mrc_io *io, const char *path, int type,
    const char *name, union param_u *pv)
{
  // FIXME : Most of these reads could probably be done more effeciently 
  // using adios_inq_var, especially the scalar ones. The problem is that I want
  // adios to function in places where serial io methods are currently used (ie, checkpointing),
  // and those i/o procedures have a nasty habit of assuming that things aren't collective.
  // Since ADIOS can mimic serial i/o via the writeblock selection, we're locking
  // into that.

  // Also note that because of way mrc objects depend on each other we cannot safely
  // schedule a bunch of reads then do one perform call. Everytime this function
  // is called the attribute needs to be read by the time it exits.

  struct mrc_adios_meta *aio = to_meta(io);

  ADIOS_FILE *fd_p = aio->read_file;
  ADIOS_SELECTION *select = aio->selection;
  assert(select);
  // Use path/name for adios name, and leave path blank.
  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));
  assert(adname);
  sprintf(adname, "%s/%s", path, name);

  char *nrname;
  int ierr;
  bool free_select = false;
  switch (type) {
  case PT_SELECT:
  case PT_INT:
  case MRC_VAR_INT:
  case PT_BOOL: 
  case MRC_VAR_BOOL:
  case PT_FLOAT:
  case MRC_VAR_FLOAT:
  case PT_DOUBLE:
  case MRC_VAR_DOUBLE:
    free_select = check_writeblock(io, fd_p, adname, &select);
    ierr = adios_schedule_read(fd_p, select, adname, 0, 1, pv); AERR(ierr);
    ierr = adios_perform_reads(fd_p, 1); AERR(ierr);
    break;
  case PT_STRING:
    free_select = check_writeblock(io, fd_p, adname, &select);
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-sz", adname);
    int size;
    ierr = adios_schedule_read(fd_p, select, nrname, 0, 1, &size); AERR(ierr);
    ierr = adios_perform_reads(fd_p, 1); AERR(ierr);
    pv->u_string = malloc(sizeof(char) * (size + 10 ));
    ierr = adios_schedule_read(fd_p, select, adname, 0, 1, (void *) pv->u_string); AERR(ierr);
    ierr = adios_perform_reads(fd_p, 1); AERR(ierr);

    if (strcmp(pv->u_string, "(NULL)") == 0) {
      free((char *) pv->u_string);
      pv->u_string = NULL;
    } 
    free(nrname);
    break;
  case PT_INT3:
  case PT_FLOAT3:
  case PT_DOUBLE3:
  case MRC_VAR_DOUBLE3:
    free_select = check_writeblock(io, fd_p, adname, &select);
    ierr = adios_schedule_read(fd_p, select, adname, 0, 1, pv); AERR(ierr);
    ierr = adios_perform_reads(fd_p, 1); AERR(ierr);
    break;
  case PT_INT_ARRAY:
    free_select = check_writeblock(io, fd_p, adname, &select);
    nrname = malloc(sizeof(*nrname) * (strlen(adname) + 10));
    assert(nrname);
    sprintf(nrname, "%s-nrvals", adname);
    ierr = adios_schedule_read(fd_p, select, nrname, 0, 1, &pv->u_int_array.nr_vals); AERR(ierr);
    ierr = adios_perform_reads(fd_p, 1); AERR(ierr);
    // FIXME : I don't like that nr_vals = 0 things can be written now..
    if (pv->u_int_array.nr_vals > 0) {
      pv->u_int_array.vals = calloc(pv->u_int_array.nr_vals, sizeof(int));
      ierr = adios_schedule_read(fd_p, select, adname, 0, 1, pv->u_int_array.vals); AERR(ierr);
      ierr = adios_perform_reads(fd_p, 1); AERR(ierr);
    }
    free(nrname);
    break;
  case PT_PTR:
    break;
  default:
    mpi_printf(mrc_io_comm(io), "mrc_io_adios: not reading attr '%s' (type %d)\n",
      adname, type);
    assert(0);
  }
  free(adname);
  if (free_select) adios_selection_delete(select);
}

static struct mrc_obj_method meta_adios_methods[] = {
  MRC_OBJ_METHOD("get_read_file",    _mrc_adios_get_read_file),
  {}
};


static void
_mrc_adios_read_ndarray(struct mrc_io *io, const char *path, struct mrc_ndarray *nd)
{
  assert(nd->n_dims);
  assert(mrc_ndarray_f_contiguous(nd));
  struct mrc_adios_meta *aio = to_meta(io);
  ADIOS_FILE *fd_p = aio->read_file;
  ADIOS_SELECTION *select = aio->selection;

  int ierr;

  // This should be written as a param from the class descriptor write (I hope)
  char *datname = (char *) malloc(sizeof(*datname) * (strlen(path) + 10));
  sprintf(datname, "%s/data", path);

  ierr = adios_schedule_read(fd_p, select, datname, 0, 1, nd->arr); AERR(ierr);
  ierr = adios_perform_reads(fd_p, 1); AERR(ierr);

  free(datname);
}

static void
_mrc_adios_read_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{

  struct mrc_adios_meta *aio = to_meta(io);
  ADIOS_FILE *fd_p = aio->read_file;
  ADIOS_SELECTION *select = aio->selection;

  int ierr;

  const char *name = mrc_fld_name(fld);

  char *adname = malloc(sizeof(*adname) * (strlen(path) + strlen(name) + 5));
  assert(adname);
  sprintf(adname, "%s/%s", path, name);

  char *dimnames = (char *) malloc(sizeof(*dimnames) * (strlen(adname) + 100));

  // FIXME : We don't actually need to bring these variables in, but they're
  // a check that we're reading back with the same patch layout we used to write.
  // This should be forced by using the writeblock selection. If we want to support
  // a different read layout (i.e. selection) these checks should be removed and instead 
  // we should define a bit of the array to read using the mrc_fld/domain patch layout
  // and adios_selection_boundingbox

  int nr_patches, nr_local_patches, patch_off;


  sprintf(dimnames, "%s/nr_global_patches", adname);
  ierr = adios_schedule_read(fd_p, select, dimnames, 0, 1, (void *) &nr_patches); AERR(ierr);
  sprintf(dimnames, "%s/nr_local_patches", adname);
  ierr = adios_schedule_read(fd_p, select, dimnames, 0, 1, (void *) &nr_local_patches); AERR(ierr);
  sprintf(dimnames, "%s/patch_off", adname);
  ierr = adios_schedule_read(fd_p, select, dimnames, 0, 1, (void *) &patch_off); AERR(ierr);
  ierr = adios_perform_reads(fd_p, 1); AERR(ierr);


  assert(nr_local_patches == mrc_fld_nr_patches(fld));

  int def_nr_patches;
  mrc_domain_get_nr_global_patches(fld->_domain, &def_nr_patches);
  assert(nr_patches == def_nr_patches);

  struct mrc_patch_info info;
  mrc_domain_get_local_patch_info(fld->_domain, 0, &info);
  assert(patch_off == info.global_patch);

  sprintf(dimnames, "%s/data", adname);
  ierr = adios_schedule_read(fd_p, select, dimnames, 0, 1, fld->_nd->arr); AERR(ierr);
  ierr = adios_perform_reads(fd_p, 1); AERR(ierr);

  free(adname);
  free(dimnames);

}

#define VAR(x) (void *)offsetof(struct mrc_adios_meta, x)
static struct param adios_meta_descr[] = {
  { "method"              , VAR(method)               , PARAM_STRING("MPI")      },
  { "transport_options"   , VAR(trans_opts)           , PARAM_STRING(NULL)       },
  { "max_buffer"          , VAR(max_buffer)           , PARAM_INT(100)           },
  { "run_define"          , VAR(run_define)           , PARAM_BOOL(false)        },
  {},
};
#undef VAR

struct mrc_io_ops mrc_io_adios_meta_ops = {
  .name          = "adios",
  .parallel      = false, // This is kind of a lie, but it should function like serial io
  .size          = sizeof(struct mrc_adios_meta),
  .param_descr   =  adios_meta_descr,
  .methods       =  meta_adios_methods,
  .open          = _meta_open,
  .close         = _meta_close,
  .write_attr    = _meta_write_attr,
  .read_attr     = _mrc_adios_read_attr,  
  .write_fld     = _meta_write_fld,
  .read_fld      = _mrc_adios_read_fld,
  .write_ndarray = _meta_write_ndarray,
  .read_ndarray  = _mrc_adios_read_ndarray,
};

#undef to_meta