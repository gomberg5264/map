/* code to support santa easter egg
 */

#include "HamClock.h"

#define SANTA_C         RA8875_RED

#define SANTA_W 17
#define SANTA_H 50
static const uint8_t santa[SANTA_W*SANTA_H] PROGMEM = {
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x07, 0x00, 0x20, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x01, 0x00, 0xf0, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0xe0, 0x00, 0x00, 0xe0, 0x03, 0xa0, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0xe0, 0x01, 0x00, 0xc0, 0x07, 0xc0, 0x01,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0xf0, 0x01, 0x00, 0xc0, 0x01, 0xc0, 0x03,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0xff, 0x01, 0x00, 0xe0, 0x00, 0x80, 0x1f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0xd0, 0xff, 0xff, 0x09, 0xf8, 0x00, 0x80, 0x0f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xf0, 0xff, 0x03, 0xfc, 0xff, 0x00, 0x00, 0x07,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xf0, 0xff, 0x0f, 0xf8, 0xff, 0x00, 0x00, 0x07,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0xe6, 0xff, 0x0c, 0xf8, 0xff, 0x00, 0x00, 0x0f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xde, 0xe1, 0x1f, 0x04, 0xf8, 0xff, 0x00, 0x80, 0x0f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xfc, 0x3f, 0xe0, 0x01, 0x04, 0xf8, 0xff, 0x01, 0xc0, 0x0f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xff, 0x1f, 0xe0, 0x00, 0x06, 0x78, 0xfe, 0x07, 0xf8, 0x1f,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0x1f, 0xe0, 0x00, 0x02, 0x3c, 0x00, 0x99, 0xfe, 0x1f,
 0xe0, 0x01, 0x3e, 0x00, 0x00, 0xfc, 0x81, 0xff, 0x1f, 0x70, 0x00, 0x00, 0x0e, 0x00, 0xe1, 0xff, 0x7f,
 0xe0, 0xe1, 0x7f, 0x00, 0xc0, 0x01, 0x80, 0xff, 0x0f, 0x38, 0x00, 0x00, 0x07, 0x80, 0x80, 0xff, 0x6f,
 0xc0, 0x07, 0x7c, 0x00, 0x18, 0x00, 0x80, 0xff, 0x1f, 0x08, 0x00, 0x80, 0x01, 0x40, 0x00, 0xff, 0x43,
 0x80, 0x1f, 0x78, 0x00, 0x03, 0x00, 0x80, 0x87, 0x31, 0x08, 0x00, 0x80, 0x00, 0x00, 0x00, 0x3f, 0x20,
 0x80, 0x7f, 0xf8, 0xe0, 0x00, 0x06, 0xc0, 0x03, 0x30, 0x08, 0x00, 0x60, 0x00, 0x00, 0x00, 0x0e, 0x20,
 0xc0, 0xff, 0xf8, 0x70, 0x00, 0x0f, 0xe0, 0x01, 0x10, 0x06, 0x00, 0x20, 0x00, 0x00, 0x00, 0x0e, 0x20,
 0xc0, 0xff, 0xf1, 0x3f, 0x00, 0x07, 0x70, 0x00, 0x08, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
 0xc0, 0xff, 0xf1, 0x0f, 0x80, 0x03, 0x10, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
 0xe0, 0xff, 0xff, 0x03, 0x80, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x03, 0x00,
 0xe0, 0xff, 0xff, 0x03, 0xc0, 0x03, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
 0xfc, 0xff, 0xff, 0x07, 0xe0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
 0xfe, 0xff, 0xff, 0x1f, 0xf0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
 0xf9, 0xff, 0xff, 0x3f, 0xf8, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
 0xc0, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
 0x80, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0xff, 0xff, 0xc7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0xff, 0x3f, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0x7f, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0x07, 0x81, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0xff, 0x10, 0x31, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0x0f, 0xc0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0xff, 0x06, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x1c, 0x90, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

#define SANTA_WPIX      (SANTA_W*8)     // width in pixels

// boundary box
SBox santa_b = {0, 0, SANTA_WPIX, SANTA_H};

/* draw santa on christmas eve, else move him off the map
 */
void drawSanta()
{
        // skip unless Christmas eve
        time_t t = nowWO();
        if (month(t) != 12 || day(t) != 24) {
            santa_b.x = santa_b.y = 0;
            return;
        }

        resetWatchdog();

        int hr = hour(t);
        int mn = minute(t);
        uint16_t santa_x, santa_y;

        // place so it moves over the globe througout the day.
        // left-right each hour, top-bottom throughout the day
        if (azm_on) {

            // find corner so santa is totally within hemisphere circle

            uint16_t map_y_cntr = map_b.y + map_b.h/2;
            uint16_t hemi_r = map_b.w/4;
            uint16_t corner_dy = sqrtf(hemi_r*hemi_r - SANTA_WPIX/2*SANTA_WPIX/2);
            uint16_t y_top = map_y_cntr - corner_dy;
            uint16_t y_bot = map_y_cntr + corner_dy - SANTA_H;
            santa_y = y_top + hr*(y_bot-y_top)/24;

            uint16_t map_x_cntr = mn > 30 ? map_b.x + 3*hemi_r : map_b.x + hemi_r;
            uint16_t dy = santa_y > map_y_cntr ? santa_y - map_y_cntr + SANTA_H : map_y_cntr - santa_y;
            uint16_t half_w = sqrtf(hemi_r*hemi_r - dy*dy);
            uint16_t x_left = map_x_cntr - half_w;
            uint16_t x_right = map_x_cntr + half_w - SANTA_WPIX;
            santa_x = x_left + (mn > 30 ? (mn-30)*(x_right-x_left)/30 : mn*(x_right-x_left)/30);

        } else {

            // move linearly through map

            santa_x = map_b.x + 20 + mn*(map_b.w - 20 - SANTA_WPIX)/60;
            santa_y = map_b.y + 10 + hr*(map_b.h - SANTA_H)/24;
        }

        // erase first if moved, unless not shown
        if (santa_b.x != 0 && (santa_b.x != santa_x || santa_b.y != santa_y)) {
            // Serial.printf ("Erasing santa from %d x %d\n", santa_b.x, santa_b.y);
            for (uint8_t sr = 0; sr < SANTA_H; sr++) {
                resetWatchdog();
                for (uint8_t sc = 0; sc < SANTA_W; sc++) {
                    for (uint8_t bc = 0; bc < 8; bc++) {
                        uint16_t sx = santa_b.x + 8*sc + bc;
                        uint16_t sy = santa_b.y + sr;
                        SCoord ss = {sx, sy};
                        drawMapCoord(ss);
                    }
                }
            }
        }

        // new location
        santa_b.x = santa_x;
        santa_b.y = santa_y;

        // check the dominant obstruction
        if (overRSS (santa_b)) {
            santa_b.x = santa_b.y = 0;
            return;
        }

        // paint
        // Serial.printf ("Painting santa at %d x %d\n", santa_b.x, santa_b.y);
        for (uint8_t sr = 0; sr < SANTA_H; sr++) {
            resetWatchdog();
            for (uint8_t sc = 0; sc < SANTA_W; sc++) {
                uint8_t mask = pgm_read_byte(&santa[sr*SANTA_W+sc]);
                for (uint8_t bc = 0; bc < 8; bc++) {
                    uint16_t sx = santa_b.x + 8*sc + bc;
                    uint16_t sy = santa_b.y + sr;
                    if (mask & (1<<bc))
                        tft.drawPixel (sx, sy, SANTA_C);
                    else {
                        SCoord s;
                        s.x = sx;
                        s.y = sy;
                        drawMapCoord(s);
                    }
                }
            }
        }

        resetWatchdog();
        // printFreeHeap (F("Santa"));
}
