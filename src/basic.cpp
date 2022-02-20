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
		case UNINITIALIZED:
			return "uninitialized";
		case UNDEFINED:
			return "undefined";
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
	if (typeStr == "uninitialized")
		type = UNINITIALIZED;
	else if (typeStr == "undefined")
		type = UNDEFINED;
	else if (typeStr == "void")
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

string ValueTypes::toStr() const
{
	string s;
	for (auto type : *this)
		s += type.toStr() + "|";
	return s.substr(0, s.length() - 1);
}

string Value::toStr() const
{
	switch (type)
	{
		case ValueType::BOOLEAN:
			return boolean ? "true" : "false";
		case ValueType::STRING:
			return str;
		case ValueType::NUMBER:
		{
			std::ostringstream stream;
			stream << std::setprecision(std::numeric_limits<double>::digits10 + 1) << number;
			return stream.str();
		}
		case ValueType::VOID:
			return "void";
		case ValueType::UNINITIALIZED:
			return "uninitialized";
		case ValueType::UNDEFINED:
			return "undefined";
		default:
			return "?";
	}
}

bool Value::operator==(const Value& x) const
{
	return (  x.type == type
	       && (  (type == ValueType::STRING && x.str == str)
	          || (type == ValueType::BOOLEAN && x.boolean == boolean)
	          || (type == ValueType::NUMBER && x.number == number)
	          || type == ValueType::VOID
	          || type == ValueType::UNINITIALIZED
	          || type == ValueType::UNDEFINED
	          )
	       );
}

void Item::addToHistory(TimePoint now, const Value& value)
{
	if (!value.isNumber() || historyPeriod == Seconds::zero())
		return;
	history.emplace_back(now, value.getNumber());
	while (!history.empty() && history.front().timePoint < now - historyPeriod)
		history.pop_front();
}

Value Item::calcMinFromHistory(TimePoint start) const
{
	if (lastValue.isUninitialized() || !lastValue.isNumber())
		return Value::newUndefined();
	auto number = lastValue.getNumber();
	for (auto pos = history.rbegin(); pos != history.rend() && pos->timePoint >= start; pos++)
		if (pos->number < number)
			number = pos->number;
	return Value::newNumber(number);
}

Value Item::calcMaxFromHistory(TimePoint start) const
{
	if (lastValue.isUninitialized() || !lastValue.isNumber())
		return Value::newUndefined();
	auto number = lastValue.getNumber();
	for (auto pos = history.rbegin(); pos != history.rend() && pos->timePoint >= start; pos++)
		if (pos->number > number)
			number = pos->number;
	return Value::newNumber(number);
}

bool Item::isSendOnTimerRequired(TimePoint now) const
{
	return sendOnTimerParams.active && !lastValue.isUninitialized() && lastSendTime + sendOnTimerParams.interval <= now;
}

bool Item::isSendOnChangeRequired(const Value& value) const
{
	if (!sendOnChangeParams.active)
		return true;

	if (lastValue == value)
		return false;

	if (value.isNumber() && lastValue.isNumber())
	{
		double oldNum = lastValue.getNumber();
		double num = value.getNumber();
		if (  num >= sendOnChangeParams.minimum
		   && num <= sendOnChangeParams.maximum
		   && num >= oldNum * (1.0 - sendOnChangeParams.relVariation / 100.0) - sendOnChangeParams.absVariation
		   && num <= oldNum * (1.0 + sendOnChangeParams.relVariation / 100.0) + sendOnChangeParams.absVariation
		   )
			return false;
	}

	return true;
}

bool Item::isPollingRequired(TimePoint now) const
{
	assert(pollingInterval != Seconds::zero());
	return lastPollingTime + pollingInterval <= now;
}

void Item::initPolling(TimePoint now)
{
	assert(pollingInterval != Seconds::zero());
	lastPollingTime = now - Seconds(std::rand() % pollingInterval.count());
}

void Item::pollingDone(TimePoint now)
{
	assert(pollingInterval != Seconds::zero());
	lastPollingTime = now;
}

void Item::validateReadable(bool _readable) const
{
	if (readable && !_readable)
		throw std::runtime_error("Item " + id + " must not be readable");
	if (!readable && _readable)
		throw std::runtime_error("Item " + id + " must be readable");
}

void Item::validateWritable(bool _writable) const
{
	if (writable && !_writable)
		throw std::runtime_error("Item " + id + " must not be writable");
	if (!writable && _writable)
		throw std::runtime_error("Item " + id + " must be writable");
}

void Item::validateResponsive(bool _responsive) const
{
	if (responsive && !_responsive)
		throw std::runtime_error("Item " + id + " must not be responsive");
	if (!responsive && _responsive)
		throw std::runtime_error("Item " + id + " must be responsive");
}

void Item::validatePollingEnabled(bool _enabled) const
{
	if (pollingInterval != Seconds::zero() && !_enabled)
		throw std::runtime_error("Item " + id + " must not be polled");
	if (pollingInterval == Seconds::zero() && _enabled)
		throw std::runtime_error("Item " + id + " must be polled");
}

void Item::validateHistory() const
{
	if (historyPeriod == Seconds::zero())
		throw std::runtime_error("Item " + id + " must be historized");
}

void Item::validateType(ValueType _type) const
{
	if (!hasType(_type))
		throw std::runtime_error("Item " + id + " must be of type " + _type.toStr());
}

void Item::validateTypeNot(ValueType _type) const
{
	if (hasType(_type))
		throw std::runtime_error("Item " + id + " must not be of type " + _type.toStr());
}

void Item::validateOwnerId(LinkId _ownerId) const
{
	if (ownerId != _ownerId)
		throw std::runtime_error("Item " + id + " must be owned by link " + _ownerId);
}

Item& Items::validate(ItemId itemId)
{
	auto pos = find(itemId);
	if (pos == end())
		throw std::runtime_error("Item " + itemId + " referenced but not defined");
	return pos->second;
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
