/* Host-only smoke: project one cube pose and print the 2D bbox.
 * Not part of the bare-metal image.  gcc -O2 -I. host_smoke.c g3d.c -o /tmp/g3d_smoke
 */
#define GL_width  64
#define GL_height 48
#define G3D_W 64
#define G3D_H 48

#include <stdio.h>
#include <stdint.h>

void uart_send_char(char c) { putchar(c); }

#include "g3d.h"

static const g3d_vec3 cube_obj[8] = {
    { -G3D_ONE, -G3D_ONE, -G3D_ONE },
    {  G3D_ONE, -G3D_ONE, -G3D_ONE },
    {  G3D_ONE,  G3D_ONE, -G3D_ONE },
    { -G3D_ONE,  G3D_ONE, -G3D_ONE },
    { -G3D_ONE, -G3D_ONE,  G3D_ONE },
    {  G3D_ONE, -G3D_ONE,  G3D_ONE },
    {  G3D_ONE,  G3D_ONE,  G3D_ONE },
    { -G3D_ONE,  G3D_ONE,  G3D_ONE },
};

int main(void)
{
    g3d_mat4 rx, ry, rot, tz, world;
    int i, xmin = 999, xmax = -999, ymin = 999, ymax = -999, ok = 0;
    g3d_rotate_x(&rx, 18);
    g3d_rotate_y(&ry, 30);
    g3d_mul_mat(&rot, &ry, &rx);
    g3d_translate(&tz, 0, 0, g3d_from_int(4));
    g3d_mul_mat(&world, &tz, &rot);
    for (i = 0; i < 8; i++) {
        g3d_vec3 eye;
        g3d_vert2 p;
        g3d_transform(&eye, &world, &cube_obj[i]);
        if (!g3d_project(&p, &eye, 32, 24, g3d_from_int(72)))
            continue;
        ok++;
        if (p.x < xmin) xmin = p.x;
        if (p.x > xmax) xmax = p.x;
        if (p.y < ymin) ymin = p.y;
        if (p.y > ymax) ymax = p.y;
        printf("v%d -> (%d,%d) z=%d\n", i, p.x, p.y, (int)(p.z >> 16));
    }
    printf("projected %d/8  bbox x[%d,%d] y[%d,%d]\n", ok, xmin, xmax, ymin, ymax);
    if (ok < 8) return 1;
    if (xmax < 0 || xmin >= 64 || ymax < 0 || ymin >= 48) return 2;
    if (xmax - xmin < 8 || ymax - ymin < 8) return 3;
    return 0;
}
