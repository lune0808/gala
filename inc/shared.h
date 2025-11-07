#ifndef GALA_SHARED_H
#define GALA_SHARED_H

#if defined(__STDC__) || defined(__cplusplus)
#include <cglm/cglm.h>
#endif

struct push_constant_data {
	mat4 mvp;
	mat4 normalmat; // row 3.xyz = camera position
	mat4 model;
};

#endif /* GALA_SHARED_H */

