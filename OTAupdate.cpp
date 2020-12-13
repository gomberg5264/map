/* handle remote firmware updating
 */

#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>


#include "HamClock.h"

static const char v_page[] = "/ham/HamClock/version.pl";

#if defined(_USE_UNIX)
static const char v_bin[] = "/ham/HamClock/ESPHamClock.zip";
#else
static const char v_bin[] = "/ham/HamClock/ESPHamClock.ino.bin";
#endif

#define	ASK_TO	        60000U				// ask timeout, millis()
#define	BOX_W		120				// box width
#define	BOX_H		40				// box height
#define	INDENT		20				// indent
#define	Q_Y		40				// question y
#define LH              30                      	// line height
#define	BOX_Y		(Q_Y+LH)      		        // yes/no boxes y
#define INFO_Y          (BOX_Y+2*LH)                    // first info y

#define PBAR_INDENT     30                      	// left and right indent
#define PBAR_H          30                      	// progress bar height
#define PBAR_W          (tft.width()-2*PBAR_INDENT)     // progress bar width
#define FLASHBPS        29000                   	// approx flash rate, b/s

static uint16_t pbar_x0, pbar_y0;               	// lower left of progress bar


#if defined(_IS_ESP8266)

/* called by ESPhttpUpdate during download with bytes so far and total.
 */
static void onProgressCB (int current, int total)
{
    if (current > 49*total/50) {
        // report estimate for overwrite program -- can't run then!
        tft.setCursor (pbar_x0+PBAR_W/2, pbar_y0-3);
        tft.printf (_FX(" ... about %d more seconds ... "), total/FLASHBPS);
    } else {
        // progressively fill to half
        tft.drawRect (pbar_x0, pbar_y0-PBAR_H, PBAR_W, PBAR_H, RA8875_WHITE);
        int w = current*PBAR_W/total/2;
        if (w > 0)              // avoid 0-width bug
            tft.fillRect (pbar_x0, pbar_y0-PBAR_H, w, PBAR_H, RA8875_WHITE);
    }
}

#else

/* called by ESPhttpUpdate during download with bytes so far and total.
 */
static void onProgressCB (int current, int total)
{
    tft.drawRect (pbar_x0, pbar_y0-PBAR_H, PBAR_W, PBAR_H, RA8875_WHITE);
    tft.fillRect (pbar_x0, pbar_y0-PBAR_H, current*PBAR_W/total, PBAR_H, RA8875_WHITE);
}

#endif




/* return whether a new version is available.
 * if so, and we care, pass back the name in new_ver[new_verl]
 * default no if error.
 */
bool newVersionIsAvailable (char *new_ver, uint16_t new_verl)
{
    WiFiClient v_client;
    bool found_newer = false;

    Serial.print (svr_host); Serial.println (v_page);
    if (wifiOk() && v_client.connect (svr_host, HTTPPORT)) {
	resetWatchdog();

	// query page
	httpGET (v_client, svr_host, v_page);

	// skip header
	if (!httpSkipHeader (v_client)) {
	    Serial.println (F("Version query header is short"));
	    goto out;
	}

	// next line is new version number
        char nv[100];
	if (!getTCPLine (v_client, nv, sizeof(nv), NULL)) {
	    Serial.println (F("Version query timed out"));
	    goto out;
        }

	// newer always ok or rc accepts same version
        Serial.printf (_FX("found version %s\n"), nv);
	float this_v = atof(HC_VERSION);
	float new_v = atof(nv);
        // Serial.printf ("V %g >? %g\n", new_v, this_v);
	if (new_v > this_v || (strstr(HC_VERSION,"rc") && new_v == this_v)) {
            found_newer = true;
            if (new_ver)
                strncpy (new_ver, nv, new_verl);
        }
    }

out:

    // finished with connection
    v_client.stop();

    return (found_newer);
}

/* ask and return whether to install the given (presumably newer) version.
 * default no if trouble of no user response.
 */
bool askOTAupdate(char *new_ver)
{
    // prep
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    char line[128];

    // ask whether to install
    tft.setCursor (INDENT, Q_Y);
    sprintf (line, _FX("New version %s is available. Update now?  ... "), new_ver);
    tft.print (line);
    uint16_t count_x = tft.getCursorX();
    uint16_t count_y = tft.getCursorY();
    uint8_t count_s = ASK_TO/1000U;
    tft.print(count_s);

    // draw yes/no boxes
    SBox no_b =  {INDENT, BOX_Y, BOX_W, BOX_H};
    SBox yes_b = {(uint16_t)(tft.width()-INDENT-BOX_W), BOX_Y, BOX_W, BOX_H};
    drawStringInBox ("No", no_b, true, RA8875_WHITE);
    drawStringInBox ("Yes", yes_b, false, RA8875_WHITE);

    // prep for potentially long wait
    closeDXCluster();       // prevent inbound msgs from clogging network
    closeGimbal();          // avoid dangling connection

    // list changes
    WiFiClient v_client;
    uint16_t liney = INFO_Y+LH;
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (INDENT, liney);
    if (wifiOk() && v_client.connect (svr_host, HTTPPORT)) {
	resetWatchdog();

	// query page
	httpGET (v_client, svr_host, v_page);

	// skip header
	if (!httpSkipHeader (v_client)) {
	    Serial.println (F("Info header is short"));
	    goto out;
	}

	// skip next line which is new version number
	if (!getTCPLine (v_client, line, sizeof(line), NULL)) {
	    Serial.println (F("Info timed out"));
	    goto out;
        }

        // remaining lines are changes
	while (getTCPLine (v_client, line, sizeof(line), NULL)) {
            tft.setCursor (INDENT, liney);
            (void) maxStringW (line, tft.width()-2*INDENT);
            tft.print(line);
            if ((liney += LH) >= tft.height()-10)
                break;
        }
    }
  out:
    v_client.stop();

    // wait for response or time out
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drainTouch();
    uint32_t t0 = millis();
    while (count_s > 0) {

        // update countdown
        wdDelay(100);
        if (timesUp(&t0,1000)) {
            tft.fillRect (count_x, count_y-30, 60, 40, RA8875_BLACK);
            tft.setCursor (count_x, count_y);
            tft.print(--count_s);
        }

        // check buttons
        SCoord s;
        if (readCalTouch(s) != TT_NONE) {
            if (inBox (s, yes_b)) {
                drawStringInBox ("Yes", yes_b, true, RA8875_WHITE);
                return (true);
            }
            if (inBox (s, no_b)) {
                drawStringInBox ("No", no_b, false, RA8875_WHITE);
                return (false);
            }
        }
    }

    // if get here we timed out
    return (false);
}

/* reload HamClock with the binary file above.
 * if successful HamClock is rebooted with new image so we never return.
 */
void doOTAupdate()
{
    Serial.println (F("Begin download"));

    // inform user
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (0, 100);
    tft.setTextColor (RA8875_WHITE);
    tft.println (F("Beginning remote update..."));
    tft.println (F("  Do not interrupt power or WiFi during this process."));
    tft.println();

    // save cursor as progress bar location
    pbar_x0 = tft.getCursorX() + PBAR_INDENT;
    pbar_y0 = tft.getCursorY();

    // connect progress callback
    ESPhttpUpdate.onProgress (onProgressCB);

    // go
    resetWatchdog();
    WiFiClient client;
    char url[128];
    sprintf (url, _FX("http://%s%s"), svr_host, v_bin);
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);
    resetWatchdog();

    // should not get here if update worked

    // move text position well below progress bar
    tft.setCursor (0, pbar_y0 + 2*PBAR_H);

    // show error message
    switch (ret) {
    case HTTP_UPDATE_FAILED:
	tft.print(F("Update failed: "));
	    tft.print(ESPhttpUpdate.getLastError());
	    tft.print(' ');
	    tft.println (ESPhttpUpdate.getLastErrorString());
	break;
    case HTTP_UPDATE_NO_UPDATES:
	tft.println (F("No updates found"));
	break;
    case HTTP_UPDATE_OK:
	tft.println (F("Update Ok?!"));
	break;
    default:
	tft.print (F("Unknown failure code: "));
	tft.println (ret);
	break;
    }

    // message dwell
    wdDelay(5000);
}
