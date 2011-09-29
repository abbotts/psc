
#include "psc_push_particles_1st.h"
#include <mrc_profile.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "c_common_push.c"

static void
do_push_part_1st_yz(int p, fields_t *pf, particles_t *pp)
{
#define S0Y(off) s0y[off+1]
#define S0Z(off) s0z[off+1]
#define S1Y(off) s1y[off+1]
#define S1Z(off) s1z[off+1]

  creal s0y[4] = {}, s0z[4] = {}, s1y[4], s1z[4];

  creal dt = ppsc->dt;
  creal dqs = .5f * ppsc->coeff.eta * dt;
  creal fnqs = sqr(ppsc->coeff.alpha) * ppsc->coeff.cori / ppsc->coeff.eta;
  creal fnqys = ppsc->dx[1] * fnqs / dt;
  creal fnqzs = ppsc->dx[2] * fnqs / dt;
  creal dxi[3] = { 1.f / ppsc->dx[0], 1.f / ppsc->dx[1], 1.f / ppsc->dx[2] };

  fields_zero(pf, JXI);
  fields_zero(pf, JYI);
  fields_zero(pf, JZI);
  
  struct psc_patch *patch = &ppsc->patch[p];
  for (int n = 0; n < pp->n_part; n++) {
    particle_t *part = particles_get_one(pp, n);
    creal vxi[3];

    // x^n, p^n -> x^(n+.5), p^n
    calc_vxi(vxi, part);
    push_xi(part, vxi, .5f * dt);

    // field interpolation

    int lg[3], lh[3];
    creal og[3], oh[3];
    find_idx_off_1st(&part->xi, lg, og, 0.f, patch->xb, dxi); // FIXME passing xi hack
    find_idx_off_1st(&part->xi, lh, oh, -.5f, patch->xb, dxi);

    // CHARGE DENSITY FORM FACTOR AT (n+.5)*dt 

    S0Y(+0) = 1.f - og[1];
    S0Y(+1) = og[1];
    S0Z(+0) = 1.f - og[2];
    S0Z(+1) = og[2];

    // FIELD INTERPOLATION

    INTERPOLATE_SETUP_1ST;

    creal exq = INTERPOLATE_FIELD_1ST(EX, g, g);
    creal eyq = INTERPOLATE_FIELD_1ST(EY, h, g);
    creal ezq = INTERPOLATE_FIELD_1ST(EZ, g, h);

    creal hxq = INTERPOLATE_FIELD_1ST(HX, h, h);
    creal hyq = INTERPOLATE_FIELD_1ST(HY, g, h);
    creal hzq = INTERPOLATE_FIELD_1ST(HZ, h, g);

    // x^(n+0.5), p^n -> x^(n+0.5), p^(n+1.0) 
    push_pxi(part, exq, eyq, ezq, hxq, hyq, hzq, dqs);

    // x^(n+0.5), p^(n+1.0) -> x^(n+1.0), p^(n+1.0) 
    calc_vxi(vxi, part);
    push_xi(part, vxi, .5 * dt);

    // CHARGE DENSITY FORM FACTOR AT (n+1.5)*dt 
    // x^(n+1), p^(n+1) -> x^(n+1.5f), p^(n+1)

    creal xi[3] = { 0.f,
		    part->yi + vxi[1] * .5f * dt, 
		    part->zi + vxi[2] * .5f * dt };

    int lf[3];
    creal of[3];
    find_idx_off_1st(xi, lf, of, 0.f, patch->xb, dxi);

    for (int i = -1; i <= 2; i++) {
      S1Y(i) = 0.f;
      S1Z(i) = 0.f;
    }

    S1Y(lf[1]-lg[1]+0) = 1.f - of[1];
    S1Y(lf[1]-lg[1]+1) = of[1];
    S1Z(lf[2]-lg[2]+0) = 1.f - of[2];
    S1Z(lf[2]-lg[2]+1) = of[2];

    // CURRENT DENSITY AT (n+1.0)*dt

    for (int i = 0; i <= 1; i++) {
      S1Y(i) -= S0Y(i);
      S1Z(i) -= S0Z(i);
    }

    int l2min, l3min, l2max, l3max;
    
    if (lf[1] == lg[1]) {
      l2min = 0; l2max = +1;
    } else if (lf[1] == lg[1] - 1) {
      l2min = -1; l2max = +1;
    } else { // (lf[1] == lg[1] + 1)
      l2min = 0; l2max = +2;
    }

    if (lf[2] == lg[2]) {
      l3min = 0; l3max = +1;
    } else if (lf[2] == lg[2] - 1) {
      l3min = -1; l3max = +1;
    } else { // (lf[2] == lg[2] + 1)
      l3min = 0; l3max = +2;
    }

    creal fnqx = vxi[0] * part->qni * part->wni * fnqs;
    for (int l3 = l3min; l3 <= l3max; l3++) {
      for (int l2 = l2min; l2 <= l2max; l2++) {
	creal wx = S0Y(l2) * S0Z(l3)
	  + .5f * S1Y(l2) * S0Z(l3)
	  + .5f * S0Y(l2) * S1Z(l3)
	  + (1.f/3.f) * S1Y(l2) * S1Z(l3);
	creal jxh = fnqx*wx;
	F3(JXI, 0,lg[1]+l2,lg[2]+l3) += jxh;
      }
    }

    creal fnqy = part->qni * part->wni * fnqys;
    for (int l3 = l3min; l3 <= l3max; l3++) {
      creal jyh = 0.f;
      for (int l2 = l2min; l2 < l2max; l2++) {
	creal wy = S1Y(l2) * (S0Z(l3) + .5f*S1Z(l3));
	jyh -= fnqy*wy;
	F3(JYI, 0,lg[1]+l2,lg[2]+l3) += jyh;
      }
    }

    creal fnqz = part->qni * part->wni * fnqzs;
    for (int l2 = l2min; l2 <= l2max; l2++) {
      creal jzh = 0.f;
      for (int l3 = l3min; l3 < l3max; l3++) {
	creal wz = S1Z(l3) * (S0Y(l2) + .5f*S1Y(l2));
	jzh -= fnqz*wz;
	F3(JZI, 0,lg[1]+l2,lg[2]+l3) += jzh;
      }
    }
  }
}

void
psc_push_particles_1st_push_yz(struct psc_push_particles *push,
			       mparticles_base_t *particles_base,
			       mfields_base_t *flds_base)
{
  mfields_t flds;
  mparticles_t particles;
  psc_mfields_get_from(&flds, EX, EX + 6, flds_base);
  psc_mparticles_get_from(&particles, particles_base);

  static int pr;
  if (!pr) {
    pr = prof_register("1st_part_yz", 1., 0, 0);
  }
  prof_start(pr);
  psc_foreach_patch(ppsc, p) {
    do_push_part_1st_yz(p, &flds.f[p], &particles.p[p]);
  }
  prof_stop(pr);

  psc_mfields_put_to(&flds, JXI, JXI + 3, flds_base);
  psc_mparticles_put_to(&particles, particles_base);
}
