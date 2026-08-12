#include "g3d.h"

#define GL_width  G3D_W
#define GL_height G3D_H
#include "GL_tty.h"

/* 256-entry Q16.16 sine, 0..2π. cos(a) = sin(a + 64). */
static const g3d_fx g3d_sintab[256] = {
        0,    1608,    3216,    4821,    6424,    8022,    9616,   11204,
    12785,   14359,   15924,   17479,   19024,   20557,   22078,   23586,
    25080,   26558,   28020,   29466,   30893,   32303,   33692,   35062,
    36410,   37736,   39040,   40320,   41576,   42806,   44011,   45190,
    46341,   47464,   48559,   49624,   50660,   51665,   52639,   53581,
    54491,   55368,   56212,   57022,   57798,   58538,   59244,   59914,
    60547,   61145,   61705,   62228,   62714,   63162,   63572,   63944,
    64277,   64571,   64827,   65043,   65220,   65358,   65457,   65516,
    65536,   65516,   65457,   65358,   65220,   65043,   64827,   64571,
    64277,   63944,   63572,   63162,   62714,   62228,   61705,   61145,
    60547,   59914,   59244,   58538,   57798,   57022,   56212,   55368,
    54491,   53581,   52639,   51665,   50660,   49624,   48559,   47464,
    46341,   45190,   44011,   42806,   41576,   40320,   39040,   37736,
    36410,   35062,   33692,   32303,   30893,   29466,   28020,   26558,
    25080,   23586,   22078,   20557,   19024,   17479,   15924,   14359,
    12785,   11204,    9616,    8022,    6424,    4821,    3216,    1608,
        0,   -1608,   -3216,   -4821,   -6424,   -8022,   -9616,  -11204,
   -12785,  -14359,  -15924,  -17479,  -19024,  -20557,  -22078,  -23586,
   -25080,  -26558,  -28020,  -29466,  -30893,  -32303,  -33692,  -35062,
   -36410,  -37736,  -39040,  -40320,  -41576,  -42806,  -44011,  -45190,
   -46341,  -47464,  -48559,  -49624,  -50660,  -51665,  -52639,  -53581,
   -54491,  -55368,  -56212,  -57022,  -57798,  -58538,  -59244,  -59914,
   -60547,  -61145,  -61705,  -62228,  -62714,  -63162,  -63572,  -63944,
   -64277,  -64571,  -64827,  -65043,  -65220,  -65358,  -65457,  -65516,
   -65536,  -65516,  -65457,  -65358,  -65220,  -65043,  -64827,  -64571,
   -64277,  -63944,  -63572,  -63162,  -62714,  -62228,  -61705,  -61145,
   -60547,  -59914,  -59244,  -58538,  -57798,  -57022,  -56212,  -55368,
   -54491,  -53581,  -52639,  -51665,  -50660,  -49624,  -48559,  -47464,
   -46341,  -45190,  -44011,  -42806,  -41576,  -40320,  -39040,  -37736,
   -36410,  -35062,  -33692,  -32303,  -30893,  -29466,  -28020,  -26558,
   -25080,  -23586,  -22078,  -20557,  -19024,  -17479,  -15924,  -14359,
   -12785,  -11204,   -9616,   -8022,   -6424,   -4821,   -3216,   -1608,
};

g3d_fx g3d_sin(int angle256) { return g3d_sintab[angle256 & 255]; }
g3d_fx g3d_cos(int angle256) { return g3d_sintab[(angle256 + 64) & 255]; }

void g3d_ident(g3d_mat4 *o)
{
    int i;
    for (i = 0; i < 16; i++)
        o->m[i] = 0;
    o->m[0] = o->m[5] = o->m[10] = o->m[15] = G3D_ONE;
}

void g3d_mul_mat(g3d_mat4 *o, const g3d_mat4 *a, const g3d_mat4 *b)
{
    g3d_mat4 t;
    int r, c, k;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            int64_t s = 0;
            for (k = 0; k < 4; k++)
                s += (int64_t)a->m[r * 4 + k] * (int64_t)b->m[k * 4 + c];
            t.m[r * 4 + c] = (g3d_fx)(s >> G3D_SHIFT);
        }
    }
    *o = t;
}

void g3d_translate(g3d_mat4 *o, g3d_fx x, g3d_fx y, g3d_fx z)
{
    g3d_ident(o);
    o->m[3]  = x;
    o->m[7]  = y;
    o->m[11] = z;
}

void g3d_rotate_x(g3d_mat4 *o, int angle256)
{
    g3d_fx c = g3d_cos(angle256);
    g3d_fx s = g3d_sin(angle256);
    g3d_ident(o);
    o->m[5]  =  c;
    o->m[6]  = -s;
    o->m[9]  =  s;
    o->m[10] =  c;
}

void g3d_rotate_y(g3d_mat4 *o, int angle256)
{
    g3d_fx c = g3d_cos(angle256);
    g3d_fx s = g3d_sin(angle256);
    g3d_ident(o);
    o->m[0]  =  c;
    o->m[2]  =  s;
    o->m[8]  = -s;
    o->m[10] =  c;
}

void g3d_rotate_z(g3d_mat4 *o, int angle256)
{
    g3d_fx c = g3d_cos(angle256);
    g3d_fx s = g3d_sin(angle256);
    g3d_ident(o);
    o->m[0] =  c;
    o->m[1] = -s;
    o->m[4] =  s;
    o->m[5] =  c;
}

void g3d_transform(g3d_vec3 *o, const g3d_mat4 *m, const g3d_vec3 *v)
{
    o->x = g3d_mul(m->m[0], v->x) + g3d_mul(m->m[1], v->y) +
           g3d_mul(m->m[2], v->z) + m->m[3];
    o->y = g3d_mul(m->m[4], v->x) + g3d_mul(m->m[5], v->y) +
           g3d_mul(m->m[6], v->z) + m->m[7];
    o->z = g3d_mul(m->m[8], v->x) + g3d_mul(m->m[9], v->y) +
           g3d_mul(m->m[10], v->z) + m->m[11];
}

int g3d_project(g3d_vert2 *o, const g3d_vec3 *eye, int cx, int cy, g3d_fx focal)
{
    if (eye->z <= (G3D_ONE / 8))
        return 0;
    o->x = cx + g3d_to_int(g3d_div(g3d_mul(eye->x, focal), eye->z));
    o->y = cy + g3d_to_int(g3d_div(g3d_mul(eye->y, focal), eye->z));
    o->z = eye->z;
    return 1;
}

void g3d_cross(g3d_vec3 *o, const g3d_vec3 *a, const g3d_vec3 *b)
{
    o->x = g3d_mul(a->y, b->z) - g3d_mul(a->z, b->y);
    o->y = g3d_mul(a->z, b->x) - g3d_mul(a->x, b->z);
    o->z = g3d_mul(a->x, b->y) - g3d_mul(a->y, b->x);
}

g3d_fx g3d_dot(const g3d_vec3 *a, const g3d_vec3 *b)
{
    return g3d_mul(a->x, b->x) + g3d_mul(a->y, b->y) + g3d_mul(a->z, b->z);
}

/* One Newton step on 1/sqrt is enough for lighting. */
void g3d_normalize(g3d_vec3 *v)
{
    g3d_fx len2 = g3d_dot(v, v);
    g3d_fx x;
    if (len2 <= 0)
        return;
    /* Integer sqrt of Q16.16 → Q16.16: sqrt(len2)*256 */
    {
        uint64_t n = (uint64_t)len2;
        uint64_t r = 0, bit = (uint64_t)1 << 30;
        while (bit > n)
            bit >>= 2;
        while (bit) {
            if (n >= r + bit) {
                n -= r + bit;
                r = (r >> 1) + bit;
            } else {
                r >>= 1;
            }
            bit >>= 2;
        }
        x = (g3d_fx)(r << 8);
    }
    if (x == 0)
        return;
    v->x = g3d_div(v->x, x);
    v->y = g3d_div(v->y, x);
    v->z = g3d_div(v->z, x);
}

int g3d_facing(const g3d_vert2 *a, const g3d_vert2 *b, const g3d_vert2 *c)
{
    int64_t ux = (int64_t)b->x - a->x;
    int64_t uy = (int64_t)b->y - a->y;
    int64_t vx = (int64_t)c->x - a->x;
    int64_t vy = (int64_t)c->y - a->y;
    return (ux * vy - uy * vx) > 0;
}

g3d_rgb g3d_shade(g3d_rgb base, const g3d_vec3 *n, const g3d_vec3 *light)
{
    g3d_fx ndotl = g3d_dot(n, light);
    int amb = 48;
    int k;
    g3d_rgb o;
    if (ndotl < 0)
        ndotl = 0;
    k = (int)((ndotl * 207) >> G3D_SHIFT);
    o.r = (uint8_t)(amb + ((base.r * k) >> 8));
    o.g = (uint8_t)(amb + ((base.g * k) >> 8));
    o.b = (uint8_t)(amb + ((base.b * k) >> 8));
    if (o.r < amb) o.r = (uint8_t)amb;
    if (o.g < amb) o.g = (uint8_t)amb;
    if (o.b < amb) o.b = (uint8_t)amb;
    return o;
}

void g3d_clear(g3d_rgb *fb, uint16_t *zbuf, g3d_rgb c)
{
    int i, n = G3D_W * G3D_H;
    for (i = 0; i < n; i++) {
        fb[i] = c;
        if (zbuf)
            zbuf[i] = 0xFFFFu;
    }
}

static uint16_t z_encode(g3d_fx zi)
{
    int v = zi >> 8;
    if (v < 0)
        v = 0;
    if (v > 65535)
        v = 65535;
    return (uint16_t)v;
}

void g3d_plot(g3d_rgb *fb, uint16_t *zbuf, int x, int y, g3d_fx zi, g3d_rgb c)
{
    int idx;
    uint16_t ze;
    if ((unsigned)x >= (unsigned)G3D_W || (unsigned)y >= (unsigned)G3D_H)
        return;
    idx = y * G3D_W + x;
    ze = z_encode(zi);
    if (zbuf) {
        if (ze >= zbuf[idx])
            return;
        zbuf[idx] = ze;
    }
    fb[idx] = c;
}

void g3d_line(g3d_rgb *fb, int x0, int y0, int x1, int y1, g3d_rgb c)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx >= 0 ? 1 : -1;
    int sy = dy >= 0 ? 1 : -1;
    int x = x0, y = y0, err, e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    err = dx - dy;
    for (;;) {
        g3d_plot(fb, 0, x, y, 0, c);
        if (x == x1 && y == y1)
            break;
        e2 = err << 1;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}

static int lerp_i(int a, int b, int num, int den)
{
    if (den == 0)
        return a;
    return a + (int)(((int64_t)(b - a) * num) / den);
}

static g3d_fx lerp_z(g3d_fx a, g3d_fx b, int num, int den)
{
    if (den == 0)
        return a;
    return a + (g3d_fx)(((int64_t)(b - a) * num) / den);
}

void g3d_triangle(g3d_rgb *fb, uint16_t *zbuf,
                  g3d_vert2 a, g3d_vert2 b, g3d_vert2 c,
                  g3d_rgb col, int y_lo, int y_hi)
{
    g3d_vert2 t;
    int y, y0, y1, y2;
    if (a.y > b.y) { t = a; a = b; b = t; }
    if (b.y > c.y) { t = b; b = c; c = t; }
    if (a.y > b.y) { t = a; a = b; b = t; }
    y0 = a.y;
    y1 = b.y;
    y2 = c.y;
    if (y2 == y0)
        return;
    if (y_lo < 0) y_lo = 0;
    if (y_hi > G3D_H) y_hi = G3D_H;
    for (y = y0; y <= y2; y++) {
        int xl, xr, x;
        g3d_fx zl, zr;
        if (y < y_lo || y >= y_hi)
            continue;
        xl = lerp_i(a.x, c.x, y - y0, y2 - y0);
        zl = lerp_z(a.z, c.z, y - y0, y2 - y0);
        if (y < y1 && y1 != y0) {
            xr = lerp_i(a.x, b.x, y - y0, y1 - y0);
            zr = lerp_z(a.z, b.z, y - y0, y1 - y0);
        } else if (y2 != y1) {
            xr = lerp_i(b.x, c.x, y - y1, y2 - y1);
            zr = lerp_z(b.z, c.z, y - y1, y2 - y1);
        } else {
            continue;
        }
        if (xl > xr) {
            int xi = xl; g3d_fx zi = zl;
            xl = xr; zl = zr;
            xr = xi; zr = zi;
        }
        for (x = xl; x <= xr; x++)
            g3d_plot(fb, zbuf, x, y, lerp_z(zl, zr, x - xl, xr - xl), col);
    }
}

void g3d_present(const g3d_rgb *fb)
{
    int x, y;
    GL_home();
    for (y = 0; y < G3D_H; y += 2) {
        for (x = 0; x < G3D_W; x++) {
            const g3d_rgb *a = &fb[y * G3D_W + x];
            const g3d_rgb *b = &fb[(y + 1) * G3D_W + x];
            GL_set2pixelsRGBhere(a->r, a->g, a->b, b->r, b->g, b->b);
        }
        GL_newline();
    }
}
