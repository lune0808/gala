#ifndef GALA_SHARED_H
#define GALA_SHARED_H

#if defined(__STDC__) || defined(__cplusplus)
#include <cglm/cglm.h>
#endif

struct push_constant_data {
	mat4 pos_tfm;
	mat4 norm_tfm;
};

#endif /* GALA_SHARED_H */

