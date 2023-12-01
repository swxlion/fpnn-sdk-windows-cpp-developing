#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <sys/timeb.h>
#include "TimeUtil.h"

using namespace fpnn;

int64_t TimeUtil::curr_sec()
{
	struct _timeb now;
	_ftime_s(&now);

	return now.time;
}

int64_t TimeUtil::curr_msec()
{
	struct _timeb now;
	_ftime_s(&now);

	return now.time * 1000 + now.millitm;
}

std::string TimeUtil::getDateStr(int64_t t, char sep){
	char buff[32] = {0};
	struct tm timeInfo;
	time_t timeValue = (time_t)t;

	if (localtime_s(&timeInfo, &timeValue) == 0)
		snprintf(buff, sizeof(buff), "%04d%c%02d%c%02d",
				timeInfo.tm_year+1900, sep, timeInfo.tm_mon+1, sep, timeInfo.tm_mday);

	return std::string(buff);
}

std::string TimeUtil::getDateStr(char sep){
    int64_t t = time(NULL);
	return TimeUtil::getDateStr(t, sep);
}

/*
std::string TimeUtil::getTimeStr(int64_t t, char sep){
	char buff[32] = {0};
	struct tm timeInfo;
	struct tm *tmT = localtime_r(&t, &timeInfo);
	if (tmT){
		snprintf(buff, sizeof(buff), "%04d%c%02d%c%02d%c%02d%c%02d%c%02d",
				tmT->tm_year+1900, sep, tmT->tm_mon+1, sep, tmT->tm_mday, sep,
				tmT->tm_hour,sep, tmT->tm_min, sep, tmT->tm_sec);
	}
	return std::string(buff);
}

std::string TimeUtil::getTimeStr(char sep){
    int64_t t = slack_real_sec();
	return TimeUtil::getTimeStr(t, sep);
}
*/

std::string TimeUtil::getDateTime(int64_t t){
	/*
	* buff[32] is enough, but On Ubuntu server 20 with g++ 9.3.0, it thinks the data maybe truncated.
	* So, I think the compiler with the ‘__builtin___snprintf_chk' function author is not intelligent.
	*/
	char buff[40] = {0};

	struct tm timeInfo;
	time_t timeValue = (time_t)t;

	if (localtime_s(&timeInfo, &timeValue) == 0)
		snprintf(buff, sizeof(buff), "%04d-%02d-%02d %02d:%02d:%02d",
				timeInfo.tm_year+1900, timeInfo.tm_mon+1, timeInfo.tm_mday,
				timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
	return std::string(buff);
}

std::string TimeUtil::getDateTime(){
    int64_t t = time(NULL);
	return TimeUtil::getDateTime(t);
}

std::string TimeUtil::getDateTimeMS(int64_t t){
    char buff[40] = {0};
	time_t sec = t / 1000;
	uint32_t msec = t % 1000;
	struct tm timeInfo;

	if (localtime_s(&timeInfo, &sec) == 0)
		snprintf(buff, sizeof(buff), "%04d-%02d-%02d %02d:%02d:%02d,%03d",
				timeInfo.tm_year+1900, timeInfo.tm_mon+1, timeInfo.tm_mday,
				timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec, msec);
	return std::string(buff);
}

std::string TimeUtil::getDateTimeMS(){
	int64_t t = curr_msec();
	return TimeUtil::getDateTimeMS(t);
}

std::string TimeUtil::getTimeRFC1123(){
	static const char* Days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
	static const char* Months[] = {"Jan","Feb","Mar", "Apr", "May", "Jun", "Jul","Aug", "Sep", "Oct","Nov","Dec"};
	char buff[32] = {0};

	time_t t = time(NULL);
	struct tm brokenTM;
	gmtime_s(&brokenTM, &t);

	snprintf(buff, sizeof(buff), "%s, %d %s %d %d:%d:%d GMT",
			Days[brokenTM.tm_wday], brokenTM.tm_mday, Months[brokenTM.tm_mon],
			brokenTM.tm_year + 1900,
			brokenTM.tm_hour, brokenTM.tm_min, brokenTM.tm_sec);
	return std::string(buff);
}
