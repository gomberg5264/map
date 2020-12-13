/* implement EEPROM class using a local file.
 * format is %08X %02X\n for each address/byte pair.
 * updates of existing address are performed in place.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "EEPROM.h"

class EEPROM EEPROM;

EEPROM::EEPROM()
{
        fp = NULL;
        filename = NULL;
}

void EEPROM::begin (int s)
{
        // establish file name
	if (!filename) {

            // insure dir
            char *home = getenv ("HOME");
	    char newfn[1024];
	    snprintf (newfn, sizeof(newfn), "%s/.hamclock", home);
            mkdir (newfn, 0755);

            // new file name
            strcat (newfn, "/eeprom");

            // preserve old file if found
	    char oldfn[1024];
	    snprintf (oldfn, sizeof(oldfn), "%s/.rpihamclock_eeprom", home);
            rename (oldfn, newfn);

	    filename = strdup (newfn);
	}

        // start over if called again
        if (fp) {
            fclose(fp);
            fp = NULL;
        }
        if (data_array) {
            free (data_array);
            data_array = NULL;
        }

        // open RW, create if necesary
	fp = fopen (filename, "r+");
        if (fp) {
            // require exclusive access
            if (flock (fileno(fp), LOCK_EX|LOCK_NB) < 0) {
                printf ("%s: Multiple access not allowed\n", filename);
                exit(1);
            }
            printf ("EEPROM %s: open ok\n", filename);
        } else {
            fp = fopen (filename, "w+");
            if (fp)
                printf ("EEPROM %s: create ok\n", filename);
            else {
                printf ("EEPROM %s: create failed: %s\n", filename, strerror(errno));
                return;
            }
        }

        // malloc memory, init as zeros
        n_data_array = s;
        data_array = (uint8_t *) calloc (n_data_array, sizeof(uint8_t));

        // init data_array from file .. support old version of random memory locations
	char line[64];
	unsigned int a, b;
	while (fgets (line, sizeof(line), fp)) {
	    // sscanf (line, "%x %x", &a, &b); printf ("R: %08X %02X\n", a, b);
	    if (sscanf (line, "%x %x", &a, &b) == 2 && a < n_data_array)
                data_array[a] = b;
        }
}

bool EEPROM::commit(void)
{
        // bail if no file or no data_array
        if (!fp || !data_array)
            return (false);

        // (over)write entire data_array array
        fseek (fp, 0L, SEEK_SET);
        for (unsigned a = 0; a < n_data_array; a++)
            fprintf (fp, "%08X %02X\n", a, data_array[a]);
        fflush (fp);

        // return whether io ok
        return (!feof(fp) && !ferror(fp));
}

void EEPROM::write (uint32_t address, uint8_t byte)
{
        // set array if available and address is in bounds
        if (data_array && address < n_data_array)
            data_array[address] = byte;
}

uint8_t EEPROM::read (uint32_t address)
{
        // use array if available and address is in bounds
        if (data_array && address < n_data_array)
            return (data_array[address]);

        // not found
        return (0);
}
