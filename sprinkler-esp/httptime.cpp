#define HTTPDATE_DEBUG
#define HTTPDATE_DEBUG_EXTRA

#ifdef ARDUINO
	#include <Arduino.h>
	#include <WiFiClient.h>
#else
	#include <string.h>
	using namespace std; 
#endif

#include "httptime.h"

// datestr must be modifiable because we hurt it
// with strtok() right now (ie. we add NUl's into it)
int setTimeFromHTTPDate(struct timedata *tdp, String datestr) {
	// Returns 0 on success
	// 1 failure
	// HTTP time: Sun, 31 Jul 2016 22:52:16 GMT
	char *strs[8];
	int i=0;
	tdp->fullstring = datestr;
	while ((strs[i] = strtok(i ? NULL : &datestr[0], ", :")))
		i++;
	if (i != 8) {
		serprintln("Time parse fail");
		return 1;
	} else {
		tdp->daystr=strs[0];
		tdp->tm.tm_mday=strtol(strs[1], NULL, 10);

		tdp->monstr=strs[2];
		tdp->mon = monstr_to_mon(tdp->monstr);
		tdp->tm.tm_mon = tdp->mon - 1;

		tdp->year = tdp->tm.tm_year = strtol(strs[3], NULL, 10);
		tdp->tm.tm_year -= 1900;

		tdp->tm.tm_hour=strtol(strs[4], NULL, 10);
		tdp->tm.tm_min=strtol(strs[5], NULL, 10);
		tdp->tm.tm_sec=strtol(strs[6], NULL, 10);
		tdp->tzstr=strs[7];
		tdp->tm.tm_wday=-1;
		tdp->tm.tm_wday=-1;
		tdp->tm.tm_yday=-1;
		tdp->tm.tm_isdst=-1;
		tdp->timet = mktime(&(tdp->tm));
		tdp->local_timet = tdp->timet + TZ_OFF*60*60;
		tdp->local_tm = *localtime(&(tdp->local_timet));
	}
	return 0; // no error
}

int monstr_to_mon(char *ms) { // Such efficient? "Jan" -> 1, ...
	// Jan, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec
 	if (!ms[0]) return 0;
 	if (ms[0] == 'J') {
 		if (ms[1] == 'a') return 1;
 		if (ms[1] != 'u') return 0;
 		if (ms[2] == 'n') return 6;
 		if (ms[2] == 'l') return 7;
 		return 0;
	}
	if (ms[0] == 'F') return 2;
	if (ms[0] == 'M') {
		if (ms[1] != 'a') return 0;
		if (ms[2] == 'r') return 3;
		if (ms[2] == 'y') return 5;
		return 0;
	}
	if (ms[0] == 'A') {
		if (ms[1] == 'p') return 4;
		if (ms[1] == 'u') return 8;
		return 0;
	}
	if (ms[0] == 'S') return 9;
	if (ms[0] == 'O') return 10;
	if (ms[0] == 'N') return 11;
	if (ms[0] == 'D') return 12;
	return 0;
}

#if 0 // not using
void wipe_time(struct timedata *tdp) { // zeros out and sets strings to ""
	memset(tdp, 0, sizeof *tdp);
	tdp->daystr = "";
	tdp->monstr = "";
	tdp->tzstr = "";
}
#endif

#ifdef ARDUINO
int get_http_time(struct timedata *tdp,
                  const char **hosts,  // list of hosts
                  int numhosts,
                  int port) {
	// Get time from first connect()'able host
	// Returns: 0 on success; something else on fail.
	// HTTP time: Sun, 31 Jul 2016 22:52:16 GMT
	WiFiClient client;
	int connected;
	int hostidx;
	for (hostidx=0;
	     hostidx<numhosts && !client.connect(hosts[hostidx], port);
	     hostidx++);
	if (hostidx >= numhosts) return 1;
	#ifdef HTTPDATE_DEBUG_EXTRA
		serprint("We done hit the time server! ");
		serprintln(hosts[hostidx]);
	#endif
	client.print("HEAD / HTTP/1.1\r\n\r\n");
	while(!!!client.available()) yield();
	#ifdef HTTPDATE_DEBUG_EXTRA
		serprintln("He/she's made herself available for our date.");
	#endif

	while(client.available()) {
		if (client.read() == '\n') {		
			if (client.read() == 'D') {		
				if (client.read() == 'a') {		
					if (client.read() == 't') {		
						if (client.read() == 'e') {		
							if (client.read() == ':') {		
								client.read();
								String date_str = client.readStringUntil('\r');
								client.stop();
								serprintln(date_str);
								return setTimeFromHTTPDate(tdp, date_str);
							}
						}
					}
				}
			}
		}
	}
	#ifdef HTTPDATE_DEBUG
		serprintln("Time Fail!");
	#endif
	return 1;
}

#endif
