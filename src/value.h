#ifndef VALUE_H
#define VALUE_H

#include <cassert>
#include <map>
#include <unordered_set>

#include "basic.h"

class UnitType
{
private:
	using Code = unsigned char;
	Code code = UNKNOWN;
	static const std::map<UnitType, string> details;

public:
	UnitType() = default;
	UnitType(Code code) : code(code) {}

	operator Code() const { return code; }
	string toStr() const;

	static constexpr Code UNKNOWN = 0;
	static constexpr Code PERIOD = 1;
	static constexpr Code SPEED = 2;
	static constexpr Code TEMPERATURE = 3;
	static constexpr Code VOLUME = 4;
	static constexpr Code ILLUMINANCE = 5;
	static constexpr Code CURRENT = 6;
	static constexpr Code ENERGY = 7;
	static constexpr Code POWER = 8;
};

class Unit
{
private:
	using Code = unsigned char;
	Code code = UNKNOWN;
	struct Detail
	{
		UnitType type;
		string str;
		bool blank;
	};
	static const std::map<Unit, Detail> details;

public:
	Unit() = default;
	Unit(Code code) : code(code) {}

	operator Code() const { return code; }
	string toStr() const;
	string toStr(string valueStr) const;
	static bool fromStr(string unitStr, Unit& unit);

	UnitType getType() const;

	bool canConvertTo(Unit targetUnit) const;
	Number convertTo(Number value, Unit targetUnit) const;

	static constexpr Code UNKNOWN = 0;
	static constexpr Code PERCENT = 1;
	static constexpr Code MINUTE = 2;
	static constexpr Code SECOND = 3;
	static constexpr Code METER_PER_SECOND = 4;
	static constexpr Code CELCIUS = 5;
	static constexpr Code LUX = 6;
	static constexpr Code KILOLUX = 7;
	static constexpr Code GRAM_PER_CUBICMETER = 8;
	static constexpr Code WATT = 9;
	static constexpr Code KILOWATTHOUR = 10;
	static constexpr Code CUBICMETER = 11;
	static constexpr Code DEGREE = 12;
	static constexpr Code LITER_PER_MINUTE = 13;
	static constexpr Code MILLIAMPERE = 14;
	static constexpr Code MILLIMETER = 15;
	static constexpr Code EURO = 16;
	static constexpr Code FAHRENHEIT = 17;
	static constexpr Code HOUR = 18;
	static constexpr Code KILOMETER_PER_HOUR = 19;
	static constexpr Code MILES_PER_HOUR = 20;
	static constexpr Code AMPERE = 21;
	static constexpr Code WATTHOUR = 22;
	static constexpr Code KILOWATT = 23;
};

class ValueType
{
private:
	using Code = unsigned char;
	Code code = UNKNOWN;

public:
	ValueType() = default;
	ValueType(Code code) : code(code) {}

	operator Code() const { return code; }
	string toStr() const;
	static bool fromStr(string typeStr, ValueType& type);

	static constexpr Code UNKNOWN = 0;
	static constexpr Code UNDEFINED = 1;
	static constexpr Code VOID = 2;
	static constexpr Code STRING = 3;
	static constexpr Code BOOLEAN = 4;
	static constexpr Code NUMBER = 5;
	static constexpr Code TIME_POINT = 6;
};

template<>
struct std::hash<ValueType>
{
	std::size_t operator()(ValueType const& vt) const noexcept
	{
		return std::hash<unsigned char>{}(vt);
	}
};

class ValueTypes: public std::unordered_set<ValueType>
{
private:
	using Base = std::unordered_set<ValueType>;

public:
	ValueTypes() = default;
	ValueTypes(Base types) : Base(types) {}
	ValueTypes(std::initializer_list<ValueType> types) : Base(types) {}

	string toStr() const;
};

class Value
{
private:
	ValueType type = ValueType::UNKNOWN;
	string str;
	bool boolean = false;
	Number number = 0.0;
	Unit unit = Unit::UNKNOWN;
	TimePoint timePoint;

	Value(ValueType type) : type(type) {}
	Value(ValueType type, bool boolean) : type(type), boolean(boolean) {}
	Value(ValueType type, Number number) : type(type), number(number) {}
	Value(ValueType type, Number number, Unit unit) : type(type), number(number), unit(unit) {}
	Value(ValueType type, string str) : type(type), str(str) {}
	Value(ValueType type, TimePoint timePoint) : type(type), timePoint(timePoint) {}

public:
	Value() = default;

	static Value newUndefined() { return Value(ValueType::UNDEFINED); }
	static Value newVoid() { return Value(ValueType::VOID); }
	static Value newString(string str) { return Value(ValueType::STRING, str); }
	static Value newBoolean(bool boolean) { return Value(ValueType::BOOLEAN, boolean); }
	static Value newNumber(Number number) { return Value(ValueType::NUMBER, number); }
	static Value newNumber(Number number, Unit unit) { return Value(ValueType::NUMBER, number, unit); }
	static Value newTimePoint(TimePoint timePoint) { return Value(ValueType::TIME_POINT, timePoint); }

	ValueType getType() const { return type; }
	bool isNull() const { return type == ValueType::UNKNOWN; }

	bool isUndefined() const { return type == ValueType::UNDEFINED; }
	bool isVoid() const { return type == ValueType::VOID; }
	bool isString() const { return type == ValueType::STRING; }
	bool isBoolean() const { return type == ValueType::BOOLEAN; }
	bool isNumber() const { return type == ValueType::NUMBER; }
	bool isTimePoint() const { return type == ValueType::TIME_POINT; }

	const string& getString() const { assert(isString()); return str; }
	bool getBoolean() const { assert(isBoolean()); return boolean; }
	Number getNumber() const { assert(isNumber()); return number; }
	Number getNumber(Unit targetUnit) const { assert(isNumber()); return unit.convertTo(number, targetUnit); }
	Unit getUnit() const { assert(isNumber()); return unit; }
	TimePoint getTimePoint() const { assert(isTimePoint()); return timePoint; }

	string toStr() const;

	bool operator==(const Value& x) const;
};

#endif
