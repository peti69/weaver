#include "link.h"

string Transformation::exportValue(string value)
{
	try
	{
		float f = std::stof(value);
		std::ostringstream stream;
		stream << f / factor;
		return stream.str();
	}
	catch (const std::exception& ex)
	{
		return "";
	}
}

string Transformation::importValue(string value)
{
	try
	{
		float f = std::stof(value);
		std::ostringstream stream;
		stream << f * factor;
		return stream.str();
	}
	catch (const std::exception& ex)
	{
		return "";
	}
}

Event Transformations::exportEvent(const Event& event)
{
	if (event.getType() == Event::READ_REQ)
		return event;

	auto pos = find(event.getItemId());
	if (pos == end())
		return event;
	else
	{
		Event newEvent(event);
		newEvent.setValue(pos->second.exportValue(newEvent.getValue()));
		return newEvent;
	}
}

Event Transformations::importEvent(const Event& event)
{
	if (event.getType() == Event::READ_REQ)
		return event;

	auto pos = find(event.getItemId());
	if (pos == end())
		return event;
	else
	{
		Event newEvent(event);
		newEvent.setValue(pos->second.importValue(newEvent.getValue()));
		return newEvent;
	}
}

Events Link::receive()
{
	if (!transformations.empty())
	{
		Events events = handler->receive();
		Events newEvents;
		for (auto& event : events)
			newEvents.add(transformations.importEvent(event));
		return newEvents;
	}
	else
		return handler->receive();
}

void Link::send(const Events& events)
{
	if (!transformations.empty())
	{
		Events newEvents;
		for (auto& event : events)
			newEvents.add(transformations.exportEvent(event));
		handler->send(newEvents);
	}
	else
		handler->send(events);
}


