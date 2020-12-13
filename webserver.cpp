/* service the port 80 commands
 */

#include "HamClock.h"


// table of each pane function and public names
typedef struct {
    uint8_t func;
    const char *name;
} PaneNameTable;
static PaneNameTable pane1_names[PLOT1_N] = {
    {PLOT1_SSN,  "SSN"},
    {PLOT1_XRAY, "XRAY"},
    {PLOT1_FLUX, "Flux"},
    {PLOT1_KP,   "KP"},
    {PLOT1_BC,   "VOACAP"},
    {PLOT1_DEWX, "DEWx"},
};
static PaneNameTable pane2_names[PLOT2_N] = {
    {PLOT2_XRAY, "XRAY"},
    {PLOT2_FLUX, "Flux"},
    {PLOT2_KP,   "KP"},
    {PLOT2_BC,   "VOACAP"},
    {PLOT2_DX,   "DXCluster"},
};
static PaneNameTable pane3_names[PLOT3_N] = {
    {PLOT3_TEMP,     "ENV_Temp"},
    {PLOT3_PRESSURE, "ENV_Pressure"},
    {PLOT3_HUMIDITY, "ENV_Humidity"},
    {PLOT3_DEWPOINT, "ENV_DewPoint"},
    {PLOT3_SDO_1,    "SDO_Composite"},
    {PLOT3_SDO_2,    "SDO_6173A"},
    {PLOT3_SDO_3,    "SDO_Magnetogram"},
    {PLOT3_GIMBAL,   "Gimbal"},
    {PLOT3_KP,       "KP"},
    {PLOT3_BC,       "VOACAP"},
    {PLOT3_NOAASWX,  "NOAA_SpaceWx"},
    {PLOT3_DXWX,     "DXWx"},
};
typedef struct {
    PaneNameTable *ptp;
    uint8_t n_names;
} PaneTable;
static PaneTable pane_tables[N_PANES] = {
    {pane1_names, PLOT1_N},
    {pane2_names, PLOT2_N},
    {pane3_names, PLOT3_N},
};



// persistent server for listening for remote connections
static WiFiServer remoteServer(HTTPPORT);


/* replace all "%20" with blank, IN PLACE
 */
static void replaceBlankEntity (char *from)
{
    char *to = from;
    while (*from) {
        if (strncmp (from, "%20", 3) == 0) {
            *to++ = ' ';
            from += 3;
        } else
            *to++ = *from++;
    }
    *to = '\0';
}

/* send initial response indicating body will be plain text
 */
static void startPlainText (WiFiClient &client)
{
    resetWatchdog();

    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line

    resetWatchdog();
}

/* send the given message as HTTP error 400 Bad request.
 * if message is empty, send "Garbled command".
 */
static void sendHTTPError (WiFiClient &client, const char *errmsg)
{
    resetWatchdog();

    // supply default if errmsg is empty
    const char *msg = (errmsg[0] == '\0') ? "Garbled command" : errmsg;

    // pronounce locally
    Serial.println (msg);

    // send to client
    FWIFIPRLN (client, F("HTTP/1.0 400 Bad request"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line
    client.println (msg);

    resetWatchdog();
}

/* send screen capture
 */
static bool getWiFiScreenCapture(WiFiClient &client, char *line)
{
    (void)(line);

    #define CORESZ 14                           // always 14 bytes at front
    #define HDRVER 108                          // BITMAPV4HEADER, also n bytes in subheader
    #define BHDRSZ (CORESZ+HDRVER)		// total header size
    uint8_t buf[300];				// any modest size ge BHDRSZ and mult of 2

#if defined(_USE_DESKTOP)
    uint32_t nrows = tft.SCALESZ*tft.height();
    uint32_t ncols = tft.SCALESZ*tft.width();
#else
    uint32_t nrows = tft.height();
    uint32_t ncols = tft.width();
#endif

    resetWatchdog();

    // build BMP header 
    uint32_t npix = nrows*ncols;		// n pixels
    uint32_t nbytes = npix*2;                   // n bytes of image data

    // 14 byte header common to all formats
    buf[0] = 'B';				// id
    buf[1] = 'M';				// id
    *((uint32_t*)(buf+ 2)) = BHDRSZ+nbytes; 	// total file size: header + pixels
    *((uint16_t*)(buf+ 6)) = 0; 		// reserved 0
    *((uint16_t*)(buf+ 8)) = 0; 		// reserved 0
    *((uint32_t*)(buf+10)) = BHDRSZ;		// offset to start of pixels

    // we use BITMAPV4INFOHEADER which supports RGB565
    *((uint32_t*)(buf+14)) = HDRVER;		// subheader type
    *((uint32_t*)(buf+18)) = ncols;		// width
    *((uint32_t*)(buf+22)) = -nrows;		// height, neg means starting at the top row
    *((uint16_t*)(buf+26)) = 1;			// n planes
    *((uint16_t*)(buf+28)) = 16;		// bits per pixel -- 16 RGB565 
    *((uint32_t*)(buf+30)) = 3;			// BI_BITFIELDS to indicate RGB bitmasks are present
    *((uint32_t*)(buf+34)) = nbytes;		// image size in bytes
    *((uint32_t*)(buf+38)) = 0;			// X pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+42)) = 0;			// Y pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+46)) = 0;			// colors in table
    *((uint32_t*)(buf+50)) = 0;			// important colors
    *((uint32_t*)(buf+54)) = 0xF800;	        // red mask
    *((uint32_t*)(buf+58)) = 0x07E0;	        // green mask
    *((uint32_t*)(buf+62)) = 0x001F;       	// blue mask
    *((uint32_t*)(buf+66)) = 0;         	// alpha mask
    *((uint32_t*)(buf+70)) = 1;                 // CSType: 1 means ignore all the remaining fields!
    *((uint32_t*)(buf+74)) = 0;                 // RedX
    *((uint32_t*)(buf+78)) = 0;                 // RedY
    *((uint32_t*)(buf+82)) = 0;                 // RedZ
    *((uint32_t*)(buf+86)) = 0;                 // GreenX
    *((uint32_t*)(buf+90)) = 0;                 // GreenY
    *((uint32_t*)(buf+94)) = 0;                 // GreenZ
    *((uint32_t*)(buf+99)) = 0;                 // BlueX
    *((uint32_t*)(buf+102)) = 0;                // BlueY
    *((uint32_t*)(buf+106)) = 0;                // BlueZ
    *((uint32_t*)(buf+110)) = 0;                // GammaRed
    *((uint32_t*)(buf+114)) = 0;                // GammaGreen
    *((uint32_t*)(buf+118)) = 0;                // GammaBlue

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: image/bmp"));
    FWIFIPR (client, F("Content-Length: ")); client.println (BHDRSZ+nbytes);
    FWIFIPRLN (client, F("Connection: close\r\n"));
    // Serial.println(F("web header sent"));

    // send the image header
    client.write ((uint8_t*)buf, BHDRSZ);
    // Serial.println(F("img header sent"));

    // send the pixels
    resetWatchdog();
    tft.graphicsMode();
    tft.setXY(0,0);
    tft.writeCommand(RA8875_MRWC);
    static bool first = true;
    if (first) {
	// skip first pixel first time
	tft.readData();
	tft.readData();
	first = false;
    }
    uint16_t bufl = 0;
    for (uint32_t i = 0; i < npix; i++) {
	if ((i % tft.width()) == 0)
	    resetWatchdog();

        // swap bytes
        buf[bufl+1] = tft.readData();
        buf[bufl+0] = tft.readData();
        bufl += 2;

	if (bufl == sizeof(buf) || i == npix-1) {

            // ESP outgoing data can deadlock if incoming buffer fills, so check for largest source.
            // dx cluster is only autonomous incoming connection to check.
            updateDXCluster();

	    client.write ((uint8_t*)buf, bufl);
	    bufl = 0;
	    resetWatchdog();
	}
    }
    // Serial.println(F("pixels sent"));

    // never fails
    return (true);
}

/* remote command to report the current count down timer value, in seconds
 */
static bool getWiFiCountdown (WiFiClient &client, char *unused)
{
    (void) unused;

    startPlainText(client);
    FWIFIPR (client, F("Countdown "));
    client.print (getCountdownLeft()/1000);   // ms -> s
    FWIFIPRLN (client, F(" secs"));

    return (true);
}

/* helper to report DE or DX info which are very similar
 */
static bool getWiFiDEDXInfo_helper (WiFiClient &client, char *unused, bool send_dx)
{
    (void) unused;

    char buf[100];

    // handy which
    TZInfo &tz  =        send_dx ? dx_tz : de_tz;
    LatLong &ll =        send_dx ? dx_ll : de_ll;
    const char *prefix = send_dx ? "DX_" : "DE_";

    // start response
    startPlainText(client);

    // send call if DE
    if (!send_dx) {
        snprintf (buf, sizeof(buf), _FX("Call      %s"), getCallsign());
        client.println (buf);
    }

    // report local time
    time_t t = nowWO();
    time_t local = t + tz.tz_secs;
    int yr = year (local);
    int mo = month(local);
    int dy = day(local);
    int hr = hour (local);
    int mn = minute (local);
    int sc = second (local);
    snprintf (buf, sizeof(buf), _FX("%stime   %d-%02d-%02dT%02d:%02d:%02d"), prefix, yr, mo, dy, hr, mn, sc);
    client.println (buf);

    // timezone
    snprintf (buf, sizeof(buf), _FX("%stz     %+g"), prefix, tz.tz_secs/3600.0);
    client.println (buf);

    // report lat
    snprintf (buf, sizeof(buf), _FX("%slat    %0.2f degs"), prefix, ll.lat_d);
    client.println (buf);

    // report lng
    snprintf (buf, sizeof(buf), _FX("%slng    %0.2f degs"), prefix, ll.lng_d);
    client.println (buf);

    // report grid
    uint32_t mnv;
    NVReadUInt32 (send_dx ? NV_DX_GRID : NV_DE_GRID, &mnv);
    char maid[5];
    unpackMaidToStr (maid, mnv);
    snprintf (buf, sizeof(buf), _FX("%sgrid   %s"), prefix, maid);
    client.println (buf);

    // report prefix and path if dx
    if (send_dx) {
        char prefix[MAX_PREF_LEN+1];
        if (getDXPrefix(prefix)) {
            snprintf (buf, sizeof(buf), "DX_prefix %s", prefix);
            client.println (buf);
        }

        float dist, B;
        propDEDXPath (show_lp, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (client, F("DX_path   "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f degs %s"), dist, show_km ? "km" : "mi",
                                    B, show_lp ? "LP" : "SP");
        client.println (buf);
    }

    // ok
    return (true);
}

/* remote report DE info
 */
static bool getWiFiDEInfo (WiFiClient &client, char *line)
{
    return (getWiFiDEDXInfo_helper (client, line, false));
}

/* remote report DX info
 */
static bool getWiFiDXInfo (WiFiClient &client, char *line)
{
    return (getWiFiDEDXInfo_helper (client, line, true));
}

/* remote report current set of DX spots
 */
static bool getWiFiDXSpots (WiFiClient &client, char *line)
{
    // retrieve spots, if available
    DXSpot *spots;
    uint8_t nspots;
    if (!getDXSpots (&spots, &nspots)) {
        strcpy_P (line, PSTR("No cluster"));
        return (false);
    }

    // start reply, even if none
    startPlainText (client);

    // print each row, similar to drawDXSpot()
    FWIFIPR (client, F("#  kHz   Call        UTC  Grid    Lat     Lng       Dist   Bear\n"));
    float sdelat = sinf(de_ll.lat);
    float cdelat = cosf(de_ll.lat);
    for (uint8_t i = 0; i < nspots; i++) {
        DXSpot *sp = &spots[i];
        char line[100];

        // pretty freq, fixed 8 chars
        const char *f_fmt = sp->freq < 1e6 ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, sp->freq);

        // grid
        char maid[5];
        unpackMaidToStr (maid, sp->grid);

        // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
        // bear will be bearing from DE to spot east-to-north in radians, -pi..pi
        float cdist, bear;
        solveSphere (sp->ll.lng-de_ll.lng, M_PI_2F-sp->ll.lat, sdelat, cdelat, &cdist, &bear);
        float dist = acosf(cdist);                      // short path angle always 0..pi
        bear = fmodf (bear + 2*M_PIF, 2*M_PIF);         // shift -pi..pi to 0..2pi
        if (show_lp) {                                  // match DX display
            bear = fmodf (bear + 3*M_PIF, 2*M_PIF);     // +180 then 0..2pi
            dist = 2*M_PIF - dist;                      // cocircular angle
        }
        dist *= ERAD_M;                                 // angle to miles
        bear *= 180/M_PIF;                              // rad -> degrees
        if (show_km)                                    // match DX display
            dist *= 1.609344F;                          // miles -> km

        // print together
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u %s   %6.2f %7.2f   %6.0f   %4.0f\n"),
                    MAX_DXSPOTCALL_LEN-1, sp->call, sp->uts, maid, sp->ll.lat_d, sp->ll.lng_d, dist, bear);
        client.print(line);
    }

    // ok
    return (true);
}

/* remote report some basic clock configuration
 */
static bool getWiFiConfig (WiFiClient &client, char *unused)
{
    (void) unused;

    const __FlashStringHelper *not_sup = F("N/A");
    char buf[50];

    // start reply
    startPlainText (client);

    // report whether screen is locked
    FWIFIPR (client, F("Screen    "));
    if (screenIsLocked())
        FWIFIPRLN (client, F("locked"));
    else
        FWIFIPRLN (client, F("unlocked"));

    // report map style
    FWIFIPR (client, F("MapStyle  "));
    client.println(getMapStyle());

    // report map projection
    FWIFIPR (client, F("MapProj   "));
    if (azm_on)
        FWIFIPRLN (client, F("Azimuthal"));
    else
        FWIFIPRLN (client, F("Mercator"));

    // report grid overlay
    FWIFIPR (client, F("MapGrid   "));
    switch (llg_on) {
    case LLG_OFF:           FWIFIPRLN (client, F("Off")); break;
    case LLG_TROPICS:       FWIFIPRLN (client, F("Tropics")); break;
    case LLG_ALL:           FWIFIPRLN (client, F("LatLng")); break;
    default:                FWIFIPRLN (client, F("unknown")); break;
    }

    // report panes
    FWIFIPR (client, F("Pane1     ")); client.println (pane1_names[plot1_ch].name);
    FWIFIPR (client, F("Pane2     ")); client.println (pane2_names[plot2_ch].name);
    FWIFIPR (client, F("Pane3     ")); client.println (pane3_names[plot3_ch].name);

    // report NCDXF beacon box state
    FWIFIPR (client, F("NCDXF     "));
    switch (brb_mode) {
    case BRB_SHOW_BEACONS:  FWIFIPRLN (client, F("Beacons")); break;
    case BRB_SHOW_ONOFF:    FWIFIPRLN (client, F("OnOff_timers")); break;
    case BRB_SHOW_PHOT:     FWIFIPRLN (client, F("Photocell")); break;
    case BRB_SHOW_BR:       FWIFIPRLN (client, F("Brightness")); break;
    default:                FWIFIPRLN (client, F("Off")); break;
    }

    // report what DE pane is being used for
    FWIFIPR (client, F("DEPane    "));
    switch (de_time_fmt) {
    case DETIME_INFO:       FWIFIPR (client, F("Info ")); break;
    case DETIME_ANALOG:     FWIFIPR (client, F("Analog ")); break;
    case DETIME_CAL:        FWIFIPR (client, F("Calendar ")); break;
    }
    snprintf (buf, sizeof(buf), _FX("TZ %+g "), de_tz.tz_secs/3600.0);
    client.print(buf);
    if (de_time_fmt == DETIME_INFO) {
        if (desrss)
            FWIFIPR (client, F("RSAtAt"));
        else
            FWIFIPR (client, F("RSInAgo"));
    }
    client.println();

    // report what DX pane is being used for
    FWIFIPR (client, F("DXPane    "));
    if (dx_info_for_sat)
        FWIFIPRLN (client, F("sat"));
    else {
        FWIFIPR (client, F("DX "));
        snprintf (buf, sizeof(buf), "TZ %+g ", dx_tz.tz_secs/3600.0);
        client.print(buf);
        switch (dxsrss) {
        case DXSRSS_INAGO:  FWIFIPR (client, F("RSInAgo")); break;
        case DXSRSS_ATAT:   FWIFIPR (client, F("RSAtAt"));  break;
        case DXSRSS_PREFIX: FWIFIPR (client, F("Prefix"));  break;
        }
        client.println();
    }

    // rss
    if (rss_on)
        FWIFIPRLN (client, F("RSS       on"));
    else
        FWIFIPRLN (client, F("RSS       off"));

    // report dxcluster state
    FWIFIPR (client, F("DXCluster "));
    if (useDXCluster()) {
        snprintf (buf, sizeof(buf), _FX("%s:%d %sconnected\n"), getDXClusterHost(), getDXClusterPort(),
                                        isDXConnected() ? "" : "dis");
        client.print (buf);
    } else
        FWIFIPRLN (client, F("off"));

    // report units
    FWIFIPR (client, F("Units     "));
    if (useMetricUnits())
        FWIFIPRLN (client, F("metric"));
    else
        FWIFIPRLN (client, F("imperial"));

    // report on/off timer
    FWIFIPR (client, F("Timers    "));
    uint16_t t_on, t_off, t_idle;
    if (getDisplayTimes (&t_on, &t_off, &t_idle)) {
        snprintf (buf, sizeof(buf), _FX("on@ %02d:%02d off@ %02d:%02d idle %d\n"),
            t_on/60, t_on%60, t_off/60, t_off%60, t_idle);
        client.print (buf);
    } else
        FWIFIPRLN (client, not_sup);

    // report BME info
    FWIFIPR (client, F("BME280    "));
    #if defined(_SUPPORT_ENVSENSOR)
        if (bme280_connected) {
            snprintf (buf, sizeof(buf), _FX("dTemp %g dPres %g\n"), getBMETempCorr(), getBMEPresCorr());
            client.print (buf);
        } else
            FWIFIPRLN (client, F("disconnected"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_ENVSENSOR

    // report KX3 info
    FWIFIPR (client, F("KX3       "));
    #if defined(_SUPPORT_KX3)
        uint32_t baud = getKX3Baud();
        if (baud > 0) {
            snprintf (buf, sizeof(buf), _FX("%d baud\n"), baud);
            client.print (buf);
        } else
            FWIFIPRLN (client, F("off"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_KX3

    // report GPIO info
    FWIFIPR (client, F("GPIO      "));
    #if defined(_SUPPORT_GPIO)
        if (GPIOOk())
            FWIFIPRLN (client, F("on"));
        else
            FWIFIPRLN (client, F("off"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_GPIO

    // report photosensor info
    FWIFIPR (client, F("Photocell "));
    #if defined(_SUPPORT_PHOT)
        if (found_phot)
            FWIFIPRLN (client, F("connected"));
        else
            FWIFIPRLN (client, F("disconnected"));
    #else
        FWIFIPRLN (client, not_sup);
    #endif // _SUPPORT_PHOT

    // done
    return (true);
}

/* report current satellite info to the given WiFi connection, or none.
 * always return true
 */
static bool getWiFiSatellite (WiFiClient &client, char *unused)
{
    (void) unused;

    // start reply
    startPlainText (client);

    // get name and current position
    float az, el, range, rate, raz, saz, rhrs, shrs;
    char name[NV_SATNAME_LEN];
    if (!getSatAzElNow (name, &az, &el, &range, &rate, &raz, &saz, &rhrs, &shrs)) {
	FWIFIPRLN (client, F("none"));
        return (true);
    }

    FWIFIPR (client, F("Name  ")); client.println (name);
    FWIFIPR (client, F("Alt   ")); client.print (el); FWIFIPRLN(client, F(" degs"));
    FWIFIPR (client, F("Az    ")); client.print (az); FWIFIPRLN(client, F(" degs"));
    FWIFIPR (client, F("Range ")); client.print (range); FWIFIPRLN(client, F(" km"));
    FWIFIPR (client, F("Rate  ")); client.print (rate); FWIFIPRLN(client, F(" m/s"));
    FWIFIPR (client, F("144MHzDoppler ")); client.print (-rate*144000/3e8); FWIFIPRLN(client, F(" kHz"));
    FWIFIPR (client, F("440MHzDoppler ")); client.print (-rate*440000/3e8); FWIFIPRLN(client, F(" kHz"));

    if (raz != SAT_NOAZ) {
        FWIFIPR (client, F("Next rise in "));
        client.print (rhrs*60, 2);
        FWIFIPR (client, F(" mins at "));
        client.print (raz, 2);
        FWIFIPRLN (client, F(" degs"));
    }
    if (saz != SAT_NOAZ) {
        FWIFIPR (client, F("Next set in "));
        client.print (shrs*60, 2);
        FWIFIPR (client, F(" mins at "));
        client.print (saz, 2);
        FWIFIPRLN (client, F(" degs"));
    }

    return (true);
}

/* send the current collection of sensor data to client in CSV format.
 */
static bool getWiFiSensorInfo (WiFiClient &client, char *line)
{
    if (!bme280_connected) {
        strcpy_P (line, PSTR("No sensor"));
        return (false);
    }

    // send html header
    startPlainText(client);

    // send content header
    if (useMetricUnits())
	FWIFIPRLN (client, F("# UTC ISO 8601       UNIX secs  Temp,C   P,hPa   Hum,%    DP,C"));
    else
	FWIFIPRLN (client, F("# UTC ISO 8601       UNIX secs  Temp,F  P,inHg   Hum,%    DP,F"));

    // print each sensor reading
    time_t t;
    float e, p, h, d;
    uint8_t n = 0;
    char buf[80];
    resetWatchdog();
    while (nextBME280Data (&t, &e, &p, &h, &d, &n)) {
	snprintf (buf, sizeof(buf), _FX("%4d-%02d-%02dT%02d:%02d:%02d %lu %7.2f %7.2f %7.2f %7.2f"),
		year(t), month(t), day(t), hour(t), minute(t), second(t), t, e, p, h, d);
	client.println (buf);
    }

    return (true);
}

/* send some misc system info
 */
static bool getWiFiSys (WiFiClient &client, char *unused)
{
    (void) unused;
    char buf[100];

    // send html header
    startPlainText(client);

    // get latest worst stats
    int worst_heap, worst_stack;
    getWorstMem (&worst_heap, &worst_stack);

    // send info
    resetWatchdog();
    FWIFIPR (client, F("Version  ")); client.println (HC_VERSION);
    FWIFIPR (client, F("MaxStack ")); client.println (worst_stack);
    FWIFIPR (client, F("MaxWDDT  ")); client.println (max_wd_dt);
#if defined(_IS_ESP8266)
    FWIFIPR (client, F("MinHeap  ")); client.println (worst_heap);
    FWIFIPR (client, F("FreeNow  ")); client.println (ESP.getFreeHeap());
    FWIFIPR (client, F("MaxBlock ")); client.println (ESP.getMaxFreeBlockSize());
    FWIFIPR (client, F("SketchSz ")); client.println (ESP.getSketchSize());
    FWIFIPR (client, F("FreeSkSz ")); client.println (ESP.getFreeSketchSpace());
    FWIFIPR (client, F("FlChipSz ")); client.println (ESP.getFlashChipRealSize());
    FWIFIPR (client, F("CPUMHz   ")); client.println (ESP.getCpuFreqMHz());
    FWIFIPR (client, F("CoreVer  ")); client.println (ESP.getCoreVersion());
    // #if defined __has_include                        should work but doesn't
        // #if __has_include (<lwip-git-hash.h>)        should work but doesn't either
            // #include <lwip-git-hash.h>
            // FWIFIPR (client, F("lwipVer  ")); client.println (LWIP_HASH_STR);
        // #endif
    // #endif
#endif

    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        snprintf (buf, sizeof(buf), _FX("%d %02d:%02d:%02d"), days, hrs, mins, secs);
        FWIFIPR (client, F("UpTime   ")); client.println (buf);
    }

    // show file system info
    int n_info;
    uint64_t fs_size, fs_used;
    char *fs_name;
    FS_Info *fip0 = getConfigDirInfo (&n_info, &fs_name, &fs_size, &fs_used);
    client.print (fs_name);
    if (fs_size > 1000000000U)
        snprintf (buf, sizeof(buf), " %llu / %llu MiB\n", fs_used/1048576U, fs_size/1048576U);
    else
        snprintf (buf, sizeof(buf), " %llu / %llu B\n", fs_used, fs_size);
    client.print (buf);
    for (int i = 0; i < n_info; i++) {
        FS_Info *fip = &fip0[i];
        snprintf (buf, sizeof(buf), "  %-32s %s %u\n", fip->name, fip->date, fip->len);
        client.print (buf);
    }
    free (fs_name);
    free (fip0);

    return (true);
}

/* finish the wifi then restart
 */
static bool doWiFiReboot (WiFiClient &client, char *unused)
{
    (void) unused;

    // send html header then close
    startPlainText(client);
    FWIFIPRLN (client, F("restarting ... bye for now."));
    wdDelay(100);
    client.flush();
    client.stop();
    wdDelay(1000);

    Serial.println (F("restarting..."));
    reboot();

    // never returns but compiler doesn't know that
    return (true);
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient &client, char *unused)
{
    (void) unused;

    // prep for response but won't be one if we succeed with update
    startPlainText(client);

    // proceed if newer version is available
    if (newVersionIsAvailable (NULL, 0)) {
        FWIFIPRLN (client, F("updating..."));
        doOTAupdate();                  // never returns if successful
        FWIFIPRLN (client, F("update failed"));
    } else
        FWIFIPRLN (client, F("You're up to date!"));    // match tapping version

    return (true);
}

/* send current clock time
 */
static bool getWiFiTime (WiFiClient &client, char *unused)
{
    (void) unused;

    // send html header
    startPlainText(client);

    // report user's idea of time
    char buf[100];
    time_t t = nowWO();
    int yr = year (t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour (t);
    int mn = minute (t);
    int sc = second (t);
    int bl = snprintf (buf, sizeof(buf)-10, _FX("Clock_UTC %d-%02d-%02dT%02d:%02d:%02d"),
                        yr, mo, dy, hr, mn, sc);

    // indicate any time offset
    int32_t off = utcOffset();
    if (off == 0) {
        buf[bl++] = 'Z';                        // append Z if time really is UTC
        buf[bl] = 0;
    } else
        sprintf (buf+bl, " %+g", off/3600.0);   // else show offset in hours
    client.println (buf);

    return (true);
}

/* remote command to set and start the count down timer.
 */
static bool setWiFiCountdown (WiFiClient &client, char line[])
{
    // crack
    int minutes = atoi(line);

    // engage
    startCountdown(minutes * 60000);            // mins -> ms

    // ack
    startPlainText (client);
    client.println (minutes);
    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient &client, char line[])
{
    #if defined(_SUPPORT_BR)

        // parse
        if (strncasecmp (line, "on ", 3) == 0)
            brightnessOn();
        else if (strncasecmp (line, "off ", 4) == 0)
            brightnessOff();
        else {
            strcpy_P (line, PSTR("Specify on or off"));
            return (false);
        }

        // ack with same state
        char *blank = strchr (line, ' ');           // can't fail because blank found above
        *blank = '\0';
        startPlainText (client);
        FWIFIPR (client, F("display "));
        client.println (line);

        // ok
        return (true);

    #else 

        strcpy_P (line, PSTR("Not supported"));
        return (false);

    #endif
}

/* remote command to set display on/off/idle times
 */
static bool setWiFiDisplayTimes (WiFiClient &client, char line[])
{

    #if defined(_SUPPORT_BR)

        // parse
        int on_hr, on_mn, off_hr, off_mn, idle_mins;
        if (sscanf (line, _FX("on=%d:%d&off=%d:%d&idle=%d"), &on_hr, &on_mn, &off_hr, &off_mn, &idle_mins) != 5) {
            line[0] = '\0';         // code for Garbled command
            return (false);
        }

        // set
        if (!setDisplayTimes (on_hr*60+on_mn, off_hr*60+off_mn, idle_mins)) {
            strcpy_P (line, PSTR("Not supported"));
            return (false);
        }

        // ack
        startPlainText (client);
        const char hm_fmt[] = "%02d:%02d";
        char buf[100];

        FWIFIPR (client, F("DisplayOn  "));
        sprintf (buf, hm_fmt, on_hr, on_mn);
        client.println (buf);

        FWIFIPR (client, F("DisplayOff "));
        sprintf (buf, hm_fmt, off_hr, off_mn);
        client.println (buf);

        FWIFIPR (client, F("IdleMins   "));
        client.println (idle_mins);

        // ok
        return (true);

    #else 

        strcpy_P (line, PSTR("Not supported"));
        return (false);

    #endif
}

/* helper to set DE or DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDEDX_helper (WiFiClient &client, bool new_dx, char line[])
{
    LatLong ll;

    // crack
    float lat, lng;
    if (sscanf(line, "lat=%f&lng=%f", &lat, &lng) != 2 || lng < -180 || lng >= 180 || lat < -90 || lat > 90) {
        line[0] = '\0';             // code for Garbled command
        return (false);
    }
    ll.lat_d = lat;
    ll.lng_d = lng;

    // engage -- including normalization
    if (new_dx)
	newDX (ll, NULL);
    else
	newDE (ll);

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, new_dx));
}

/* set DE from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDE (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX_helper (client, false, line));
}

/* set DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDX (WiFiClient &client, char line[])
{
    return (setWiFiNewDEDX_helper (client, true, line));
}

/* set DE or DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewGrid_helper (WiFiClient &client, bool new_dx, char line[])
{
    Serial.println (line);

    // check and convert
    if (strlen (line) < 5 || line[4] != ' ') {
        strcpy_P (line, PSTR("Grid must be 4 chars"));
	return (false);
    }
    line[4] = '\0';
    LatLong ll;
    if (!maidenhead2ll (ll, line)) {
        strcpy_P (line, PSTR("Invalid grid"));
        return (false);
    }

    // engage
    if (new_dx)
	newDX (ll, NULL);
    else
	newDE (ll);

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, new_dx));
}

/* set DE from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDEGrid (WiFiClient &client, char line[])
{
    return (setWiFiNewGrid_helper (client, false, line));
}

/* set DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDXGrid (WiFiClient &client, char line[])
{
    return (setWiFiNewGrid_helper (client, true, line));
}


/* given a pane and function choice, return whether it is currently viable.
 * switch is required because each setPlotN has a different signature.
 * N.B. we assume 1 <= pane <= N_PANES
 */
static bool setWiFiPane_helper (int pane, uint8_t func)
{
    switch (pane) {
    case 1: return (setPlot1 ((PLOT1_Choices)func));
    case 2: return (setPlot2 ((PLOT2_Choices)func));
    case 3: return (setPlot3 ((PLOT3_Choices)func));
    default: return (false);
    }
}

/* set one of the plot panes to the given function if possible.
 * return whether ok
 */
static bool setWiFiPane (WiFiClient &client, char line[])
{

    // crack and scrub components
    int pane;           // 1-based !!
    char name[30];
    if (sscanf (line, "Pane%d=%20s", &pane, name) != 2) {
        line[0] = '\0';         // code for Garbled command
        return (false);
    }

    if (pane < 1 || pane > N_PANES) {
        strcpy_P (line, PSTR("Bad pane no"));
        return (false);
    }

    // try to do it -- remember pane is 1-based
    uint8_t n_names = pane_tables[pane-1].n_names;
    PaneNameTable *ptp = pane_tables[pane-1].ptp;
    const char *table_name = NULL;
    bool set_ok = false;
    for (uint8_t i = 0; i < n_names; i++) {
        if (!strcasecmp (name, ptp[i].name)) {
            table_name = ptp[i].name;
            set_ok = setWiFiPane_helper (pane, ptp[i].func);
            break;
        }
    }

    // report if not acceptable
    if (!table_name) {
        sprintf (line, _FX("Unknown choice for pane %d"), pane);
        return (false);
    }
    if (!set_ok) {
        sprintf (line, _FX("%s unavailable for pane %d"), table_name, pane);
        return (false);
    }

    // ack, same format as get_config
    char buf[40];
    startPlainText (client);
    snprintf (buf, sizeof(buf), "Pane%d %s\n", pane, table_name);
    client.print(buf);

    // good
    return (true);
}



/* try to set the satellite to the given name.
 * return whether command is successful.
 */
static bool setWiFiSatName (WiFiClient &client, char line[])
{
    resetWatchdog();

    // replace any %20
    replaceBlankEntity (line);

    // remove trailing HTTP, if any (curl sends it, chrome doesn't)
    char *http = strstr (line, " HTTP");
    if (*http)
        *http = '\0';

    // do it
    if (setSatFromName (line))
        return (getWiFiSatellite (client, line));

    // nope
    strcpy_P (line, PSTR("Unknown sat"));
    return (false);
}

/* set satellite from given TLE: set_sattle?name=n&t1=line1&t2=line2
 * return whether command is successful.
 */
static bool setWiFiSatTLE (WiFiClient &client, char line[])
{
    resetWatchdog();

    // find components
    char *name = strstr (line, "name=");
    char *t1 = strstr (line, "&t1=");
    char *t2 = strstr (line, "&t2=");
    if (!name || !t1 || !t2) {
        line[0] = '\0';         // code for Garbled command
        return (false);
    }

    // break into proper separate strings
    name += 5; *t1 = '\0';
    t1 += 4; *t2 = '\0';
    t2 += 4;

    // replace %20 with blanks
    replaceBlankEntity (name);
    replaceBlankEntity (t1);
    replaceBlankEntity (t2);

    // enforce known line lengths
    size_t t1l = strlen(t1);
    if (t1l < TLE_LINEL-1) {
        strcpy_P (line, PSTR("t1 short"));
        return(false);
    }
    t1[TLE_LINEL-1] = '\0';
    size_t t2l = strlen(t2);
    if (t2l < TLE_LINEL-1) {
        strcpy_P (line, PSTR("t2 short"));
        return(false);
    }
    t2[TLE_LINEL-1] = '\0';

    // try to install
    if (setSatFromTLE (name, t1, t2))
	return (getWiFiSatellite (client, line));

    // nope
    strcpy_P (line, PSTR("Bad spec"));
    return (false);
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient &client, char line[])
{
    resetWatchdog();

    int yr, mo, dy, hr, mn, sc;

    if (strncmp (line, "Now", 3) == 0) {

	changeTime (0);

    } else if (strncmp (line, "unix=", 5) == 0) {

	// crack and engage
	changeTime (atol(line+5));

    } else if (sscanf (line, _FX("ISO=%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) == 6) {

	// reformat
	tmElements_t tm;
	tm.Year = yr - 1970;
	tm.Month = mo;
	tm.Day = dy;
	tm.Hour = hr;
	tm.Minute = mn;
	tm.Second = sc;

	// convert and engage
	changeTime (makeTime(tm));

    } else {

        line[0] = '\0';         // code for Garbled command
	return (false);
    }

    // reply
    startPlainText(client);
    char buf[30];
    snprintf (buf, sizeof(buf), "UNIX_time %ld\n", nowWO());
    client.print (buf);

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient &client, char line[])
{
    // crack raw screen x and y
    int x, y, h = 0;
    if (sscanf (line, "x=%d&y=%d&hold=%d", &x, &y, &h) < 2) {
        line[0] = '\0';         // code for Garbled command
        return (false);
    }

    // must be over display
    if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {
        strcpy_P (line, PSTR("Invalid range"));
        return (false);
    }

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = h ? TT_HOLD : TT_TAP;

    // ack
    startPlainText (client);
    FWIFIPR (client, F("Touch_x "));
    client.println (wifi_tt_s.x);
    FWIFIPR (client, F("Touch_y "));
    client.println (wifi_tt_s.y);

    // ok
    return (true);
}

/* service remote connection.
 * of ro, only accept get commands and set_touch
 */
static void serveRemote(WiFiClient &client, bool ro)
{
    /* table of command strings, each implementing function and additional info for help.
     * functions are called with user input string beginning just after the command.
     * N.B. functions returning false shall replace the same string with a brief error message.
     *      functions returning true shall send http reply to client.
     * N.B. can't use static PROGMEM because PSTR is a runtime expression!
     */
    typedef struct {
        PGM_P command;                                  // GET command, including delim
        bool (*funp)(WiFiClient &client, char *line);   // function to implement
        PGM_P help;                                     // more info after command, if any
    } CmdTble;
    const CmdTble command_table[] = {
        { PSTR("get_capture.bmp "),   getWiFiScreenCapture,  NULL },
        { PSTR("get_config.txt "),    getWiFiConfig,         NULL },
        { PSTR("get_countdown.txt "), getWiFiCountdown,      NULL },
        { PSTR("get_de.txt "),        getWiFiDEInfo,         NULL },
        { PSTR("get_dx.txt "),        getWiFiDXInfo,         NULL },
        { PSTR("get_dxspots.txt "),   getWiFiDXSpots,        NULL },
        { PSTR("get_satellite.txt "), getWiFiSatellite,      NULL },
        { PSTR("get_sensors.txt "),   getWiFiSensorInfo,     NULL },
        { PSTR("get_sys.txt "),       getWiFiSys,            NULL },
        { PSTR("get_time.txt "),      getWiFiTime,           NULL },
        { PSTR("set_countdown?"),     setWiFiCountdown,      PSTR("minutes") },
        { PSTR("set_displayOnOff?"),  setWiFiDisplayOnOff,   PSTR("on|off") },
        { PSTR("set_displayTimes?"),  setWiFiDisplayTimes,   PSTR("on=HR:MN&off=HR:MN&idle=mins") },
        { PSTR("set_newde?"),         setWiFiNewDE,          PSTR("lat=X&lng=Y") },
        { PSTR("set_newdegrid?"),     setWiFiNewDEGrid,      PSTR("AB12") },
        { PSTR("set_newdx?"),         setWiFiNewDX,          PSTR("lat=X&lng=Y") },
        { PSTR("set_newdxgrid?"),     setWiFiNewDXGrid,      PSTR("AB12") },
        { PSTR("set_pane?"),          setWiFiPane,           PSTR("Pane[123]=XXX") },
        { PSTR("set_satname?"),       setWiFiSatName,        PSTR("abc|none") },
        { PSTR("set_sattle?"),        setWiFiSatTLE,         PSTR("name=abc&t1=line1&t2=line2") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("ISO=YYYY-MM-DDTHH:MM:SS") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("Now") },
        { PSTR("set_time?"),          setWiFiTime,           PSTR("unix=secs_since_1970") },
        { PSTR("set_touch?"),         setWiFiTouch,          PSTR("x=X&y=Y&hold=[0,1]") },
        { PSTR("restart "),           doWiFiReboot,          NULL },
        { PSTR("updateVersion "),     doWiFiUpdate,          NULL },
    };
    #define N_CT NARRAY(command_table)          // n entries in command table

    StackMalloc line_mem(TLE_LINEL*3);          // accommodate longest query, probably set_sattle
    char *line = (char *) line_mem.getMem();    // handy access to malloced buffer
    char *skipget = line+5;                     // handy location within line[] after "GET /"

    // first line should be the GET
    if (!getTCPLine (client, line, line_mem.getSize(), NULL)) {
        sendHTTPError (client, "empty web query");
        goto out;
    }
    if (strncmp (line, "GET /", 5)) {
        Serial.println (line);
        sendHTTPError (client, "Method Not Allowed");
        goto out;
    }

    // discard remainder of header
    (void) httpSkipHeader (client);

    Serial.print (F("Command from "));
        Serial.print(client.remoteIP());
        Serial.print(F(": "));
        Serial.println(line);


    // search for command depending on context, execute its implementation function if found
    if (!ro || !strncmp (skipget, "get_", 4) || !strncmp (skipget, "set_touch", 9)) {
        resetWatchdog();
        for (uint8_t i = 0; i < N_CT; i++) {
            const CmdTble *ctp = &command_table[i];
            size_t cmd_len = strlen_P (ctp->command);
            if (strncmp_P (skipget, ctp->command, cmd_len) == 0) {
                // found command so run its implenting function passing string after command
                if (!(*ctp->funp)(client, skipget+cmd_len))
                    sendHTTPError (client, skipget+cmd_len);
                goto out;
            }
        }
    }

    // if get here, command was not found so list help
    startPlainText(client);
    for (uint8_t i = 0; i < N_CT; i++) {
        const CmdTble *ctp = &command_table[i];

        // skip if not available for ro
        if (ro && strncmp_P ("get_", ctp->command, 4) && strncmp_P ("set_touch", ctp->command, 9))
            continue;

        client.print (FPSTR(ctp->command));
        if (ctp->help)
            client.println (FPSTR(ctp->help));
        else
            client.println();

        // also list function names for get_config
        if (ctp->funp == setWiFiPane) {
            for (uint8_t p = 0; p < N_PANES; p++) {
                char buf[30];
                PaneNameTable *ptp = pane_tables[p].ptp;
                uint8_t n_names = pane_tables[p].n_names;
                snprintf (buf, sizeof(buf), "  Pane%d=", p+1);
                client.print(buf);
                for (uint8_t n = 0; n < n_names; n++) {
                    snprintf (buf, sizeof(buf), "%s%c", ptp[n].name, n < n_names-1 ? ',' : '\n');
                    client.print(buf);
                }
            }
        }
    }

  out:

    client.stop();
    printFreeHeap (F("serveRemote"));
}

void checkWebServer()
{
    // check if someone is trying to tell/ask us something
    WiFiClient client = remoteServer.available();
    if (client)
	serveRemote(client, false);
}

void initWebServer()
{
    resetWatchdog();
    remoteServer.begin();
}

/* like readCalTouch() but also checks for remote web server touch.
 * N.B. use only for non-main pages like stopwatch, sat selection, etc.
 */
TouchType readCalTouchWS (SCoord &s)
{
    // avoid main screen update from getTCPLine()
    hideClocks();

    // check for remote command
    WiFiClient client = remoteServer.available();
    if (client)
	serveRemote(client, true);

    // return remote else local touch
    TouchType tt;
    if (wifi_tt != TT_NONE) {
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
    } else {
        tt = readCalTouch (s);
    }
    return (tt);
}

