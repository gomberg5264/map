#ifndef _HTTP_UPDATE_H
#define _HTTP_UPDATE_H

#include "WiFiClient.h"

typedef int t_httpUpdate_return;

class ESPhttpUpdate
{
    public:

	t_httpUpdate_return update(WiFiClient &client, const char *url);
	int getLastError(void);
	const char *getLastErrorString(void);
        void onProgress (void (*progressCB)(int current, int total));

    private:
        void (*progressCB)(int current, int total);
        bool runCommand (int prog_start, int prog_end, int prog_maxlines, const char *cmd, const char *err);
};

enum {
	HTTP_UPDATE_OK,
	HTTP_UPDATE_FAILED,
	HTTP_UPDATE_NO_UPDATES,
};

extern ESPhttpUpdate ESPhttpUpdate;

#endif // _HTTP_UPDATE_H
