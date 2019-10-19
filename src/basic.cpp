#include <cstring>
#include <iomanip>

#include "basic.h"

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
		stream << std::setw(2) << std::setfill('0') << std::hex << int(Byte(s[i])) << " ";
	return stream.str();
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

Value ValueType::convert(const Value& value) const
{
	if (value.getType() == code)
		return value;

	if (code == VOID)
		return Value::newVoid();

	if (value.getType() == STRING)
	{
		string str = value.getString();
		switch (code)
		{
			case NUMBER:
				try
				{
					return Value(std::stod(str));
				}
				catch (const std::exception& ex)
				{
					// ignore
				}
				break;
			case BOOLEAN:
				if (str == "1" || str == "yes" || str == "YES" || str == "true" || str == "TRUE")
					return Value(true);
				else if (str == "0" || str == "no" || str == "NO" || str == "false" || str == "FALSE")
					return Value(false);
				break;
		}
	}

	return Value();
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

bool Item::isPollRequired(std::time_t now) const
{
	return pollInterval && lastPollTime + pollInterval <= now;
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
		if (  num < minimum
		   || num > maximum
		   || num < oldNum * (1.0 - relVariation / 100.0) - absVariation
		   || num > oldNum * (1.0 + relVariation / 100.0) + absVariation
		   )
			return true;
	}

	return true;
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
