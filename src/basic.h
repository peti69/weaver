#ifndef BASIC_H
#define BASIC_H

#include <string>
#include <map>
#include <list>
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

extern string cnvToAsciiStr(ByteString s);
extern ByteString cnvFromAsciiStr(string s);

class Value;

class ValueType
{
private:
	typedef uint8_t Code;
	Code code;

public:
	ValueType() : code(VOID) {}
	ValueType(Code _code) : code(_code) {}

	operator Code() const { return code; }
	string toStr() const;
	static bool fromStr(string typeStr, ValueType& type);

	Value convert(const Value& value) const;

	static const Code VOID = 0;
	static const Code STRING = 1;
	static const Code NUMBER = 2;
	static const Code BOOLEAN = 3;
};

class Value
{
private:
	ValueType type;
	bool null;
	double num;
	bool boo;
	string str;

	Value(ValueType _type, bool _null) : type(_type), null(_null), num(0.0), boo(false) {}

public:
	Value() : type(ValueType::VOID), null(true), num(0.0), boo(false) {}
	Value(bool _boo) : type(ValueType::BOOLEAN), null(false), num(0.0), boo(_boo) {}
	Value(double _num) : type(ValueType::NUMBER), null(false), num(_num), boo(false) {}
	Value(string _str) : type(ValueType::STRING), null(false), num(0.0), boo(false), str(_str) {}

	static Value newVoid() { return Value(ValueType::VOID, false); }

	bool isNull() const { return null; }
	ValueType getType() const { assert(!null); return type; }

	bool isVoid() const { return !null && type == ValueType::VOID; }
	bool isNumber() const { return !null && type == ValueType::NUMBER; }
	bool isBoolean() const { return !null && type == ValueType::BOOLEAN; }
	bool isString() const { return !null && type == ValueType::STRING; }

	double getNumber() const { assert(isNumber()); return num; }
	bool getBoolean() const { assert(isBoolean()); return boo; }
	string getString() const { assert(isString()); return str; }

	string toStr() const;

	bool operator==(const Value& x) const;
};

class Item
{
private:
	// Id of item for unique identification purpose.
	string id;
	
	// Type of item and its value.
	ValueType type;
	
	// Id of link who manages the item. That is, the link over which READ_REQ and WRITE_REQ for the item are sent
	// and on which STATE_IND for the item are received.
	string ownerId;

	// Value of last seen and accepted STATE_IND event for the item.
	Value value;
	
public:	
	Item(string _id, ValueType _type, string _ownerId) :  id(_id), type(_type), ownerId(_ownerId) {}
	string getId() const { return id; }
	ValueType getType() const { return type; }
	string getOwnerId() const { return ownerId; }
	const Value& getValue() const { return value; }
	void setValue(const Value& newValue) { value = newValue; }
};

class Items: public std::map<string, Item>
{
public:
	void add(Item item) { insert(value_type(item.getId(), item)); }
	bool exists(string itemId) const { return find(itemId) != end(); }
	string getOwnerId(string itemId) const;
};

class EventType
{
private:
	typedef uint8_t Code;
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
	string originId;
	
	// Id of item for which the event occurs. 
	string itemId;
	
	// STATE_IND, WRITE_REQ or READ_REQ.
	EventType type;
	
	// In case of STATE_IND the current value of the item. For WRITE_REQ it is the new value which should
	// be assigned to the item. READ_REQ events do not make use of it. 
	Value value;
	
public:
	Event(string _originId, string _itemId, EventType _type, const Value& _value) : 
		originId(_originId), itemId(_itemId), type(_type), value(_value) {}
	string getOriginId() const { return originId; }
	string getItemId() const { return itemId; }
	EventType getType() const { return type; }
	const Value& getValue() const { return value; }
	void setValue(const Value& _value) { value = _value; }
};

class Events: public std::list<Event>
{
public:
	void add(Event event) { push_back(event); }
};

#endif
