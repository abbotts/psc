
#ifndef PSC_PARTICLES_PRIVATE_H
#define PSC_PARTICLES_PRIVATE_H

#include "psc_particles.h"
#ifdef HAVE_ADIOS
#include "psc_adios.h"
#endif

struct psc_particles {
  struct mrc_obj obj;
  int n_part;
  int p; //< patch number
  unsigned int flags;
};

struct psc_particles_ops {
  MRC_SUBCLASS_OPS(struct psc_particles);
  void (*reorder)(struct psc_particles *prts);
#ifdef HAVE_ADIOS
  void (*define_adios)(struct psc_particles *prts, struct mrc_domain *domain, int64_t m_adios_group);
  uint64_t (*calc_size_adios)(struct psc_particles *prts);
  void (*write_adios)(struct psc_particles *prts, struct mrc_domain *domain, int64_t fd_p);
#endif
};

#define psc_particles_ops(prts) ((struct psc_particles_ops *) ((prts)->obj.ops))

typedef void (*psc_particles_copy_to_func_t)(struct psc_particles *,
					     struct psc_particles *,
					     unsigned int);
typedef void (*psc_particles_copy_from_func_t)(struct psc_particles *,
					       struct psc_particles *,
					       unsigned int);

// ======================================================================

extern struct psc_particles_ops psc_particles_c_ops;
extern struct psc_particles_ops psc_particles_single_ops;
extern struct psc_particles_ops psc_particles_double_ops;
extern struct psc_particles_ops psc_particles_single_by_block_ops;
extern struct psc_particles_ops psc_particles_fortran_ops;
extern struct psc_particles_ops psc_particles_cuda_ops;
extern struct psc_particles_ops psc_particles_cuda2_ops;
extern struct psc_particles_ops psc_particles_acc_ops;

extern struct psc_mparticles_ops psc_mparticles_cuda2_ops;
extern struct psc_mparticles_ops psc_mparticles_acc_ops;

#endif
