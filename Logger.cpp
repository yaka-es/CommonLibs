/* CommonLibs/Logger.cpp */
/*-
 * Copyright 2009, 2010 Free Software Foundation, Inc.
 * Copyright 2010 Kestrel Signal Processing, Inc.
 * Copyright 2011, 2012, 2014 Range Networks, Inc.
 *
 * This software is distributed under the terms of the GNU Affero Public License.
 * See the COPYING file in the main directory for details.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdarg.h>
#include <string.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "Configuration.h"
#include "Logger.h"
#include "Threads.h" // pat added
#include "Timeval.h"

using namespace std;

/**@ The global alarms table. */
//@{
Mutex alarmsLock;
list<string> alarmsList;
void addAlarm(const string &);
//@}

// (pat 3-2014) Note that the logger is used by multiple programs.
pid_t gPid = 0;

// (pat) If Log messages are printed before the classes in this module are inited
// (which happens when static classes have constructors that do work)
// the OpenBTS just crashes.
// Prevent that by setting sLoggerInited to true when this module is inited.
static bool sLoggerInited = 0;
static struct CheckLoggerInitStatus {
	CheckLoggerInitStatus() { sLoggerInited = 1; }
} sCheckloggerInitStatus;

/** Names of the logging levels. */
static const char *levelNames[] = {"EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG"};
static int numLevels = 8;
static bool gLogToConsole = false;
static FILE *gLogFile = nullptr;
static Mutex gLogToLock;
LogGroup gLogGroup;

namespace Logging {

static std::string logApplicationName = "(none)";

void basicConfig(const std::string &appName)
{
	logApplicationName = appName;
}

bool openLogFile(const std::string &filename)
{
	if (gLogFile != nullptr) {
		fclose(gLogFile);
		gLogFile = nullptr;
	}

	if (filename.size() == 0)
		return false;

	bool use_logfile = false;

	if (0 == strncmp(gCmdName, "Open", 4)) use_logfile = true;
	if (0 == strncmp(gCmdName, "RMSC", 4)) use_logfile = true;
	if (0 == strncmp(gCmdName, "RangeFinderGW", 13)) use_logfile = true;

	if (!use_logfile)
		return false;

	fprintf(stderr, "%s: opening log file %s ...\n", gCmdName, filename.c_str());
	gLogFile = fopen(filename.c_str(), "w"); // New log file each time we start.
	if (gLogFile == nullptr) {
		fprintf(stderr, "%s: log file open error: %s\n", gCmdName, strerror(errno));
		return false;

	}

	setvbuf(gLogFile, nullptr, _IONBF, 0);

	string when = Timeval::isoTime(time(NULL), true);
	fprintf(gLogFile, "Starting at %s\n", when.c_str());
	std::cerr << logApplicationName << " logging to file: " << filename << "\n";

	return true;
}

}; /* namespace Logging */

int levelStringToInt(const string &name)
{
	// Reverse search, since the numerically larger levels are more common.
	for (int i = numLevels - 1; i >= 0; i--) {
		if (name == levelNames[i])
			return i;
	}

	// Common substitutions.
	if (name == "INFORMATION")
		return 6;
	if (name == "WARN")
		return 4;
	if (name == "ERROR")
		return 3;
	if (name == "CRITICAL")
		return 2;
	if (name == "EMERGENCY")
		return 0;

	// Unknown level.
	return -1;
}

/** Given a string, return the corresponding level name. */
static int lookupLevel2(const string &key, const string &keyVal)
{
	int level = levelStringToInt(keyVal);

	if (level == -1) {
		string defaultLevel = gConfigTable.mSchema["Log.Level"].getDefaultValue();
		level = levelStringToInt(defaultLevel);
		_LOG(CRIT) << "undefined logging level (" << key << " = \"" << keyVal << "\") defaulting to \""
			   << defaultLevel
			   << ".\" Valid levels are: EMERG, ALERT, CRIT, ERR, WARNING, NOTICE, INFO or DEBUG";
		gConfigTable.set(key, defaultLevel);
	}

	return level;
}

static int lookupLevel(const string &key)
{
	string val = gConfigTable.getStr(key);
	return lookupLevel2(key, val);
}

int getLoggingLevel(const char *filename)
{
	// Default level?
	if (!filename)
		return lookupLevel("Log.Level");

	// This can afford to be inefficient since it is not called that often.
	string keyName;
	keyName.reserve(100);
	keyName.append("Log.Level.");
	keyName.append(filename);
	if (gConfigTable.defines(keyName)) {
		string keyVal = gConfigTable.getStr(keyName);
		// (pat 4-2014) The CLI 'unconfig' command does not unset the value, it just gives an empty value,
		// so check for that and treat it as an unset value, ie, do nothing.
		if (keyVal.size()) {
			return lookupLevel2(keyName, keyVal);
		}
	}
	return lookupLevel("Log.Level");
}

int gGetLoggingLevel(const char *filename)
{
	// This is called a lot and needs to be efficient.

	static Mutex sLogCacheLock;
	static map<uint64_t, int> sLogCache;
	static unsigned sCacheCount;
	static const unsigned sCacheRefreshCount = 1000;

	if (filename == NULL)
		return gGetLoggingLevel("");

	const char *base = strrchr(filename, '/');

	if (base != NULL)
		filename = base + 1;

	HashString hs(filename);
	uint64_t key = hs.hash();

	sLogCacheLock.lock();
	// Time for a cache flush?
	if (sCacheCount > sCacheRefreshCount) {
		sLogCache.clear();
		sCacheCount = 0;
	}
	// Is it cached already?
	map<uint64_t, int>::const_iterator where = sLogCache.find(key);
	sCacheCount++;
	if (where != sLogCache.end()) {
		int retVal = where->second;
		sLogCacheLock.unlock();
		return retVal;
	}
	// Look it up in the config table and cache it.
	// FIXME: Figure out why unlock and lock below fix the config table deadlock.
	// (pat) Probably because getLoggingLevel may call LOG recursively via lookupLevel().
	sLogCacheLock.unlock();
	int level = getLoggingLevel(filename);
	sLogCacheLock.lock();
	sLogCache.insert(pair<uint64_t, int>(key, level));
	sLogCacheLock.unlock();
	return level;
}

// copies the alarm list and returns it. list supposed to be small.
list<string> gGetLoggerAlarms()
{
	alarmsLock.lock();
	list<string> ret;
	// excuse the "complexity", but to use std::copy with a list you need
	// an insert_iterator - copy technically overwrites, doesn't insert.
	insert_iterator<list<string>> ii(ret, ret.begin());
	copy(alarmsList.begin(), alarmsList.end(), ii);
	alarmsLock.unlock();
	return ret;
}

/** Add an alarm to the alarm list. */
void addAlarm(const string &s)
{
	alarmsLock.lock();
	alarmsList.push_back(s);
	unsigned maxAlarms = gConfigTable.getNum("Log.Alarms.Max");
	while (alarmsList.size() > maxAlarms)
		alarmsList.pop_front();
	alarmsLock.unlock();
}

/*
 * class Log
 */

// (pat) This is the log initialization function.
// It is invoked by this line in OpenBTS.cpp, and similar lines in other programs like the TransceiverRAD1:
// 		Log dummy("openbts",gConfigTable.getStr("Log.Level").c_str(),LOG_LOCAL7);
// The LOCAL7 corresponds to the "local7" line in the file /etc/rsyslog.d/OpenBTS.log.
Log::Log(const char *name, const char *level, int facility)
{
	// (pat) This 'constructor' has nothing to do with the regular use of the Log class, so we have
	// to set this special flag to prevent the destructor from generating a syslog message.
	// This is really goofy, but there is a reason - this is the way whoever wrote this got the Logger initialized
	// early during static class initialization since OpenBTS has so many static classes whose constructors do work
	// (a really bad idea) and may generate log messages.
	mDummyInit = true;
	gLogInit(name, level, facility);
}

Log::~Log()
{
	if (mDummyInit)
		return;

	std::string message(mStream.str());

	int mlen = message.size();
	int neednl = (mlen == 0 || message[mlen - 1] != '\n');
	// Anything at or above LOG_CRIT is an "alarm".
	// Save alarms in the local list and echo them to stderr.
	if (mPriority <= LOG_CRIT) {
		if (sLoggerInited)
			addAlarm(message.c_str());
		if (!gLogToConsole) {
			cerr << message;
			if (neednl)
				std::cerr << std::endl;
		}
	}
	// Current logging level was already checked by the macro.
	// So just log.
	syslog(mPriority, "%s", message.c_str());
	// pat added for easy debugging.
	if (gLogToConsole || gLogFile) {
		gLogToLock.lock();
		if (gLogToConsole) {
			// The COUT() macro prevents messages from stomping each other but adds uninteresting thread
			// numbers, so just use std::cout.
			std::cerr << message;
			if (neednl)
				std::cerr << std::endl;
		}
		if (gLogFile) {
			fputs(message.c_str(), gLogFile);
			if (neednl)
				fputc('\n', gLogFile);
			fflush(gLogFile);
		}
		gLogToLock.unlock();
	}
}

ostringstream &Log::get()
{
	assert(mPriority < numLevels);
	mStream << levelNames[mPriority] << ' ';
	return mStream;
}

void gLogInit(const std::string &appName, const std::string &level, int facility)
{
	Logging::logApplicationName = appName;

	gLogToConsole = true; // gConfigTable.getBool("Log.Console");

	// Set the level if one has been specified.
	if (!level.empty()) {
		gConfigTable.set("Log.Level", level);
	}

	gPid = getpid();

	// Pat added, tired of the syslog facility.
	// Both the transceiver and OpenBTS use this same Logger class, but only RMSC/OpenBTS/OpenNodeB may use this log
	// file:
	std::string key = "Log.File." + Logging::logApplicationName;

	if (gConfigTable.defines(key)) {
		string logfilename = gConfigTable.getStr(key);
		if (!logfilename.empty())
			Logging::openLogFile(logfilename);
	}

	// Open the log connection.
	openlog(Logging::logApplicationName.c_str(), 0, facility);

	// We cant call this from the Mutex itself because the Logger uses Mutex.
	gMutexLogLevel = gGetLoggingLevel("Mutex.cpp");
}

void gLogEarly(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(level | LOG_USER, fmt, args);
	va_end(args);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
}

// Return _NumberOfLogGroups if invalid.
LogGroup::Group LogGroup::groupNameToIndex(const char *groupName) const
{
	for (unsigned g = (Group)0; g < _NumberOfLogGroups; g++) {
		if (0 == strcasecmp(mGroupNames[g], groupName)) {
			return (Group)g;
		} // happiness
	}
	return _NumberOfLogGroups; // failed
}

LogGroup::LogGroup() { LogGroupInit(); }

// These must match LogGroup::Group.
const char *LogGroup::mGroupNames[] = {
	"Control",
	"SIP",
	"GSM",
	"GSM_L1",
	"GSM_L2",
	"GSM_L3",
	"GPRS",
	"Layer2",
	"SMS",
	NULL
};

void LogGroup::LogGroupInit()
{
	// Error check some more.
	assert(0 == strcmp(mGroupNames[Control], "Control"));
	assert(0 == strcmp(mGroupNames[SIP], "SIP"));
	assert(0 == strcmp(mGroupNames[GSM], "GSM"));
	assert(0 == strcmp(mGroupNames[GSM_L1], "GSM_L1"));
	assert(0 == strcmp(mGroupNames[GSM_L2], "GSM_L2"));
	assert(0 == strcmp(mGroupNames[GSM_L3], "GSM_L3"));
	assert(0 == strcmp(mGroupNames[GPRS], "GPRS"));
	assert(0 == strcmp(mGroupNames[Layer2], "Layer2"));

	static_assert((sizeof(mGroupNames) / sizeof(mGroupNames[0])) == (_NumberOfLogGroups + 1), "mGroupNames size mismatch");

	for (unsigned g = 0; g < _NumberOfLogGroups; g++) {
		mDebugLevel[g] = 0;
		mWatchLevel[g] = 0;
	}
}

static const char *getNonEmptyStrIfDefined(string param)
{
	if (!gConfigTable.defines(param)) {
		return NULL;
	}
	string result = gConfigTable.getStr(param);
	// (pat) The "unconfig" command does not remove the value, it just gives it an empty value, so check for that.
	return result.size() ? result.c_str() : NULL;
}

// Set all the Log.Group debug levels based on database settings
void LogGroup::setAll()
{
	LOG(DEBUG);
	string groupprefix = string("Log.Group.");
	string watchprefix = string("Log.Watch.");
	for (unsigned g = 0; g < _NumberOfLogGroups; g++) {
		{
			string param = groupprefix + mGroupNames[g];
			int level = 0;
			if (const char *levelName = getNonEmptyStrIfDefined(param)) {
				level = lookupLevel2(param, levelName);
			}
			mDebugLevel[g] = level;
		}

		{
			string watchparam = watchprefix + mGroupNames[g];
			int watchlevel = 0;
			if (const char *levelName = getNonEmptyStrIfDefined(watchparam)) {
				watchlevel = lookupLevel2(watchparam, levelName);
			}
			mWatchLevel[g] = watchlevel;
		}
	}
}
