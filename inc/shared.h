#ifndef GALA_SHARED_H
#define GALA_SHARED_H

#if defined(__STDC__) || defined(__cplusplus)
#include <cglm/cglm.h>

typedef
#endif
struct transforms_t {
	mat4 model;
	mat4 view;
	mat4 proj;
}
#if defined(__STDC__) || defined(__cplusplus)
transforms
#endif
;

#endif /* GALA_SHARED_H */

