#ifndef G_DEV_FB_H
#define G_DEV_FB_H

struct v_world;

unsigned char *g_fb_dump_text(struct v_world *);
int g_fb_init(struct v_world *, unsigned long);
void g_fb_write(struct v_world *, unsigned char, unsigned char);
int g_fb_getx(struct v_world *);
int g_fb_gety(struct v_world *);
void g_fb_setx(struct v_world *, int);
void g_fb_sety(struct v_world *, int);

struct g_dev_fb {
    unsigned int x;
    unsigned int y;
};

#endif
