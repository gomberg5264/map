/* use wifi for NTP and scraping web sites.
 * call updateClocks() during lengthy operations, particularly after making new connections.
 */

#include "HamClock.h"



// agent
#if defined(__linux__)
static const char agent[] = "HamClock-linux";
#elif defined (__APPLE__)
static const char agent[] = "MacHamClock-apple";
#else
static const char agent[] = "ESPHamClock";
#endif

// RSS info
#define	RSS_INTERVAL	15000			// polling period, millis()
static const char rss_page[] = "/ham/HamClock/RSS/web15rss.pl";
#define NRSS            15                      // max number RSS entries to cache

// kp historical and predicted info, new data posted every 3 hours
#define	KP_INTERVAL	3500000UL		// polling period, millis()
#define	KP_COLOR	RA8875_YELLOW		// loading message text color
static const char kp_page[] = "/ham/HamClock/geomag/kindex.txt";

// xray info, new data posted every 10 minutes
#define	XRAY_INTERVAL	600000UL		// polling interval, millis()
#define	XRAY_LCOLOR	RGB565(255,50,50)	// long wavelength plot color, reddish
#define	XRAY_SCOLOR	RGB565(50,50,255)	// short wavelength plot color, blueish
static const char xray_page[] = "/ham/HamClock/xray/xray.txt";

// sunspot info, new data posted daily
#define	SSPOT_INTERVAL	3400000UL		// polling interval, millis()
#define	SSPOT_COLOR	RGB565(100,100,255)	// loading message text color
static const char sspot_page[] = "/ham/HamClock/ssn/ssn.txt";

// solar flux info, new data posted three times a day
#define	FLUX_INTERVAL	3300000UL		// polling interval, millis()
#define	FLUX_COLOR	RA8875_GREEN		// loading message text color
static const char sf_page[] = "/ham/HamClock/solar-flux/solarflux.txt";

// band conditions and map, models change each hour
#define	BC_INTERVAL	2400000UL		// polling interval, millis()
#define	VOACAP_INTERVAL	2500000UL		// polling interval, millis()
static const char bc_page[] = "/ham/HamClock/fetchBandConditions.pl";
static bool bc_reverting;                       // set while waiting for BC after WX
static int bc_hour, map_hour;                   // hour when valid
static bool bc_error;                           // set while BC error message is visible
uint16_t bc_power;                              // VOACAP power setting
PropMapSetting prop_map = PROP_MAP_OFF;         // whether/how showing background prop map

// NOAA RSG space weather scales
#define	NOAASWX_INTERVAL	3700000UL	// polling interval, millis()
static const char noaaswx_page[] = "/ham/HamClock/NOAASpaceWX/noaaswx.txt";

// geolocation web page
static const char locip_page[] = "/ham/HamClock/fetchIPGeoloc.pl";

// SDO images
#define	SDO_INTERVAL	3200000UL		// polling interval, millis()
#define	SDO_COLOR	RA8875_MAGENTA		// loading message text color
static struct {
    const char *read_msg;
    const char *file_name;
} sdo_images[3] = {
#if defined(_CLOCK_1600x960)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_340.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_340_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_340_HMIB.bmp"}
#elif defined(_CLOCK_2400x1440)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_510.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_510_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_510_HMIB.bmp"}
#elif defined(_CLOCK_3200x1920)
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_680.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_680_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_680_HMIB.bmp"}
#else
    { "Reading SDO composite",   "/ham/HamClock/SDO/f_211_193_171_170.bmp"},
    { "Reading SDO 6173 A",      "/ham/HamClock/SDO/latest_170_HMIIC.bmp"},
    { "Reading SDO magnetogram", "/ham/HamClock/SDO/latest_170_HMIB.bmp"}
#endif
};

// weather displays
#define	DEWX_INTERVAL	1700000UL               // polling interval, millis()
#define	DXWX_INTERVAL	1600000UL               // polling interval, millis()


// list of default NTP servers unless user has set their own
typedef struct {
    const char *server;                         // name of server
    int rsp_time;                               // last known response time, millis()
} NTPList;
static NTPList ntp_list[] = {                   // init times to 0 insures all get tried initially
    {"pool.ntp.org", 0},
    {"time.google.com", 0},
    {"time.apple.com", 0},
    {"time.nist.gov", 0},
    {"europe.pool.ntp.org", 0},
    {"asia.pool.ntp.org", 0},
    {"ru.pool.ntp.org", 0},
};
#define NNTP NARRAY(ntp_list)
#define NTP_TOO_LONG 3000                       // too long response time, millis()


// web site retry interval, millis()
#define	WIFI_RETRY	15000UL

// millis() of next attempts -- 0 will refresh immediately -- reset in initWiFiRetry()
static uint32_t next_flux;
static uint32_t next_ssn;
static uint32_t next_xray;
static uint32_t next_kp;
static uint32_t next_rss;
static uint32_t next_sdo;
static uint32_t next_noaaswx;
static uint32_t next_dewx;
static uint32_t next_dxwx;
static uint32_t next_bc;
static uint32_t next_map;

// local funcs
static bool updateKp(SBox &box);
static bool updateXRay(const SBox &box);
static bool updateSDO(void);
static bool updateSunSpots(void);
static bool updateSolarFlux(const SBox &box);
static bool updateBandConditions(const SBox &box);
static bool updateNOAASWx(const SBox &box);
static uint32_t crackBE32 (uint8_t bp[]);


/* set de_ll.lat_d and de_ll.lng_d from our public ip.
 * report status via tftMsg
 */
static void geolocateIP ()
{
    WiFiClient iploc_client;				// wifi client connection
    float lat, lng;
    char llline[80];
    char ipline[80];
    char credline[80];
    int nlines = 0;

    Serial.println(locip_page);
    resetWatchdog();
    if (wifiOk() && iploc_client.connect(svr_host, HTTPPORT)) {
	httpGET (iploc_client, svr_host, locip_page);
	if (!httpSkipHeader (iploc_client)) {
            Serial.println (F("geoIP header short"));
            goto out;
        }

        // expect 4 lines: LAT=, LNG=, IP= and CREDIT=, anything else first line is error message
	if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lat = atof (llline+4);
	if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lng = atof (llline+4);
	if (!getTCPLine (iploc_client, ipline, sizeof(ipline), NULL))
            goto out;
        nlines++;
	if (!getTCPLine (iploc_client, credline, sizeof(credline), NULL))
            goto out;
        nlines++;
    }

out:

    if (nlines == 4) {
        // ok

        tftMsg (true, 0, _FX("IP %s geolocation by:"), ipline+3);
        tftMsg (true, 0, _FX("  %s"), credline+7);

        de_ll.lat_d = lat;
        de_ll.lng_d = lng;
        normalizeLL (de_ll);
        setMaidenhead(NV_DE_GRID, de_ll);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

    } else {
        // trouble, error message if 1 line

        if (nlines == 1) {
            tftMsg (true, 0, _FX("IP geolocation err:"));
            tftMsg (true, 1000, _FX("  %s"), llline);
        } else
            tftMsg (true, 1000, _FX("IP geolocation failed"));
    }

    iploc_client.stop();
    resetWatchdog();
    printFreeHeap (F("geolocateIP"));
}

/* search ntp_list for the fastest so far.
 * N.B. always return one of ntp_list, never NULL
 */
static NTPList *findBestNTP()
{
    int rsp_min = ntp_list[0].rsp_time;
    NTPList *best_ntp = &ntp_list[0];
    for (unsigned i = 0; i < NNTP; i++) {
        NTPList *np = &ntp_list[i];
        if (np->rsp_time < rsp_min) {
            best_ntp = np;
            rsp_min = np->rsp_time;
        }
    }
    return (best_ntp);
}


/* init and connect, inform via tftMsg() if verbose.
 * non-verbose is used for automatic retries that should not clobber the display.
 */
static void initWiFi (bool verbose)
{
    // N.B. look at the usages and make sure this is "big enough"
    static const char dots[] = ".........................................";

    // probable mac when only localhost -- used to detect LAN but no WLAN
    const char *mac_lh = _FX("FF:FF:FF:FF:FF:FF");

    tftMsg (verbose, 0, _FX("Starting Network:"));
    resetWatchdog();

    // begin
    // N.B. ESP seems to reconnect much faster if avoid begin() unless creds change
    // N.B. non-RPi UNIX systems return NULL from getWiFI*()
    WiFi.mode(WIFI_STA);
    const char *myssid = getWiFiSSID();
    const char *mypw = getWiFiPW();
    if (myssid && mypw && (strcmp (WiFi.SSID().c_str(), myssid) || strcmp (WiFi.psk().c_str(), mypw)))
        WiFi.begin ((char*)myssid, (char*)mypw);

    // prep
    resetWatchdog();
    uint32_t t0 = millis();
    uint32_t timeout = verbose ? 30000UL : 3000UL;      // dont wait nearly as long for a retry, millis
    uint16_t ndots = 0;                                 // progress counter
    char mac[30];
    strcpy (mac, WiFi.macAddress().c_str());
    tftMsg (verbose, 0, _FX("MAC addr: %s"), mac);

    // wait for connection
    resetWatchdog();
    if (myssid)
        tftMsg (verbose, 0, "\r");                      // init overwrite
    do {
	if (myssid)
            tftMsg (verbose, 0, _FX("Connecting to %s %.*s\r"), myssid, ndots, dots);
	Serial.printf (_FX("Trying network %d\n"), ndots);
	if (millis() - t0 > timeout || ndots == (sizeof(dots)-1)) {
            tftMsg (verbose, 1000, _FX("WiFi failed -- check credentials?"));
	    break;
	}

        wdDelay(1000);
        ndots++;

        // WiFi.printDiag(Serial);

    } while (strcmp (mac, mac_lh) && (WiFi.status() != WL_CONNECTED));

    // init retry times
    initWiFiRetry();

    // report stats
    resetWatchdog();
    if (WiFi.status() == WL_CONNECTED) {
	IPAddress ip;
	ip = WiFi.localIP();
	tftMsg (verbose, 0, _FX("IP: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.subnetMask();
	tftMsg (verbose, 0, _FX("Mask: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.gatewayIP();
	tftMsg (verbose, 0, _FX("GW: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
	ip = WiFi.dnsIP();
	tftMsg (verbose, 0, _FX("DNS: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
	tftMsg (verbose, 0, _FX("Hostname: %s"), WiFi.hostname().c_str());
	if (WiFi.RSSI() < 10) {
	    tftMsg (verbose, 0, _FX("Signal strength: %d dBm"), WiFi.RSSI());
	    tftMsg (verbose, 0, _FX("Channel: %d"), WiFi.channel());
	}
	tftMsg (verbose, 0, _FX("S/N: %u"), ESP.getChipId());
    }

    // start web server for remote commands
    if (WiFi.status() == WL_CONNECTED || !strcmp (mac, mac_lh)) {
        tftMsg (verbose, 0, _FX("Start web server"));
        initWebServer();
    } else {
        tftMsg (verbose, 0, _FX("No web server"));
    }
}

/* init wifi and maps and maybe time and location, reporting on initial startup screen with tftMsg
 */
void initSys()
{
    // start/check WLAN
    initWiFi(true);

    // init location if desired
    if (useGeoIP()) {
        if (WiFi.status() == WL_CONNECTED)
            geolocateIP ();
        else
            tftMsg (true, 0, _FX("no network for geo IP"));
    } else if (useGPSD()) {
        LatLong lltmp;
        if (getGPSDLatLong(&lltmp)) {
            // good -- set de_ll
            de_ll = lltmp;
            normalizeLL (de_ll);
            setMaidenhead(NV_DE_GRID, de_ll);
            NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
            NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
            tftMsg (true, 0, _FX("GPSD: lat %g long %g"), de_ll.lat_d, de_ll.lng_d);
        } else
            tftMsg (true, 1000, _FX("GPSD: no Lat/Long"));
    }


    // init time service as desired
    if (useGPSD()) {
        if (getGPSDUTC()) {
            tftMsg (true, 0, _FX("GPSD: time ok"));
            initTime();
        } else
            tftMsg (true, 1000, _FX("GPSD: no time"));

    } else if (WiFi.status() == WL_CONNECTED) {

        if (useLocalNTPHost()) {

            // test user choice
            const char *local_ntp = getLocalNTPHost();
            tftMsg (true, 0, _FX("NTP test %s ...\r"), local_ntp);
            if (getNTPUTC())
                tftMsg (true, 0, _FX("NTP %s: ok\r"), local_ntp);
            else
                tftMsg (true, 0, _FX("NTP %s: fail\r"), local_ntp);
        } else {

            // try all the NTP servers to find the fastest (with sneaky way out)
            SCoord s;
            drainTouch();
            tftMsg (true, 0, _FX("Finding best NTP ...\r"));
            NTPList *best_ntp = NULL;
            for (unsigned i = 0; i < NNTP; i++) {
                NTPList *np = &ntp_list[i];

                // measure the next. N.B. assumes we stay in sync
                if (getNTPUTC() == 0)
                    tftMsg (true, 0, _FX("%s: err\r"), np->server);
                else {
                    tftMsg (true, 0, _FX("%s: %d ms\r"), np->server, np->rsp_time);
                    if (!best_ntp || np->rsp_time < best_ntp->rsp_time)
                        best_ntp = np;
                }

                // abort scan if tapped and found at least one good
                if (best_ntp && readCalTouch(s) != TT_NONE) {
                    Serial.println (F("NTP skip"));
                    break;
                }
            }
            if (best_ntp)
                tftMsg (true, 0, _FX("Best NTP: %s %d ms\r"), best_ntp->server, best_ntp->rsp_time);
            else
                tftMsg (true, 0, _FX("No NTP\r"));
            drainTouch();
        }
        tftMsg (true, 0, NULL);   // next row

        // go
        initTime();

    } else {

        tftMsg (true, 0, _FX("No time"));
    }


    // check desired map files are installed and ready to go
    if (!installBackgroundMap (true, getCoreMapStyle()))
        tftMsg (true, 0, _FX("No map"));


    // offer time to peruse
    static const SBox skip_b = {730,10,55,35};      // skip box, nice if same as sat ok
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drawStringInBox (_FX("Skip"), skip_b, false, RA8875_WHITE);
    #define	TO_DS 100                               // timeout delay, decaseconds
    uint8_t s_left = TO_DS/10;                      // seconds remaining
    uint32_t t0 = millis();
    drainTouch();
    for (uint8_t ds_left = TO_DS; ds_left > 0; --ds_left) {
        SCoord s;
        if (readCalTouch(s) != TT_NONE && inBox(s, skip_b)) {
            drawStringInBox (_FX("Skip"), skip_b, true, RA8875_WHITE);
            break;
        }
        if ((TO_DS - (millis() - t0)/100)/10 < s_left) {
            // just printing every ds_left/10 is too slow due to overhead
            char buf[30];
            sprintf (buf, _FX("Ready ... %d\r"), s_left--);
            tftMsg (true, 0, buf);
        }
        wdDelay(100);
    }


    // handy place to init bc_power
    if (bc_power == 0 && !NVReadUInt16 (NV_BCPOWER, &bc_power))
        bc_power = 100;
}

/* return which box is currently showing the band conditions, if any
 */
static SBox *findBCBox()
{
    if (plot1_ch == PLOT1_BC)
        return (&plot1_b);
    if (plot2_ch == PLOT2_BC)
        return (&plot2_b);
    if (plot3_ch == PLOT3_BC)
        return (&plot3_b);
    return (NULL);
}

/* check whether an update to BandConditions pane in box b is needed or requested.
 */
static void checkBandConditions (const SBox &b, bool force)
{
    // update if asked to or out of sync with map or time to refresh
    bool update_bc = force || (prop_map != PROP_MAP_OFF && bc_hour != map_hour) || (millis() > next_bc);
    if (!update_bc)
        return;

    bool ok = updateBandConditions(b);
    if (ok) {
        // no error visible
        bc_error = false;
        // reschedule later
        next_bc = millis() + BC_INTERVAL;
        bc_hour = hour(nowWO());
    } else {
        // note error is in view
        bc_error = true;
        // retry
        next_bc = millis() + WIFI_RETRY;
    }

    printFreeHeap (F("checkBandConditions"));
}

/* check whether an update to the VOACAP map is needed or requested.
 */
static void checkVOACAPMap (bool force)
{
    // update if asked to, or map is propagation and time to refresh our out of sync with BC pane.
    SBox *bc_box = findBCBox();
    bool update_map = force ||
            (prop_map != PROP_MAP_OFF && (millis() > next_map || (bc_box && map_hour != bc_hour)));
    if (!update_map)
        return;

    // show pending unless prior BC error or no BC box
    if (!bc_error && bc_box)
        BCHelper (bc_box, 1, NULL, NULL);

    // update prop map if on
    bool ok = true;
    if (prop_map != PROP_MAP_OFF) {
        ok = installPropMaps (propMap2MHz (prop_map));
        if (ok) {
            // ok: reschedule later
            next_map = millis() + VOACAP_INTERVAL;      // fresh time after long update
            map_hour = hour(nowWO());
        } else {
            // err: retry
            next_map = millis() + WIFI_RETRY;
        }
    }

    // reinstall standard map if trouble or it was turned off.
    if (!ok || prop_map == PROP_MAP_OFF) {
        if (!installBackgroundMap (false, getCoreMapStyle()))
            ok = false;
    }

    // regardless we need to redraw the map unless holding for path
    if (!waiting4DXPath())
        initEarthMap();

    // show result of effort unless prior BC error or no BC box
    if (!bc_error && bc_box)
        BCHelper (bc_box, ok ? 0 : -1, NULL, NULL);

    // above can take a while, so drain any taps that happened to avoid backing up even more
    drainTouch();

    printFreeHeap (F("checkVOACAPMap"));
}


/* check for tap at s known to be within BandConditions box b.
 * tapping a band loads prop map, tapping power cycles bc_power 1-1000 W.
 * return whether tap was really useful for us.
 * N.B. coordinate tap positions with BChelper(), need not be perfect
 */
static bool checkBCTouch (const SCoord &s, const SBox &b)
{
    // done if tap title
    if (s.y < b.y+b.h/5)
        return (false);

    // check ll corner for power cycle
    if (s.y > b.y+9*b.h/10 && s.x < b.x + b.w/3) {

        // rotate power through 1..1000
        switch (bc_power) {
        case 1:   bc_power = 10;   break;
        case 10:  bc_power = 100;  break;
        case 100: bc_power = 1000; break;
        default:  bc_power = 1;    break;
        }

        // update band conditions and map for sure with new power level
        checkBandConditions (b, true);
        checkVOACAPMap (true);

        // persist
        NVWriteUInt16 (NV_BCPOWER, bc_power);

        // ours
        return (true);
    }


    // toggle band depending on position, if any.
    // N.B. we assume plot areas for title and power have already been checked
    PropMapSetting new_prop_map = prop_map;
    uint16_t ty = b.y + 27;
    uint16_t cy = b.y+b.h-10;
    uint16_t col1_x = b.x + 10;
    uint16_t col2_x = b.x + 5*b.w/9;
    uint16_t row_h = (cy-2-ty)/(PROP_MAP_N/2);
    for (int i = 0; i < PROP_MAP_N; i++) {
        SBox tb;
        tb.x = (i < PROP_MAP_N/2) ? col1_x : col2_x;
        tb.y = ty + (i%(PROP_MAP_N/2))*row_h;
        tb.w = b.w/4;
        tb.h = row_h;
        if (inBox (s, tb)) {
            if (i == prop_map) {
                // toggle current setting
                new_prop_map = prop_map == PROP_MAP_OFF ? ((PropMapSetting)i) : PROP_MAP_OFF;
            } else {
                // set different
                new_prop_map = (PropMapSetting)i;
            }
            break;
        }
    }

    // update map if state change
    if (new_prop_map != prop_map) {
        prop_map = new_prop_map;
        checkVOACAPMap (true);
    }

    // nothing but still ours
    return (true);
}

/* arrange to resume plot1 after dt millis
 */
void revertPlot1 (uint32_t dt)
{
    uint32_t new_t = millis() + dt;

    switch (plot1_ch) {
    case PLOT1_SSN:
	next_ssn = new_t;
        break;
    case PLOT1_XRAY:
	next_xray = new_t;
        break;
    case PLOT1_FLUX:
	next_flux = new_t;
        break;
    case PLOT1_KP:
	next_kp = new_t;
        break;
    case PLOT1_BC:
        next_bc = new_t;
        bc_reverting = true;
        break;
    case PLOT1_DEWX:
	next_dewx = new_t;
        break;
    case PLOT1_N:               // lint
        break;
    }
}

/* check if it is time to update any info via wifi.
 * probably should be named updatePlots()
 */
void updateWiFi(void)
{
    resetWatchdog();

    // time now
    uint32_t t0 = millis();

    // proceed even if no wifi to allow subsystems to update

    // freshen plot1 contents
    switch (plot1_ch) {
    case PLOT1_SSN:
        if (t0 >= next_ssn) {
	    if (updateSunSpots())
		next_ssn = millis() + SSPOT_INTERVAL;
	    else
		next_ssn = millis() + WIFI_RETRY;
	}
        break;

    case PLOT1_XRAY:
        if (t0 >= next_xray) {
	    if (updateXRay(plot1_b))
		next_xray = millis() + XRAY_INTERVAL;
	    else
		next_xray = millis() + WIFI_RETRY;
	}
        break;

    case PLOT1_FLUX:
        if (t0 >= next_flux) {
	    if (updateSolarFlux(plot1_b))
		next_flux = millis() + FLUX_INTERVAL;
	    else
		next_flux = millis() + WIFI_RETRY;
	}
        break;

    case PLOT1_KP:
	if (t0 >= next_kp) {
	    if (updateKp(plot1_b))
		next_kp = millis() + KP_INTERVAL;
	    else
		next_kp = millis() + WIFI_RETRY;
	}
        break;

    case PLOT1_BC:
        checkBandConditions (plot1_b, false);
        break;

    case PLOT1_DEWX:
	if (t0 >= next_dewx) {
	    if (updateDEWX(plot1_b))
		next_dewx = millis() + DEWX_INTERVAL;
	    else
		next_dewx = millis() + WIFI_RETRY;
	}
        break;

    case PLOT1_N:               // lint
        break;
    }

    // freshen plot2 contents
    switch (plot2_ch) {
    case PLOT2_XRAY:
	if (t0 >= next_xray) {
	    if (updateXRay(plot2_b))
		next_xray = millis() + XRAY_INTERVAL;
	    else
		next_xray = millis() + WIFI_RETRY;
	}
        break;

    case PLOT2_FLUX:
	if (t0 >= next_flux) {
	    if (updateSolarFlux(plot2_b))
		next_flux = millis() + FLUX_INTERVAL;
	    else
		next_flux = millis() + WIFI_RETRY;
	}
        break;

    case PLOT2_KP:
	if (t0 >= next_kp) {
	    if (updateKp(plot2_b))
		next_kp = millis() + KP_INTERVAL;
	    else
		next_kp = millis() + WIFI_RETRY;
	}
        break;

    case PLOT2_BC:
        checkBandConditions (plot2_b, false);
        break;

    case PLOT2_DX:
        updateDXCluster();
        break;

    case PLOT2_N:               // lint
        break;
    }

    // freshen plot3 contents
    switch (plot3_ch) {
    case PLOT3_TEMP:     // fallthru
    case PLOT3_PRESSURE: // fallthru
    case PLOT3_HUMIDITY: // fallthru
    case PLOT3_DEWPOINT: // fallthru
        // always updated in main loop()
        break;

    case PLOT3_SDO_1:    // fallthru
    case PLOT3_SDO_2:    // fallthru
    case PLOT3_SDO_3:
	if (t0 >= next_sdo) {
	    if (updateSDO())
		next_sdo = millis() + SDO_INTERVAL;
	    else
		next_sdo = millis() + WIFI_RETRY;
	}
        break;

    case PLOT3_GIMBAL:
        updateGimbal();
        break;

    case PLOT3_KP:
	if (t0 >= next_kp) {
	    if (updateKp(plot3_b))
		next_kp = millis() + KP_INTERVAL;
	    else
		next_kp = millis() + WIFI_RETRY;
	}
        break;

    case PLOT3_BC:
        checkBandConditions (plot3_b, false);
        break;

    case PLOT3_NOAASWX:
	if (t0 >= next_noaaswx) {
	    if (updateNOAASWx(plot3_b))
		next_noaaswx = millis() + NOAASWX_INTERVAL;
	    else
		next_noaaswx = millis() + WIFI_RETRY;
	}
        break;

    case PLOT3_DXWX:
	if (t0 >= next_dxwx) {
	    if (updateDXWX(plot3_b))
		next_dxwx = millis() + DXWX_INTERVAL;
	    else
		next_dxwx = millis() + WIFI_RETRY;
	}
        break;

    default:
        break;
    }

    // check if time for VOACAP map update
    checkVOACAPMap (false);

    // freshen RSS
    if (t0 >= next_rss) {
	if (updateRSS())
	    next_rss = millis() + RSS_INTERVAL;
	else
	    next_rss = millis() + WIFI_RETRY;
    }

    // check for server commands
    checkWebServer();
}

/* try to set plot1_ch to the given choice, if ok schedule for immediate refresh.
 * return whether successful
 */
bool setPlot1 (PLOT1_Choices p1)
{
    switch (p1) {

    case PLOT1_SSN:
        // no constraints
        plot1_ch = PLOT1_SSN;
        next_ssn = 0;
        break;

    case PLOT1_XRAY:
        // insure not already on plot2
        if (plot2_ch == PLOT2_XRAY)
            return (false);
        plot1_ch = PLOT1_XRAY;
        next_xray = 0;
        break;

    case PLOT1_FLUX:
        // insure not already on plot2
        if (plot2_ch == PLOT2_FLUX)
            return (false);
        plot1_ch = PLOT1_FLUX;
        next_flux = 0;
        break;

    case PLOT1_KP:
        // insure not already elsewhere
        if (plot2_ch == PLOT2_KP || plot3_ch == PLOT3_KP)
            return (false);
        plot1_ch = PLOT1_KP;
        next_kp = 0;
        break;

    case PLOT1_BC:
        // insure not already elsewhere
        if (plot2_ch == PLOT2_BC || plot3_ch == PLOT3_BC)
            return (false);
        plot1_ch = PLOT1_BC;
        next_bc = 0;	                        // schedule new fetch in next updateWiFi()
        break;

    case PLOT1_DEWX:
        // no constraints
        plot1_ch = PLOT1_DEWX;
        next_dewx = 0;
        break;

    case PLOT1_N:
        return (false);

    // don't have a default so unhandled cases get caught at compile file

    }

    // persist
    NVWriteUInt8 (NV_PLOT_1, plot1_ch);

    // ok!
    return (true);
}


/* handle plot1 touch, return whether ours.
 * even if ours, do nothing if some conflict prevents changing pane assignment.
 */
bool checkPlot1Touch (const SCoord &s)
{
    if (!inBox (s, plot1_b))
	return (false);

    // rotate to next pane
    switch (plot1_ch) {

    default:
        // come here in case plot1_b is something unexpected we can try to make it better defined

        // FALLTHRU

    case PLOT1_SSN:
        if (setPlot1 (PLOT1_XRAY))
            break;

        // FALLTHRU

    case PLOT1_XRAY:
        if (setPlot1 (PLOT1_FLUX))
            break;

        // FALLTHRU

    case PLOT1_FLUX:
        if (setPlot1 (PLOT1_KP))
            break;

        // FALLTHRU

    case PLOT1_KP:
        if (setPlot1 (PLOT1_BC))
            break;

        // FALLTHRU

    case PLOT1_BC:
        if (plot1_ch == PLOT1_BC && checkBCTouch (s, plot1_b)) {   // N.B. xtra check becuz fallthru
            // stay with BC
            break;
        }
        if (setPlot1 (PLOT1_DEWX))
            break;

        // FALLTHRU

    case PLOT1_DEWX:
        if (setPlot1 (PLOT1_SSN))
            break;
    }

    // did our best
    return (true);
}

/* try to set plot2_ch to the given choice, if ok schedule for immediate refresh.
 * return whether successful
 */
bool setPlot2 (PLOT2_Choices p2)
{
    switch (p2) {

    case PLOT2_XRAY:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_XRAY)
            return (false);
        plot2_ch = PLOT2_XRAY;
        next_xray = 0;
        break;

    case PLOT2_FLUX:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_FLUX)
            return (false);
        plot2_ch = PLOT2_FLUX;
        next_flux = 0;
        break;

    case PLOT2_KP:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_KP || plot3_ch == PLOT3_KP)
            return (false);
        plot2_ch = PLOT2_KP;
        next_kp = 0;
        break;

    case PLOT2_BC:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_BC || plot3_ch == PLOT3_BC)
            return (false);
        plot2_ch = PLOT2_BC;
        next_bc = 0;                            // schedule new fetch in next updateWiFi()
        break;

    case PLOT2_DX:
        // insure enabled
        if (!useDXCluster())
            return (false);
        plot2_ch = PLOT2_DX;
        initDXCluster();
        break;

    case PLOT2_N:
        return (false);

    // don't have a default so unhandled cases get caught at compile file

    }

    // insure DX off if not displayed
    if (plot2_ch != PLOT2_DX)
        closeDXCluster();

    // persist
    NVWriteUInt8 (NV_PLOT_2, plot2_ch);

    // ok!
    return (true);
}

/* handle plot2 touch, return whether ours.
 * even if ours, do nothing if some conflict prevents changing pane assignment.
 */
bool checkPlot2Touch (const SCoord &s)
{
    if (!inBox (s, plot2_b))
        return (false);

    // rotate to next pane
    switch (plot2_ch) {

    default:
        // come here in case plot2_b is something unexpected we can try to make it better defined

        // FALLTHRU

    case PLOT2_XRAY:
        if (setPlot2 (PLOT2_FLUX))
            break;

        // FALLTHRU

    case PLOT2_FLUX:
        if (setPlot2 (PLOT2_KP))
            break;

        // FALLTHRU

    case PLOT2_KP:
        if (setPlot2 (PLOT2_BC))
            break;

        // FALLTHRU

    case PLOT2_BC:
        if (plot2_ch == PLOT2_BC && checkBCTouch (s, plot2_b)) {       // N.B. xtra check becuz fallthru
            // stay with BC
            break;
        }
        if (setPlot2 (PLOT2_DX))
            break;

        // FALLTHRU

    case PLOT2_DX:
        // first check if user has tapped a dx, else roll to next available pane
        if (plot2_ch == PLOT2_DX) {             // N.B. xtra check becuz fallthru
            if (checkDXTouch(s)) 
                break;
            closeDXCluster();
        }

        // one of these must work because plot1_ch can't be both
        if (!setPlot2 (PLOT2_XRAY))
            (void) setPlot2 (PLOT2_FLUX);
        break;
    }

    // did our best
    return (true);
}

/* try to set plot3_ch to the given choice, if ok schedule for immediate refresh.
 * return whether successful
 */
bool setPlot3 (PLOT3_Choices p3)
{
    switch (p3) {

    case PLOT3_TEMP:                            // fallthru
    case PLOT3_PRESSURE:                        // fallthru
    case PLOT3_HUMIDITY:                        // fallthru
    case PLOT3_DEWPOINT:
        if (!bme280_connected)
            return (false);
        plot3_ch = p3;
        initBME280Retry();                      // prep fresh update from next updateBME280()
        break;

    case PLOT3_SDO_1:                           // fallthru
    case PLOT3_SDO_2:                           // fallthru
    case PLOT3_SDO_3:
        // no constraints
        plot3_ch = p3;
        next_sdo = 0;
        break;

    case PLOT3_GIMBAL:
        if (!haveGimbal())
            return (false);
        plot3_ch = PLOT3_GIMBAL;
        initGimbalGUI();
        break;

    case PLOT3_KP:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_KP || plot2_ch == PLOT2_KP)
            return (false);
        plot3_ch = PLOT3_KP;
        next_kp = 0;
        break;

    case PLOT3_BC:
        // insure not already elsewhere
        if (plot1_ch == PLOT1_BC || plot2_ch == PLOT2_BC)
            return (false);
        plot3_ch = PLOT3_BC;
        next_bc = 0;
        break;

    case PLOT3_NOAASWX:
        // no constraints
        plot3_ch = PLOT3_NOAASWX;
        next_noaaswx = 0;
        break;

    case PLOT3_DXWX:
        // no constraints
        plot3_ch = PLOT3_DXWX;
        next_dxwx = 0;
        break;

    case PLOT3_N:
        return (false);

    // don't have a default so unhandled cases get caught at compile file

    }

    // persist
    NVWriteUInt8 (NV_PLOT_3, plot3_ch);

    // ok!
    return (true);
}

/* handle plot3 touch, return whether ours.
 */
bool checkPlot3Touch (const SCoord &s)
{
    // check whether in box at all
    if (!inBox (s, plot3_b))
	return (false);

    // top tap changes major category (env, SDO, Gimbal, KP, VOACAP), lower changes minor if appropriate.
    SBox top_b = plot3_b;
    top_b.h /= 2;
    bool tap_top = inBox (s, top_b);
    bool tap_bottom = !tap_top;

    // rotate to next pane
    switch (plot3_ch) {

    default:
        // come here in case plot3_b is something unexpected we can try to make it better defined

        // FALLTHRU

    case PLOT3_TEMP:
        if (tap_top || !setPlot3 (PLOT3_PRESSURE))
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
        break;

    case PLOT3_PRESSURE:
        if (tap_top || !setPlot3 (PLOT3_HUMIDITY))
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
        break;

    case PLOT3_HUMIDITY:
        if (tap_top || !setPlot3 (PLOT3_DEWPOINT))
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
        break;

    case PLOT3_DEWPOINT:
        if (tap_top || !setPlot3 (PLOT3_TEMP))
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
        break;

    case PLOT3_SDO_1:
        if (tap_bottom) {
            (void) setPlot3 (PLOT3_SDO_2);              // can't fail
            break;
        }

        // FALLTHRU

    case PLOT3_SDO_2:
        if (tap_bottom) {
            (void) setPlot3 (PLOT3_SDO_3);              // can't fail
            break;
        }

        // FALLTHRU

    case PLOT3_SDO_3:
        if (tap_bottom) {
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
            break;
        }

        if (setPlot3 (PLOT3_GIMBAL))
            break;

        // FALLTHRU

    case PLOT3_GIMBAL:
        if (plot3_ch == PLOT3_GIMBAL && checkGimbalTouch(s)) {     // N.B. xtra check becuz fallthru
            // tap was for gimbal so no change
            return (true);
        }
        if (setPlot3 (PLOT3_KP))
            break;

        // FALLTHRU

    case PLOT3_KP:
        if (setPlot3 (PLOT3_BC))
            break;

        // FALLTHRU

    case PLOT3_BC:
        if (plot3_ch == PLOT3_BC && checkBCTouch (s, plot3_b)) {   // N.B. xtra check becuz fallthru
            // stay with BC
            break;
        }
        if (!setPlot3 (PLOT3_NOAASWX)) {
            (void) setPlot3 (PLOT3_SDO_2);              // can't fail
        }
        break;

    case PLOT3_NOAASWX:
        if (setPlot3 (PLOT3_DXWX))
            break;

    case PLOT3_DXWX:
        if (!setPlot3 (PLOT3_TEMP))
            (void) setPlot3 (PLOT3_SDO_1);              // can't fail
        break;
    }

    // ours
    return (true);
}

/* NTP time server query.
 * returns UNIX time, or 0 if trouble.
 * for good NTP packet description try
 *   http://www.cisco.com
 *	/c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
 */
time_t getNTPUTC(void)
{
    // NTP contents packet
    static const uint8_t timeReqA[] = { 0xE3, 0x00, 0x06, 0xEC };
    static const uint8_t timeReqB[] = { 0x31, 0x4E, 0x31, 0x34 };

    // need wifi
    if (!wifiOk())
	return (0);

    // need udp
    WiFiUDP ntp_udp;
    resetWatchdog();
    if (!ntp_udp.begin(1234)) {					// any local port
	Serial.println (F("NTP: UDP startup failed"));
	return (0);
    }

    // decide on server: user's else fastest 
    NTPList *ntp_use = &ntp_list[0];                            // a place for rsp_time if useLocal
    const char *ntp_server;
    if (useLocalNTPHost()) {
        ntp_server = getLocalNTPHost();
    } else {
        ntp_use = findBestNTP();
        ntp_server = ntp_use->server;
    }

    // NTP buffer and timers
    uint8_t  buf[48];
    uint32_t tx_ms, rx_ms;

    // Assemble request packet
    memset(buf, 0, sizeof(buf));
    memcpy(buf, timeReqA, sizeof(timeReqA));
    memcpy(&buf[12], timeReqB, sizeof(timeReqB));

    // send
    Serial.printf(_FX("NTP: Issuing request to %s\n"), ntp_server);
    resetWatchdog();
    ntp_udp.beginPacket (ntp_server, 123);			// NTP uses port 123
    ntp_udp.write(buf, sizeof(buf));
    tx_ms = millis();						// record when packet sent
    if (!ntp_udp.endPacket()) {
	Serial.println (F("NTP: UDP write failed"));
	ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
	ntp_udp.stop();
	return (0UL);
    }
    // Serial.print (F("NTP: Sent 48 ... "));
    resetWatchdog();

    // receive response
    // Serial.print(F("NTP: Awaiting response ... "));
    memset(buf, 0, sizeof(buf));
    uint32_t t0 = millis();
    while (!ntp_udp.parsePacket()) {
	if (millis() - t0 > 3000U) {
	    Serial.println(F("NTP: UDP timed out"));
	    ntp_use->rsp_time = NTP_TOO_LONG;                   // force different choice next time
	    ntp_udp.stop();
	    return (0UL);
	}
	resetWatchdog();
	wdDelay(10);
    }
    rx_ms = millis();						// record when packet arrived
    resetWatchdog();

    // record response time
    ntp_use->rsp_time = rx_ms - tx_ms;
    Serial.printf (_FX("NTP: %s replied after %d ms\n"), ntp_server, ntp_use->rsp_time);

    // read response
    if (ntp_udp.read (buf, sizeof(buf)) != sizeof(buf)) {
	Serial.println (F("NTP: UDP read failed"));
	ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
	ntp_udp.stop();
	return (0UL);
    }
    // IPAddress from = ntp_udp.remoteIP();
    // Serial.printf (_FX("NTP: received 48 from %d.%d.%d.%d\n"), from[0], from[1], from[2], from[3]);

    // only accept server responses which are mode 4
    uint8_t mode = buf[0] & 0x7;
    if (mode != 4) {						// insure server packet
	Serial.print (F("NTP: RX mode should be 4 but it is ")); Serial.println (mode);
	ntp_udp.stop();
	return (0UL);
    }

    // crack and advance to next whole second
    time_t unix_s = crackBE32 (&buf[40]) - 2208988800UL;	// packet transmit time - (1970 - 1900)
    if ((uint32_t)unix_s > 0x7FFFFFFFUL) { 			// sanity check beyond unsigned value
	Serial.print (F("NTP: crazy large UNIX time: ")); Serial.println ((uint32_t)unix_s);
	ntp_udp.stop();
	return (0UL);
    }
    uint32_t fraction_more = crackBE32 (&buf[44]);		// x / 10^32 additional second
    uint16_t ms_more = 1000UL*(fraction_more>>22)/1024UL;	// 10 MSB to ms
    uint16_t transit_time = (rx_ms - tx_ms)/2;			// transit = half the round-trip time
    if (transit_time < 1) {					// don't trust unless finite
	Serial.println (F("NTP: too fast"));
	ntp_udp.stop();
	return (0UL);
    }
    ms_more += transit_time;					// with transit now = unix_s + ms_more
    uint16_t sec_more = ms_more/1000U+1U;			// whole seconds behind rounded up
    wdDelay (sec_more*1000U - ms_more);				// wait to next whole second
    unix_s += sec_more;						// account for delay
    // Serial.print (F("NTP: Fraction ")); Serial.print(ms_more);
    // Serial.print (F(", transit ")); Serial.print(transit_time);
    // Serial.print (F(", seconds ")); Serial.print(sec_more);
    // Serial.print (F(", UNIX ")); Serial.print (unix_s); Serial.println();
    resetWatchdog();

    Serial.println (F("NTP: sync ok"));
    ntp_udp.stop();
    printFreeHeap (F("NTP"));
    return (unix_s);
}

/* read next char from client.
 * return whether another character was in fact available.
 */
bool getChar (WiFiClient &client, char *cp)
{
    #define GET_TO 5000	// millis()

    resetWatchdog();

    // wait for char
    uint32_t t0 = millis();
    while (!client.available()) {
	resetWatchdog();
	if (!client.connected()) {
            Serial.print (F("surprise getChar disconnect\n"));
	    return (false);
        }
	if (millis() - t0 > GET_TO) {
            Serial.print (F("surprise getChar timeout\n"));
	    return (false);
        }
	wdDelay(10);
    }

    // read, which has another way to indicate failure
    int c = client.read();
    if (c < 0) {
	Serial.print (F("bad getChar read\n"));
	return (false);
    }

    // got one
    *cp = (char)c;
    return (true);
}

/* send User-Agent to client
 */
void sendUserAgent (WiFiClient &client)
{
    StackMalloc agent_mem(200);
    char *ua = agent_mem.getMem();
    size_t ual = agent_mem.getSize();

    if (logUsageOk()) {

        #if defined(_CLOCK_1600x960)
            int build_size = 1600;
        #elif defined(_CLOCK_2400x1440)
            int build_size = 2400;
        #elif defined(_CLOCK_3200x1920)
            int build_size = 3200;
        #else
            int build_size = 800;
        #endif

        #if defined(_USE_FB0)
            int use_fb0 = 1;
        #else
            int use_fb0 = 0;
        #endif

        snprintf (ua, ual,
            _FX("User-Agent: %s/%s (id %u up %ld) crc %d LV3 %s %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f %.2f %d %d %d %d\r\n"),
            agent, HC_VERSION, ESP.getChipId(), getUptime(NULL,NULL,NULL,NULL), flash_crc_ok,
            getMapStyle(), azm_on, llg_on, plot1_ch, plot2_ch, plot3_ch, de_time_fmt, brb_mode,
            dx_info_for_sat, rss_on, useMetricUnits(), bme280_connected, getKX3Baud(), found_phot,
            getBMETempCorr(), getBMEPresCorr(), desrss, dxsrss, build_size, use_fb0);
    } else {
        snprintf (ua, ual, _FX("User-Agent: %s/%s (id %u up %ld) crc %d\r\n"),
            agent, HC_VERSION, ESP.getChipId(), getUptime(NULL,NULL,NULL,NULL), flash_crc_ok);
    }

    // send
    client.print(ua);
}

/* issue an HTTP Get
 */
void httpGET (WiFiClient &client, const char *server, const char *page)
{
    resetWatchdog();

    FWIFIPR (client, F("GET ")); client.print(page); FWIFIPRLN (client, F(" HTTP/1.0"));
    FWIFIPR (client, F("Host: ")); client.println (server);
    sendUserAgent (client);
    FWIFIPRLN (client, F("Connection: close\r\n"));

    resetWatchdog();
}

/* given a standard 3-char abbreviation for month, set *monp to 1-12 and return true, else false
 * if nothing matches
 */
static bool crackMonth (const char *name, int *monp)
{
    static const char months[12][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (int i = 0; i < 12; i++) {
        if (strcmp (name, months[i]) == 0) {
            *monp = i + 1;      // want 1-based month
            return (true);
        }
    }

    return (false);
}

/* skip the given wifi client stream ahead to just after the first blank line, return whether ok.
 * this is often used so subsequent stop() on client doesn't slam door in client's face with RST.
 * Along the way, if lastmodp != NULL look for Last-Modified and set as a UNIX time, or 0 if not found.
 */
bool httpSkipHeader (WiFiClient &client, uint32_t *lastmodp)
{
    StackMalloc line_mem(150);
    char *line = line_mem.getMem();

    // assume no Last-Modified until found
    if (lastmodp)
        *lastmodp = 0;

    do {
	if (!getTCPLine (client, line, line_mem.getSize(), NULL))
	    return (false);
	// Serial.println (line);
        
        // look for last-mod of the form: Last-Modified: Tue, 29 Sep 2020 22:55:02 GMT
        if (lastmodp) {
            char mstr[10];
            int dy, mo, yr, hr, mn, sc;
            if (sscanf (line, _FX("Last-Modified: %*[^,], %d %3s %d %d:%d:%d"), &dy, mstr, &yr, &hr, &mn, &sc)
                                                == 6 && crackMonth (mstr, &mo)) {
                tmElements_t tm;
                tm.Year = yr - 1970;
                tm.Month = mo;
                tm.Day = dy;
                tm.Hour = hr;
                tm.Minute = mn;
                tm.Second = sc;
                *lastmodp = makeTime (tm);
            }
        }

    } while (line[0] != '\0');  // getTCPLine absorbs \r\n so this tests for a blank line

    return (true);
}

/* same but when don't care about lastmod time
 */
bool httpSkipHeader (WiFiClient &client)
{
    return (httpSkipHeader (client, NULL));
}

/* retrieve and plot latest and predicted kp indices, return whether all ok
 */
static bool updateKp(SBox &b)
{
    // data are provided every 3 hours == 8/day. collect 7 days of history + 2 days of predictions
    #define	NHKP		(8*7)			// N historical Kp values
    #define	NPKP		(8*2)			// N predicted Kp values
    #define	NKP		(NHKP+NPKP)		// N total Kp values
    uint8_t kp[NKP];					// kp collection
    uint8_t kp_i = 0;					// next kp index to use
    char line[100];					// text line
    WiFiClient kp_client;				// wifi client connection
    bool ok = false;					// set iff all ok

    plotMessage (b, KP_COLOR, _FX("Reading kpmag data..."));

    Serial.println(kp_page);
    resetWatchdog();
    if (wifiOk() && kp_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (kp_client, svr_host, kp_page);

	// skip response header
	if (!httpSkipHeader (kp_client)) {
            Serial.println (F("Kp header short"));
	    goto out;
        }

	// read lines into kp array
	// Serial.println(F("reading k indices"));
	for (kp_i = 0; kp_i < NKP && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
	    // Serial.print(kp_i); Serial.print(_FX("\t")); Serial.println(line);
	    kp[kp_i] = atof(line);
	}

    } else {
	Serial.println (F("connection failed"));
    }

    // require all
    ok = (kp_i == NKP);
    if (!ok)
        Serial.printf (_FX("Kp only %d of %d\n"), kp_i, NKP);

out:

    // plot
    if (ok)
	plotKp (b, kp, NHKP, NPKP, KP_COLOR);
    else
	plotMessage (b, KP_COLOR, _FX("No Kp data"));

    // clean up
    kp_client.stop();
    resetWatchdog();
    printFreeHeap (F("Kp"));
    return (ok);
}

/* given a GOES XRAY Flux value, return its event level designation in buf.
 */
static char *xrayLevel (float flux, char *buf)
{
    if (flux < 1e-8)
	strcpy (buf, _FX("A0.0"));
    else {
	static const char levels[] = "ABCMX";
	int power = floorf(log10f(flux));
	if (power > -4)
	    power = -4;
	float mantissa = flux*powf(10.0F,-power);
	char alevel = levels[8+power];
	sprintf (buf, _FX("%c%.1f"), alevel, mantissa);
    }
    return (buf);
}

// retrieve and plot latest xray indices, return whether all ok
static bool updateXRay(const SBox &box)
{
    #define NXRAY 150			                // n lines to collect = 25 hours @ 10 mins per line
    StackMalloc lxray_mem(NXRAY*sizeof(float));
    StackMalloc sxray_mem(NXRAY*sizeof(float));
    StackMalloc x_mem(NXRAY*sizeof(float));
    float *lxray = (float *) lxray_mem.getMem();	// long wavelength values
    float *sxray = (float *) sxray_mem.getMem();	// short wavelength values
    float *x = (float *) x_mem.getMem();                // x coords of plot
    uint8_t xray_i;			                // next index to use
    char line[100];
    uint16_t ll;
    bool ok = false;
    WiFiClient xray_client;

    plotMessage (box, XRAY_LCOLOR, _FX("Reading XRay data..."));

    Serial.println(xray_page);
    resetWatchdog();
    if (wifiOk() && xray_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// query web page
	httpGET (xray_client, svr_host, xray_page);

        // soak up remaining header
	if (!httpSkipHeader (xray_client)) {
            Serial.println (F("XRay header short"));
            goto out;
        }

	// collect content lines and extract both wavelength intensities
	xray_i = 0;
	float current_flux = 1;
	while (xray_i < NXRAY && getTCPLine (xray_client, line, sizeof(line), &ll)) {
	    // Serial.println(line);
	    if (line[0] == '2' && ll >= 56) {
		float s = atof(line+35);
		if (s <= 0) 			// missing values are set to -1.00e+05, also guard 0
		    s = 1e-9;
		sxray[xray_i] = log10f(s);
		float l = atof(line+47);
		if (l <= 0) 			// missing values are set to -1.00e+05, also guard 0
		    l = 1e-9;
		lxray[xray_i] = log10f(l);
		// Serial.print(l); Serial.print('\t'); Serial.println (s);
		xray_i++;
		if (xray_i == NXRAY)
		    current_flux = l;
	    }
	}

	// proceed iff we found all
	if (xray_i == NXRAY) {
	    resetWatchdog();

	    // create x in hours back from 0
	    for (int16_t i = 0; i < NXRAY; i++)
		x[i] = (i-NXRAY)/6.0;		// 6 entries per hour

	    // use two values on right edge to force constant plot scale -2 .. -9
	    x[NXRAY-3] = 0; lxray[NXRAY-3] = lxray[NXRAY-4];
	    x[NXRAY-2] = 0; lxray[NXRAY-2] = -2;
	    x[NXRAY-1] = 0; lxray[NXRAY-1] = -9;

	    // overlay short over long
	    ok = plotXYstr (box, x, lxray, NXRAY, _FX("Hours"), _FX("GOES 16 Xray"), XRAY_LCOLOR,
	    			xrayLevel(current_flux, line))
		 && plotXY (box, x, sxray, NXRAY, NULL, NULL, XRAY_SCOLOR, 0.0);

	} else {
	    Serial.print (F("Only found ")); Serial.print (xray_i); Serial.print(F(" of "));
            Serial.println (NXRAY);
	}
    } else {
	Serial.println (F("connection failed"));
    }

out:

    // clean up xray_client regardless
    if (!ok)
        plotMessage (box, XRAY_LCOLOR, _FX("No XRay data"));
    xray_client.stop();
    resetWatchdog();
    printFreeHeap (F("XRay"));
    return (ok);
}


// retrieve and plot latest sun spot indices, return whether all ok
static bool updateSunSpots()
{
    // collect lines, assume one day
    #define NSUNSPOT 8			// go back 7 days, including 0
    float sspot[NSUNSPOT+1];		// values plus forced 0 at end so plot y axis always starts at 0
    float x[NSUNSPOT+1];		// time axis plus ... "
    char line[100];
    WiFiClient ss_client;
    bool ok = false;

    plotMessage (plot1_b, SSPOT_COLOR, _FX("Reading Sunspot data..."));

    Serial.println(sspot_page);
    resetWatchdog();
    if (wifiOk() && ss_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// query web page
	httpGET (ss_client, svr_host, sspot_page);

	// skip response header
	if (!httpSkipHeader (ss_client)) {
            Serial.println (F("SSN header short"));
	    goto out;
        }

	// read lines into sspot array and build corresponding time value
	// Serial.println(F("reading ssn"));
	uint8_t ssn_i;
	for (ssn_i = 0; ssn_i < NSUNSPOT && getTCPLine (ss_client, line, sizeof(line), NULL); ssn_i++) {
	    // Serial.print(ssn_i); Serial.print("\t"); Serial.println(line);
	    sspot[ssn_i] = atof(line+11);
	    x[ssn_i] = -7 + ssn_i;
	}

	// plot if found all, display current value
	updateClocks(false);
	resetWatchdog();
	if (ssn_i == NSUNSPOT) {
            x[NSUNSPOT] = x[NSUNSPOT-1];        // dup last time
            sspot[NSUNSPOT] = 0;                // set value to 0
	    ok = plotXY (plot1_b, x, sspot, NSUNSPOT+1, _FX("Days"), _FX("Sunspot Number"),
                                        SSPOT_COLOR, sspot[NSUNSPOT-1]);
        }

    } else {
	Serial.println (F("connection failed"));
    }

    // clean up
out:
    if (!ok)
        plotMessage (plot1_b, SSPOT_COLOR, _FX("No Sunspot data"));
    ss_client.stop();
    resetWatchdog();
    printFreeHeap (F("Sunspots"));
    return (ok);
}

/* retrieve and plot latest and predicted solar flux indices, return whether all ok.
 */
static bool updateSolarFlux(const SBox &box)
{
    // collect lines, three per day for 10 days
    #define	NSFLUX		30
    StackMalloc x_mem(NSFLUX*sizeof(float));
    StackMalloc flux_mem(NSFLUX*sizeof(float));
    float *x = (float *) x_mem.getMem();
    float *flux = (float *) flux_mem.getMem();
    WiFiClient sf_client;
    char line[120];
    bool ok = false;

    plotMessage (box, FLUX_COLOR, _FX("Reading solar flux ..."));

    Serial.println (sf_page);
    resetWatchdog();
    if (wifiOk() && sf_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (sf_client, svr_host, sf_page);

	// skip response header
	if (!httpSkipHeader (sf_client)) {
            Serial.println (F("Flux header short"));
	    goto out;
        }

	// read lines into flux array and build corresponding time value
	// Serial.println(F("reading flux"));
	uint8_t flux_i;
	for (flux_i = 0; flux_i < NSFLUX && getTCPLine (sf_client, line, sizeof(line), NULL); flux_i++) {
	    // Serial.print(flux_i); Serial.print("\t"); Serial.println(line);
	    flux[flux_i] = atof(line);
	    x[flux_i] = -6.667 + flux_i/3.0;	// 7 days history + 3 days predictions
	}

	// plot if found all, display current value
	updateClocks(false);
	resetWatchdog();
	if (flux_i == NSFLUX)
	    ok = plotXY (box, x, flux, NSFLUX, _FX("Days"), _FX("Solar flux"), FLUX_COLOR, flux[NSFLUX-10]);

    } else {
	Serial.println (F("connection failed"));
    }

    // clean up
out:
    if (!ok)
        plotMessage (box, FLUX_COLOR, _FX("No solarflux data"));
    sf_client.stop();
    resetWatchdog();
    printFreeHeap (F("SolarFlux"));
    return (ok);
}

/* retrieve and draw latest band conditions in the given box, return whether all ok.
 * N.B. reset bc_reverting
 */
static bool updateBandConditions(const SBox &box)
{
    StackMalloc response_mem(100);
    StackMalloc config_mem(100);
    char *response = (char *) response_mem.getMem();
    char *config = (char *) config_mem.getMem();
    WiFiClient bc_client;
    bool ok = false;

    plotMessage (box, RA8875_YELLOW, _FX("Reading conditions ..."));

    // build query
    const size_t qsize = sizeof(bc_page)+200;
    StackMalloc query_mem (qsize);
    char *query = (char *) query_mem.getMem();
    time_t t = nowWO();
    snprintf (query, qsize,
                _FX("%s?YEAR=%d&MONTH=%d&RXLAT=%.3f&RXLNG=%.3f&TXLAT=%.3f&TXLNG=%.3f&UTC=%d&PATH=%d&POW=%d"),
                bc_page, year(t), month(t), dx_ll.lat_d, dx_ll.lng_d, de_ll.lat_d, de_ll.lng_d,
                hour(t), show_lp, bc_power);

    Serial.println (query);
    resetWatchdog();
    if (wifiOk() && bc_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);
	resetWatchdog();

	// query web page
	httpGET (bc_client, svr_host, query);

	// skip response header
	if (!httpSkipHeader (bc_client)) {
            plotMessage (box, RA8875_RED, _FX("No BC header"));
	    goto out;
        }

        // next line is CSV path reliability, 80-10m
        if (!getTCPLine (bc_client, response, response_mem.getSize(), NULL)) {
            plotMessage (box, RA8875_RED, _FX("No BC response"));
            goto out;
        }

        // next line is configuration summary
        if (!getTCPLine (bc_client, config, config_mem.getSize(), NULL)) {
            Serial.println(response);
            plotMessage (box, RA8875_RED, _FX("No BC config"));
            goto out;
        }

        // keep time fresh
	updateClocks(false);
	resetWatchdog();

	// plot
        Serial.printf (_FX("BC response: %s\n"), response);
        Serial.printf (_FX("BC config: %s\n"), config);
        ok = plotBandConditions (box, response, config);

    } else {
	plotMessage (box, RA8875_RED, _FX("Connection failed"));
    }

    // clean up
out:
    bc_reverting = false;
    bc_client.stop();
    resetWatchdog();
    printFreeHeap (F("BandConditions"));
    return (ok);
}

/* read SDO image and display in plot3_b
 */
static bool updateSDO ()
{
    WiFiClient sdo_client;

    // choose file and message
    uint8_t sdoi = (plot3_ch - PLOT3_SDO_1) % NARRAY(sdo_images);
    const char *sdo_fn = sdo_images[sdoi].file_name;
    const char *sdo_rm = sdo_images[sdoi].read_msg;;

    // inform user
    plotMessage (plot3_b, SDO_COLOR, sdo_rm);

    // assume bad unless proven otherwise
    bool ok = false;

    Serial.println(sdo_fn);
    resetWatchdog();
    if (wifiOk() && sdo_client.connect(svr_host, HTTPPORT)) {
	updateClocks(false);

	// composite types
	union { char c[4]; uint32_t x; } i32;
	union { char c[2]; uint16_t x; } i16;

	// query web page
	httpGET (sdo_client, svr_host, sdo_fn);

	// skip response header
	if (!httpSkipHeader (sdo_client)) {
            Serial.println (F("SDO header short"));
	    goto out;
        }

	// keep track of our offset in the image file
	uint32_t byte_os = 0;

	// read first two bytes to confirm correct format
	char c;
	if (!getChar(sdo_client,&c) || c != 'B' || !getChar(sdo_client,&c) || c != 'M') {
	    Serial.println (F("SDO image is not BMP"));
	    goto out;
	}
	byte_os += 2;

	// skip down to byte 10 which is the offset to the pixels offset
	while (byte_os++ < 10) {
	    if (!getChar(sdo_client,&c)) {
		Serial.println (F("SDO header 1 error"));
		goto out;
	    }
	}
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO pix_start error"));
		goto out;
	    }
	}
	uint32_t pix_start = i32.x;
	// Serial.printf (_FX("pixels start at %d\n"), pix_start);

	// next word is subheader size, must be 40 BITMAPINFOHEADER
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO hdr size error"));
		goto out;
	    }
	}
	uint32_t subhdr_size = i32.x;
	if (subhdr_size != 40) {
	    Serial.printf (_FX("SDO DIB must be 40: %d\n"), subhdr_size);
	    goto out;
	}

	// next word is width
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO width error"));
		goto out;
	    }
	}
	int32_t img_w = i32.x;

	// next word is height
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	int32_t img_h = i32.x;
	Serial.printf (_FX("SDO image is %d x %d = %d\n"), img_w, img_h, img_w*img_h);

	// next short is n color planes
	for (uint8_t i = 0; i < 2; i++, byte_os++) {
	    if (!getChar(sdo_client,&i16.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	uint16_t n_planes = i16.x;
	if (n_planes != 1) {
	    Serial.printf (_FX("SDO planes must be 1: %d\n"), n_planes);
	    goto out;
	}

	// next short is bits per pixel
	for (uint8_t i = 0; i < 2; i++, byte_os++) {
	    if (!getChar(sdo_client,&i16.c[i])) {
		Serial.println (F("SDO height error"));
		goto out;
	    }
	}
	uint16_t n_bpp = i16.x;
	if (n_bpp != 24) {
	    Serial.printf (_FX("SDO bpp must be 24: %d\n"), n_bpp);
	    goto out;
	}

	// next word is compression method
	for (uint8_t i = 0; i < 4; i++, byte_os++) {
	    if (!getChar(sdo_client,&i32.c[i])) {
		Serial.println (F("SDO compression error"));
		goto out;
	    }
	}
	uint32_t comp = i32.x;
	if (comp != 0) {
	    Serial.printf (_FX("SDO compression must be 0: %d\n"), comp);
	    goto out;
	}

	// skip down to start of pixels
	while (byte_os++ <= pix_start) {
	    if (!getChar(sdo_client,&c)) {
		Serial.println (F("SDO header 3 error"));
		goto out;
	    }
	}

	// display box depends on actual output size.
	SBox v_b;
#if defined(_USE_DESKTOP)
	v_b.x = plot3_b.x * tft.SCALESZ;
	v_b.y = plot3_b.y * tft.SCALESZ;
	v_b.w = plot3_b.w * tft.SCALESZ;
	v_b.h = plot3_b.h * tft.SCALESZ;
#else
	v_b = plot3_b;
#endif

	// clip and center the image within v_b
	uint16_t xborder = img_w > v_b.w ? (img_w - v_b.w)/2 : 0;
	uint16_t yborder = img_h > v_b.h ? (img_h - v_b.h)/2 : 0;

	// scan all pixels ...
	for (uint16_t img_y = 0; img_y < img_h; img_y++) {

	    // keep time active
	    resetWatchdog();
	    updateClocks(false);

	    for (uint16_t img_x = 0; img_x < img_w; img_x++) {

		char b, g, r;

		// read next pixel
		if (!getChar (sdo_client, &b) || !getChar (sdo_client, &g) || !getChar (sdo_client, &r)) {
		    Serial.printf (_FX("SDO read error after %d pixels\n"), img_y*img_w + img_x);
		    goto out;
		}

		// ... but only draw if fits inside border
		if (img_x > xborder && img_x < xborder + v_b.w - 1
			    && img_y > yborder && img_y < yborder + v_b.h - 1) {

		    uint8_t ur = r;
		    uint8_t ug = g;
		    uint8_t ub = b;
		    uint16_t color16 = RGB565(ur,ug,ub);
#if defined(_USE_DESKTOP)
		    tft.drawSubPixel (v_b.x + img_x - xborder,
		    		v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
#else
		    tft.drawPixel (v_b.x + img_x - xborder,
		    		v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
#endif
		}
	    }

	    // skip padding to bring total row length to multiple of 4
	    uint8_t extra = img_w % 4;
	    if (extra > 0) {
		for (uint8_t i = 0; i < 4 - extra; i++) {
		    if (!getChar(sdo_client,&c)) {
			Serial.println (F("SDO row padding error"));
			goto out;
		    }
		}
	    }
	}

	Serial.println (F("SDO image complete"));
        tft.drawRect (plot3_b.x, plot3_b.y, plot3_b.w, plot3_b.h, GRAY);
	ok = true;

    } else {
	Serial.println (F("connection failed"));
    }

out:
    if (!ok)
	plotMessage (plot3_b, SDO_COLOR, _FX("SDO failed"));
    sdo_client.stop();
    printFreeHeap(F("SDO"));
    return (ok);
}

/* get next line from client in line[] then return true, else nothing and return false.
 * line[] will have \r and \n removed and end with \0, optional line length in *ll will not include \0.
 * if line is longer than line_len it will be silently truncated.
 */
bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll)
{
    // keep clocks current
    updateClocks(false);

    // decrement available length so there's always room to add '\0'
    line_len -= 1;

    // read until find \n or time out.
    uint16_t i = 0;
    while (true) {
	char c;
	if (!getChar (client, &c))
	    return (false);
	if (c == '\r')
	    continue;
	if (c == '\n') {
	    line[i] = '\0';
	    if (ll)
		*ll = i;
	    // Serial.println(line);
	    return (true);
	} else if (i < line_len)
	    line[i++] = c;
    }
}

/* convert an array of 4 big-endian network-order bytes into a uint32_t
 */
static uint32_t crackBE32 (uint8_t bp[])
{
    union {
        uint32_t be;
        uint8_t ba[4];
    } be4;

    be4.ba[3] = bp[0];
    be4.ba[2] = bp[1];
    be4.ba[1] = bp[2];
    be4.ba[0] = bp[3];

    return (be4.be);
}

/* called when RSS has just been turned on: update now and restart refresh cycle
 */
void updateRSSNow()
{
    next_rss = 0;
}

/* display next RSS feed item if on, return whether ok
 */
bool updateRSS ()
{
    // persistent list of malloced titles
    static char *titles[NRSS];
    static uint8_t n_titles, title_i;

    // skip and clear cache if off
    if (!rss_on) {
        while (n_titles > 0) {
            free (titles[--n_titles]);
            titles[n_titles] = NULL;
        }
	return (true);
    }

    // reserve mem
    StackMalloc line_mem(150);
    char *line = line_mem.getMem();

    // prepare background
    tft.fillRect (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.w, rss_bnr_b.h, RSS_BG_COLOR);
    tft.drawLine (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.x+rss_bnr_b.w, rss_bnr_b.y, GRAY);
    drawRSSButton();

    // fill titles[] if empty
    if (title_i >= n_titles) {

        // reset count and index
        n_titles = title_i = 0;

        // TCP client
        WiFiClient rss_client;
        
        Serial.println(rss_page);
        resetWatchdog();
        if (wifiOk() && rss_client.connect(svr_host, HTTPPORT)) {

            resetWatchdog();
            updateClocks(false);

            // fetch feed page
            httpGET (rss_client, svr_host, rss_page);

            // skip response header
            if (!httpSkipHeader (rss_client)) {
                Serial.println (F("RSS header short"));
                goto out;
            }

            // get up to NRSS more titles[]
            for (n_titles = 0; n_titles < NRSS; n_titles++) {
                if (!getTCPLine (rss_client, line, line_mem.getSize(), NULL))
                    goto out;
                if (titles[n_titles])
                    free (titles[n_titles]);
                titles[n_titles] = strdup (line);
                // Serial.printf (_FX("RSS[%d] len= %d\n"), n_titles, strlen(titles[n_titles]));
            }
        }

      out:
        rss_client.stop();

        // real trouble if still no titles
        if (n_titles == 0) {
            // report error 
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (RSS_FG_COLOR);
            tft.setCursor (rss_bnr_b.x + rss_bnr_b.w/2-100, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
            tft.print (F("RSS network error"));
            Serial.println (F("RSS failed"));
            return (false);
        }
        printFreeHeap (F("RSS"));
    }

    // draw next title
    char *title = titles[title_i];
    size_t ll = strlen(title);

    // usable banner drawing x and width
    uint16_t ubx = rss_btn_b.x + rss_btn_b.w + 5;
    uint16_t ubw = rss_bnr_b.x + rss_bnr_b.w - ubx;

    resetWatchdog();

    // find pixel width of title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RSS_FG_COLOR);
    uint16_t bw = getTextWidth (title);

    // draw as 1 or 2 lines to fit within ubw
    if (bw < ubw) {
        // title fits on one row, draw centered horizontally and vertically
        tft.setCursor (ubx + (ubw-bw)/2, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
        tft.print (title);
    } else {
        // title too long, split near center
        char *row2 = strchr (title+ll/2, ' ');
        if (!row2)
            row2 = title+ll/2;	        // no blanks! just split in half?
        *row2++ = '\0';                 // replace with EOS and move to start of row 2
        uint16_t r1w, r2w;	        // row 1 and 2 pixel widths
        r1w = getTextWidth (title);
        r2w = getTextWidth (row2);

        // draw if fits
        if (r1w <= ubw && r2w <= ubw) {
            tft.setCursor (ubx + (ubw-r1w)/2, rss_bnr_b.y + rss_bnr_b.h/2 - 8);
            tft.print (title);
            tft.setCursor (ubx + (ubw-r2w)/2, rss_bnr_b.y + rss_bnr_b.h - 9);
            tft.print (row2);
        } else {
            Serial.printf (_FX("RSS not fit: '%s' '%s'\n"), title, row2);
        }
    }

    // remove from list and advance to next title
    free (titles[title_i]);
    titles[title_i++] = NULL;
 
    resetWatchdog();
    return (true);
}

/* display the RSG NOAA solar environment scale values.
 */
static bool updateNOAASWx(const SBox &box)
{
    // expecting 3 reply lines of the form:
    //  R  0 0 0 0
    //  S  0 0 0 0
    //  G  0 0 0 0
    char lines[3][50];
    uint8_t nlines = 0;

    // TCP client
    WiFiClient noaaswx_client;

    // starting msg
    plotMessage (box, RA8875_WHITE, _FX("Reading NOAA scale..."));
        
    // read scales
    Serial.println(noaaswx_page);
    resetWatchdog();
    if (wifiOk() && noaaswx_client.connect(svr_host, HTTPPORT)) {

        resetWatchdog();
        updateClocks(false);

        // fetch page
        httpGET (noaaswx_client, svr_host, noaaswx_page);

        // skip header then read the 3 lines
        if (httpSkipHeader (noaaswx_client)) {
            for (nlines = 0; nlines < 3; nlines++) {
                if (!getTCPLine (noaaswx_client, lines[nlines], sizeof(lines[nlines]), NULL)) {
                    break;
                }
                // Serial.printf ("NOAA: %s\n", lines[nlines]);
            }
        } else
            Serial.println (F("NOAASWx header short"));
    }

    // finished with connection
    noaaswx_client.stop();

    // test strings
    // sprintf (lines[0], "R 0 5 4 2");
    // sprintf (lines[1], "S 1 3 5 5");
    // sprintf (lines[2], "G 5 2 3 0");

    // plot if got all else report error
    if (nlines == 3)
        plotNOAASWx (box, lines);
    else {
        Serial.printf (_FX("NOAA Scales only %d/3 lines\n"), nlines);
        plotMessage (box, RA8875_RED, _FX("No NOAA scales"));
    }

    // done
    resetWatchdog();
    printFreeHeap (F("updateNOAASWx"));
    return (nlines == 3);
}

/* it is MUCH faster to print F() strings in a String than using them directly.
 * see esp8266/2.3.0/cores/esp8266/Print.cpp::print(const __FlashStringHelper *ifsh) to see why.
 */
void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.print(_sp);
}

void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.println(_sp);
}

// handy wifi health check
bool wifiOk()
{
    if (WiFi.status() == WL_CONNECTED)
	return (true);

    // retry occasionally
    static uint32_t last_wifi;
    if (timesUp (&last_wifi, WIFI_RETRY)) {
	initWiFi(false);
	return (WiFi.status() == WL_CONNECTED);
    } else
	return (false);
}

/* reset the wifi retry flags so all the ones that can not refresh from memory are restarted immediately
 */
void initWiFiRetry()
{
    next_flux = 0;
    next_ssn = 0;
    next_xray = 0;
    next_kp = 0;
    next_rss = 0;
    next_sdo = 0;
    next_noaaswx = 0;
    next_dewx = 0;
    next_dxwx = 0;
    next_bc = 0;

    // map is in memory
    // next_map = 0;
}

/* called to schedule an update to the band conditions pane.
 * if BC is on plot1_b wait for a revert in progress otherwie update immediately.
 */
void newBC()
{
    if ((plot1_ch == PLOT1_BC && !bc_reverting) || plot2_ch == PLOT2_BC || plot3_ch == PLOT3_BC)
        next_bc = 0;
}

/* called to schedule an immediate update of the VOACAP map.
 * ignore if one is not being shown.
 */
void newVOACAPMap()
{
    if (prop_map != PROP_MAP_OFF)
        next_map = 0;
}
