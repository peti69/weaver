#include <iomanip>

#include "value.h"

const std::map<UnitType, string> UnitType::details{
	{UnitType::UNKNOWN, "unknown"},
	{UnitType::PERIOD, "period"},
	{UnitType::SPEED, "speed"},
	{UnitType::TEMPERATURE, "temperature"},
	{UnitType::VOLUME, "volume"},
	{UnitType::ILLUMINANCE, "illuminance"},
	{UnitType::CURRENT, "current"},
	{UnitType::ENERGY, "energy"},
	{UnitType::VOLTAGE, "voltage"}};

string UnitType::toStr() const
{
	if (auto pos = details.find(code); pos != details.end())
		return pos->second;
	else
		return "?";
}

const std::map<Unit, Unit::Detail> Unit::details{
	{Unit::UNKNOWN, {UnitType::UNKNOWN, "unknown", true}},
	{Unit::PERCENT, {UnitType::UNKNOWN, "%", true}},
	{Unit::MINUTE, {UnitType::PERIOD, "min", true}},
	{Unit::SECOND, {UnitType::PERIOD, "s", true}},
	{Unit::METER_PER_SECOND, {UnitType::SPEED, "m/s", true}},
	{Unit::CELCIUS, {UnitType::TEMPERATURE, "°C", true}},
	{Unit::LUX, {UnitType::ILLUMINANCE, "lx", true}},
	{Unit::KILOLUX, {UnitType::ILLUMINANCE, "klx", true}},
	{Unit::GRAM_PER_CUBICMETER, {UnitType::UNKNOWN, "g/m³", true}},
	{Unit::WATT, {UnitType::POWER, "W", true}},
	{Unit::KILOWATT, {UnitType::POWER, "kW", true}},
	{Unit::WATTHOUR, {UnitType::ENERGY, "Wh", true}},
	{Unit::KILOWATTHOUR, {UnitType::ENERGY, "kWh", true}},
	{Unit::CUBICMETER, {UnitType::VOLUME, "m³", true}},
	{Unit::DEGREE, {UnitType::UNKNOWN, "°", false}},
	{Unit::LITER_PER_MINUTE, {UnitType::UNKNOWN, "l/min", true}},
	{Unit::MILLIAMPERE, {UnitType::CURRENT, "mA", true}},
	{Unit::AMPERE, {UnitType::CURRENT, "A", true}},
	{Unit::MILLIVOLT, {UnitType::VOLTAGE, "mV", true}},
	{Unit::VOLT, {UnitType::VOLTAGE, "V", true}},
	{Unit::MILLIMETER, {UnitType::UNKNOWN, "mm", true}},
	{Unit::EURO, {UnitType::UNKNOWN, "€", true}},
	{Unit::HOUR, {UnitType::PERIOD, "h", true}},
	{Unit::KILOMETER_PER_HOUR, {UnitType::SPEED, "km/h", true}},
	{Unit::MILES_PER_HOUR, {UnitType::SPEED, "mi/h", true}},
	{Unit::FAHRENHEIT, {UnitType::TEMPERATURE, "°F", true}}};

string Unit::toStr() const
{
	if (auto pos = details.find(code); pos != details.end())
		return pos->second.str;
	else
		return "?";
}

string Unit::toStr(string valueStr) const
{
	if (code == UNKNOWN)
		return valueStr;
	else if (auto pos = details.find(code); pos != details.end())
		return valueStr + (pos->second.blank ? " " : "") + pos->second.str;
	else
		return "?";
}

bool Unit::fromStr(string unitStr, Unit& unit)
{
	for (const auto& detailPair : details)
		if (unitStr == detailPair.second.str)
		{
			unit = detailPair.first;
			return true;
		}
	return false;
}

UnitType Unit::getType() const
{
	if (auto pos = details.find(code); pos != details.end())
		return pos->second.type;
	else
		return UnitType::UNKNOWN;
}

bool Unit::canConvertTo(Unit targetUnit) const
{
	return targetUnit == *this || getType() == targetUnit.getType();
}

Number Unit::convertTo(Number value, Unit targetUnit) const
{
	assert(canConvertTo(targetUnit));
	if (targetUnit == code)
		return value;
	switch (code)
	{
		case SECOND:
			switch (targetUnit)
			{
				case MINUTE: return value / 60;
				case HOUR: return value / 3600;
			}
			break;
		case MINUTE:
			switch (targetUnit)
			{
				case SECOND: return value * 60;
				case HOUR: return value / 60;
			}
			break;
		case HOUR:
			switch (targetUnit)
			{
				case SECOND: return value * 3600;
				case MINUTE: return value * 60;
			}
			break;
		case CELCIUS:
			switch (targetUnit)
			{
				case FAHRENHEIT: return value * 9/5 + 32;
			}
			break;
		case FAHRENHEIT:
			switch (targetUnit)
			{
				case CELCIUS: return (value - 32) * 5/9;
			}
			break;
		case METER_PER_SECOND:
			switch (targetUnit)
			{
				case KILOMETER_PER_HOUR: return value * 3.6;
				case MILES_PER_HOUR: return value * 2.236942;
			}
			break;
		case KILOMETER_PER_HOUR:
			switch (targetUnit)
			{
				case METER_PER_SECOND: return value / 3.6;
				case MILES_PER_HOUR: return value * 0.62137;
			}
			break;
		case MILES_PER_HOUR:
			switch (targetUnit)
			{
				case METER_PER_SECOND: return value * 1/2.236942;
				case KILOMETER_PER_HOUR: return value / 0.62137;
			}
			break;
		case LUX:
			switch (targetUnit)
			{
				case KILOLUX: return value / 1000;
			}
			break;
		case KILOLUX:
			switch (targetUnit)
			{
				case LUX: return value * 1000;
			}
			break;
		case WATTHOUR:
			switch (targetUnit)
			{
				case KILOWATTHOUR: return value / 1000;
			}
			break;
		case KILOWATTHOUR:
			switch (targetUnit)
			{
				case WATTHOUR: return value * 1000;
			}
			break;
		case MILLIAMPERE:
			switch (targetUnit)
			{
				case AMPERE: return value / 1000;
			}
			break;
		case AMPERE:
			switch (targetUnit)
			{
				case MILLIAMPERE: return value * 1000;
			}
			break;
		case MILLIVOLT:
			switch (targetUnit)
			{
				case VOLT: return value / 1000;
			}
			break;
		case VOLT:
			switch (targetUnit)
			{
				case MILLIVOLT: return value * 1000;
			}
			break;
	}
	return 0;
}

string ValueType::toStr() const
{
	switch (code)
	{
		case UNKNOWN:
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
		case TIME_POINT:
			return "timePoint";
		default:
			return "?";
	}
}

bool ValueType::fromStr(string typeStr, ValueType& type)
{
	if (typeStr == "uninitialized")
		type = UNKNOWN;
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
	else if (typeStr == "timePoint")
		type = TIME_POINT;
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
			std::stringstream stream;
			stream << std::setprecision(std::numeric_limits<Number>::digits10 + 1) << number;
			return stream.str();
		}
		case ValueType::VOID:
			return "void";
		case ValueType::UNKNOWN:
			return "uninitialized";
		case ValueType::UNDEFINED:
			return "undefined";
		case ValueType::TIME_POINT:
			return timePoint.toStr();
		default:
			return "?";
	}
}

bool Value::operator==(const Value& x) const
{
	return x.type == type && x.str == str && x.boolean == boolean
			&& x.number == number && x.unit == unit && x.timePoint == timePoint;
}

