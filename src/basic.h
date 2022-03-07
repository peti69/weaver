#ifndef BASIC_H
#define BASIC_H

#include <chrono>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <list>
#include <deque>
#include <iostream> 
#include <sstream> 
#include <memory> 
#include <cassert>

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

extern string cnvToBinStr(string s);

extern string cnvToAsciiStr(ByteString s);
extern ByteString cnvFromAsciiStr(string s);

using Seconds = std::chrono::seconds;
using Clock = std::chrono::system_clock;
using namespace std::chrono_literals;

class TimePoint: public std::chrono::time_point<Clock>
{
private:
	using TP = std::chrono::time_point<Clock>;

public:
	TimePoint() : TP(0s) {}
	TimePoint(TP tp) : TP(tp) {}

	bool isNull() const { return *this == TP(0s); }
	void setToNull() { *this = TP(0s); }

	string toStr(string timePointFormat = "%Y-%m-%dT%H:%M:%S") const;
	static bool fromStr(string timePointStr, TimePoint& timePoint, string timePointFormat = "%Y-%m-%dT%H:%M:%S");
};

using Number = double;

class UnitType
{
private:
	using Code = unsigned char;
	Code code;
	static const std::map<UnitType, string> details;

public:
	UnitType() : code(UNKNOWN) {}
	UnitType(Code code) : code(code) {}

	operator Code() const { return code; }
	string toStr() const;

	static constexpr Code UNKNOWN = 0;
	static constexpr Code PERIOD = 1;
	static constexpr Code SPEED = 2;
	static constexpr Code TEMPERATURE = 3;
	static constexpr Code VOLUME = 4;
	static constexpr Code ILLUMINANCE = 5;
};

class Unit
{
private:
	using Code = unsigned char;
	Code code;
	struct Detail
	{
		UnitType type;
		string str;
		bool blank;
	};
	static const std::map<Unit, Detail> details;

public:
	Unit() : code(UNKNOWN) {}
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
	static constexpr Code GRAM_PER_CUBIC_METER = 8;
	static constexpr Code WATT = 9;
	static constexpr Code KILOWATT_HOUR = 10;
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
};

class ValueType
{
private:
	typedef unsigned char Code;
	Code code;

public:
	ValueType() : code(UNINITIALIZED) {}
	ValueType(Code code) : code(code) {}

	operator Code() const { return code; }
	string toStr() const;
	static bool fromStr(string typeStr, ValueType& type);

	static const Code UNINITIALIZED = 0;
	static const Code UNDEFINED = 1;
	static const Code VOID = 2;
	static const Code STRING = 3;
	static const Code BOOLEAN = 4;
	static const Code NUMBER = 5;
	static const Code TIME_POINT = 6;
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
public:
	ValueTypes() {}
	ValueTypes(std::unordered_set<ValueType> types) : std::unordered_set<ValueType>(types) {}
	ValueTypes(std::initializer_list<ValueType> types) : std::unordered_set<ValueType>(types) {}

	string toStr() const;
};

class Value
{
private:
	ValueType type;
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
	Value() : Value(ValueType::UNINITIALIZED) {}

	static Value newUndefined() { return Value(ValueType::UNDEFINED); }
	static Value newVoid() { return Value(ValueType::VOID); }
	static Value newString(string str) { return Value(ValueType::STRING, str); }
	static Value newBoolean(bool boolean) { return Value(ValueType::BOOLEAN, boolean); }
	static Value newNumber(Number number) { return Value(ValueType::NUMBER, number); }
	static Value newNumber(Number number, Unit unit) { return Value(ValueType::NUMBER, number, unit); }
	static Value newTimePoint(TimePoint timePoint) { return Value(ValueType::TIME_POINT, timePoint); }

	ValueType getType() const { return type; }

	bool isUninitialized() const { return type == ValueType::UNINITIALIZED; }
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

class Items;

using LinkId = std::string;
using ItemId = std::string;
using ItemIds = std::unordered_set<ItemId>;

// Link id used for events not produced or items not owned by a link handler.
const ItemId controlLinkId = "CONTROL";

class Item
{
public:
	struct SendOnTimerParams
	{
		// Shall unsolicited STATE_IND events be generated in case the owner link has not provided any for
		// a time span?
		bool active = false;

		// In case active = true:
		// Defines the time span starting from the last STATE_IND until an unsolicited STATE_IND is generated.
		Seconds interval = Seconds::zero();
	};

	struct SendOnChangeParams
	{
		// Shall STATE_IND events generated by the owner link only be forwarded in case the new item values
		// are different from the old ones?
		bool active = false;

		// In case active = true:
		// (current value) * (100 - (variation percentage)) <= new value <= (current value) * (100 + (variation percentage)) => ignore event
		Number relVariation = 0.0;

		// In case active = true:
		// New item values are suppressed in case they are inside the interval defined by this delta value.
		// (current value) - (variation value) <= new value <= (current value) + (variation value) => ignore event
		Number absVariation = 0.0;

		// In case active = true:
		// New item values are suppressed if they are greater than or equal to this one.
		Number minimum = std::numeric_limits<Number>::lowest();

		// In case active = true:
		// New item values are suppressed if they are lower than or equal to this one.
		Number maximum = std::numeric_limits<Number>::max();
	};

private:
	// Id of item for unique identification purpose.
	ItemId id;

	// Types of values which can be assigned to the item.
	ValueTypes valueTypes;

	// Unit of item values in case the item holds numbers.
	Unit unit;

	// Id of link who manages the item. That is, the link over which READ_REQ and WRITE_REQ for the item
	// are sent and on which STATE_IND for the item are received.
	LinkId ownerId;

	// Indicates whether the item can be queried by means of READ_REQ to the owner link.
	bool readable = true;

	// Indicates whether the item can be modified by means of WRITE_REQ to the owner link.
	bool writable = true;

	// Indicates whether the owner link automatically reacts with STATE_IND in case WRITE_REQ modifies the item.
	bool responsive = true;

	// Frequency in which READ_REQ events are generated automatically and passed to the owner link.
	Seconds pollingInterval = Seconds::zero();

	// Parameters for timer based STATE_IND generation.
	SendOnTimerParams sendOnTimerParams;

	// Parameters for change based STATE_IND suppression.
	SendOnChangeParams sendOnChangeParams;

	// Last seen and accepted value for the item.
	Value lastValue;

	struct HistoricValue
	{
		TimePoint timePoint;
		Number number;
		HistoricValue(TimePoint timePoint, Number number) : timePoint(timePoint), number(number) {};
	};

	// History of seen and accepted values ordered chronologically from newest to oldest.
	std::deque<HistoricValue> history;

	// Defines how long historic values are kept till they are discarded.
	Seconds historyPeriod = Seconds::zero();

	// Time of last published STATE_IND event for the item.
	TimePoint lastSendTime;

	// Time of last (internally) generated READ_REQ event for the item.
	TimePoint lastPollingTime;

public:	
	Item(ItemId id) : id(id) {}

	ItemId getId() const { return id; }

	void setOwnerId(LinkId _ownerId) { ownerId = _ownerId; }
	LinkId getOwnerId() const { return ownerId; }

	void setValueTypes(ValueTypes _valueTypes) { valueTypes = _valueTypes; }
	const ValueTypes& getValueTypes() const { return valueTypes; }
	bool hasValueType(ValueType valueType) const { return valueTypes.count(valueType); }

	void setUnit(Unit _unit) { unit = _unit; }
	Unit getUnit() const { return unit; }

	void setReadable(bool _readable) { readable = _readable; }
	bool isReadable() const { return readable; }

	void setWritable(bool _writable) { writable = _writable; }
	bool isWritable() const { return writable; }

	void setResponsive(bool _responsive) { responsive = _responsive; }
	bool isResponsive() const { return responsive; }

	void setLastValue(const Value& _value) { lastValue = _value; }
	const Value& getLastValue() const { return lastValue; }

	void setHistoryPeriod(Seconds _period) { historyPeriod = _period; }
	void addToHistory(TimePoint now, const Value& value);
	Value calcMinFromHistory(TimePoint start) const;
	Value calcMaxFromHistory(TimePoint start) const;

	void setSendOnTimerParams(SendOnTimerParams params) { sendOnTimerParams = params; }
	bool isSendOnTimerRequired(TimePoint now) const;

	void setLastSendTime(TimePoint _time) { lastSendTime = _time; }
	TimePoint getLastSendTime() const { return lastSendTime; }

	void setSendOnChangeParams(SendOnChangeParams params) { sendOnChangeParams = params; }
	bool isSendOnChangeEnabled() const { return sendOnChangeParams.active; }
	bool isSendOnChangeRequired(const Value& value) const;

	void setPollingInterval(Seconds _pollingInterval) { pollingInterval = _pollingInterval; }
	bool isPollingEnabled() const { return pollingInterval != Seconds::zero(); }
	bool isPollingRequired(TimePoint now) const;
	void initPolling(TimePoint now);
	void pollingDone(TimePoint now);

	void validateReadable(bool _readable) const;
	void validateWritable(bool _writable) const;
	void validateResponsive(bool _responsive) const;
	void validatePollingEnabled(bool _enabled) const;
	void validateHistory() const;
	void validateValueType(ValueType _valueType) const;
	void validateValueTypeNot(ValueType _valueType) const;
	void validateUnitType(UnitType _unitType) const;
	void validateOwnerId(LinkId _ownerId) const;
};

class Items: public std::unordered_map<ItemId, Item>
{
public:
	void add(Item item) { insert({item.getId(), item}); }
	bool exists(ItemId id) const { return find(id) != end(); }
	const Item& get(ItemId id) const { auto pos = find(id); assert(pos != end()); return pos->second; }
	Item& get(ItemId id) { auto pos = find(id); assert(pos != end()); return pos->second; }
	LinkId getOwnerId(ItemId id) const { return get(id).getOwnerId(); }

	Item& validate(ItemId itemId);
};

class EventType
{
private:
	typedef unsigned char Code;
	Code code;

public:
	EventType() : code(STATE_IND) {}
	EventType(Code _code) : code(_code) {}

	operator Code() const { return code; }
	string toStr() const;
	static bool fromStr(string typeStr, EventType& type);

	static const Code STATE_IND = 0;
	static const Code WRITE_REQ = 1;
	static const Code READ_REQ = 2;
};

class Event
{
private:
	// Id of link which generated the event.
	LinkId originId;
	
	// Id of item for which the event occurs. 
	ItemId itemId;
	
	// STATE_IND, WRITE_REQ or READ_REQ.
	EventType type;
	
	// In case of STATE_IND the current value of the item. For WRITE_REQ it is the new value which should
	// be assigned to the item. READ_REQ events do not make use of it. 
	Value value;
	
public:
	Event(LinkId originId, ItemId itemId, EventType type, const Value& value) :
		originId(originId), itemId(itemId), type(type), value(value) {}
	LinkId getOriginId() const { return originId; }
	ItemId getItemId() const { return itemId; }
	EventType getType() const { return type; }
	const Value& getValue() const { return value; }
	void setValue(const Value& _value) { value = _value; }
};

class Events: public std::list<Event>
{
public:
	void add(Event event) { push_back(event); }
	void add(Events& events) { splice(begin(), events); }
};

#endif
