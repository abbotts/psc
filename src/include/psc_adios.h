#ifndef PSC_ADIOS_H
#define PSC_ADIOS_H

#include <adios.h>
#include <stdio.h>

#define AERR(ierr); if (ierr != 0) { adios_err_print(ierr); }

static inline void
adios_err_print(int ierr)
{
  const char * errmsg = adios_get_last_errmsg();
  fprintf(stderr, "ADIOS Error # %d: %s\n", ierr, errmsg);
  assert(0);
}

#endif