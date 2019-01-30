#ifndef LOGGER_H
#define LOGGER_H

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

class LogStream: public std::ostringstream
{
	friend Logger;
	friend void operator<<(std::ostream&, endOfMsg);

	private:
	LogLevel level;
	const Logger& logger;
	bool throwException;

	public:
	LogStream(const LogStream& stream) : logger(stream.logger), level(stream.level), throwException(stream.throwException)  {}

	private:
	LogStream(const Logger& _logger, LogLevel _level, bool _throwException = false) : logger(_logger), level(_level), throwException(_throwException) {}
	void end();
};

class Logger
{
	private:
	string component;

	public:
	Logger(string _component) : component(_component) {}
	string getComponent() const { return component; }
	LogStream debug() const { return LogStream(*this, LogLevel::DEBUG); }
	LogStream info() const { return LogStream(*this, LogLevel::INFO); } 
	LogStream warn() const { return LogStream(*this, LogLevel::WARN); }
	LogStream error() const { return LogStream(*this, LogLevel::ERROR); }
	LogStream errorX() const { return LogStream(*this, LogLevel::ERROR, true); }
};

#endif