#include <iomanip>
#include <cstring>
#include <ctime>
#include <stdexcept>
//#include <chrono>

#include <sys/time.h>

#include "logger.h"

std::ostream& operator<<(std::ostream& stream, unixError error)
{
	return stream << "Error " << error.getErrorNo() << " (" << strerror(error.getErrorNo()) << ") in function " << error.getErrorFunc() << " occurred";
}

void operator<<(std::ostream& stream, endOfMsg)
{
	auto pLogStream = dynamic_cast<LogStream*>(&stream);
	if (pLogStream)
		pLogStream->end();
}

unixError::unixError(string _errorFunc) : errorFunc(_errorFunc), errorNo(errno) 
{
}

void LogStream::end()
{
	if (throwException)
		throw std::runtime_error(str());
	else
	{
		timeval tv;
		gettimeofday(&tv, 0);
		//using std::chrono::system_clock;
		//std::time_t now = system_clock::to_time_t(system_clock::now());
		std::time_t now = tv.tv_sec;
		//cout << std::put_time(std::localtime(&now), "%F %T ");
		char nowStr[40];
		strftime(nowStr, sizeof(nowStr), "%F %T", std::localtime(&now));
		cout << nowStr << "." << std::setw(3) << std::setfill('0') << tv.tv_usec / 1000 << " ";
		
		switch (level)
		{
			case LogLevel::DEBUG:
				cout << "[DEBUG]"; break;
			case LogLevel::INFO:
				cout << "[INFO ]"; break;
			case LogLevel::WARN:
				cout << "[WARN ]"; break;
			case LogLevel::ERROR:
				cout << "[ERROR]"; break;
		}

		cout << " " << logger.getComponent() << ": " << str() << endl;
	}
}
