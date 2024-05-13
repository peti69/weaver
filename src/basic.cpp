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
	for (auto i : s)
		stream << std::setw(2) << std::setfill('0') << std::hex << int(i);
	return stream.str();
}

string cnvToHexStr(string s)
{
	auto int2char = [](int input)
	{
		if (input >= 0 && input <= 9)
			return '0' + input;
		if (input >= 10 && input <= 15)
			return 'A' + input - 10;
		throw std::invalid_argument("cnvToHexStr: Invalid input character");
	};

	string result(s.length() * 2, '0');
	for (int i = 0; i < s.length(); i++)
	{
		result[i * 2] = int2char((s[i] & 0xF0) >> 4);
		result[i * 2 + 1] = int2char(s[i] & 0x0F);
	}
	return result;
}

string cnvFromHexStr(string s)
{
	auto char2int = [](char input)
	{
		if (input >= '0' && input <= '9')
			return input - '0';
		if (input >= 'A' && input <= 'F')
			return input - 'A' + 10;
		if (input >= 'a' && input <= 'f')
			return input - 'a' + 10;
		throw std::invalid_argument("cnvFromHexStr: Invalid input character");
	};

	if (s.length() % 2 != 0)
		throw std::invalid_argument("cnvFromHexStr: Invalid input string");

	string result(s.length() / 2, 0x00);
	for (int i = 0; i < s.length() / 2; i++)
		result[i] = char2int(s[i * 2]) * 16 + char2int(s[i * 2 + 1]);
	return result;
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
	tp.tm_isdst = -1;
	timePoint = TimePoint(std::chrono::system_clock::from_time_t(std::mktime(&tp)));
	return true;
}

