#include <iomanip>
#include <algorithm>
#include <bitset>

#include "basic.h"

string toUpper(string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
	return s;
}

string cnvToHexStr(Byte b)
{
	std::ostringstream stream;
	stream << std::setw(2) << std::setfill('0') << std::hex << int(b);
	return stream.str();
}

string cnvToHexStr(ByteString s)
{
	std::ostringstream stream;
	for (int i = 0; i < s.length(); i++) 
		stream << (i > 0 ? " " : "") << std::setw(2) << std::setfill('0') << std::hex << int(s[i]);
	return stream.str();
}

string cnvToHexStr(string s)
{
	std::ostringstream stream;
	for (int i = 0; i < s.length(); i++) 
		stream << (i > 0 ? " " : "") << std::setw(2) << std::setfill('0') << std::hex << int(Byte(s[i]));
	return stream.str();
}

string cnvToBinStr(string s)
{
	string r;
	r.reserve(s.length() * 8);
	for (int i = 0; i < s.length(); i++)
		r += std::bitset<8>(Byte(s[i])).to_string();
	return r;
}

string cnvToAsciiStr(ByteString s)
{
	return string(reinterpret_cast<const char*>(s.data()), s.length());
}

ByteString cnvFromAsciiStr(string s)
{
	return ByteString(reinterpret_cast<const unsigned char*>(s.data()), s.length());
}

string TimePoint::toStr(string timePointFormat) const
{
	std::time_t tp = std::chrono::system_clock::to_time_t(*this);
	std::stringstream stream;
	stream << std::put_time(std::localtime(&tp), timePointFormat.c_str());
	return stream.str();
}

bool TimePoint::fromStr(string timePointStr, TimePoint& timePoint, string timePointFormat)
{
	std::stringstream stream(timePointStr);
	std::tm tp;
	stream >> std::get_time(&tp, timePointFormat.c_str());
	if (stream.fail())
		return false;
	timePoint = TimePoint(std::chrono::system_clock::from_time_t(std::mktime(&tp)));
	return true;
}

