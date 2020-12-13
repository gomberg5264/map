/* Get lat/long from gpsd daemon running on any host port 2947.
 *
 *   general info: https://gpsd.gitlab.io/gpsd/
 *   raw interface: https://gpsd.gitlab.io/gpsd/client-howto.html
 *
 * Simple server test, run this command:
 *   while true; do echo '"mode":2 "lat":34.567 "lon":-123.456 "time":"2020-01-02T03:04:05.000Z"' | nc -l 192.168.7.11 2947; done
 */

#include "HamClock.h"

#define GPSD_PORT       2947                            // tcp port


/* look for time and sufficient mode in the given string from gpsd.
 * if found, save in arg (ptr to a time_t) and return true, else return false.
 */
static bool lookforTime (const char *buf, size_t n_buf, void *arg)
{
        // quick check for fields
        if (n_buf < 43)
            return(false);
        const char *modestr = strstr (buf, "\"mode\":");
        if (!modestr || n_buf-(modestr-buf) < 10)
            return(false);
        const char *timestr = strstr (buf, "\"time\":");
        if (!timestr || n_buf-(timestr-buf) < 33)
            return(false);

        // start pessimistic
        bool ok = false;

        // crack fields
        int mode = atoi (modestr+7);
        if (mode >= 2) {

            // get time now so we can correct after we process and display
            uint32_t t0 = millis();

            // crack time form: "time":"2012-04-05T15:00:01.501Z"
            int yr, mo, dy, hr, mn, sc;
            if (sscanf (timestr+8, _FX("%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) != 6)
                return (false);
            Serial.printf (_FX("GPSD: %04d-%02d-%02dT%02d:%02d:%02d\n"), yr, mo, dy, hr, mn, sc);

            // reformat
            tmElements_t tm;
            tm.Year = yr - 1970;
            tm.Month = mo;
            tm.Day = dy;
            tm.Hour = hr;
            tm.Minute = mn;
            tm.Second = sc;
            time_t gpsd_time = makeTime (tm);

            // correct for time spent here
            gpsd_time += (millis() - t0 + 500)/1000;

            // good
            *(time_t*)arg = gpsd_time;
            ok = true;
        }

        // success?
        return (ok);
}

/* look for lat and lon and sufficient mode in the given string from gpsd.
 * if found, save in arg (ptr to LatLong) and return true, else return false and tell user if mode < 2
 */
static bool lookforLatLong (const char *buf, size_t n_buf, void *arg)
{

        // quick check for fields
        if (n_buf < 34)
            return (false);
        const char *modestr = strstr (buf, "\"mode\":");
        if (!modestr || n_buf-(modestr-buf) < 10)
            return (false);
        const char *latstr = strstr (buf, "\"lat\":");
        if (!latstr || n_buf-(latstr-buf) < 12)
            return (false);
        const char *lonstr = strstr (buf, "\"lon\":");
        if (!lonstr || n_buf-(lonstr-buf) < 12)
            return (false);

        // start pessimistic
        bool ok = false;

        // crack fields
        int mode = atoi(modestr+7);
        if (mode >= 2) {

            // store
            LatLong *llp = (LatLong *) arg;
            llp->lat_d = atof(latstr+6);
            llp->lng_d = atof(lonstr+6);

            // good
            Serial.printf (_FX("GPSD: lat %.2f long %.2f\n"), llp->lat_d, llp->lng_d);
            ok = true;
        }

        // success?
        return (ok);
}

/* connect to gpsd and return whether lookf() found what it wants.
 */
static bool getGPSDSomething(bool (*lookf)(const char *buf, size_t n_buf, void *arg), void *arg)
{
        // skip if no configured
        if (!useGPSD())
            return (false);

        // avoid hammering on gpsd too rapidly
        static uint32_t prev_t;
        if (!timesUp (&prev_t, 500))
            wdDelay(500);

        // get host name
        const char *host = getGPSDHost();
        Serial.printf (_FX("GPSD: trying %s:%d\n"), host, GPSD_PORT);

        // prep state
        WiFiClient gpsd_client;
        bool look_ok = false;
        bool connect_ok = false;
        bool got_something = false;

        // connect to and read from gpsd server, 
        // Serial.printf (_FX("GPSD: %s\n"), host);
        resetWatchdog();
        if (wifiOk() && gpsd_client.connect (host, GPSD_PORT)) {

            // initial progress
            connect_ok = true;

            // enable reporting and ask for immediate report
            gpsd_client.println (F("?WATCH={\"enable\":true,\"json\":true}"));
            gpsd_client.println (F("?POLL;"));

            // build buf, give to lookf, done when it's happy or no more
            StackMalloc buf_mem(3000);          // give up if not found after this much
            char *buf = (char *) buf_mem.getMem();
            size_t n_buf = 0;
            while (!look_ok && n_buf < buf_mem.getSize()-1 && getChar (gpsd_client, &buf[n_buf])) {
                got_something = true;
                buf[++n_buf] = '\0';
                look_ok = (*lookf)(buf, n_buf, arg);
            }
        }

        // finished with connection
        gpsd_client.stop();

        // report problems
        if (!look_ok) {
            if (got_something)
                Serial.println (F("GPSD: unexpected response"));
            else if (connect_ok)
                Serial.println (F("GPSD: connected but no response"));
            else
                Serial.println (F("GPSD: no connection"));
        }

        // success?
        return (look_ok);
}

/* return time from GPSD if available, else return 0
 */
time_t getGPSDUTC()
{
        time_t gpsd_time;

        if (getGPSDSomething (lookforTime, &gpsd_time))
            return (gpsd_time);
        return (0);
}

/* get lat/long from GPSD and set de_ll, return whether successful.
 */
bool getGPSDLatLong(LatLong *llp)
{
        return (getGPSDSomething (lookforLatLong, llp));
}
