#include <iomanip>
#include <cstring>
#include <ctime>
#include <stdexcept>

#include <sys/time.h>

#include "logger.h"

std::ostream& operator<<(std::ostream& stream, unixError error)
{
	return stream << "Error " << error.getErrorNo() << " (" << strerror(error.getErrorNo()) << ") returned by " << error.getErrorFunc() << "()";
}

void operator<<(std::ostream& stream, endOfMsg)
{
	auto pLogMsg = dynamic_cast<LogMsg*>(&stream);
	if (pLogMsg)
		pLogMsg->end();
	else
	{
		auto pError = dynamic_cast<Error*>(&stream);
		if (pError)
			throw std::runtime_error(pError->str());
	}
}

unixError::unixError(string _errorFunc) : errorFunc(_errorFunc), errorNo(errno) 
{
}

void LogMsg::end()
{
	if (throwException)
		throw std::runtime_error(str());
	else
	{
		std::ostringstream stream;

		timeval tv;
		gettimeofday(&tv, 0);
		//using std::chrono::system_clock;
		//std::time_t now = system_clock::to_time_t(system_clock::now());
		std::time_t now = tv.tv_sec;
		//cout << std::put_time(std::localtime(&now), "%F %T ");
		char nowStr[40];
		strftime(nowStr, sizeof(nowStr), "%F %T", std::localtime(&now));
		stream << nowStr << "." << std::setw(3) << std::setfill('0') << tv.tv_usec / 1000 << " ";
		
		switch (level)
		{
			case LogLevel::DEBUG:
				stream << "[DEBUG]"; break;
			case LogLevel::INFO:
				stream << "[INFO ]"; break;
			case LogLevel::WARN:
				stream << "[WARN ]"; break;
			case LogLevel::ERROR:
				stream << "[ERROR]"; break;
		}

		stream << " " << component << ": " << str();
		log.addMsg(stream.str());
	}
}

void Log::init(LogConfig _config) 
{ 
	config = _config;
	
	if (!config.getFileName().empty())
	{
		logFile.open(config.getFileName(), std::ios::out | std::ios::app);
		if (!logFile.is_open())
			throw std::runtime_error("Can not open file " + config.getFileName());
	}
}

void Log::addMsg(string msg) 
{
	if (!config.getFileName().empty())
	{
		if (!logFile.is_open())
			logFile.open(config.getFileName(), std::ios::out | std::ios::app);
		if (logFile.is_open())
		{
			logFile << msg << endl;
			if (config.getMaxFileSize() > 0)
			{
				long pos = logFile.tellp();
				if (pos > config.getMaxFileSize())
				{
					logFile.close();

					rename(config.getFileName().c_str(), (config.getFileName() + ".0").c_str());
					remove((config.getFileName() + "." + cnvToStr(config.getMaxFileCount())).c_str());
					for (int i = config.getMaxFileCount() - 1; i >= 0; i--)
						rename((config.getFileName() + "." + cnvToStr(i)).c_str(), (config.getFileName() + "." + cnvToStr(i + 1)).c_str());
				}
			}
		}
	}
	cout << msg << endl;
}

