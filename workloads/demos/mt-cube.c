/*
 * mt-cube — rotating wireframe cube on four harts.
 *
 * Cores 0..n-1 each transform a slice of the 8 vertices.
 * Core 0 draws the 12 edges into the software FB and presents
 * over UART (GL is not thread-safe).
 */

#define GL_width  64
#define GL_height 48
#define G3D_W     64
#define G3D_H     48

#include "GL_tty.h"
#include "g3d.h"
#include "util.h"

#define NVERT 8
#define NEDGE 12

static const g3d_vec3 cube_obj[NVERT] = {
    { -G3D_ONE, -G3D_ONE, -G3D_ONE },
    {  G3D_ONE, -G3D_ONE, -G3D_ONE },
    {  G3D_ONE,  G3D_ONE, -G3D_ONE },
    { -G3D_ONE,  G3D_ONE, -G3D_ONE },
    { -G3D_ONE, -G3D_ONE,  G3D_ONE },
    {  G3D_ONE, -G3D_ONE,  G3D_ONE },
    {  G3D_ONE,  G3D_ONE,  G3D_ONE },
    { -G3D_ONE,  G3D_ONE,  G3D_ONE },
};

static const unsigned char cube_edge[NEDGE][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

static g3d_rgb fb[G3D_W * G3D_H];
static g3d_vert2 projected[NVERT];
static volatile int angle_y;

void thread_entry(int cid, int nc)
{
    const g3d_rgb black = {0, 0, 0};
    const g3d_rgb cyan  = {80, 220, 255};
    const g3d_fx  focal = g3d_from_int(72);
    const int cx = G3D_W / 2;
    const int cy = G3D_H / 2;

    if (cid == 0) {
        angle_y = 20;
        GL_init();
    }
    barrier(nc);

    for (;;) {
        g3d_mat4 ry, tz, world;
        int i, i0, i1;

        g3d_rotate_y(&ry, angle_y);
        g3d_translate(&tz, 0, 0, g3d_from_int(4));
        g3d_mul_mat(&world, &tz, &ry);

        i0 = (cid * NVERT) / nc;
        i1 = ((cid + 1) * NVERT) / nc;
        for (i = i0; i < i1; i++) {
            g3d_vec3 eye;
            g3d_transform(&eye, &world, &cube_obj[i]);
            g3d_project(&projected[i], &eye, cx, cy, focal);
        }
        barrier(nc);

        if (cid == 0) {
            g3d_clear(fb, 0, black);
            for (i = 0; i < NEDGE; i++) {
                const g3d_vert2 *a = &projected[cube_edge[i][0]];
                const g3d_vert2 *b = &projected[cube_edge[i][1]];
                g3d_line(fb, a->x, a->y, b->x, b->y, cyan);
            }
            g3d_present(fb);
            angle_y = (angle_y + 3) & 255;
        }
        barrier(nc);
    }
}
