#ifndef BASIC_H
#define BASIC_H

#include <string>
#include <map>
#include <list>
#include <iostream> 
#include <sstream> 
#include <memory> 

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

class Item
{
	private:
	string id;

	public:	
	Item(string _id) : id(_id) {}
	string getId() const { return id; }
};

class Items: public std::map<string, Item>
{
	public:
	void add(Item item) { insert(value_type(item.getId(), item)); }
	bool exists(string itemId) const { return find(itemId) != end(); }
};

class Event
{
	public:
	enum Type { STATE_IND, WRITE_REQ, READ_REQ };

	private:
	string originId;
	string itemId;
	Type type;
	string value;
	
	public:
	Event(string _originId, string _itemId, Type _type, string _value) : 
		originId(_originId), itemId(_itemId), type(_type), value(_value) {}
	string getOriginId() const { return originId; }
	string getItemId() const { return itemId; }
	Type getType() const { return type; }
	string getValue() const { return value; }
	void setValue(string _value) { value = _value; }
};

class Events: public std::list<Event>
{
	public:
	void add(Event event) { push_back(event); }
};

#endif