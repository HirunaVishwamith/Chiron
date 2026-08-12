#ifndef G3D_LIB_H
#define G3D_LIB_H

/*
 * g3d — bare-metal software 3D for Chiron (RV64IMA, no F/D).
 *
 * Fixed-point Q16.16 throughout. UART truecolor output via GL_tty.h.
 * Framebuffer raster is thread-safe across disjoint row bands, so the
 * four harts can transform / fill in parallel; only present() talks
 * to the UART (core 0).
 *
 * Layers, simple → advanced:
 *   1. Q16.16 + sin/cos LUT
 *   2. 4×4 transforms, perspective project
 *   3. Wireframe lines into a software FB
 *   4. Filled triangles + z-buffer + flat lighting
 */

#include <stdint.h>

#ifndef G3D_W
#define G3D_W 64
#endif
#ifndef G3D_H
#define G3D_H 48
#endif

typedef int32_t g3d_fx;
#define G3D_ONE   ((g3d_fx)65536)
#define G3D_SHIFT 16

typedef struct { g3d_fx x, y, z; } g3d_vec3;
typedef struct { int x, y; g3d_fx z; } g3d_vert2;
typedef struct { g3d_fx m[16]; } g3d_mat4;
typedef struct { uint8_t r, g, b; } g3d_rgb;

static inline g3d_fx g3d_mul(g3d_fx a, g3d_fx b)
{
    return (g3d_fx)(((int64_t)a * (int64_t)b) >> G3D_SHIFT);
}

static inline g3d_fx g3d_div(g3d_fx a, g3d_fx b)
{
    if (b == 0)
        return 0;
    return (g3d_fx)(((int64_t)a << G3D_SHIFT) / (int64_t)b);
}

static inline g3d_fx g3d_from_int(int v) { return (g3d_fx)v << G3D_SHIFT; }
static inline int    g3d_to_int(g3d_fx v) { return (int)(v >> G3D_SHIFT); }

g3d_fx g3d_sin(int angle256);
g3d_fx g3d_cos(int angle256);

void g3d_ident(g3d_mat4 *o);
void g3d_mul_mat(g3d_mat4 *o, const g3d_mat4 *a, const g3d_mat4 *b);
void g3d_translate(g3d_mat4 *o, g3d_fx x, g3d_fx y, g3d_fx z);
void g3d_rotate_x(g3d_mat4 *o, int angle256);
void g3d_rotate_y(g3d_mat4 *o, int angle256);
void g3d_rotate_z(g3d_mat4 *o, int angle256);
void g3d_transform(g3d_vec3 *o, const g3d_mat4 *m, const g3d_vec3 *v);

/* Returns 0 if the point is behind the near plane. */
int g3d_project(g3d_vert2 *o, const g3d_vec3 *eye, int cx, int cy, g3d_fx focal);

void g3d_cross(g3d_vec3 *o, const g3d_vec3 *a, const g3d_vec3 *b);
g3d_fx g3d_dot(const g3d_vec3 *a, const g3d_vec3 *b);
void g3d_normalize(g3d_vec3 *v);

/* Screen-space cross; >0 means CCW (front) with y growing down. */
int g3d_facing(const g3d_vert2 *a, const g3d_vert2 *b, const g3d_vert2 *c);

g3d_rgb g3d_shade(g3d_rgb base, const g3d_vec3 *n, const g3d_vec3 *light);

void g3d_clear(g3d_rgb *fb, uint16_t *zbuf, g3d_rgb c);
void g3d_plot(g3d_rgb *fb, uint16_t *zbuf, int x, int y, g3d_fx zi, g3d_rgb c);
void g3d_line(g3d_rgb *fb, int x0, int y0, int x1, int y1, g3d_rgb c);

/* Rasterize only scanlines in [y_lo, y_hi). */
void g3d_triangle(g3d_rgb *fb, uint16_t *zbuf,
                  g3d_vert2 a, g3d_vert2 b, g3d_vert2 c,
                  g3d_rgb col, int y_lo, int y_hi);

/* UART dump. Caller must have #included GL_tty.h with matching GL_width/height. */
void g3d_present(const g3d_rgb *fb);

#endif /* G3D_LIB_H */
