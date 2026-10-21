/* Simulator controls (host only). */
#ifndef SIM_H
#define SIM_H

extern int sim_update_count;
extern int sim_closed;
void sim_advance(int ms);
long long sim_now(void);
void sim_set_lang(const char *lang);
int sim_save_png(const char *path);
/* Number of framebuffer pixels exactly equal to the 0xRRGGBB colour. */
int sim_count_rgb(int rgb);

#endif
