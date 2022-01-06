#include <cstring>
#include <cctype>
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

string ValueType::toStr() const
{
	switch (code)
	{
		case VOID:
			return "void";
		case NUMBER:
			return "number";
		case STRING:
			return "string";
		case BOOLEAN:
			return "boolean";
		default:
			return "?";
	}
}

bool ValueType::fromStr(string typeStr, ValueType& type)
{
	if (typeStr == "void")
		type = VOID;
	else if (typeStr == "number")
		type = NUMBER;
	else if (typeStr == "string")
		type = STRING;
	else if (typeStr == "boolean")
		type = BOOLEAN;
	else
		return false;
	return true;
}

Value ValueType::convert(string valueStr) const
{
	switch (code)
	{
		case VOID:
			return Value::newVoid();
		case NUMBER:
			try
			{
				return Value(std::stod(valueStr));
			}
			catch (const std::exception& ex)
			{
			}
			return Value();
		case STRING:
			return Value(valueStr);
		case BOOLEAN:
			valueStr = toUpper(valueStr);
			if (valueStr == "TRUE" || valueStr == "YES" || valueStr == "ON" || valueStr == "1")
				return true;
			else if (valueStr == "FALSE" || valueStr == "NO" || valueStr == "OFF" || valueStr == "0")
				return false;
			else
				return Value();
		default:
			return Value();
	}
}

string Value::toStr() const
{
	if (null)
		return "null";
	else
		switch (type)
		{
			case ValueType::BOOLEAN:
				return boo ? "true" : "false";
			case ValueType::STRING:
				return str;
			case ValueType::NUMBER:
				return cnvToStr(num);
			case ValueType::VOID:
				return "-";
			default:
				return "?";
		}
}

bool Value::operator==(const Value& x) const
{
	return (  (null && x.null)
           || (  !null && !x.null
              && x.type == type 
	          && (  (type == ValueType::STRING && x.str == str)
	             || (type == ValueType::BOOLEAN && x.boo == boo)
	             || (type == ValueType::NUMBER && x.num == num)
	             || type == ValueType::VOID
	             )
	          )
	       );
}

bool Item::isPollingRequired(std::time_t now) const
{
	assert(pollingInterval);
	return lastPollingTime + pollingInterval <= now;
}

void Item::initPolling(std::time_t now)
{
	assert(pollingInterval);
	lastPollingTime = now - std::rand() % pollingInterval;
}

void Item::pollingDone(std::time_t now)
{
	assert(pollingInterval);
	lastPollingTime = now;
}

bool Item::isSendRequired(std::time_t now) const
{
	return sendOnTimer && !lastSendValue.isNull() && lastSendTime + duration <= now;
}

bool Item::isSendRequired(const Value& value) const
{
	if (!sendOnChange)
		return true;

	if (lastSendValue == value)
		return false;

	if (value.isNumber() && lastSendValue.isNumber())
	{
		double oldNum = lastSendValue.getNumber();
		double num = value.getNumber();
		if (  num >= minimum
		   && num <= maximum
		   && num >= oldNum * (1.0 - relVariation / 100.0) - absVariation
		   && num <= oldNum * (1.0 + relVariation / 100.0) + absVariation
		   )
			return false;
	}

	return true;
}

void Item::validateReadable(bool _readable)
{
	if (readable && !_readable)
		throw std::runtime_error("Item " + id + " must not be readable");
	if (!readable && _readable)
		throw std::runtime_error("Item " + id + " must be readable");
}

void Item::validateWritable(bool _writable)
{
	if (writable && !_writable)
		throw std::runtime_error("Item " + id + " must not be writable");
	if (!writable && _writable)
		throw std::runtime_error("Item " + id + " must be writable");
}

void Item::validateResponsive(bool _responsive)
{
	if (responsive && !_responsive)
		throw std::runtime_error("Item " + id + " must not be responsive");
	if (!responsive && _responsive)
		throw std::runtime_error("Item " + id + " must be responsive");
}

void Item::validatePollingEnabled(bool _enabled)
{
	if (pollingInterval > 0 && !_enabled)
		throw std::runtime_error("Item " + id + " must not be polled");
	if (pollingInterval <= 0 && _enabled)
		throw std::runtime_error("Item " + id + " must be polled");
}

void Item::validateType(ValueType _type)
{
	if (type != _type)
		throw std::runtime_error("Item " + id + " must be of type " + _type.toStr());
}

void Item::validateTypeNot(ValueType _type)
{
	if (type == _type)
		throw std::runtime_error("Item " + id + " must not be of type " + _type.toStr());
}

void Item::validateOwnerId(string _ownerId)
{
	if (ownerId != _ownerId)
		throw std::runtime_error("Item " + id + " must be owned by link " + _ownerId);
}

Item& Items::validate(string itemId)
{
	auto pos = find(itemId);
	if (pos == end())
		throw std::runtime_error("Item " + itemId + " referenced but not defined");
	return pos->second;
}

string Items::getOwnerId(string itemId) const 
{ 
	auto pos = find(itemId); 
	assert(pos != end());
	return pos->second.getOwnerId();
}

string EventType::toStr() const
{
	switch (code)
	{
		case STATE_IND:
			return "STATE_IND";
		case WRITE_REQ:
			return "WRITE_REQ";
		case READ_REQ:
			return "READ_REQ";
		default:
			return "?";
	}
}

bool EventType::fromStr(string typeStr, EventType& type)
{
	if (typeStr == "STATE_IND")
		type = STATE_IND;
	else if (typeStr == "WRITE_REQ")
		type = WRITE_REQ;
	else if (typeStr == "READ_REQ")
		type = READ_REQ;
	else
		return false;
	return true;
}
