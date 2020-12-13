/* handle the DX Cluster display.
 *
 * Clusters:
 *   [ ] support DXSpider only
 *   [ ] code for AR-Cluster exists but it is not active -- see comment below.
 *
 * WSJT-X:
 *   [ ] packet definition: https://github.com/roelandjansen/wsjt-x/blob/master/NetworkMessage.hpp
 *   [ ] We don't actually enforce the Status ID to be WSJT-X so this may also work for, say, JTDX.
 */

#include "HamClock.h"



// uncomment if want to try AR Cluster support
// #define _SUPPORT_ARCLUSTER

/* AR-Cluster commands are inconsistent but we have attempted to implement version 6. But worse, results
 * from "show heading" are unreliable, often, but not always, due to a sign error in longitude. This is not
 * likely to get fixed because it seems the author is SK : https://www.qrz.com/db/ab5k
 * 
 * Example of poor location:
 *
 * telnet dxc.nc7j.com 7373                                                          // AR-Cluster
 *   set station grid DM42jj
 *   set station latlon 32 0 N -111 0 W
 *   show heading ut7lw
 *   Heading/distance to: UT7LW/Ukraine   48 deg/228 lp   3873 mi/6233 km            // N Atlantic!
 *
 * telnet dxc.ww1r.com 7300                                                          // Spider
 *   set/qra DM42jj
 *   set/location 32 0 N -111 0 W
 *   show/heading ut7lw
 *   UT Ukraine-UR: 23 degs - dist: 6258 mi, 10071 km Reciprocal heading: 329 degs   // reasonable
 *
 *
 * Examples of some command variations:
 *
 * telnet dxc.nc7j.com 7373
 *
 *   NC7J AR-Cluster node version 6.1.5123
 *   *** TAKE NOTE! AR-Cluster 6 Commands ***
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   show/heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *
 * telnet w6cua.no-ip.org 7300
 *
 *   Welcome to the W6CUA AR-Cluster Telnet port in Castro Valley, Ca.
 *   
 *   Your name is Elwood.  Is this correct? (Y or N) >
 *   Your QTH is Tucson, AZ.  Is this correct? (Y or N) >
 *   
 *   Please set your latitude/longitude information with SET/LOCATION.  Thank you!
 *   
 *   set/qra DM42jj
 *   QRA set to DM42jj
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  24 deg (LP 204) 6500 mi (10458 km) from W6CUA
 *   
 *   set/location 30 00 N 110 0 W
 *   Lat/Lon set to 30 00 N 110 0 W
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  32 deg (LP 212) 6629 mi (10666 km) from WB0OEW
 *   
 *   logout/back in: no questions, it remembered everything
 *
 *   logout/back in: give fictious call
 *
 *   Welcome to the W6CUA AR-Cluster node Telnet port!
 *   Please enter your call: w0oew
 *
 *   Please enter your name
 *   set/qra DM42jj
 *   Your name is set/qra DM42jj.  Is this correct? (Y or N) >
 *   set/qra DM42jj
 *   Please enter your name
 *    
 *
 * telnet dxc.ai9t.com 7373
 *    
 *   Running AR-Cluster Version 6 Software 
 *    
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *    
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *    
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6567 mi/10569 km
 *
 */



#define TITLE_COLOR     RA8875_GREEN
#define LISTING_COLOR   RA8875_WHITE

#define DX_TIMEOUT      60000           // send line feed if idle this long, millis
#define MAX_AGE         300000          // max age to restore spot in list, millis
#define TITLE_Y0        27              // title dy, match VOACAP title position
#define HOSTNM_Y0       32              // host name y down from box top
#define LISTING_Y0      47              // first spot y down from box top
#define LISTING_DY      16              // listing row separation
#define FONT_H          7               // listing font height
#define FONT_W          6               // listing font width
#define DWELL_MS        5000            // period to show non-fatal message, ms
#define LISTING_N       ((PLOTBOX_H - LISTING_Y0)/LISTING_DY)       // max n list rows
#define MAX_HOST_LEN    ((plot2_b.w-2)/FONT_W)                      // max host name len
#define LISTING_Y(r)    (plot2_b.y + LISTING_Y0 + (r)*LISTING_DY)   // screen y for listing row r
#define LISTING_R(Y)    (((Y)+LISTING_DY/2-FONT_H/2-plot2_b.y-LISTING_Y0)/LISTING_DY) // row from screen Y

static WiFiClient dx_client;            // persistent TCP connection while displayed ...
static WiFiUDP wsjtx_server;            // or persistent UDP "connection" to WSJT-X client program
static uint32_t last_dxaction;          // time of most recent dx cluster or user activity, millis()

static DXSpot spots[LISTING_N];
static uint8_t n_spots;                 // n spots already displayed

typedef enum {
    CT_UNKNOWN,
    CT_ARCLUSTER,
    CT_DXSPIDER,
    CT_WSJTX,
} ClusterType;
static ClusterType cl_type;


static bool addDXSpot (float kHz, const char call[], uint32_t grid, uint16_t ut);
static void engageRow (DXSpot &s);



static void dxcTrace (const char *buf)
{
        Serial.printf (_FX("DXC: %s\n"), buf);
}

static void setDXSpotMapPosition (DXSpot &s)
{
        char prefix[MAX_PREF_LEN];
        char *tag;

        if (plotSpotCallsigns())
            tag = s.call;
        else {
            call2Prefix (s.call, prefix);
            tag = prefix;
        }

        SCoord center;
        ll2s (s.ll, center, 0);
        setMapTagBox (tag, center, 0, s.map_b);
}

static void drawSpotOnMap (DXSpot &s)
{
        if (mapDXSpots()) {
            if (plotSpotCallsigns()) {
                drawMapTag (s.call, s.map_b);
            } else {
                char prefix[MAX_PREF_LEN];
                call2Prefix (s.call, prefix);
                drawMapTag (prefix, s.map_b);
            }
        }
}

#if 0
static void eraseSpotOnMap (const DXSpot &s)
{
        for (uint16_t y = s.map_b.y; y <= s.map_b.y + s.map_b.h; y++)
            for (uint16_t x = s.map_b.x; x <= s.map_b.x + s.map_b.w; x++)
                drawMapCoord (x, y);
}
#endif

/* given address of pointer into a WSJT-X message, extract bool and advance pointer to next field.
 */
static bool wsjtx_bool (uint8_t **bpp)
{
        bool x = **bpp > 0;
        *bpp += 1;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract uint32_t and advance pointer to next field.
 * bytes are big-endian order.
 */
static uint32_t wsjtx_quint32 (uint8_t **bpp)
{
        uint32_t x = ((*bpp)[0] << 24) | ((*bpp)[1] << 16) | ((*bpp)[2] << 8) | (*bpp)[3];
        *bpp += 4;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract utf8 string and advance pointer to next field.
 * N.B. returned string points into message so will only be valid as long as message memory is valid.
 */
static char *wsjtx_utf8 (uint8_t **bpp)
{
        // save begining of this packet entry
        uint8_t *bp0 = *bpp;

        // decode length
        uint32_t len = wsjtx_quint32 (bpp);

        // check for flag meaning null length string same as 0 for our purposes
        if (len == 0xffffffff)
            len = 0;

        // advance packet pointer over contents
        *bpp += len;

        // copy contents to front, overlaying length, to make room to add EOS
        memmove (bp0, bp0+4, len);
        bp0[len] = '\0';

        // Serial.printf (_FX("DXC: utf8 %d '%s'\n"), len, (char*)bp0);

        // return address of content now within packet
        return ((char *)bp0);
}

/* given address of pointer into a WSJT-X message, extract double and advance pointer to next field.
 */
static uint64_t wsjtx_quint64 (uint8_t **bpp)
{
        uint64_t x;

        x = ((uint64_t)(wsjtx_quint32(bpp))) << 32;
        x |= wsjtx_quint32 (bpp);

        return (x);
}

/* return whether the given packet contains a WSJT-X Status packet.
 * if true, leave *bpp positioned just after ID.
 */
static bool wsjtxIsStatusMsg (uint8_t **bpp)
{
        resetWatchdog();

        // crack magic header
        uint32_t magic = wsjtx_quint32 (bpp);
        // Serial.printf (_FX("DXC: magic 0x%x\n"), magic);
        if (magic != 0xADBCCBDA) {
            Serial.println (F("DXC: packet received but wrong magic"));
            return (false);
        }

        // crack and ignore the max schema value
        (void) wsjtx_quint32 (bpp);                         // skip past max schema

        // crack message type. we only care about Status messages which are type 1
        uint32_t msgtype = wsjtx_quint32 (bpp);
        // Serial.printf (_FX("DXC: type %d\n"), msgtype);
        if (msgtype != 1)
            return (false);

        // if we get this far assume packet is what we want.
        // crack ID but ignore to allow compatibility with clones.
        volatile char *id = wsjtx_utf8 (bpp);
        (void)id;           // lint
        // Serial.printf (_FX("DXC: id '%s'\n"), id);
        // if (strcmp ("WSJT-X", id) != 0)
            // return (false);

        // ok!
        return (true);
}

/* parse and process WSJT-X message known to be Status.
 * *bpp is positioned just after ID field.
 */
static void wsjtxParseStatusMsg (uint8_t **bpp)
{
        resetWatchdog();
        // Serial.println (_FX("DXC: Parsing status"));

        // crack remaining fields down to DX grid
        uint64_t dial_freq = wsjtx_quint64 (bpp);           // capture Hz
        (void) wsjtx_utf8 (bpp);                            // skip over mode
        char *dx_call = wsjtx_utf8 (bpp);                   // capture DX call
        (void) wsjtx_utf8 (bpp);                            // skip over report
        (void) wsjtx_utf8 (bpp);                            // skip over Tx mode
        (void) wsjtx_bool (bpp);                            // skip over Tx enabled flag
        (void) wsjtx_bool (bpp);                            // skip over transmitting flag
        (void) wsjtx_bool (bpp);                            // skip over decoding flag
        (void) wsjtx_quint32 (bpp);                         // skip over Rx DF -- not always correct
        (void) wsjtx_quint32 (bpp);                         // skip over Tx DF
        (void) wsjtx_utf8 (bpp);                            // skip over DE call
        (void) wsjtx_utf8 (bpp);                            // skip over DE grid
        char *dx_grid = wsjtx_utf8 (bpp);                   // capture DX grid

        // Serial.printf (_FX("DXC: dial freq %lu\n"), dial_freq);
        // Serial.printf (_FX("DXC: dx call %s\n"), dx_call);
        // Serial.printf (_FX("DXC: dx grid %s\n"), dx_grid);

        // ignore if frequency is clearly bogus (which I have seen)
        if (dial_freq == 0)
            return;

        // prep grid if valid
        LatLong ll;
        if (!maidenhead2ll (ll, dx_grid)) {
            // Serial.printf (_FX("DXC: %s invalid grid: %s\n"), dx_call, dx_grid);
            return;
        }
        uint32_t grid;
        memcpy (&grid, dx_grid, 4);

        // prep current UT time
        int hr = hour();
        int mn = minute();
        uint16_t ut = hr*100 + mn;

        // add to list with actual frequency and set DX if new
        if (addDXSpot (dial_freq*1e-3, dx_call, grid, ut)) {                  // Hz to kHz
            // Serial.printf (_FX("DXC: WSJT-X %s @ %s\n"), dx_call, dx_grid);
            engageRow (spots[n_spots-1]);
        }

        // printFreeHeap(F("wsjtxParseStatusMsg"));
}

/* convert any upper case letter in str to lower case IN PLACE
 */
static void strtolower (char *str)
{
        for (char c = *str; c != '\0'; c = *++str)
            if (isupper(c))
                *str = tolower(c);
}

/* draw a spot at the given row
 */
static void drawSpotOnList (uint8_t row)
{
        DXSpot *sp = &spots[row];
        char line[50];

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(LISTING_COLOR);

        uint16_t x = plot2_b.x+4;
        uint16_t y = LISTING_Y(row);
        tft.fillRect (x, y, plot2_b.w-5, LISTING_DY-1, RA8875_BLACK);
        tft.setCursor (x, y);

        // pretty freq, fixed 8 chars
        const char *f_fmt = sp->freq < 1e6 ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, sp->freq);

        // add remaining fields
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u"), MAX_DXSPOTCALL_LEN-1, sp->call, sp->uts);
        tft.print (line);
}

/* display the given error message and shut down the connection.
 */
static void showClusterErr (const char *msg)
{
        // erase list area
        tft.fillRect (plot2_b.x+1, HOSTNM_Y0+10, plot2_b.w-2, plot2_b.h-HOSTNM_Y0-10-1, RA8875_BLACK);

        // show message
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(RA8875_RED);
        uint16_t mw = getTextWidth ((char*)msg);
        tft.setCursor (plot2_b.x + (plot2_b.w-mw)/2, LISTING_Y(2));
        tft.print (msg);

        // log too
        dxcTrace (msg);

        // shut down connection
        closeDXCluster();
}

/* read cluster into buf until see a line containing the given string. 
 * intended for seeking command responses.
 */
static bool lookForDXString (char *buf, uint16_t bufl, const char *str)
{
        for (int i = 0; i < 3; i++)
            if (getTCPLine (dx_client, buf, bufl, NULL) && strstr (buf, str))
                return (true);

        dxcTrace (_FX("Failed to find cluster response"));
        return (false);
}

/* given heading from DE in degrees E of N, dist in miles, return lat degs +N and longitude degs +E
 */
static void findLLFromDEHeadingDist (float heading, float miles, LatLong &ll)
{
        float A = deg2rad(heading);
        float b = miles/ERAD_M;             // 2Pi * miles / (2Pi*ERAD_M)
        float cx = de_ll.lat;               // really (Pi/2 - lat) then exchange sin/cos
        float ca, B;                        // cos polar angle, delta lng
        solveSphere (A, b, sinf(cx), cosf(cx), &ca, &B);
        ll.lat_d = rad2deg(asinf(ca));      // asin(ca) = Pi/2 - acos(ca)
        ll.lng_d = rad2deg(de_ll.lng + B);
        normalizeLL (ll);
}

/* search through buf for " <number> str" followed by non-alnum.
 * if found set *valuep to number and return true, else return false.
 */
static bool findLabeledValue (const char *buf, int *valuep, const char *str)
{
        size_t strl = strlen(str);

        for (; *buf; buf++) {
            if (*buf == ' ' && isdigit(buf[1])) {
                // found start of a number: crack then look for str to follow
                char *vend;
                int v = strtol (buf, &vend, 10);
                if (*vend++ == ' ' && strncmp (vend, str, strl) == 0 && !isalnum(vend[strl])) {
                    // found it
                    *valuep = v;
                    return (true);
                }
            }
        }

        return (false);
}


/* given a call sign return its lat/long by querying dx_client.
 * technique depends on cl_type.
 * return whether successful.
 */
static bool getDXSpotLL (const char *call, LatLong &ll)
{
        char buf[120];

        if (cl_type == CT_DXSPIDER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show/heading %s"), call);
            dxcTrace (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXString (buf, sizeof(buf), "degs"))
                return (false);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show heading %s"), call);
            dxcTrace (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXString (buf, sizeof(buf), "distance"))
                return (false);

    #endif // _SUPPORT_ARCLUSTER

        } else {

            Serial.printf (_FX("Bug! cl_type= %d\n"), cl_type);
            return (false);
        }



        // if get here we should have a line containing <heading> deg .. <miles> mi
        // strcpy(buf,"9miW8WTS Michigan-K: 71 degs - dist: 790 mi, 1272 km Reciprocal heading: 261 degs");
        dxcTrace (buf);
        strtolower(buf);
        int heading, miles;
        if (findLabeledValue (buf, &heading, "degs") && findLabeledValue (buf, &miles, "mi")) {
            findLLFromDEHeadingDist (heading, miles, ll);
            Serial.printf (_FX("DXC: %s heading= %d miles= %d lat= %g lon= %g\n"), call,
                                                                    heading, miles, ll.lat_d, ll.lng_d);
        } else {
            Serial.println (F("DXC: No heading"));
            return (false);
        }

        // if get here it worked!
        return (true);
}

/* try to connect to the DX cluster defined by getDXClusterHost():getDXClusterPort().
 * if success: dx_client or wsjtx_server is live and return true,
 * else: both are closed, display error msg, return false.
 * if called while already connected just return true immediately.
 */
static bool connectDXCluster()
{
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        // just continue if already connected
        if (isDXConnected()) {
            Serial.printf (_FX("DXC: Resume %s:%d\n"), dxhost, dxport);
            return (true);
        }

        Serial.printf (_FX("DXC: Connecting to %s:%d\n"), dxhost, dxport);
        resetWatchdog();

        // decide type from host name
        if (!strcasecmp (dxhost, "WSJT-X") || !strcasecmp (dxhost, "JTDX")) {

            // create fresh UDP for WSJT-X
            wsjtx_server.stop();
            if (wsjtx_server.begin(dxport)) {

                // record and claim ok so far
                cl_type = CT_WSJTX;
                return (true);
            }

        } else {

            // open fresh
            dx_client.stop();
            if (wifiOk() && dx_client.connect(dxhost, dxport)) {

                // look alive
                resetWatchdog();
                updateClocks(false);
                dxcTrace (_FX("connect ok"));

                // assume we have been asked for our callsign
                dx_client.println (getCallsign());

                // read until find a line ending with '>', looking for clue about type of cluster
                uint16_t bl;
                StackMalloc buf_mem(200);
                char *buf = buf_mem.getMem();
                cl_type = CT_UNKNOWN;
                while (getTCPLine (dx_client, buf, buf_mem.getSize(), &bl)) {
                    // Serial.println (buf);
                    strtolower(buf);
                    if (strstr (buf, "dx") && strstr (buf, "spider"))
                        cl_type = CT_DXSPIDER;
    #if defined(_SUPPORT_ARCLUSTER)
                    else if (strstr (buf, "ar-cluster") && strstr (buf, "ersion") && strchr (buf, '6'))
                        cl_type = CT_ARCLUSTER;
    #endif // _SUPPORT_ARCLUSTER

                    if (buf[bl-1] == '>')
                        break;
                }

                if (cl_type == CT_UNKNOWN) {
                    showClusterErr (_FX("Cluster type unknown"));
                    return (false);
                }

                if (!sendDELLGrid()) {
                    showClusterErr (_FX("Failed sending DE grid"));
                    return (false);
                }

                // confirm still ok
                if (!dx_client) {
                    showClusterErr (_FX("Login failed"));
                    return (false);
                }

                // all ok so far
                return (true);
            }
        }

        // sorry
        showClusterErr (_FX("Connection failed"));    // also calls dx_client.stop()
        return (false);
}

/* add and display a new spot both on map and in list, scrolling list if already full.
 * use grid to get ll if set, else look up call to set both.
 * or return false if same spot again or some error.
 */
static bool addDXSpot (float kHz, const char call[], uint32_t grid, uint16_t ut)
{
        // skip if same station on same freq as previous
        if (n_spots > 0) {
            DXSpot *sp = &spots[n_spots-1];
            if (fabsf(kHz-sp->freq) < 0.1F && strcmp (call, sp->call) == 0)
                return (false);
        }

        // find next available row, scrolling if necessary
        if (n_spots == LISTING_N) {
            // scroll up, discarding top (first) entry
            for (uint8_t i = 0; i < LISTING_N-1; i++) {
                spots[i] = spots[i+1];
                drawSpotOnList (i);
            }
            n_spots = LISTING_N-1;
        }
        DXSpot *sp = &spots[n_spots];

        // store some
        sp->freq = kHz;
        memcpy (sp->call, call, MAX_DXSPOTCALL_LEN-1);      // preserve existing EOS
        sp->uts = ut;

        // store ll and grid some way
        char errmsg[50] = "";
        bool ok = false;
        if (grid) {
            // save grid then get ll from grid
            sp->grid = grid;
            char maid[5];
            unpackMaidToStr (maid, grid);
            ok = maidenhead2ll (sp->ll, maid);
            if (ok)
                Serial.printf (_FX("DXC: %s %s lat= %g lng= %g\n"),
                                        sp->call, maid, sp->ll.lat_d, sp->ll.lng_d);
            else
                snprintf (errmsg, sizeof(errmsg), _FX("%s bad grid: %s"), call, maid);
        } else {
            // get ll from cluster, then grid from ll
            char maid25[2][5];
            ok = getDXSpotLL (call, sp->ll);
            if (ok) {
                ll2maidenhead (maid25, sp->ll);
                memcpy (&sp->grid, maid25[0], 4);
            } else
                snprintf (errmsg, sizeof(errmsg), _FX("%s ll lookup failed"), call);
        }
        if (!ok) {
            // error set grid and ll to 0/0
            dxcTrace (errmsg);
            char maid[] = "JJ00";
            memcpy (&sp->grid, maid, 4);
            memset (&sp->ll, 0, sizeof(sp->ll));
            return (false);
        }


        // draw
        drawSpotOnList (n_spots);
        setDXSpotMapPosition (*sp);
        drawSpotOnMap (*sp);

        // ok
        n_spots++;
        return (true);
}

/* display the current cluster host and port in the given color
 */
static void showHostPort (uint16_t c)
{
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        char name[MAX_HOST_LEN];
        snprintf (name, sizeof(name), _FX("%s:%d"), dxhost, dxport);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(c);
        uint16_t nw = getTextWidth (name);
        tft.setCursor (plot2_b.x + (plot2_b.w-nw)/2, plot2_b.y + HOSTNM_Y0);
        tft.print (name);
}

/* set radio and DX from given row, known to be defined
 */
static void engageRow (DXSpot &s)
{
        // get ll 
        LatLong ll;

        if (cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) {

            // get LL from cluster
            if (!getDXSpotLL (s.call, ll))
                return;

        } else if (cl_type == CT_WSJTX) {

            // get LL from grid
            LatLong ll;
            char maid[5];
            memcpy (maid, &s.grid, 4);
            maid[4] = 0;
            if (!maidenhead2ll (ll, maid)) {
                Serial.printf (_FX("DXC: bogus grid %s for %s\n"), maid, s.call);
                return;
            }
        } else {

            Serial.printf (_FX("Bug! cl_type= %d\n"), cl_type);
            return;
        }

        // do it
        newDX (ll, s.call);
        setRadioSpot(s.freq);
}

/* send our lat/long and grid to dx_client, depending on cluster type.
 * return whether successful.
 * N.B. can be called any time so be prepared to do nothing if not appropriate.
 */
bool sendDELLGrid()
{
        if (!useDXCluster() || plot2_ch != PLOT2_DX || !dx_client)
            return (true);

        char buf[100];
        char maid4[5];

        // handy DE grid as string
        uint32_t mnv;
        NVReadUInt32 (NV_DE_GRID, &mnv);
        memcpy (maid4, &mnv, 4);
        maid4[4] = '\0';

        // handy DE lat/lon in common format
        char llstr[30];
        snprintf (llstr, sizeof(llstr), _FX("%.0f %.0f %c %.0f %.0f %c"),
                    fabsf(de_ll.lat_d), fmodf(60*fabsf(de_ll.lat_d), 60), de_ll.lat_d < 0 ? 'S' : 'N',
                    fabsf(de_ll.lng_d), fmodf(60*fabsf(de_ll.lng_d), 60), de_ll.lng_d < 0 ? 'W' : 'E');

        if (cl_type == CT_DXSPIDER) {

            // set grid
            snprintf (buf, sizeof(buf), _FX("set/qra %sjj"), maid4);    // fake 6-char grid
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXString (buf, sizeof(buf), ">")) {
                Serial.println (F("No > after set/qra"));
                return (false);
            }

            // set DE ll
            snprintf (buf, sizeof(buf), _FX("set/location %s"), llstr);
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXString (buf, sizeof(buf), ">")) {
                Serial.println (F("No > after set/loc"));
                return (false);
            }

            // ok!
            return (true);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // friendly turn off skimmer just avoid getting swamped
            strcpy_P (buf, PSTR("set dx filter not skimmer"));
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXString (buf, sizeof(buf), "filter"))
                return (false);

            // set grid
            snprintf (buf, sizeof(buf), _FX("set station grid %sjj"), maid4);    // fake 6-char grid
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXString (buf, sizeof(buf), "set to"))
                return (false);

            // set ll
            snprintf (buf, sizeof(buf), _FX("set station latlon %s"), llstr);
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXString (buf, sizeof(buf), "location"))
                return (false);

            // ok!
            return (true);

    #endif // _SUPPORT_ARCLUSTER

        }

        // fail
        return (false);
}

/* prep plot2_b and connect dx_client to a dx cluster or wsjtx_server
 */
void initDXCluster()
{
        if (!useDXCluster())
            return;

        // erase all except bottom line which is map border
        tft.fillRect (plot2_b.x, plot2_b.y, plot2_b.w, plot2_b.h-1, RA8875_BLACK);
        tft.drawRect (plot2_b.x, plot2_b.y, plot2_b.w, plot2_b.h, GRAY);

        // title
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor(TITLE_COLOR);
        tft.setCursor (plot2_b.x + 27, plot2_b.y + TITLE_Y0);
        tft.print (F("DX Cluster"));

        // show cluster host busy
        showHostPort (RA8875_YELLOW);

        // connect to dx cluster
        if (connectDXCluster()) {

            // ok: show host in green
            showHostPort (RA8875_GREEN);

            // restore known spots if not too old else reset list
            if (millis() - last_dxaction < MAX_AGE) {
                for (uint8_t i = 0; i < n_spots; i++)
                    drawSpotOnList (i);
            } else {
                n_spots = 0;
            }

            // init time
            last_dxaction = millis();

        } // else already displayed error message

        printFreeHeap(F("initDXCluster"));
}


/* try to reconnect if not already
 */
static void reconnect(bool now)
{
        if (!dx_client && !wsjtx_server) {
            static uint32_t last_retry;
            if (now || timesUp (&last_retry, 15000))
                initDXCluster();
        }
}

/* called repeatedly by main loop to drain cluster connection and show another DXCluster entry if any.
 */
void updateDXCluster()
{
        // skip if we are not up
        if (plot2_ch != PLOT2_DX || !useDXCluster())
            return;

        // insure connected
        reconnect(false);

        if ((cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) && dx_client) {

            // this works for both types of cluster

            // roll any new spots into list
            bool gotone = false;
            char line[120];
            char call[30];
            float kHz;
            while (dx_client.available() && getTCPLine (dx_client, line, sizeof(line), NULL)) {
                // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98

                // look alive
                updateClocks(false);
                resetWatchdog();

                // log but note some clusters embed \a bell in their reports, remove so they don't beep
                for (char *lp = line; *lp; lp++)
                    if (!isprint(*lp))
                        *lp = ' ';

                // crack
                if (sscanf (line, _FX("DX de %*s %f %10s"), &kHz, call) == 2) {
                    dxcTrace (line);

                    // looks like a spot, extract time also
                    char *utp = &line[70];
                    uint16_t ut = atoi(utp) % 2400;

                    // note and display
                    gotone = true;
                    (void) addDXSpot (kHz, call, 0, ut);
                }
            }

            // check for lost connection
            if (!dx_client.connected()) {
                showClusterErr (_FX("Lost connection"));
                return;
            }

            // send newline if it's been a while
            uint32_t t = millis();
            if (gotone) {
                last_dxaction = t;
            } else if (t - last_dxaction > DX_TIMEOUT) {
                last_dxaction = t;
                dxcTrace (_FX("feeding"));
                dx_client.print("\r\n");
            }

        } else if (cl_type == CT_WSJTX && wsjtx_server) {

            resetWatchdog();

            // drain ALL pending packets, retain most recent Status message if any

            uint8_t *any_msg = NULL;        // malloced if get a new packet of any type
            uint8_t *sts_msg = NULL;        // malloced if find Status msg

            int packet_size;
            while ((packet_size = wsjtx_server.parsePacket()) > 0) {
                // Serial.printf (_FX("DXC: WSJT-X size= %d heap= %d\n"), packet_size, ESP.getFreeHeap());
                any_msg = (uint8_t *) realloc (any_msg, packet_size);
                resetWatchdog();
                if (wsjtx_server.read (any_msg, packet_size) > 0) {
                    uint8_t *bp = any_msg;
                    if (wsjtxIsStatusMsg (&bp)) {
                        // save from bp to the end in prep for wsjtxParseStatusMsg()
                        int n_skip = bp - any_msg;
                        // Serial.printf (_FX("DXC: skip= %d packet_size= %d\n"), n_skip, packet_size);
                        sts_msg = (uint8_t *) realloc (sts_msg, packet_size - n_skip);
                        memcpy (sts_msg, any_msg + n_skip, packet_size - n_skip);
                    }
                }
            }

            // process then free newest Status message if received
            if (sts_msg) {
                uint8_t *bp = sts_msg;
                wsjtxParseStatusMsg (&bp);
                free (sts_msg);
            }

            // clean up
            if (any_msg)
                free (any_msg);
        }

}

/* insure cluster connection is closed
 */
void closeDXCluster()
{
        // make sure connection is closed
        if (dx_client) {
            dx_client.stop();
            Serial.printf (_FX("DXC: disconnect %s\n"), dx_client ? "failed" : "ok");
        }
        if (wsjtx_server) {
            wsjtx_server.stop();
            Serial.printf (_FX("DXC: WSTJ-X disconnect %s\n"), wsjtx_server ? "failed" : "ok");
        }
}

/* try to set DX from the touched spot.
 * return true if looks like user is interacting with the cluster, false if wants to change pane.
 */
bool checkDXTouch (const SCoord &s)
{
        // ours at all?
        if (plot2_ch != PLOT2_DX || !inBox (s, plot2_b))
            return (false);

        // tapping title always leaves this pane
        if (s.y < plot2_b.y + TITLE_Y0) {
            closeDXCluster();               // insure disconnected
            last_dxaction = millis();       // in case op wants to come back soon
            return (false);
        }

        // reconnect if off
        reconnect(true);

        // engage tapped row, if defined
        int click_row = LISTING_R(s.y);
        if (click_row >= 0 && click_row < n_spots && spots[click_row].call[0] != '\0' && isDXConnected())
            engageRow (spots[click_row]);

        // ours
        return (true);
}

/* pass back current spots list, and return whether enabled at all.
 * ok to pass back if not displayed because spot list is still intact.
 */
bool getDXSpots (DXSpot **spp, uint8_t *nspotsp)
{
        if (useDXCluster()) {
            *spp = spots;
            *nspotsp = n_spots;
            return (true);
        }

        return (false);
}

/* update map positions of all spots, eg, because the projection has changed
 */
void updateDXSpotScreenLocations()
{
        for (uint8_t i = 0; i < n_spots; i++)
            setDXSpotMapPosition (spots[i]);
}

/* draw all DX spots on map, if up
 */
void drawDXSpotsOnMap ()
{
        // skip if we are not up or don't want spots on map
        if (plot2_ch != PLOT2_DX || !useDXCluster() || !mapDXSpots())
            return;

        for (uint8_t i = 0; i < n_spots; i++)
            drawSpotOnMap (spots[i]);
}

/* return whether the given screen coord lies over any DX spot label.
 * N.B. we assume map_s are set
 */
bool overAnyDXSpots(const SCoord &s)
{
        // false for sure if spots are not on
        if (plot2_ch != PLOT2_DX || !useDXCluster())
            return (false);

        for (uint8_t i = 0; i < n_spots; i++)
            if (inBox (s, spots[i].map_b))
                return (true);

        return (false);
}

/* return whether cluster is currently connected
 */
bool isDXConnected()
{
        return (dx_client || wsjtx_server);
}
