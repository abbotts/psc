#ifndef PSC_ADIOS_H
#define PSC_ADIOS_H

#include <adios.h>
#include <adios_read.h>
#include <stdio.h>

#define AERR(ierr); if (ierr != 0) { adios_err_print(ierr); }

static inline void
adios_err_print(int ierr)
{
  const char * errmsg = adios_get_last_errmsg();
  fprintf(stderr, "ADIOS Error # %d: %s\n", ierr, errmsg);
  assert(0);
}

typedef void (*a_get_size_t)(struct mrc_io *, uint64_t *size);
typedef void (*a_add_to_size_t)(struct mrc_io *, uint64_t size);
typedef void (*a_set_size_t)(struct mrc_io *, uint64_t size);
typedef void (*a_get_read_t)(struct mrc_io *io, ADIOS_FILE **rfp);
typedef void (*a_get_write_t)(struct mrc_io *io, int64_t *wfp);
typedef void (*a_get_gid_t)(struct mrc_io *io, int64_t *gid);

#endif