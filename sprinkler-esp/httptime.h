#ifdef ARDUINO
	#include <Arduino.h>
	#define serprintln(v) Serial.println(v)
	#define serprint(v)   Serial.print(v)
#else // Running outside of Arduino
	#include <iostream> // cout
	#include <string>
	using namespace std; 
	#define serprintln(v) cout << v << "\n"
	#define serprint(v)   cout << v
	#define String string
#endif

#include <time.h>

#define TZ_OFF -8  // my own timezone (dst not handled)

struct timedata {
	String fullstring;
	char *daystr;
	char *monstr;
	struct tm tm;
	struct tm local_tm;
	time_t timet;
	time_t local_timet;
	int year;
	int mon;
	char *tzstr;
};

int setTimeFromHTTPDate(struct timedata *tdp, String datestr);
int monstr_to_mon(char *ms); // Such efficient? "Jan" -> 1, ...
void wipe_time(struct timedata *tdp);  // zeros out and sets strings to ""
int get_http_time(struct timedata *tdp,
                  const char **hosts,  // list of hosts
                  int numhosts,
                  int port);
