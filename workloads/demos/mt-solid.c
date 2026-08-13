/*
 * mt-solid — filled, backface-culled, flat-shaded cube.
 *
 * All harts transform the 8 vertices (cheap). Each hart then fills
 * every triangle but only writes its row band of the framebuffer, so
 * the z-buffer stays race-free. Core 0 presents over UART.
 */

#define GL_width  64
#define GL_height 48
#define G3D_W     64
#define G3D_H     48

#include "GL_tty.h"
#include "g3d.h"
#include "util.h"

#define NVERT 8
#define NFACE 12

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

/* Two triangles per cube face. Winding is CCW when viewed from outside. */
static const unsigned char cube_tri[NFACE][3] = {
    {0, 1, 2}, {0, 2, 3}, /* -Z */
    {5, 4, 7}, {5, 7, 6}, /* +Z */
    {4, 0, 3}, {4, 3, 7}, /* -X */
    {1, 5, 6}, {1, 6, 2}, /* +X */
    {4, 5, 1}, {4, 1, 0}, /* -Y */
    {3, 2, 6}, {3, 6, 7}, /* +Y */
};

static const g3d_vec3 face_n[NFACE] = {
    { 0, 0, -G3D_ONE }, { 0, 0, -G3D_ONE },
    { 0, 0,  G3D_ONE }, { 0, 0,  G3D_ONE },
    {-G3D_ONE, 0, 0 },  {-G3D_ONE, 0, 0 },
    { G3D_ONE, 0, 0 },  { G3D_ONE, 0, 0 },
    { 0, -G3D_ONE, 0 }, { 0, -G3D_ONE, 0 },
    { 0,  G3D_ONE, 0 }, { 0,  G3D_ONE, 0 },
};

static const g3d_rgb face_col[6] = {
    {220,  70,  70},
    { 70, 200,  80},
    { 70, 110, 230},
    {230, 200,  60},
    {200,  80, 200},
    { 70, 210, 210},
};

static g3d_rgb    fb[G3D_W * G3D_H];
static uint16_t   zbuf[G3D_W * G3D_H];
static g3d_vert2  projected[NVERT];
static g3d_vec3   nrot[NFACE];
static volatile int angle_x;
static volatile int angle_y;
static volatile int video_go;

void thread_entry(int cid, int nc)
{
    const g3d_rgb black = {8, 8, 16};
    const g3d_vec3 light = { 24576, 24576, -49152 }; /* ~ (0.37, 0.37, -0.75) */
    const g3d_fx focal = g3d_from_int(72);
    const int cx = G3D_W / 2;
    const int cy = G3D_H / 2;
    int y_lo, y_hi;

    y_lo = (cid * G3D_H) / nc;
    y_hi = ((cid + 1) * G3D_H) / nc;

    /* Don't AMO-spin in barrier while hart 0 is still talking to the UART. */
    if (cid == 0) {
        angle_x = 18;
        angle_y = 30;
        printf("\033[?25l\033[H\033[2J");
        video_go = 1;
    } else {
        while (!video_go)
            ;
    }
    barrier(nc);

    for (;;) {
        g3d_mat4 rx, ry, rot, tz, world;
        int i;

        g3d_rotate_x(&rx, angle_x);
        g3d_rotate_y(&ry, angle_y);
        g3d_mul_mat(&rot, &ry, &rx);
        g3d_translate(&tz, 0, 0, g3d_from_int(4));
        g3d_mul_mat(&world, &tz, &rot);

        for (i = cid; i < NVERT; i += nc) {
            g3d_vec3 eye;
            g3d_transform(&eye, &world, &cube_obj[i]);
            g3d_project(&projected[i], &eye, cx, cy, focal);
        }
        for (i = cid; i < NFACE; i += nc) {
            /* Rotate the model-space normal with the 3×3 of `rot`. */
            const g3d_vec3 *n = &face_n[i];
            nrot[i].x = g3d_mul(rot.m[0], n->x) + g3d_mul(rot.m[1], n->y) + g3d_mul(rot.m[2], n->z);
            nrot[i].y = g3d_mul(rot.m[4], n->x) + g3d_mul(rot.m[5], n->y) + g3d_mul(rot.m[6], n->z);
            nrot[i].z = g3d_mul(rot.m[8], n->x) + g3d_mul(rot.m[9], n->y) + g3d_mul(rot.m[10], n->z);
        }
        barrier(nc);

        /* Each hart clears and fills only its row band. */
        {
            int y, x;
            for (y = y_lo; y < y_hi; y++) {
                for (x = 0; x < G3D_W; x++) {
                    int idx = y * G3D_W + x;
                    fb[idx] = black;
                    zbuf[idx] = 0xFFFFu;
                }
            }
        }
        for (i = 0; i < NFACE; i++) {
            const g3d_vert2 *a = &projected[cube_tri[i][0]];
            const g3d_vert2 *b = &projected[cube_tri[i][1]];
            const g3d_vert2 *c = &projected[cube_tri[i][2]];
            g3d_rgb col;
            /* y grows down after projection, so a front face is clockwise. */
            if (g3d_facing(a, b, c))
                continue;
            col = g3d_shade(face_col[i / 2], &nrot[i], &light);
            g3d_triangle(fb, zbuf, *a, *b, *c, col, y_lo, y_hi);
        }
        barrier(nc);

        if (cid == 0) {
            g3d_present(fb);
            angle_y = (angle_y + 3) & 255;
            angle_x = (angle_x + 1) & 255;
        }
        barrier(nc);
    }
}
