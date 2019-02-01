#ifndef LINK_H
#define LINK_H

#include "basic.h"

class Transformation
{
	private:
	string itemId;
	float factor;
	
	public:
	Transformation(string _itemId, float _factor) : itemId(_itemId), factor(_factor) {}
	string getItemId() const { return itemId; }
	string exportValue(string value);
	string importValue(string value);
};

class Transformations: public std::map<string, Transformation> 
{
	public:
	void add(Transformation transformation) { insert(value_type(transformation.getItemId(), transformation)); }
	bool exists(string itemId) const { return find(itemId) != end(); }
	Event exportEvent(const Event& event);
	Event importEvent(const Event& event);	
};

class Handler
{
	public:
	virtual int getReadDescriptor() = 0;
	virtual int getWriteDescriptor() = 0;
	virtual Events receive() = 0;
	virtual void send(const Events& events) = 0;
};

class Link
{
	private:
	string id;
	std::shared_ptr<Handler> handler;
	Transformations transformations;
	
	public:
	Link(string _id, Transformations _transformations, std::shared_ptr<Handler> _handler) : 
		id(_id), transformations(_transformations), handler(_handler) {}
	string getId() const { return id; }
	int getReadDescriptor() const { return handler->getReadDescriptor(); }
	int getWriteDescriptor() const { return handler->getWriteDescriptor(); }
	Events receive();
	void send(const Events& events);
};

#endif