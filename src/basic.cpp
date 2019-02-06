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

	if (value.getType() == STRING)
	{
		string str = value.getString();
		switch (code)
		{
			case VOID:
				return Value();
			case NUMBER:
				try
				{
					return Value(std::stod(str));
				}
				catch (const std::exception& ex)
				{
					// ignore
				}
			case BOOLEAN:
				if (str == "1" || str == "yes" || str == "YES" || str == "true" || str == "TRUE")
					return Value(true);
				else if (str == "0" || str == "no" || str == "NO" || str == "false" || str == "FALSE")
					return Value(false);
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
				return "void";
			default:
				return "?";
		}
}


bool Value::operator==(const Value& x) const
{
	return (  null && x.null
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

bool Item::updateValue(const Value& newValue)
{
	if (value == newValue)
		return false;

	if (newValue.isNumber() && value.isNumber())
	{
		double oldNum = value.getNumber();
		double newNum = newValue.getNumber();
		if (newNum >= oldNum * (100 - relDelta) - absDelta && newNum <= oldNum * (100 + relDelta) + absDelta)
			return false;
	}
	
	value = newValue;
	return true;
}

string Items::getOwnerId(string itemId) const 
{ 
	auto pos = find(itemId); 
	assert(pos != end());
	return pos->second.getOwnerId();
}

