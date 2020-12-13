/* this file manages the background maps, both static styles and VOACAP area propagation.
 *
 * On ESP:
 *    maps are stored in a LittleFS file system, accessed with file.seek and file.read with a cache
 * On all desktops:
 *    maps are stored in $HOME/.hamclock, accessed with mmap
 *
 * maps files are RGB565 BMP format.
 */


#include "HamClock.h"




/******************************************************************************************
 *
 * code common to ESP and UNIX
 *
 */

// BMP file format parameters
#define CORESZ 14                                       // always 14 bytes at front
#define HDRVER 108                                      // BITMAPV4HEADER, also n bytes in subheader
#define BHDRSZ (CORESZ+HDRVER)                          // total header size
#define BPBMPP 2                                        // bytes per BMP pixel


// match column headings in voacapx.out
float propMap2MHz (PropMapSetting pms)
{
        switch (pms) {
        case PROP_MAP_80M: return ( 3.6);
        case PROP_MAP_40M: return ( 7.1);
        case PROP_MAP_30M: return (10.1);
        case PROP_MAP_20M: return (14.1);
        case PROP_MAP_17M: return (18.1);
        case PROP_MAP_15M: return (21.1);
        case PROP_MAP_12M: return (24.9);
        case PROP_MAP_10M: return (28.2);
        default: Serial.printf (_FX("Bug! bad MHz PMS %d\n"), pms); return (0);
        }
}

int propMap2Band (PropMapSetting pms)
{
        switch (pms) {
        case PROP_MAP_80M: return (80);
        case PROP_MAP_40M: return (40);
        case PROP_MAP_30M: return (30);
        case PROP_MAP_20M: return (20);
        case PROP_MAP_17M: return (17);
        case PROP_MAP_15M: return (15);
        case PROP_MAP_12M: return (12);
        case PROP_MAP_10M: return (10);
        default: Serial.printf (_FX("Bug! bad Band PMS %d\n"), pms); return (0);
        }
}

/* ESP chip can't access unaligned 32 bit values
 */
static uint32_t unpackLE4 (char *buf)
{
        union {
            uint32_t le4;
            char a[4];
        } le4;

        le4.a[0] = buf[0];
        le4.a[1] = buf[1];
        le4.a[2] = buf[2];
        le4.a[3] = buf[3];

        return (le4.le4);
}

/* return whether the given header is the correct BMP format and the total expected file size.
 */
static bool bmpHdrOk (char *buf, uint32_t w, uint32_t h, uint32_t *filesizep)
{
        if (buf[0] != 'B' || buf[1] != 'M') {
            Serial.printf (_FX("Hdr err: 0x%02X 0x%02X\n"), (unsigned)buf[0], (unsigned)buf[1]);
            return (false);
        }

        *filesizep = unpackLE4(buf+2);
        uint32_t type = unpackLE4(buf+14);
        uint32_t nrows = - (int32_t)unpackLE4(buf+22);      // nrows<0 means display upside down
        uint32_t ncols = unpackLE4(buf+18);
        uint32_t pixbytes = unpackLE4(buf+34);

        if (pixbytes != nrows*ncols*BPBMPP || type != HDRVER || w != ncols || h != nrows) {
            Serial.printf (_FX("Hdr err: %d %d %d %d\n"), pixbytes, type, nrows, ncols);
            return (false);
        }

        return (true);
}


/* marshall the day and night file names and titles for the given style.
 * N.B. we do not check for suffient room in the arrays
 */
static void getMapNames (const char *style, char *dfile, char *nfile, char *dtitle, char *ntitle)
{
        sprintf (dfile, "/map-D-%dx%d-%s.bmp", HC_MAP_W, HC_MAP_H, style);
        sprintf (nfile, "/map-N-%dx%d-%s.bmp", HC_MAP_W, HC_MAP_H, style);

        sprintf (dtitle, _FX("%s D map"), style);
        sprintf (ntitle, _FX("%s N map"), style);
}

/* qsort-style compare two FS_Info by name
 */
static int fsinfoQsort (const void *p1, const void *p2)
{
    const FS_Info *fip1 = (FS_Info *)p1;
    const FS_Info *fip2 = (FS_Info *)p2;
    return (strcmp (fip1->name, fip2->name));
}





/******************************************************************************************
 *
 * ESP8266
 *
 */

#if defined(_IS_ESP8266)

#include "LittleFS.h"

/* LittleFS seek+read performance: read_ms = 88 + 0.23/byte
 * Thus longer caches help mercator but greatly slow azimuthal due to cache misses. Whole row is
 * a huge loss for azimuthal.
 */

// persistant state
#define N_CACHE_COLS     50                             // n read-ahead columns to cache
static File day_file, night_file;                       // open LittleFS file handles
static uint8_t *day_row_cache, *night_row_cache;        // row caches
static uint16_t day_cache_row, night_cache_row;         // which starting rows
static uint16_t day_cache_col, night_cache_col;         // which starting cols


/* download the given file of expected size and load into LittleFS.
 * client starts postioned at beginning of remote file.
 * ESP version
 */
static bool downloadMapFile (bool verbose, WiFiClient &client, const char *file, const char *title)
{
        resetWatchdog();

        const uint32_t npixbytes = HC_MAP_W*HC_MAP_H*BPBMPP;
        uint32_t nbufbytes = 0;

        bool ok = false;

        // alloc copy buffer
        #define COPY_BUF_SIZE 1024                      // > BHDRSZ but beware RAM pressure
        StackMalloc buf_mem(COPY_BUF_SIZE);
        char *copy_buf = (char *) buf_mem.getMem();
        if (!copy_buf) {
            tftMsg (verbose, 1000, _FX("%s: no mem\r"), title);
            return (false);
        }

        // create or rewrite file
        // extra close addresses garbage collection bug when overwriting existing file.
        File f = LittleFS.open (file, "w");
        if (f)
            f.close();
        f = LittleFS.open (file, "w");
        if (!f) {
            tftMsg (verbose, 1000, _FX("%s: FLASH create failed\r"), title);
            return (false);
        }

        // read and check remote header
        for (int i = 0; i < BHDRSZ; i++) {
            if (!getChar (client, &copy_buf[i])) {
                Serial.printf (_FX("short header: %.*s\n"), i, copy_buf); // might be err message
                tftMsg (verbose, 1000, _FX("%s: header is short\r"), title);
                goto out;
            }
        }
        uint32_t filesize;
        if (!bmpHdrOk (copy_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            Serial.printf (_FX("bad header: %.*s\n"), BHDRSZ, copy_buf); // might be err message
            tftMsg (verbose, 1000, _FX("%s: bad header\r"), title);
            goto out;
        }
        if (filesize != npixbytes + BHDRSZ) {
            Serial.printf (_FX("%s: wrong size %u != %u\n"), title, filesize, npixbytes);
            tftMsg (verbose, 1000, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // write header
        f.write (copy_buf, BHDRSZ);
        updateClocks(false);

        // copy pixels
        tftMsg (verbose, 500, _FX("%s: downloading\r"), title);
        for (uint32_t nbytescopy = 0; nbytescopy < npixbytes; nbytescopy++) {
            resetWatchdog();

            if (((nbytescopy%(npixbytes/10)) == 0) || nbytescopy == npixbytes-1)
                tftMsg (verbose, 0, _FX("%s: %3d%%\r"), title, 100*(nbytescopy+1)/npixbytes);

            // read more
            if (nbufbytes < COPY_BUF_SIZE && !getChar (client, &copy_buf[nbufbytes++])) {
                Serial.printf (_FX("%s: file is short: %u %u\n"), title, nbytescopy, npixbytes);
                tftMsg (verbose, 1000, _FX("%s: file is short\r"), title);
                goto out;
            }

            // write when copy_buf is full or last
            if (nbufbytes == COPY_BUF_SIZE || nbytescopy == npixbytes-1) {
                updateClocks(false);
                if (f.write (copy_buf, nbufbytes) != nbufbytes) {
                    tftMsg (verbose, 1000, _FX("%s: FLASH write failed\r"), title);
                    goto out;
                }
                nbufbytes = 0;
            }
        }

        // ok!
        ok = true;

    out:

        f.close();
        if (!ok)
            LittleFS.remove (file);

        printFreeHeap (F("_downloadMapFile"));
        return (ok);
}


/* open the given FLASH file and confirm its size, downloading fresh if not found, no match or newer.
 * if successful return open LittleFS File and set file offset to first pixel,
 * else return a closed File
 * ESP version
 * N.B. as a hack when !verbose we know this is for propmap which is never on server
 */
static File openMapFile (bool verbose, const char *file, const char *title)
{
        resetWatchdog();

        // putting all variables up here avoids pendantic goto warnings
        File f;
        WiFiClient client;
        uint32_t filesize;
        uint32_t local_time = 0;
        uint32_t remote_time = 0;
        char hdr_buf[BHDRSZ];
        int nr = 0;
        bool file_ok = false;

        Serial.printf (_FX("%s: %s\n"), title, file);
        tftMsg (verbose, 500, _FX("%s: checking\r"), title);

        // start remote file download, even if only to check whether newer
        if (verbose && wifiOk() && client.connect(svr_host, HTTPPORT)) {
            snprintf (hdr_buf, sizeof(hdr_buf), _FX("/ham/HamClock/maps/%s"), file);
            httpGET (client, svr_host, hdr_buf);
            if (!httpSkipHeader (client, &remote_time) || remote_time == 0) {
                tftMsg (verbose, 1000, _FX("%s: server err; trying local\r"), title);
                client.stop();
            }
            Serial.printf (_FX("%s: %d remote_time\n"), title, remote_time);
        }
        
        // even if no net connection, still try using local file if available

        // open local file
        f = LittleFS.open (file, "r");
        if (!f) {
            tftMsg (verbose, 1000, _FX("%s: not in FLASH\r"), title);
            goto out;
        }

        // file is "bad" if remote is newer than flash
        local_time = f.getCreationTime();
        Serial.printf (_FX("%s: %d local_time\n"), title, local_time);
        if (verbose && client.connected() && remote_time > local_time) {
            tftMsg (verbose, 1000, _FX("%s: found newer map\r"), title);
            goto out;
        }

        // read local file header
        nr = f.read ((uint8_t*)hdr_buf, BHDRSZ);
        if (nr != BHDRSZ) {
            tftMsg (verbose, 1000, _FX("%s: FLASH read err\r"), title);
            goto out;
        }

        // check flash file type and size
        if (!bmpHdrOk (hdr_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            tftMsg (verbose, 1000, _FX("%s: bad format\r"), title);
            goto out;
        }
        if (filesize != f.size()) {
            tftMsg (verbose, 1000, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // all good
        file_ok = true;

    out:

        // download if not ok and remote connection ok
        if (!file_ok && client.connected()) {

            if (f) {
                // file exists but is not correct in some way
                f.close();
                LittleFS.remove(file);
            }

            // download and open again if success
            if (downloadMapFile (verbose, client, file, title))
                f = LittleFS.open (file, "r");
        }

        // leave error message up if not ok
        if (f)
            tftMsg (verbose, 0, _FX("%s: good\r"), title);
        tftMsg(verbose, 0, NULL);   // next row

        // finished with remote connection
        client.stop();

        // return result, good or bad
        printFreeHeap (F("_openMapFile"));
        return (f);
}

/* insure day and night maps for the given style and appropriate size are installed and ready for getMapPixel,
 *    downloading them if not found or newer.
 * if verbose then update display with tftMsg, else just log.
 * return whether successful.
 * ESP version
 */
bool installBackgroundMap (bool verbose, const char *style)
{
        resetWatchdog();

        // create names and titles
        char dfile[LFS_NAME_MAX];
        char nfile[LFS_NAME_MAX];
        char dtitle[NV_MAPSTYLE_LEN+10];
        char ntitle[NV_MAPSTYLE_LEN+10];
        getMapNames (style, dfile, nfile, dtitle, ntitle);

        // close previous, if any
        if (day_file)
            day_file.close();
        if (day_row_cache) {
            free (day_row_cache);
            day_row_cache = NULL;
        }
        if (night_file)
            night_file.close();
        if (night_row_cache) {
            free (night_row_cache);
            night_row_cache = NULL;
        }

        // init FLASH file system, formatting if first time
        LittleFS.begin();

        // install our function for time stamps
        LittleFS.setTimeCallback(now);


        // open each file, downloading if not found
        day_file = openMapFile (verbose, dfile, dtitle);
        night_file = openMapFile (verbose, nfile, ntitle);
        if (!day_file || !night_file) {
            if (day_file)
                day_file.close();
            if (night_file)
                night_file.close();
            return (false);
        }

        // good so init row caches for getMapDay/NightPixel()
        day_row_cache = (uint8_t *) malloc (BPBMPP*N_CACHE_COLS);
        night_row_cache = (uint8_t *) malloc (BPBMPP*N_CACHE_COLS);
        day_cache_col = day_cache_row = ~0;     // mark as invalid
        night_cache_col = night_cache_row = ~0;

        printFreeHeap (F("installBackgroundMap"));

        // ok!
        return (true);
}

/* return day RGB565 pixel at the given location.
 * ESP version
 */
bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp)
{
        // beware no map
        if (!day_row_cache) {
            *dayp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("day cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: day %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != day_cache_row || col < day_cache_col || col >= day_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!day_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPBMPP, SeekSet)
                        || !day_file.read (day_row_cache, BPBMPP*N_CACHE_COLS)) {
                Serial.println (F("day FLASH read err"));
                return (false);
            }
            day_cache_row = row;
            day_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-day_cache_col)*BPBMPP;
        *dayp = *(uint16_t*)(&day_row_cache[idx0]);

        // ok
        return (true);
}


/* return night RGB565 pixel at the given location.
 * ESP version
 */
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp)
{
        // beware no map
        if (!night_row_cache) {
            *nightp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("night cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: night %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != night_cache_row || col < night_cache_col || col >= night_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!night_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPBMPP, SeekSet)
                        || !night_file.read (night_row_cache, BPBMPP*N_CACHE_COLS)) {
                Serial.println (F("night FLASH read err"));
                return (false);
            }
            night_cache_row = row;
            night_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-night_cache_col)*BPBMPP;
        *nightp = *(uint16_t*)(&night_row_cache[idx0]);

        // ok
        return (true);
}


/* produce a list of system directory info.
 * return malloced array and malloced name -- N.B. caller must free()
 * ESP version
 */
FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used)
{
        // init fs
        LittleFS.begin();
        LittleFS.setTimeCallback(now);

        // get basic fs info
        FSInfo fs_info;
        LittleFS.info(fs_info);

        // pass back basic info
        *fs_name = strdup ("FLASH file system");
        *fs_size = fs_info.totalBytes;
        *fs_used = fs_info.usedBytes;

        // build each entry
        FS_Info *fs_array = NULL;
        int n_fs = 0;
        Dir dir = LittleFS.openDir("/");
        while (dir.next()) {

            // extend array
            fs_array = (FS_Info *) realloc (fs_array, (n_fs+1)*sizeof(FS_Info));
            FS_Info *fip = &fs_array[n_fs++];

            // store name
            strncpy (fip->name, dir.fileName().c_str(), sizeof(fip->name));

            // store time
            uint32_t t = dir.fileCreationTime();
            int yr = year(t);
            int mo = month(t);
            int dy = day(t);
            int hr = hour(t);
            int mn = minute(t);
            int sc = second(t);
            snprintf (fip->date, sizeof(fip->date), "%04d-%02d-%02dT%02d:%02d:%02dZ", yr, mo, dy, hr, mn, sc);

            // store length
            fip->len = dir.fileSize();
        }
        // Dir has not close, hope destructor cleans up

        // nice sorted order
        qsort (fs_array, n_fs, sizeof(FS_Info), fsinfoQsort);

        // ok
        *n_info = n_fs;
        return (fs_array);
}

#endif





/******************************************************************************************
 *
 * All UNIX systems
 *
 */

#if defined(_USE_UNIX) || defined(_IS_RPI)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/statvfs.h>


// persistent state of open files, allows restarting
static FILE *day_file, *night_file;
static int day_fbytes, night_fbytes;
static char *day_pixels, *night_pixels;


/* given a map file name, return full path.
 * N.B. returned pointer is to static storage -- do not free
 * N.B. must cooperate with EEPROM::begin()
 */
static const char *path (const char *file)
{
        static char fn[1024];
        snprintf (fn, sizeof(fn), "%s/.hamclock/%s", getenv ("HOME"), file);
        return (fn);
}


/* download the given file of expected size and load into local storage.
 * client starts postioned at beginning of remote file.
 * UNIX version
 */
static bool downloadMapFile (bool verbose, WiFiClient &client, const char *file, const char *title)
{
        resetWatchdog();

        #define COPY_BUF_SIZE 8192
        const uint32_t npixbytes = HC_MAP_W*HC_MAP_H*BPBMPP;
        uint32_t nbufbytes = 0;
        char copy_buf[COPY_BUF_SIZE];

        bool ok = false;

        // create or rewrite file
        FILE *fp = fopen (path(file), "w");
        if (!fp) {
            Serial.printf ("%s: %s\n", path(file), strerror(errno));
            tftMsg (verbose, 1000, _FX("%s: create failed\r"), title);
            return (false);
        }

        // read and check remote header
        for (int i = 0; i < BHDRSZ; i++) {
            if (!getChar (client, &copy_buf[i])) {
                Serial.printf (_FX("short header: %.*s\n"), i, copy_buf); // might be err message
                tftMsg (verbose, 1000, _FX("%s: header is short: %d\r"), title, i);
                goto out;
            }
        }
        uint32_t filesize;
        if (!bmpHdrOk (copy_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            Serial.printf (_FX("bad header: %.*s\n"), BHDRSZ, copy_buf); // might be err message
            tftMsg (verbose, 1000, _FX("%s: bad header\r"), title);
            goto out;
        }
        if (filesize != npixbytes + BHDRSZ) {
            Serial.printf (_FX("%s: wrong size %u != %u\n"), title, filesize, npixbytes);
            tftMsg (verbose, 1000, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // write header
        fwrite (copy_buf, 1, BHDRSZ, fp);
        updateClocks(false);

        // copy pixels
        tftMsg (verbose, 500, _FX("%s: downloading\r"), title);
        for (uint32_t nbytescopy = 0; nbytescopy < npixbytes; nbytescopy++) {
            resetWatchdog();

            if (((nbytescopy%(npixbytes/10)) == 0) || nbytescopy == npixbytes-1)
                tftMsg (verbose, 0, _FX("%s: %3d%%\r"), title, 100*(nbytescopy+1)/npixbytes);

            // read more
            if (nbufbytes < COPY_BUF_SIZE && !getChar (client, &copy_buf[nbufbytes++])) {
                Serial.printf (_FX("%s: file is short: %u %u\n"), title, nbytescopy, npixbytes);
                tftMsg (verbose, 1000, _FX("%s: file is short\r"), title);
                goto out;
            }

            // write when copy_buf is full or last
            if (nbufbytes == COPY_BUF_SIZE || nbytescopy == npixbytes-1) {
                updateClocks(false);
                if (fwrite (copy_buf, 1, nbufbytes, fp) != nbufbytes) {
                    tftMsg (verbose, 1000, _FX("%s: file write failed\r"), title);
                    goto out;
                }
                nbufbytes = 0;
            }
        }

        // ok!
        ok = true;

    out:

        fclose(fp);
        if (!ok)
            unlink (path(file));

        return (ok);
}


/* open the given map file and confirm its size, downloading fresh if not found, no match or newer.
 * if successful return open FILE and set file offset to first pixel,
 * else return NULL.
 * UNIX version
 * N.B. as a hack when !verbose we know this is for propmap which is never on server
 */
static FILE *openMapFile (bool verbose, const char *file, const char *title)
{
        resetWatchdog();

        // putting all variables up here avoids pendantic goto warnings
        FILE *fp = NULL;
        WiFiClient client;
        uint32_t filesize;
        uint32_t remote_time = 0;
        char hdr_buf[BHDRSZ];
        struct stat sbuf;
        int nr = 0;
        bool file_ok = false;

        Serial.printf (_FX("%s: %s\n"), title, file);
        tftMsg (verbose, 500, _FX("%s: checking\r"), title);

        // start remote file download, even if only to check whether newer
        if (verbose && wifiOk() && client.connect(svr_host, HTTPPORT)) {
            snprintf (hdr_buf, sizeof(hdr_buf), _FX("/ham/HamClock/maps/%s"), file);
            httpGET (client, svr_host, hdr_buf);
            if (!httpSkipHeader (client, &remote_time) || remote_time == 0) {
                tftMsg (verbose, 1000, _FX("%s: server err; trying local\r"), title);
                client.stop();
            }
            Serial.printf (_FX("%s: %d remote_time\n"), title, remote_time);
        }
        
        // even if no net connection, still try using local file if available

        // open local file
        fp = fopen (path(file), "r");
        if (!fp) {
            Serial.printf ("%s: %s\n", path(file), strerror(errno));
            tftMsg (verbose, 1000, _FX("%s: not found\r"), title);
            goto out;
        }

        // file is "bad" if remote is newer than local
        fstat (fileno (fp), &sbuf);
        if (verbose) {
            uint32_t local_time = sbuf.st_mtime;
            Serial.printf (_FX("%s: %d local_time\n"), title, local_time);
            if (client.connected() && remote_time > local_time) {
                tftMsg (verbose, 1000, _FX("%s: found newer map\r"), title);
                goto out;
            }
        }

        // read local file header
        nr = fread (hdr_buf, 1, BHDRSZ, fp);
        if (nr != BHDRSZ) {
            tftMsg (verbose, 1000, _FX("%s: local read err\r"), title);
            goto out;
        }

        // check local file type and size
        if (!bmpHdrOk (hdr_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            tftMsg (verbose, 1000, _FX("%s: bad format\r"), title);
            goto out;
        }
        if (filesize != (uint32_t)sbuf.st_size) {
            Serial.printf (_FX("%s: wrong size: %u %u\r"), title, filesize, sbuf.st_size);
            tftMsg (verbose, 1000, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // all good
        file_ok = true;

    out:

        // download if not ok and remote connection ok
        if (!file_ok && client.connected()) {

            if (fp) {
                // file exists but is not correct in some way
                fclose(fp);
                fp = NULL;
                unlink (path(file));
            }

            // download and open again if success
            if (downloadMapFile (verbose, client, file, title))
                fp = fopen (path(file), "r");
        }

        // leave error message up if not ok
        if (fp)
            tftMsg (verbose, 0, _FX("%s: good\r"), title);
        tftMsg(verbose, 0, NULL);   // next row

        // finished with remote connection
        client.stop();

        // return result, good or bad
        return (fp);
}

/* insure day and night maps for the given style and appropriate size are installed and ready for drawing,
 *    downloading them if not found or newer.
 * if verbose then update display with tftMsg, else just log.
 * return whether successful.
 * UNIX version
 */
bool installBackgroundMap (bool verbose, const char *style)
{
        resetWatchdog();

        // create names and titles
        char dfile[32];                 // match LFS_NAME_MAX
        char nfile[32];
        char dtitle[NV_MAPSTYLE_LEN+10];
        char ntitle[NV_MAPSTYLE_LEN+10];
        getMapNames (style, dfile, nfile, dtitle, ntitle);

        // close previous, if any
        if (day_file) {
            fclose (day_file);
            day_file = NULL;
        }
        if (day_pixels) {
            munmap (day_pixels, day_fbytes);
            day_pixels = NULL;
        }
        if (night_file) {
            fclose (night_file);
            night_file = NULL;
        }
        if (night_pixels) {
            munmap (night_pixels, day_fbytes);
            night_pixels = NULL;
        }

        // open each file, downloading if not found
        day_file = openMapFile (verbose, dfile, dtitle);
        night_file = openMapFile (verbose, nfile, ntitle);
        if (!day_file || !night_file) {
            if (day_file)
                fclose(day_file);
            if (night_file)
                fclose(night_file);
            return (false);
        }

        // mmap and install into Adafruit_RA8875

        day_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;          // n bytes of 16 bit RGB565 pixels
        night_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;
        day_pixels = (char *)                             // allow OS to choose addr
            mmap (NULL, day_fbytes, PROT_READ, MAP_FILE|MAP_PRIVATE, fileno(day_file), 0);
        night_pixels = (char *)
            mmap (NULL, night_fbytes, PROT_READ, MAP_FILE|MAP_PRIVATE, fileno(night_file), 0);

        if (day_pixels == MAP_FAILED || night_pixels == MAP_FAILED) {
            // boo!

            if (day_pixels == MAP_FAILED) {
                Serial.printf ("%s mmap failed: %s\n", dfile, strerror(errno));
                tftMsg (verbose, 1000, "%s mmap failed", dfile);
            }
            munmap (day_pixels, day_fbytes);
            day_pixels = NULL;

            if (night_pixels == MAP_FAILED) {
                Serial.printf ("%s mmap failed: %s\n", nfile, strerror(errno));
                tftMsg (verbose, 1000, "%s mmap failed", nfile);
            }
            munmap (night_pixels, night_fbytes);
            night_pixels = NULL;

            fclose (day_file);
            day_file = NULL;
            fclose (night_file);
            night_file = NULL;

            Serial.println (F("mmaps failed"));
            tft.setEarthPix (NULL, NULL);
            return (false);

        } else {
            // ok!
            // Serial.println (F("both mmaps good"));
            tft.setEarthPix (day_pixels+BHDRSZ, night_pixels+BHDRSZ);
            return (true);
        }
}



/* produce a list of system directory info.
 * return malloced array and malloced name -- N.B. caller must free()
 * UNIX version
 */
FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used)
{
        resetWatchdog();

        // get config dir path
        char dirname[1024];
        snprintf (dirname, sizeof(dirname), "%s/.hamclock", getenv("HOME"));

        // pass back basic info
        *fs_name = strdup (dirname);
        struct statvfs svs;
        if (statvfs (dirname, &svs) < 0) {
            Serial.printf (_FX("%s: %s\n"), dirname, strerror(errno));
            *fs_size = 0;
            *fs_used = 0;
        } else {
            *fs_size = svs.f_blocks * svs.f_frsize;
            *fs_used = (svs.f_blocks - svs.f_bavail) * svs.f_frsize;
        }

        // build each entry
        FS_Info *fs_array = NULL;
        int n_fs = 0;
        DIR *dir = opendir (dirname);
        if (dir) {

            // add each entry
            struct dirent *ent;
            while ((ent = readdir (dir)) != NULL) {

                // skip dirs
                char *fname = ent->d_name;
                if (fname[0] == '.')
                    continue;

                // extend array
                fs_array = (FS_Info *) realloc (fs_array, (n_fs+1)*sizeof(FS_Info));
                FS_Info *fip = &fs_array[n_fs++];

                // store name
                memset (fip->name, 0, sizeof(fip->name));
                memcpy (fip->name, fname, sizeof(fip->name)-1);

                // get file info
                char fullpath[2048];
                struct stat mystat;
                snprintf (fullpath, sizeof(fullpath), "%s/%s", dirname, fname);
                if (stat (fullpath, &mystat) < 0) {
                    Serial.printf (_FX("%s: %s\n"), fullpath, strerror(errno));
                    continue;
                }

                // break out time
                struct tm *tmp = gmtime (&mystat.st_mtime);
                strftime (fip->date, sizeof(fip->date), "%Y-%m-%dT%H:%M:%SZ", tmp);

                // store length
                fip->len = mystat.st_size;
            }

            // finished with dir
            closedir (dir);

            // nice sorted order
            qsort (fs_array, n_fs, sizeof(FS_Info), fsinfoQsort);

        } else {

            Serial.printf (_FX("%s: %s\n"), dirname, strerror(errno));

        }

        // ok
        *n_info = n_fs;
        return (fs_array);
}

// dummies for linking

bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }

#endif // UNIX




/* install and activate VOACAP world-wide propagation files to be used as background maps
 *    for the current time and given band.
 * return whether ok
 * shared version.
 */
bool installPropMaps (float MHz)
{
        static char prop_page[] = "/ham/HamClock/fetchVOACAPArea.pl";

        resetWatchdog();

        // get clock time
        time_t t = nowWO();
        int yr = year(t);
        int mo = month(t);
        int hr = hour(t);

        // prepare query
        #define DEF_TOA 3.0            // TODO
        static char qfmt[] = 
     "%s?YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%.3f&TXLNG=%.3f&PATH=%d&WATTS=%d&WIDTH=%d&HEIGHT=%d&MHZ=%.2f&TOA=%.1f";
        StackMalloc query_mem(300);
        char *query = (char *) query_mem.getMem();
        snprintf (query, query_mem.getSize(), qfmt,
            prop_page, yr, mo, hr, de_ll.lat_d, de_ll.lng_d, show_lp, bc_power, HC_MAP_W, HC_MAP_H,
            MHz, DEF_TOA);

        Serial.printf ("PropMap query: %s\n", query);

        // assign a style and compose names and titles
        const char style[] = "PropMap";
        char dfile[32];                 // match LFS_NAME_MAX
        char nfile[32];
        char dtitle[NV_MAPSTYLE_LEN+10];
        char ntitle[NV_MAPSTYLE_LEN+10];
        getMapNames (style, dfile, nfile, dtitle, ntitle);

#if defined(_IS_ESP8266)
        // ESP FLASH can only hold two sets of maps, so remove all but the ones for the current style
        const char *style_now = getCoreMapStyle();
        int n_info;
        uint64_t fs_size, fs_used;
        char *fs_name;
        FS_Info *fip0 = getConfigDirInfo (&n_info, &fs_name, &fs_size, &fs_used);
        for (int i = 0; i < n_info; i++) {
            FS_Info *fip = &fip0[i];
            if (!strstr (fip->name, style_now)) {
                Serial.printf (_FX("%s: rm %s\n"), style, fip->name);
                LittleFS.remove (fip->name);
            }
        }
        free (fs_name);
        free (fip0);

#endif // _IS_ESP8266

        // compute and download and engage maps
        updateClocks(false);
        WiFiClient client;
        bool ok = false;
        if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
            httpGET (client, svr_host, query);
            if (httpSkipHeader (client) && downloadMapFile (false, client, dfile, dtitle)
                                        && downloadMapFile (false, client, nfile, ntitle)) {
                client.stop();          // close socket before opening files next
                ok = installBackgroundMap (false, style);
            } else {
                // closed failed socket
                client.stop();
            }
        }

        if (!ok)
            Serial.printf (_FX("%s: download failed\n"), style);

        printFreeHeap (F("installPropMaps"));

        return (ok);
}

/* return the current effective map style
 */
const char *getMapStyle()
{
        if (prop_map == PROP_MAP_OFF)
            return (getCoreMapStyle());

        static char ms_str[30];
        snprintf (ms_str, sizeof(ms_str), "%dmPropMap", propMap2Band(prop_map));
        return (ms_str);
}
