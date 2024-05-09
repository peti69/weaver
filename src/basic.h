#ifndef BASIC_H
#define BASIC_H

#include <chrono>
#include <string>
#include <iostream> 
#include <sstream> 

using std::string;
using std::cout;
using std::endl;

typedef unsigned char Byte;
typedef std::basic_string<Byte> ByteString;

//template<typename T>
//string cnvToHexStr(T v)
//{
//	std::ostringstream stream;
//	stream << std::hex << v;
//	return stream.str();
//}

template<typename T>
string cnvToStr(T v)
{
	std::ostringstream stream;
	stream << v;
	return stream.str();
}

extern string cnvToHexStr(Byte b);
extern string cnvToHexStr(ByteString s);
extern string cnvToHexStr(string s);
extern string cnvFromHexStr(string s);

extern string cnvToBinStr(string s);

extern string cnvToAsciiStr(ByteString s);
extern ByteString cnvFromAsciiStr(string s);

using Seconds = std::chrono::seconds;
using Clock = std::chrono::system_clock;
using namespace std::chrono_literals;

class TimePoint: public std::chrono::time_point<Clock>
{
private:
	using Base = std::chrono::time_point<Clock>;

public:
	TimePoint() : Base(0s) {}
	TimePoint(Base tp) : Base(tp) {}

	bool isNull() const { return *this == Base(0s); }
	void setToNull() { *this = Base(0s); }

	string toStr(string timePointFormat = "%Y-%m-%dT%H:%M:%S") const;
	static bool fromStr(string timePointStr, TimePoint& timePoint, string timePointFormat = "%Y-%m-%dT%H:%M:%S");
};

using Number = double;

struct Stopwatch
{
	using Clock = std::chrono::steady_clock;
	Clock::time_point start;
	Stopwatch() : start(Clock::now()) {}
	int getRuntime() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count(); }
};


#endif
