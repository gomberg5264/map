#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "Arduino.h"

char *our_name;

/* return milliseconds since first call
 */
uint32_t millis(void)
{
	static struct timespec t0;

	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);

	if (t0.tv_sec == 0 && t0.tv_nsec == 0)
	    t0 = t;

	int32_t dt_ms = (t.tv_sec - t0.tv_sec)*1000 + (t.tv_nsec - t0.tv_nsec)/1000000;
	// printf ("millis %u: %ld.%09ld - %ld.%09ld\n", dt_ms, t.tv_sec, t.tv_nsec, t0.tv_sec, t0.tv_nsec);
	return (dt_ms);
}

void delay (uint32_t ms)
{
	usleep (ms*1000);
}

int random(int max)
{
	return ((int)((max-1.0F)*::random()/RAND_MAX));
}

uint16_t analogRead(int pin)
{
	return (0);		// not supported on Pi, consider https://www.adafruit.com/product/1083
}



/* Every normal C program requires a main().
 * This is provided as magic in the Arduino IDE so here we must do it ourselves.
 */
int main (int ac, char *av[])
{
	// save our name for remote update
	our_name = av[0];

        // synchronous logging
        setbuf (stdout, NULL);

	// call setup one time
	setup();

	// call loop forever
	for (;;) {
	    loop();

            // this loop by itself would run 100% CPU so try to be a better citizen and throttle back

            // measure cpu time used during previous loop
            static struct rusage ru0;
            struct rusage ru1;
            getrusage (RUSAGE_SELF, &ru1);
            if (ru0.ru_utime.tv_sec == 0 && ru0.ru_utime.tv_usec == 0)
                ru0 = ru1;
            struct timeval *ut0 = &ru0.ru_utime;
            struct timeval *ut1 = &ru1.ru_utime;
            struct timeval *st0 = &ru0.ru_stime;
            struct timeval *st1 = &ru1.ru_stime;
            int ut_us = (ut1->tv_sec - ut0->tv_sec)*1000000 + (ut1->tv_usec - ut0->tv_usec);
            int st_us = (st1->tv_sec - st0->tv_sec)*1000000 + (st1->tv_usec - st0->tv_usec);
            int cpu_us = ut_us + st_us;
            ru0 = ru1;
            // printf ("ut %d st %d\n", ut_us, st_us);

            // sleep for 20% longer to keep CPU < 80%, beware lengthy loop()
            if (cpu_us < 10000)
                usleep (cpu_us/5);

	}
}
