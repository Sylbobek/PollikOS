#ifndef POLLIK_SOFT3D_H
#define POLLIK_SOFT3D_H
#include "gfx_device.h"
typedef struct { float x,y; } Vec2;
typedef struct { float x,y,z; } Vec3;
typedef struct { float x,y,z,w; } Vec4;
typedef struct { float m[16]; } Mat4;
typedef struct { Vec4 p; Vec3 color; Vec2 uv; } Vertex;
typedef struct { Vertex v[3]; } Triangle;
/* Caller-owned RGB888/u32 texels; width and height must be powers of two.
 * Sampling wraps by bit mask; no allocation, no libm, no GPU handles. */
typedef struct { const u32 *pixels; int width, height; } GfxTexture;
Mat4 soft3d_identity(void);
Mat4 soft3d_multiply(Mat4 a, Mat4 b);
Vec4 soft3d_transform(Mat4 m, Vec4 v);
/* Right handed, camera looks down -Z, vertical focal length cot(fov/2). */
Mat4 soft3d_perspective(float focal, float aspect, float near_z, float far_z);
/* Homogeneous six-plane clipping BEFORE divide. RGB 0..1. Reject nonfinite or
 * out-of-contract inputs (absolute clip coords <= 16384). CCW front in NDC.
 * Row scissor [first,last) allows cooperative rasterization, no allocation.
 * mode 0=wire,1=flat,2=interpolated color. Return depth-passing pixel writes. */
u32 soft3d_triangle(GfxTarget *t, const Triangle *tri, int cull, int mode, int first, int last);
/* Textured variant of the same pipeline. mode 3 samples uv; perspective=0
 * interpolates uv affinely in screen space, perspective=1 divides by
 * interpolated 1/w per pixel. bilinear=0 nearest texel, 1 blends 2x2 texels.
 * Out-of-contract texture pointers are rejected (returns 0). */
u32 soft3d_triangle_textured(GfxTarget *t, const Triangle *tri, int cull, int first, int last,
    const GfxTexture *tex, int perspective, int bilinear);
#endif
