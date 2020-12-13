/* HamClock glue
 */


#ifndef _HAMCLOCK_H
#define	_HAMCLOCK_H


// handy build categories
#if defined(_USE_X11) || defined(_USE_FB0)
#define _USE_DESKTOP
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(_IS_ODROID)
#define _USE_UNIX
#endif

#if defined __has_include && defined(__arm__) && defined(__linux__)
#  if __has_include(<bcm_host.h>)
#    define _IS_RPI
#  endif
#endif

#if defined(ESP8266)
#define _IS_ESP8266
#endif

// Flip screen only on ESP
#if defined(_IS_ESP8266)
#define _SUPPORT_FLIP
#endif

// kx3 on ESP or rpi
#if defined(_IS_ESP8266) || defined(_IS_RPI)
#define _SUPPORT_KX3
#endif

// temp sensor on ESP or Pi
#if defined(_IS_ESP8266) || defined(_IS_RPI)
#define _SUPPORT_ENVSENSOR
#endif

// phot only supported on ESP and then only if phot is detected
#if defined(_IS_ESP8266)
#define _SUPPORT_PHOT
#endif

// on/off supported on ESP and all fb0; dimming supported on ESP and fb0 but only if HDMI is found to be DSI
#if defined(_IS_ESP8266) || defined(_USE_FB0)
#define _SUPPORT_BR
#endif

// only RPi and ESP have GPIO pins for control
#if defined(_IS_ESP8266) || defined(_IS_RPI)
#define _SUPPORT_GPIO
#endif // _IS_RPI



// actual map size
#if defined(_CLOCK_1600x960)

#define HC_MAP_W 1320
#define HC_MAP_H 660

#elif defined(_CLOCK_2400x1440)

#define HC_MAP_W 1980
#define HC_MAP_H 990

#elif defined(_CLOCK_3200x1920)

#define HC_MAP_W 2640
#define HC_MAP_H 1320

#else   // original size

#define HC_MAP_W 660
#define HC_MAP_H 330

#endif



// canonical map size 
#define EARTH_H   330
#define EARTH_XH  1
#define EARTH_W   660
#define EARTH_XW  1


// UNIX-like modules
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// see Adafruit_RA8875.h
#define USE_ADAFRUIT_GFX_FONTS

// community modules
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>
#include "Adafruit_RA8875_R.h"

// HamClock modules
#include "calibrate.h"
#include "version.h"
#include "P13.h"

// handy nelements in array
// N.B. call with real array, not a pointer
#define NARRAY(a)       (sizeof(a)/sizeof(a[0]))

// float versions
#define	M_PIF	3.14159265F
#define	M_PI_2F	(M_PIF/2)



/* handy malloc wrapper that frees automatically when leaves scope
 */
class StackMalloc 
{
    public:

        StackMalloc (size_t nbytes) {
            // printf ("SM: new %lu\n", nbytes);
            mem = (char *) malloc (nbytes);
            siz = nbytes;
        }

        StackMalloc (const char *string) {
            // printf ("SM: new %s\n", string);
            mem = (char *) strdup (string);
            siz = strlen(string) + 1;
        }

        ~StackMalloc (void) {
            // printf ("SM: free(%d)\n", siz);
            free (mem);
        }

        size_t getSize(void) {
            return (siz);
        }

        char *getMem(void) {
            return (mem);
        }

    private:

        char *mem;
        size_t siz;
};





/*********************************************************************************************
 *
 * ESPHamClock.ino
 *
 */

extern const char svr_host[];

// screen coordinates, upper left at [0,0]
typedef struct {
    uint16_t x, y;
} SCoord;

// screen coords of box ul and size
typedef struct {
    uint16_t x, y, w, h;
} SBox;

// screen center, radius
typedef struct {
    SCoord s;
    uint16_t r;
} SCircle;

// timezone info
typedef struct {
    SBox box;
    uint16_t color;
    int32_t tz_secs;
} TZInfo;


// map lat, lng, + radians N and E
typedef struct {
    float lat, lng;			// radians north, east
    float lat_d, lng_d;			// degrees
} LatLong;

#define	LIFE_LED	0

#define DE_INFO_ROWS    3
#define DX_INFO_ROWS    5


extern Adafruit_RA8875_R tft;
extern TZInfo de_tz, dx_tz;
extern SBox NCDXF_b;

extern SBox brightness_b;

#define PLOTBOX_W 160
#define PLOTBOX_H 149
extern SBox plot1_b, plot2_b, plot3_b;
extern SBox sensor_b;

extern SBox clock_b;

extern SBox rss_bnr_b;
extern SBox rss_btn_b;
extern uint8_t rss_on;

extern SBox desrss_b, dxsrss_b;
extern uint8_t desrss, dxsrss;
enum {
    DXSRSS_INAGO,
    DXSRSS_ATAT,
    DXSRSS_PREFIX,              // must be last
    DXSRSS_N,
};

// show NCDXF beacons or up to one of several brightness controls in brightness_b
extern uint8_t brb_mode;
typedef enum {
    BRB_SHOW_BEACONS,           // NCDXF beacons
    BRB_SHOW_ONOFF,             // on/off/idle times
    BRB_SHOW_PHOT,              // brightness and phot controls
    BRB_SHOW_BR,                // just brightness control
    BRB_SHOW_NOTHING,
} BRB_MODE;

extern uint8_t plot1_ch, plot2_ch, plot3_ch;

extern SBox azm_btn_b;
extern uint8_t azm_on;

extern SBox llg_btn_b;
extern uint8_t llg_on;

extern SBox dx_info_b;
extern SBox satname_b;
extern SBox de_info_b;
extern SBox map_b;
extern SBox dx_maid_b;
extern SBox de_maid_b;
extern SBox lkscrn_b;


// ESP mechanism to save lots of RAM by storing what appear to be RAM strings in FLASH
#if defined (_IS_ESP8266)
#define _FX(x)  _FX_helper (F(x))
extern const char *_FX_helper(const __FlashStringHelper *flash_string);
#else
#define _FX(x)  x
#endif

#define RSS_BG_COLOR    RGB565(0,40,80)		// RSS banner background color
#define RSS_FG_COLOR    RA8875_WHITE		// RSS banner text color

extern uint8_t azimuthal_mode;
extern SBox azmmode_b;

extern char *stack_start;

enum {
    LLG_OFF,
    LLG_TROPICS,
    LLG_ALL
};

#define MAX_PREF_LEN     4              // maximumm prefix length

extern void drawDXTime(void);
extern void drawDXMarker(bool force);
extern void drawAllSymbols(bool erase_too);
extern void drawTZ(const TZInfo &tzi);
extern bool inBox (const SCoord &s, const SBox &b);
extern bool inCircle (const SCoord &s, const SCircle &c);
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...);
extern void reboot(void);
extern void printFreeHeap (const __FlashStringHelper *label);
extern void getWorstMem (int *heap, int *stack);
extern void resetWatchdog(void);
extern void wdDelay(int ms);
extern bool timesUp (uint32_t *prev, uint32_t dt);
extern void setDXPathInvalid(void);
extern void drawRSSButton(void);
extern bool overMap (const SCoord &s);
extern bool overAnySymbol (const SCoord &s);
extern bool overRSS (const SCoord &s);
extern bool overRSS (const SBox &b);
extern void drawAzmMercButton (void);
extern void setAzmMerc(bool on);
extern void drawLLGridButton (void);
extern void changeLLGrid(void);
extern void newDE (LatLong &ll);
extern void newDX (LatLong &ll, const char *override_prefix);
extern void roundLL (LatLong &ll);
extern void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp);
extern uint16_t getTextWidth (const char str[]);
extern void normalizeLL (LatLong &ll);
extern bool screenIsLocked(void);
extern time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs);
extern void eraseScreen(void);
extern void setMapTagBox (const char *tag, const SCoord &c, uint16_t r, SBox &box);
extern void drawMapTag (const char *tag, SBox &box);
extern void setDXPrefixOverride (char p[MAX_PREF_LEN]);
extern bool getDXPrefix (char p[MAX_PREF_LEN+1]);
extern void call2Prefix (const char *call, char prefix[MAX_PREF_LEN]);





/*********************************************************************************************
 *
 * OTAupdate.cpp
 *
 */

extern bool newVersionIsAvailable (char *nv, uint16_t nvl);
extern bool askOTAupdate(char *ver);
extern void doOTAupdate(void);



/*********************************************************************************************
 *
 * astro.cpp
 *
 */

extern void subSolar (time_t t, LatLong &ll);
extern void subLunar (time_t t, LatLong &ll);
extern void sunrs (const time_t &t0, const LatLong &ll, time_t *riset, time_t *sett);
extern float rad2deg(float r);
extern float deg2rad(float d);

#define SPD             (3600*24L)      // seconds per day



/*********************************************************************************************
 *
 * brightness.cpp
 *
 */


extern void drawBrightness (void);
extern void initBrightness (void);
extern void setupBrightness (void);
extern void followBrightness (void);
extern void changeBrightness (SCoord &s);
extern bool brightnessOn(void);
extern void brightnessOff(void);
extern bool checkBeaconTouch (SCoord &s);
extern bool setDisplayTimes (uint16_t on, uint16_t off, uint16_t idle);
extern bool getDisplayTimes (uint16_t *onp, uint16_t *offp, uint16_t *idlep);
extern bool brControlOk(void);
extern uint8_t getBrMax(void);
extern uint8_t getBrMin(void);
extern bool found_phot;






/*********************************************************************************************
 *
 * clocks.cpp
 *
 */


enum {
    DETIME_INFO,
    DETIME_ANALOG,
    DETIME_CAL,
    DETIME_N,
};

extern uint8_t de_time_fmt;
extern void initTime(void);
extern time_t nowWO(void);
extern void updateClocks(bool all);
extern bool clockTimeOk(void);
extern void changeTime (time_t t);
extern bool checkClockTouch (SCoord &s);
extern bool checkTZTouch (const SCoord &s, TZInfo &tzi, const LatLong &ll);
extern void enableSyncProvider(void);
extern void drawDESunRiseSetInfo(void);
extern void drawCalendar(bool force);
extern void hideClocks(void);
extern void showClocks(void);
extern void drawDXSunRiseSetInfo(void);
extern int32_t utcOffset(void);




/*********************************************************************************************
 *
 * color.cpp
 *
 */

// convert 8-bit each (R,G,B) to 5R : 6G : 5G
// would expect this to be in graphics lib but can't find it...
#define RGB565(R,G,B)   ((((uint16_t)(R) & 0xF8) << 8) | (((uint16_t)(G) & 0xFC) << 3) | ((uint16_t)(B) >> 3))

// extract 8-bit colors from uint16_t RGB565 color in range 0-255
#define RGB565_R(c)     (((c) & 0xF800) >> 8)
#define RGB565_G(c)     (((c) & 0x07E0) >> 3)
#define RGB565_B(c)     (((c) & 0x001F) << 3)

#define	GRAY	RGB565(140,140,140)
#define	BRGRAY	RGB565(200,200,200)

extern void hsvtorgb(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t h, uint8_t s, uint8_t v);






/*********************************************************************************************
 *
 * dxcluster.cpp
 *
 */

#define MAX_DXSPOTCALL_LEN              12
typedef struct {
    char call[MAX_DXSPOTCALL_LEN];      // call
    float freq;                         // kHz
    uint32_t grid;                      // 4 packed chars, used only with WSJT-X
    uint16_t uts;                       // UT spotted
    LatLong ll;                         // lat, long
    SBox map_b;                         // map label
} DXSpot;

extern void initDXCluster(void);
extern void updateDXCluster(void);
extern void closeDXCluster(void);
extern bool checkDXTouch (const SCoord &s);
extern bool getDXSpots (DXSpot **spp, uint8_t *nspotsp);
extern bool overAnyDXSpots(const SCoord &s);
extern void drawDXSpotsOnMap (void);
extern void updateDXSpotScreenLocations(void);
extern bool isDXConnected(void);
extern bool sendDELLGrid(void);







/*********************************************************************************************
 *
 * earthmap.cpp
 *
 */



#define	DX_R	8             		// dx marker radius (erases better if even)
#define	DX_COLOR RA8875_GREEN

extern SCircle dx_c;
extern LatLong dx_ll;

extern uint16_t map_x0, map_y0;
extern uint16_t map_w, map_h;

extern uint8_t show_km;
extern uint8_t show_lp;
#define	ERAD_M	3959.0F			// earth radius, miles

#define	DE_R 8				// radius of DE marker	 (erases better if even)
#define	DEAP_R 8			// radius of DE antipodal marker (erases better if even)
#define	DE_COLOR  RGB565(255,125,0)	// orange

extern SCircle de_c;
extern LatLong de_ll;
extern float sdelat, cdelat;
extern SCircle deap_c;
extern LatLong deap_ll;
extern LatLong sun_ss_ll;
extern LatLong moon_ss_ll;

#define SUN_R 9				// radius of sun marker
extern float sslng, sslat, csslat, ssslat;
extern SCircle sun_c;

#define MOON_R 9			// radius of moon marker
#define	MOON_COLOR  RGB565(150,150,150)
extern SCircle moon_c;

extern uint32_t max_wd_dt;
extern uint8_t flash_crc_ok;

extern void drawMoreEarth (void);
extern void eraseDEMarker (void);
extern void eraseDEAPMarker (void);
extern void drawDEMarker (bool force);
extern void drawDEAPMarker (void);
extern void drawDEInfo (void);
extern void drawDETime (bool center);
extern void drawDXTime (void);
extern void initEarthMap (void);
extern void antipode (LatLong &to, const LatLong &from);
extern void drawMapCoord (const SCoord &s);
extern void drawMapCoord (uint16_t x, uint16_t y);
extern void drawSun (void);
extern void drawMoon (void);
extern void drawDXInfo (void);
extern void ll2s (const LatLong &ll, SCoord &s, uint8_t edge);
extern void ll2s (float lat, float lng, SCoord &s, uint8_t edge);
extern bool s2ll (uint16_t x, uint16_t y, LatLong &ll);
extern bool s2ll (const SCoord &s, LatLong &ll);
extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);
extern bool checkDistTouch (const SCoord &s);
extern bool checkPathDirTouch (const SCoord &s);
extern void propDEDXPath (bool long_path, float *distp, float *bearp);
extern bool waiting4DXPath(void);
extern void eraseSCircle (const SCircle &c);

extern bool checkDELLTouch (const SCoord &s, LatLong &ll);
extern bool checkDXLLTouch (const SCoord &s, LatLong &ll);
extern void roundLatLong (LatLong &ll);
extern void initScreen(void);



/*********************************************************************************************
 *
 * BME280.cpp
 *
 */


extern bool bme280_connected;
extern void updateBME280 (void);
extern void initBME280 (void);
extern bool nextBME280Data (time_t *t, float *temp, float *pressure, float *humidity, float *dp, uint8_t *n);
extern void initBME280Retry(void);


/*********************************************************************************************
 *
 * earthsat.cpp
 *
 */

extern void updateSatPath(void);
extern void updateSatPass(void);
extern bool querySatSelection(void);
extern bool checkSatTouch (const SCoord &s);
extern bool checkSatNameTouch (const SCoord &s);
extern void displaySatInfo(void);
extern void setSatObserver (float lat, float lng);
extern void drawSatPointsOnRow (uint16_t r);
extern void drawSatNameOnRow(uint16_t y);
extern bool dx_info_for_sat;
extern bool setSatFromName (const char *new_name);
extern bool setSatFromTLE (const char *name, const char *t1, const char *t2);
extern bool initSatSelection(void);
extern bool getSatAzElNow (char *name, float *azp, float *elp, float *rangep, float *ratep,
        float *razp, float *sazp, float *rdtp, float *sdtp);
extern bool isNewPass(void);
extern bool isSatMoon(void);

#define SAT_NOAZ        (-999)  // error flag
#define SAT_MIN_EL      1.0F    // rise elevation
#define TLE_LINEL       70      // including EOS






/*********************************************************************************************
 *
 * gimbal.cpp
 *
 */

extern void initGimbalGUI(void);
extern bool haveGimbal(void);
extern void updateGimbal (void);
extern bool checkGimbalTouch (const SCoord &s);
extern void stopGimbalNow(void);
extern void closeGimbal(void);
extern bool getGimbalWrapAz (float *azp);





/*********************************************************************************************
 *
 * gpsd.cpp
 *
 */

extern bool getGPSDLatLong(LatLong *llp);
extern time_t getGPSDUTC(void);



/*********************************************************************************************
 *
 * setup.cpp
 *
 */


extern void clockSetup(void);
extern const char *getWiFiSSID(void);
extern const char *getWiFiPW(void);
extern const char *getCallsign(void);
extern const char *getDXClusterHost(void);
extern int getDXClusterPort(void);
extern bool useMetricUnits(void);
extern bool useGeoIP(void);
extern bool useGPSD(void);
extern bool mapDXSpots(void);
extern bool plotSpotCallsigns(void);
extern bool rotateScreen(void);
extern float getBMETempCorr(void);
extern float getBMEPresCorr(void);
extern const char *getGPSDHost(void);
extern bool useLocalNTPHost(void);
extern bool GPIOOk(void);
extern const char *getLocalNTPHost(void);
extern bool useDXCluster(void);
extern uint32_t getKX3Baud(void);
extern const char *getCoreMapStyle(void);
extern void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color);
extern bool logUsageOk(void);




/*********************************************************************************************
 *
 * magdecl.cpp
 *
 */

extern int magdecl (float l, float L, float e, float y, float *mdp);




/*********************************************************************************************
 *
 * mapmanage.cpp
 *
 */


typedef enum {
    PROP_MAP_80M,
    PROP_MAP_40M,
    PROP_MAP_30M,
    PROP_MAP_20M,
    PROP_MAP_17M,
    PROP_MAP_15M,
    PROP_MAP_12M,
    PROP_MAP_10M,
    PROP_MAP_N
} PropMapSetting;
#define PROP_MAP_OFF    PROP_MAP_N
extern PropMapSetting prop_map;


extern float propMap2MHz (PropMapSetting pms);
extern int propMap2Band (PropMapSetting pms);
extern bool installPropMaps (float MHz);
extern bool installBackgroundMap (bool verbose, const char *style);
extern bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp);
extern bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp);
extern const char *getMapStyle(void);

typedef struct {
    char name[33];      // name with EOS
    char date[21];      // ISO 8601 date with EOS
    uint32_t len;       // n bytes
} FS_Info;
extern FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used);





/*********************************************************************************************
 *
 * ncdxf.cpp
 *
 */

extern void updateBeacons (bool erase_too, bool immediate, bool force);
extern void updateBeaconScreenLocations(void);
extern bool overAnyBeacon (const SCoord &s);
extern void drawBeaconBox();

typedef uint8_t BeaconID;




/*********************************************************************************************
 *
 * nvram.cpp
 *
 */


/* names of each entry
 * N.B. the entries here must match those in nv_sizes[]
 */
typedef enum {
    NV_TOUCH_CAL_A,
    NV_TOUCH_CAL_B,
    NV_TOUCH_CAL_C,
    NV_TOUCH_CAL_D,
    NV_TOUCH_CAL_E,
    NV_TOUCH_CAL_F,
    NV_TOUCH_CAL_DIV,
    NV_DE_DST,  // not used
    NV_DE_TIMEFMT,
    NV_DE_LAT,
    NV_DE_LNG,
    NV_DE_GRID,
    NV_DX_DST,  // not used
    NV_DX_LAT,
    NV_DX_LNG,
    NV_DX_GRID,
    NV_CALL_FG_COLOR,
    NV_CALL_BG_COLOR,
    NV_CALL_BG_RAINBOW,
    NV_DIST_KM,
    NV_UTC_OFFSET,
    NV_PLOT_1,
    NV_PLOT_2,
    NV_BRB_MODE,
    NV_PLOT_3,
    NV_RSS_ON,
    NV_BPWM_DIM,
    NV_PHOT_DIM,
    NV_BPWM_BRIGHT,
    NV_PHOT_BRIGHT,
    NV_LP,
    NV_METRIC_ON,
    NV_LKSCRN_ON,
    NV_AZIMUTHAL_ON,
    NV_ROTATE_SCRN,
    NV_WIFI_SSID,
    NV_WIFI_PASSWD_OLD,
    NV_CALLSIGN,
    NV_SATNAME,
    NV_DE_SRSS,
    NV_DX_SRSS,
    NV_LLGRID,
    NV_DPYON,
    NV_DPYOFF,
    NV_DXHOST,
    NV_DXPORT,
    NV_SWHUE,
    NV_TEMPCORR,
    NV_GPSDHOST,
    NV_KX3BAUD,
    NV_BCPOWER,
    NV_CD_PERIOD,
    NV_PRESCORR,
    NV_BR_IDLE,
    NV_BR_MIN,
    NV_BR_MAX,
    NV_DE_TZ,
    NV_DX_TZ,
    NV_MAPSTYLE,
    NV_USEDXCLUSTER,
    NV_USEGPSD,
    NV_LOGUSAGE,
    NV_MAPSPOTS,
    NV_WIFI_PASSWD,
    NV_NTPSET,
    NV_NTPHOST,
    NV_GPIOOK,
    NV_N
} NV_Name;

// string valued lengths including trailing EOS
#define	NV_WIFI_SSID_LEN	32
#define	NV_WIFI_PW_LEN_OLD	32
#define	NV_CALLSIGN_LEN	        12
#define	NV_SATNAME_LEN	        9
#define	NV_DXHOST_LEN	        26
#define	NV_GPSDHOST_LEN	        18
#define	NV_NTPHOST_LEN	        18
#define NV_MAPSTYLE_LEN         10
#define	NV_WIFI_PW_LEN	        64


// accessor functions
extern void NVWriteFloat (NV_Name e, float f);
extern void NVWriteUInt32 (NV_Name e, uint32_t u);
extern void NVWriteInt32 (NV_Name e, int32_t u);
extern void NVWriteUInt16 (NV_Name e, uint16_t u);
extern void NVWriteUInt8 (NV_Name e, uint8_t u);
extern void NVWriteString (NV_Name e, const char *str);
extern bool NVReadFloat (NV_Name e, float *fp);
extern bool NVReadUInt32 (NV_Name e, uint32_t *up);
extern bool NVReadInt32 (NV_Name e, int32_t *up);
extern bool NVReadUInt16 (NV_Name e, uint16_t *up);
extern bool NVReadUInt8 (NV_Name e, uint8_t *up);
extern bool NVReadString (NV_Name e, char *buf);




/*********************************************************************************************
 *
 * maidenhead.cpp
 *
 */


extern void ll2maidenhead (char maid[2][5], const LatLong &ll);
extern bool maidenhead2ll (LatLong &ll, const char maid[5]);
extern void drawMaidenhead(NV_Name nv, SBox &b, uint16_t color);
extern void setMaidenhead(NV_Name nv, LatLong &ll);
extern void toggleMaidenhead(NV_Name nv, LatLong &ll);
extern uint32_t packMaidFromStr (char maidstr[4]);
extern void unpackMaidToStr (char maidstr[5], uint32_t grid);





/*********************************************************************************************
 *
 * plot.cpp
 *
 */

typedef enum {
    PLOT1_SSN,
    PLOT1_XRAY,
    PLOT1_FLUX,
    PLOT1_KP,
    PLOT1_BC,
    PLOT1_DEWX,
    PLOT1_N,
} PLOT1_Choices;

typedef enum {
    PLOT2_XRAY,
    PLOT2_FLUX,
    PLOT2_KP,
    PLOT2_BC,
    PLOT2_DX,
    PLOT2_N,
} PLOT2_Choices;

typedef enum {
    PLOT3_TEMP, PLOT3_PRESSURE, PLOT3_HUMIDITY, PLOT3_DEWPOINT,
    PLOT3_SDO_1, PLOT3_SDO_2, PLOT3_SDO_3,
    PLOT3_GIMBAL,
    PLOT3_KP,
    PLOT3_BC,
    PLOT3_NOAASWX,
    PLOT3_DXWX,
    PLOT3_N
} PLOT3_Choices;

#define N_PANES 3       // see switch in setWiFiPane()

typedef struct {
    char city[32];
    float temperature_c;
    float humidity_percent;
    float wind_speed_mps;
    char wind_dir_name[4];
    char clouds[32];
    char conditions[32];
    char attribution[32];
} WXInfo;
#define	N_WXINFO_FIELDS	8

extern void BCHelper (const SBox *bp, int busy, float rel_table[PROP_MAP_N], char *config_str);
extern bool plotBandConditions (const SBox &box, char response[], char config[]);
extern bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
	const char *ylabel, uint16_t color, float center_value);
extern bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
	const char *ylabel, uint16_t color, char *label_str);
extern void plotKp (SBox &b, uint8_t kp[], uint8_t nhkp, uint8_t npkp, uint16_t color);
extern void plotWX (const SBox &b, uint16_t color, const WXInfo &wi);
extern void plotMessage (const SBox &b, uint16_t color, const char *message);
extern void plotNOAASWx (const SBox &b, const char rsglines[3][50]);
extern uint16_t maxStringW (char *str, uint16_t maxw);





/*********************************************************************************************
 *
 * prefixes.cpp
 *
 */

extern bool nearestPrefix (const LatLong &ll, char prefix[MAX_PREF_LEN+1]);




/*********************************************************************************************
 *
 * radio.cpp
 *
 */

void setRadioSpot (float kHz);



/*********************************************************************************************
 *
 * santa.cpp
 *
 */

extern void drawSanta(void);
extern SBox santa_b;



/*********************************************************************************************
 *
 * selectFont.cpp
 *
 */


extern const GFXfont Germano_Regular16pt7b PROGMEM;
extern const GFXfont Germano_Bold16pt7b PROGMEM;
extern const GFXfont Germano_Bold30pt7b PROGMEM;

typedef enum {
    BOLD_FONT,
    LIGHT_FONT
} FontWeight;

typedef enum {
    FAST_FONT,
    SMALL_FONT,
    LARGE_FONT
} FontSize;

extern void selectFontStyle (FontWeight w, FontSize s);




/*********************************************************************************************
 *
 * sphere.cpp
 *
 */

extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);



/*********************************************************************************************
 *
 * touch.cpp
 *
 */


// touch screen actions
typedef enum {
    TT_NONE,				// no touch event
    TT_TAP,				// brief touch event
    TT_HOLD,				// at least TOUCH_HOLDT
} TouchType;

extern void calibrateTouch(bool force);
extern void drainTouch(void);
extern TouchType readCalTouch (SCoord &s);

extern TouchType wifi_tt;
extern SCoord wifi_tt_s;




/*********************************************************************************************
 *
 * stopwatch.cpp
 *
 */

extern SBox stopwatch_b;
extern uint32_t countdown_period;
extern void checkStopwatchTouch(TouchType tt);
extern bool runStopwatch(void);
extern void drawMainPageStopwatch (bool force);
extern void startCountdown(uint32_t ms);
extern uint32_t getCountdownLeft(void);





/*********************************************************************************************
 *
 * tz.cpp
 *
 */
extern int32_t getTZ (const LatLong &ll);



/*********************************************************************************************
 *
 * webserver.cpp
 *
 */

extern void initWebServer(void);
extern void checkWebServer(void);
extern TouchType readCalTouchWS (SCoord &s);



/*********************************************************************************************
 *
 * wifi.cpp
 *
 */

extern void initSys (void);
extern void initWiFiRetry(void);
extern void newBC(void);
extern void newVOACAPMap(void);
extern void updateWiFi(void);
extern void revertPlot1 (uint32_t dt);
extern bool checkPlot1Touch (const SCoord &s);
extern bool checkPlot2Touch (const SCoord &s);
extern bool checkPlot3Touch (const SCoord &s);
extern bool getChar (WiFiClient &client, char *cp);
extern time_t getNTPUTC(void);
extern bool updateRSS (void);
extern void updateRSSNow(void);
extern bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll);
extern void sendUserAgent (WiFiClient &client);
extern bool wifiOk(void);
extern void httpGET (WiFiClient &client, const char *server, const char *page);
extern bool httpSkipHeader (WiFiClient &client);
extern bool httpSkipHeader (WiFiClient &client, uint32_t *lastmodp);
extern void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str);
extern void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str);
extern bool setPlot1 (PLOT1_Choices p1);
extern bool setPlot2 (PLOT2_Choices p2);
extern bool setPlot3 (PLOT3_Choices p3);
extern uint16_t bc_power;

// standard ports
#define HTTPPORT        80



/*********************************************************************************************
 *
 * wx.cpp
 *
 */


extern bool updateDEWX (const SBox &box);
extern bool updateDXWX (const SBox &box);
extern void showDXWX(void);
extern void showDEWX(void);


#endif // _HAMCLOCK_H
