#ifndef LOGGER_H
#define LOGGER_H

#include <fstream>

#include "basic.h"

class endOfMsg
{
};

class unixError
{
	private:
	string errorFunc;
	int errorNo;
	
	public:
	unixError(string errorFunc);
	string getErrorFunc() const { return errorFunc; }
	int getErrorNo() const { return errorNo; }
};

extern void operator<<(std::ostream&, endOfMsg);
extern std::ostream& operator<<(std::ostream&, unixError);

enum class LogLevel
{
	DEBUG,
	INFO,
	WARN,
	ERROR
};

class Logger;
class Log;

class Error: public std::ostringstream
{
};

class LogMsg: public std::ostringstream
{
	friend Logger;
	friend Log;
	friend void operator<<(std::ostream&, endOfMsg);

	private:
	Log& log;
	string component;
	LogLevel level;
	bool throwException;

	public:
	LogMsg(const LogMsg& x) : log(x.log), component(x.component), level(x.level), throwException(x.throwException)  {}

	private:
	LogMsg(Log& log, string component, LogLevel level, bool throwException = false) :
		log(log), component(component), level(level), throwException(throwException) {}
	void end();
};

class Logger
{
	friend Log;

	private:
	Log& log;
	string component;
	Logger(Log& log, string component) : log(log), component(component) {}

	public:
	LogMsg debug() const { return LogMsg(log, component, LogLevel::DEBUG); }
	LogMsg info() const { return LogMsg(log, component, LogLevel::INFO); } 
	LogMsg warn() const { return LogMsg(log, component, LogLevel::WARN); }
	LogMsg error() const { return LogMsg(log, component, LogLevel::ERROR); }
	LogMsg errorX() const { return LogMsg(log, component, LogLevel::ERROR, true); }
};

class LogConfig
{
	private:
	string fileName;
	int maxFileSize = 0;
	int maxFileCount = 0;
	LogLevel minLevel = LogLevel::DEBUG;

	public:
	LogConfig() {}
	LogConfig(string fileName, int maxFileSize, int maxFileCount, LogLevel minLevel) :
		fileName(fileName), maxFileSize(maxFileSize), maxFileCount(maxFileCount), minLevel(minLevel)
	{}
	string getFileName() const { return fileName; }
	int getMaxFileSize() const { return maxFileSize; }
	int getMaxFileCount() const { return maxFileCount; }
	LogLevel getMinLevel() const { return minLevel; }
};

class Log
{
	friend LogMsg;

	private:
	LogConfig config;
	std::ofstream logFile;

	public:
	void init(LogConfig _config);
	Logger newLogger(string component) { return Logger(*this, component); }

	private:
	void addMsg(const LogMsg& msg);
};

#endif
