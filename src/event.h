#ifndef EVENT_H
#define EVENT_H

#include <list>

#include "value.h"

using LinkId = std::string;
using ItemId = std::string;

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
	static bool fromStr(const string& typeStr, EventType& type);

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
	Event(const LinkId& originId, const ItemId& itemId, EventType type, const Value& value) :
		originId(originId), itemId(itemId), type(type), value(value) {}
	const LinkId& getOriginId() const { return originId; }
	const ItemId& getItemId() const { return itemId; }
	EventType getType() const { return type; }
	const Value& getValue() const { return value; }
	void setValue(const Value& _value) { value = _value; }
};

class Events: public std::list<Event>
{
public:
	void add(const Event& event) { push_back(event); }
	void add(Events& events) { splice(begin(), events); }
};

#endif
