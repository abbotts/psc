
#include "psc.h"
#include "psc_particles_c.h"

#include <stdlib.h>
#include <assert.h>

// ----------------------------------------------------------------------
// photons_alloc

void
photons_alloc(photons_t *pp, int nr)
{
  pp->nr_alloced = nr * 1.2;
  pp->photons = calloc(pp->nr_alloced, sizeof(*pp->photons));
}

// ----------------------------------------------------------------------
// photons_realloc

void
photons_realloc(photons_t *pp, int new_nr)
{
  if (new_nr <= pp->nr)
    return;

  pp->nr_alloced = new_nr * 1.2;
  pp->photons = realloc(pp->photons, pp->nr_alloced * sizeof(*pp->photons));
}

// ----------------------------------------------------------------------
// photons_free

void
photons_free(photons_t *pp)
{
  free(pp->photons);
  pp->nr_alloced = 0;
  pp->photons = NULL;
}

// ----------------------------------------------------------------------
// mphotons_alloc

void
mphotons_alloc(mphotons_t *mphotons)
{
  mphotons->p = calloc(psc.nr_patches, sizeof(*mphotons->p));
}

// ----------------------------------------------------------------------
// mphotons_destroy

void
mphotons_destroy(mphotons_t *mphotons)
{
  foreach_patch(p) {
    photons_free(&mphotons->p[p]);
  }
  free(mphotons->p);
}
