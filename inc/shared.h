#ifndef GALA_SHARED_H
#define GALA_SHARED_H

#if defined(__STDC__) || defined(__cplusplus)
#include <cglm/cglm.h>
#include "types.h"
typedef u32 uint;
#endif

struct push_constant_data {
	mat4 viewproj;
	uint baseindex;
	uint tree_height;
	uint tree_n;
	float time;
	float dt;
};

#define MAX_FRAMES_RENDERING (2)
#define MAX_ITEMS_PER_FRAME (1 << 10)
#define MAX_ITEMS (MAX_ITEMS_PER_FRAME * MAX_FRAMES_RENDERING)
#define MAX_LOD (4)
#define LOCAL_SIZE (64)

struct orbit_spec {
	vec4 startoffset[MAX_ITEMS_PER_FRAME];
	vec4 orbitaxis[MAX_ITEMS_PER_FRAME];
	float orbitspeed[MAX_ITEMS_PER_FRAME];
	float itemscale[MAX_ITEMS_PER_FRAME];
	float texindex[MAX_ITEMS_PER_FRAME];
	uint parent[MAX_ITEMS_PER_FRAME];
};

#endif /* GALA_SHARED_H */

