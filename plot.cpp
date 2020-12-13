/* handle each plotting area.
 */


#include "HamClock.h"

#define	BORDER_COLOR	GRAY
#define TICKLEN		2			// length of plot tickmarks, pixels
#define	LGAP		21			// left gap for labels
#define	BGAP		15			// bottom gap for labels
#define	FONTW		6			// font width with gap
#define	FONTH		8			// font height
#define	NBRGAP		15			// large plot overlay number top gap

static int tickmarks (float min, float max, int numdiv, float ticks[]);

/* plot the given data within the given box.
 * return whether had anything to plot.
 * N.B. if both labels are NULL, use same labels and limits as previous call
 */
bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float label_value)
{
	char buf[32];
        sprintf (buf, "%.*f", label_value >= 1000 ? 0 : 1, label_value);
	return (plotXYstr (box, x, y, nxy, xlabel, ylabel, color, buf));
}

/* same as plotXY but label is a string
 */
bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, char *label_str)
{
    resetWatchdog();

    // no labels implies overlay previous plot
    bool overlay = xlabel == NULL && ylabel == NULL;

    // persistent scale info in case of subsequent overlay
#   define MAXTICKS	10
    static float xticks[MAXTICKS+2], yticks[MAXTICKS+2];
    static uint8_t nxt, nyt;
    static float minx, maxx;
    static float miny, maxy;
    static float dx, dy;

    char buf[32];
    uint8_t bufl;

    // set font and color
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // report if no data
    if (nxy < 1 || !x || !y) {
	plotMessage (box, color, "No data");
	return (false);
    }

    // find new limits unless this is an overlay
    if (!overlay) {

	// find data extrema
	minx = x[0]; maxx = x[0];
	miny = y[0]; maxy = y[0];
	for (int i = 1; i < nxy; i++) {
	    if (x[i] > maxx) maxx = x[i];
	    if (x[i] < minx) minx = x[i];
	    if (y[i] > maxy) maxy = y[i];
	    if (y[i] < miny) miny = y[i];
	}
	minx = floor(minx);
	maxx = ceil(maxx);
	if (maxx < minx + 1) {
	    minx -= 1;
	    maxx += 1;
	}
	miny = floor(miny);
	maxy = ceil(maxy);
	if (maxy < miny + 1) {
	    miny -= 1;
	    maxy += 1;
	}

	// find tickmarks
	nxt = tickmarks (minx, maxx, MAXTICKS, xticks);
	nyt = tickmarks (miny, maxy, MAXTICKS, yticks);

        // handy ends
	minx = xticks[0];
	maxx = xticks[nxt-1];
	miny = yticks[0];
	maxy = yticks[nyt-1];
	dx = maxx-minx;
	dy = maxy-miny;

	// erase all except bottom line which is map border
	tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);

	// y tickmarks just to the left of the plot
	bufl = sprintf (buf, "%d", (int)maxy);
	tft.setCursor (box.x+LGAP-TICKLEN-bufl*FONTW, box.y); tft.print (buf);
	bufl = sprintf (buf, "%d", (int)miny);
	tft.setCursor (box.x+LGAP-TICKLEN-bufl*FONTW, box.y+box.h-BGAP-FONTH); tft.print (buf);
	for (int i = 0; i < nyt; i++) {
	    uint16_t ty = (uint16_t)(box.y + (box.h-BGAP)*(1 - (yticks[i]-miny)/dy) + 0.5);
	    tft.drawLine (box.x+LGAP-TICKLEN, ty, box.x+LGAP, ty, color);
	}

	// y label is down the left side
	uint8_t ylen = strlen(ylabel);
	uint16_t ly0 = box.y + (box.h - BGAP - ylen*FONTH)/2;
	for (uint8_t i = 0; i < ylen; i++) {
	    tft.setCursor (box.x+LGAP/3, ly0+i*FONTH);
	    tft.print (ylabel[i]);
	}

	// x tickmarks just below plot
        uint16_t txty = box.y+box.h-FONTH-2;
	tft.setCursor (box.x+LGAP, txty); tft.print (minx,0);
	bufl = sprintf (buf, "%c%d", maxx > 0 ? '+' : ' ', (int)maxx);
	tft.setCursor (box.x+box.w-2-bufl*FONTW, txty); tft.print (buf);
	for (int i = 0; i < nxt; i++) {
	    uint16_t tx = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(xticks[i]-minx)/dx);
	    tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
	}

        // always label 0 if within larger range
        if (minx < 0 && maxx > 0) {
	    uint16_t zx = (uint16_t)(box.x+LGAP + (box.w-LGAP)*(0-minx)/dx + 0.5);
            tft.setCursor (zx-FONTW/2, txty); tft.print (0);
        }

	// x label is centered about the plot across the bottom
	uint8_t xlen = strlen(xlabel);
	uint16_t lx0 = box.x + LGAP + (box.w - LGAP - xlen*FONTW)/2;
	for (uint8_t i = 0; i < xlen; i++) {
	    tft.setCursor (lx0+i*FONTW, box.y+box.h-FONTH-2);
	    tft.print (xlabel[i]);
	}

    }

    // draw plot
    uint16_t last_px = 0, last_py = 0;
    resetWatchdog();
    for (int i = 0; i < nxy; i++) {
	uint16_t px = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(x[i]-minx)/dx + 0.5);
	uint16_t py = (uint16_t)(box.y + 1 + (box.h-BGAP-2)*(1 - (y[i]-miny)/dy) + 0.5);
	if (i > 0 && (last_px != px || last_py != py))
	    tft.drawLine (last_px, last_py, px, py, color);		// avoid bug with 0-length lines
	else if (nxy == 1)
	    tft.drawLine (box.x+LGAP, py, box.x+box.w-1, py, color);	// one point clear across
	last_px = px;
	last_py = py;
    }

    // draw plot border
    tft.drawRect (box.x+LGAP, box.y, box.w-LGAP, box.h-BGAP, BORDER_COLOR);

    if (!overlay) {

	// overlay large center value on top in gray
	tft.setTextColor(BRGRAY);
	selectFontStyle (BOLD_FONT, LARGE_FONT);
	uint16_t bw, bh;
	getTextBounds (label_str, &bw, &bh);
	uint16_t text_x = box.x+LGAP+(box.w-LGAP-bw)/2;
	uint16_t text_y = box.y+NBRGAP+bh;
	tft.setCursor (text_x, text_y);
	tft.print (label_str);
    }

    // printFreeHeap (F("plotXYstr"));

    // ok
    return (true);
}

/* plot values of geomagnetic Kp index in boxy form in box b.
 * 8 values per day, nhkp historical followed by npkp predicted.
 * ala http://www.swpc.noaa.gov/products/planetary-k-index
 */
void plotKp (SBox &box, uint8_t kp[], uint8_t nhkp, uint8_t npkp, uint16_t color)
{
    resetWatchdog();

#   define	MAXKP	9
    // N.B. null font origin is upper left
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // erase all except bottom line which is map border
    tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);

    // y tickmarks just to the left of the plot
    tft.setCursor (box.x+LGAP-TICKLEN-2-FONTW, box.y); tft.print (MAXKP);
    tft.setCursor (box.x+LGAP-TICKLEN-2-FONTW, box.y+box.h-BGAP-FONTH); tft.print (0);
    for (uint8_t k = 0; k <= MAXKP; k++) {
	uint16_t h = k*(box.h-BGAP)/MAXKP;
	uint16_t ty = box.y + box.h - BGAP - h;
	tft.drawLine (box.x+LGAP-TICKLEN, ty, box.x+LGAP, ty, color);
    }

    // y label is down the left side
    static const char ylabel[] = "Planetary Kp";
    uint8_t ylen = sizeof(ylabel)-1;
    uint16_t ly0 = box.y + (box.h - BGAP - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
	tft.setCursor (box.x+LGAP/3, ly0+i*FONTH);
	tft.print (ylabel[i]);
    }

    // x labels
    uint8_t nkp = nhkp + npkp;
    tft.setCursor (box.x+LGAP, box.y+box.h-FONTH-2);
    tft.print (-nhkp/8);
    tft.setCursor (box.x+box.w-2*FONTW, box.y+box.h-FONTH-2);
    tft.print('+'); tft.print (npkp/8);
    tft.setCursor (box.x+LGAP+(box.w-LGAP)/2-2*FONTW, box.y+box.h-FONTH-2);
    tft.print (F("Days"));

    // label now if within wider range
    if (nhkp > 0 && npkp > 0) {
	uint16_t zx = box.x + LGAP + nhkp*(box.w-LGAP)/nkp;
        tft.setCursor (zx-FONTW/2, box.y+box.h-FONTH-2);
        tft.print(0);
    }

    // x ticks
    for (uint8_t i = 0; i < nkp/8; i++) {
	uint16_t tx = box.x + LGAP + 8*i*(box.w-LGAP)/nkp;
	tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
    }

    // plot Kp values as colored vertical bars depending on strength
    resetWatchdog();
    for (uint8_t i = 0; i < nkp; i++) {
	int8_t k = kp[i];
	uint16_t c = k < 4 ? RA8875_GREEN : k == 4 ? RA8875_YELLOW : RA8875_RED;
	uint16_t x = box.x + LGAP + i*(box.w-LGAP)/nkp;
	uint16_t w = (box.w-LGAP)/nkp-1;
	uint16_t h = k*(box.h-BGAP)/MAXKP;
	uint16_t y = box.y + box.h - BGAP - h;
	if (w > 0 || h > 0)
	    tft.fillRect (x, y, w, h, c);
    }

    // data border
    tft.drawRect (box.x+LGAP, box.y, box.w-LGAP, box.h-BGAP, BORDER_COLOR);

    // overlay large current value on top in gray
    tft.setTextColor(BRGRAY);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    char buf[32];
    sprintf (buf, "%d", kp[nhkp-1]);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);
    uint16_t text_x = box.x+LGAP+(box.w-LGAP-bw)/2;
    uint16_t text_y = box.y+NBRGAP+bh;
    tft.setCursor (text_x, text_y);
    tft.print (buf);

    // printFreeHeap (F("plotKp"));
}

/* shorten str IN PLACE as needed to be less that maxw pixels wide.
 * return final width in pixels.
 */
uint16_t maxStringW (char *str, uint16_t maxw)
{
    uint8_t strl = strlen (str);
    uint16_t bw;

    while ((bw = getTextWidth(str)) >= maxw)
	str[strl--] = '\0';

    return (bw);
}

/* print weather info in the given box
 */
void plotWX (const SBox &box, uint16_t color, const WXInfo &wi)
{
    resetWatchdog();

    // erase all except bottom line which is map border then add border
    tft.fillRect (box.x, box.y, box.w, box.h-1, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, GRAY);

    const uint8_t indent = FONTW;	// allow for attribution
    uint16_t dy = box.h/3;
    uint16_t ddy = box.h/5;
    float f;
    char buf[32];
    uint16_t w;

    // large temperature with degree symbol and units
    tft.setTextColor(color);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    f = useMetricUnits() ? wi.temperature_c : 9*wi.temperature_c/5+32;
    sprintf (buf, "%.0f %c", f, useMetricUnits() ? 'C' : 'F');
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2-8, box.y+dy);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf+strlen(buf)-2, &bw, &bh);
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (tft.getCursorX()-bw, tft.getCursorY()-2*bh/3);
    tft.print('o');
    dy += ddy;


    // remaining info smaller
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // humidity
    sprintf (buf, "%.0f%% RH", wi.humidity_percent);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // wind
    f = (useMetricUnits() ? 3.6 : 2.237) * wi.wind_speed_mps; // kph or mph
    sprintf (buf, "%s @ %.0f %s", wi.wind_dir_name, f, useMetricUnits() ? "kph" : "mph");
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // nominal conditions
    strcpy (buf, wi.conditions);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+indent+(box.w-indent-w)/2, box.y+dy);
    tft.print(buf);

    // attribution very small down the left side
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint8_t ylen = strlen(wi.attribution);
    uint16_t ly0 = box.y + (box.h - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
	tft.setCursor (box.x+2, ly0+i*FONTH);
	tft.print (wi.attribution[i]);
    }

    // printFreeHeap (F("plotWX"));
}

/* this function handles the actual drawing of the Band Conditions pane. It can be called in two quite
 * different ways:
 * 1. when called by plotBandConditions, we are given a table containing relative propagation values for each
 *    band and a summary line to be drawn across the bottom.
 * 2. we can also be called just to update the visual appearance of one of the band indicators, in which
 *    case the table and summary line are NULL. In this case we only draw the band indicated by the global
 *    prop_map, and leave the rest of the pane alone.
 * N.B. coordinate layout geometry with checkBCTouch()
 * N.B. may be called before first plotBC so beware no rel table yet.
 */
void BCHelper (const SBox *bp, int busy, float rel_tbl[PROP_MAP_N], char *cfg_str)
{
    // which box to draw in is required 
    if (!bp)
        return;

    // handy conversion of rel to text color
    #define RELCOL(r)       ((r) < 0.33 ? RA8875_RED : ((r) < 0.66 ? RA8875_YELLOW : RA8875_GREEN))

    // prep layout
    uint16_t ty = bp->y + 27;           // BOTTOM of title; match DX Cluster title
    uint16_t cy = bp->y+bp->h-10;       // TOP of config string; beware comma descender
    uint16_t br_gap = bp->w/5;
    uint16_t col1_x = bp->x + 10;
    uint16_t col2_x = bp->x + 5*bp->w/9;
    uint16_t row_h = (cy-2-ty)/(PROP_MAP_N/2);

    // start over of we have a new table
    if (rel_tbl && cfg_str) {

        // erase all then draw border
        tft.fillRect (bp->x, bp->y, bp->w, bp->h, RA8875_BLACK);
        tft.drawRect (bp->x, bp->y, bp->w, bp->h, GRAY);

        // center title across the top
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor(RA8875_WHITE);
        const char *title = "VOACAP DE-DX";
        uint16_t bw = getTextWidth (title);
        tft.setCursor (bp->x+(bp->w-bw)/2, ty);
        tft.print ((char*)title);

        // center the config across the bottom
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(GRAY);
        bw = maxStringW (cfg_str, bp->w);
        tft.setCursor (bp->x+(bp->w-bw)/2, cy);
        tft.print ((char*)cfg_str);

        // draw each rel_tab entry, 4 rows between ty and cy
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        for (int i = 0; i < PROP_MAP_N; i++) {
            uint16_t row_x = (i < PROP_MAP_N/2) ? col1_x : col2_x;
            uint16_t row_y = ty + row_h + (i%(PROP_MAP_N/2))*row_h;              // this is bottom of string

            char buf[10];
            tft.setTextColor(RELCOL(rel_tbl[i]));
            tft.setCursor (row_x + br_gap, row_y);
            snprintf (buf, sizeof(buf), "%2.0f", 99*rel_tbl[i]); // 100 doesn't fit
            tft.print (buf);
            if (i == PROP_MAP_80M)
                tft.print("%");
        }

    } 

    // always draw each band number
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (int i = 0; i < PROP_MAP_N; i++) {
        uint16_t row_x = (i < PROP_MAP_N/2) ? col1_x : col2_x;
        uint16_t row_y = ty + row_h + (i%(PROP_MAP_N/2))*row_h;

        // background square then number
        if (i == prop_map) {
            // show highlighted as per busy
            uint16_t rect_col = busy > 0 ? RA8875_YELLOW : (busy < 0 ? RA8875_RED : GRAY);
            tft.fillRect (row_x-1, row_y-row_h+4, bp->w/6, row_h-2, rect_col);
            tft.setTextColor(RA8875_BLACK);
        } else {
            // show plain
            tft.fillRect (row_x-1, row_y-row_h+4, bp->w/6, row_h-2, RA8875_BLACK);
            tft.setTextColor(BRGRAY);
        }
        tft.setCursor (row_x, row_y);
        tft.print (propMap2Band((PropMapSetting)i));
    }

    printFreeHeap (F("BCHelper"));
}

/* print the band conditions in the given box based on given server response and config message.
 * response is CSV percent reliability for bands 80-10. if anything else print it as an error message.
 */
bool plotBandConditions (const SBox &box, char response[], char config[])
{
    resetWatchdog();

    // crack response from server, show as error message if not conforming to expected format
    float rel[PROP_MAP_N];
    if (sscanf (response, _FX("%f,%*f,%f,%f,%f,%f,%f,%f,%f"),        // skip 60m
            &rel[PROP_MAP_80M], &rel[PROP_MAP_40M], &rel[PROP_MAP_30M], &rel[PROP_MAP_20M],
            &rel[PROP_MAP_17M], &rel[PROP_MAP_15M], &rel[PROP_MAP_12M], &rel[PROP_MAP_10M]) != PROP_MAP_N) {
        plotMessage (box, RA8875_RED, response);
        return (false);
    }

    // display
    BCHelper (&box, 0, rel, config);

    // ok
    return (true);
}

/* print the NOAA RSG Space Weather Scales in the given box.
 * response is 3 lines of the form:
 *  R  0 0 0 0
 *  S  0 0 0 0
 *  G  0 0 0 0
 * if any line does not match expected format print it as an error message.
 */
void plotNOAASWx (const SBox &box, const char rsglines[3][50])
{
    resetWatchdog();

    // erase all then draw border
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, GRAY);

    // title
    tft.setTextColor(RA8875_YELLOW);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t h = box.h/5-2;                             // text row height
    char *title = (char *) "NOAA SpaceWx";
    uint16_t bw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-bw)/2, box.y+h);
    tft.print (title);

    // crack and print each line
    for (uint8_t i = 0; i < 3; i++) {
        char RSG;
        int day[4];
        if (sscanf (rsglines[i], _FX("%c %d %d %d %d"), &RSG, &day[0], &day[1], &day[2], &day[3]) != 5) {
            plotMessage (box, RA8875_RED, rsglines[i]);
            break;
        }

        uint16_t w = box.w/7-1;
        h += box.h/4;
        tft.setCursor (box.x+w+(i==2?-2:0), box.y+h);   // center the G
        tft.setTextColor(GRAY);
        tft.print (RSG);

        w += box.w/10;
        for (uint8_t d = 0; d < 4; d++) {
            w += box.w/7;
            tft.setCursor (box.x+w, box.y+h);
            tft.setTextColor(day[d] == 0 ? RA8875_GREEN : (day[d] <= 3 ? RA8875_YELLOW : RA8875_RED));
            tft.print (day[d]);
        }
    }
}


/* print a message in a (plot?) box, take care not to go outside
 */
void plotMessage (const SBox &box, uint16_t color, const char *message)
{
    // log
    Serial.printf (_FX("PlotMsg: %s\n"), message);

    // prep font
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // prep box
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, GRAY);

    // make a copy so we can use destructive maxStringW
    char *msg_cpy = strdup (message);
    size_t msg_len = strlen (message);
    uint16_t msg_printed = 0;
    uint16_t y = box.y + box.h/4;

    // show up to at least a few lines
    resetWatchdog();
    for (int n_lines = 0; n_lines < 5 && msg_printed < msg_len; n_lines++) {

        // draw one line
        uint16_t msgw = maxStringW (msg_cpy, box.w-2);
        tft.setCursor (box.x+(box.w-msgw)/2, y);                // horizontally centered
        tft.print(msg_cpy);

        // advance
        msg_printed += strlen (msg_cpy);
        strcpy (msg_cpy, message + msg_printed);
        y += 2*FONTH;
    }

    // done
    free (msg_cpy);
}

/* given min and max and an approximate number of divisions desired,
 * fill in ticks[] with nicely spaced values and return how many.
 * N.B. return value, and hence number of entries to ticks[], might be as
 *   much as 2 more than numdiv.
 */
static int
tickmarks (float min, float max, int numdiv, float ticks[])
{
    static int factor[] = { 1, 2, 5 };
#   define NFACTOR    NARRAY(factor)
    float minscale;
    float delta;
    float lo;
    float v;
    int n;

    minscale = fabs(max - min);

    if (minscale == 0) {
	/* null range: return ticks in range min-1 .. min+1 */
	for (n = 0; n < numdiv; n++)
	    ticks[n] = min - 1.0 + n*2.0/numdiv;
	return (numdiv);
    }

    delta = minscale/numdiv;
    for (n=0; n < (int)NFACTOR; n++) {
	float scale;
	float x = delta/factor[n];
	if ((scale = (powf(10.0F, ceilf(log10f(x)))*factor[n])) < minscale)
	    minscale = scale;
    }
    delta = minscale;

    lo = floor(min/delta);
    for (n = 0; (v = delta*(lo+n)) < max+delta; )
	ticks[n++] = v;

    return (n);
}
