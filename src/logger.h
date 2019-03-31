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
	friend void operator<<(std::ostream&, endOfMsg);

	private:
	Log& log;
	string component;
	LogLevel level;
	bool throwException;

	public:
	LogMsg(const LogMsg& x) : log(x.log), component(x.component), level(x.level), throwException(x.throwException)  {}

	private:
	LogMsg(Log& _log, string _component, LogLevel _level, bool _throwException = false) : 
		log(_log), component(_component), level(_level), throwException(_throwException) {}
	void end();
};

class Logger
{
	friend Log;

	private:
	Log& log;
	string component;
	Logger(Log& _log, string _component) : log(_log), component(_component) {}

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
	int maxFileSize;
	int maxFileCount;
	
	public:
	LogConfig() : maxFileSize(0), maxFileCount(0) {}
	LogConfig(string _fileName, int _maxFileSize, int _maxFileCount) :
		fileName(_fileName), maxFileSize(_maxFileSize), maxFileCount(_maxFileCount)
	{}
	string getFileName() const { return fileName; }
	int getMaxFileSize() const { return maxFileSize; }
	int getMaxFileCount() const { return maxFileCount; }
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
	void addMsg(string msg);
};

#endif