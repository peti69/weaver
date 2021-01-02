#include "link.h"

Value Modifier::exportValue(const Value& value) const
{
	if (value.isNumber())
		return Value(value.getNumber() / factor);
	else
		return value;
}

Value Modifier::importValue(const Value& value) const
{
	if (value.isNumber())
		return Value(value.getNumber() * factor);
	else
		return value;
}

void Link::validate(Items& items) const
{
	// check and manipulate properties of control items
	if (errorCounter != "")
	{
		Item& item = items.validate(errorCounter);
		item.validateOwnerId(controlLinkId);
		item.setReadable(false);
		item.setWritable(false);
//		item.validateReadable(false);
//		item.validateWritable(false);
		item.validateType(ValueType::NUMBER);
	}

	// check modifiers
	for (auto& modifierPair : modifiers)
	{
		const Modifier& modifier = modifierPair.second;

		// provide item
		Item& item = items.validate(modifier.getItemId());
	}

	handler->validate(items);
}

long Link::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	return pendingEvents.size() ? 0 : handler->collectFds(readFds, writeFds, excpFds, maxFd);
}

Events Link::receive(Items& items)
{
	Events events;

	if (pendingEvents.size())
	{
		events = pendingEvents;
		pendingEvents.clear();
	}
	else
	{
		events = handler->receive(items);

		if (errorCounter != "")
		{
			// monitor handler state
			HandlerState state = handler->getState();
			if (state.errorCounter != oldHandlerState.errorCounter)
			{
				events.add(Event(controlLinkId, errorCounter, EventType::STATE_IND, double(state.errorCounter)));
				oldHandlerState = state;
			}
		}
	}

	for (auto eventPos = events.begin(); eventPos != events.end();)
	{
		// provide event
		auto& event = *eventPos;

		// provide item
		auto itemPos = items.find(event.getItemId());
		if (itemPos == items.end())
		{
			logger.warn() << event.getType().toStr() << " event received for unknown item " << event.getItemId() << endOfMsg();

			eventPos = events.erase(eventPos);
			continue;
		}
		auto& item = itemPos->second;

		// provide modifier
		auto modifierPos = modifiers.find(event.getItemId());
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : 0;

		// remove READ_REQ and WRITE_REQ in case the link is the owner of the item
		if (event.getType() != EventType::STATE_IND && item.getOwnerId() == id)
		{
			logger.warn() << event.getType().toStr() << " event received for item " << event.getItemId()
			              << " which is owned by the link" << endOfMsg();

			eventPos = events.erase(eventPos);
			continue;
		}

		// remove STATE_IND in case the link is not the owner of the item
		if (event.getType() == EventType::STATE_IND && item.getOwnerId() != id && item.getOwnerId() != controlLinkId)
		{
			logger.warn() << event.getType().toStr() << " event received for item " << event.getItemId()
			              << " which is not owned by the link" << endOfMsg();

			eventPos = events.erase(eventPos);
			continue;
		}

		// remove WRITE_REQ in case the item is not writable
		if (event.getType() == EventType::WRITE_REQ && !item.isWritable())
		{
			logger.warn() << event.getType().toStr() << " event received for item " << event.getItemId()
			              << " which is not writable" << endOfMsg();

			eventPos = events.erase(eventPos);
			continue;
		}

		// remove READ_REQ in case the item is not readable
//		if (event.getType() == EventType::READ_REQ && !item.isReadable())
//		{
//			logger.warn() << event.getType().toStr() << " event received for item " << event.getItemId()
//			              << " which is not readable" << endOfMsg();
//
//			eventPos = events.erase(eventPos);
//			continue;
//		}

		if (event.getType() != EventType::READ_REQ)
		{
			// convert event value type
			auto& value = event.getValue();
			bool convertError = false;
			if (value.isString() && numberAsString && item.getType() == ValueType::NUMBER)
			{
				try
				{
					event.setValue(Value(std::stod(value.getString())));
				}
				catch (const std::exception& ex)
				{
					convertError = true;
				}
			}
			else if (value.isString() && booleanAsString && item.getType() == ValueType::BOOLEAN)
				if (item.isWritable())
					if (value.getString() == falseValue)
						event.setValue(Value(false));
					else if (value.getString() == trueValue)
						event.setValue(Value(true));
					else
						convertError = true;
				else
					if (value.getString() == unwritableFalseValue)
						event.setValue(Value(false));
					else if (value.getString() == unwritableTrueValue)
						event.setValue(Value(true));
					else
						convertError = true;
			else if (item.getType() == ValueType::VOID)
				event.setValue(Value::newVoid());
			if (convertError)
			{
				logger.error() << "Event STRING value '" << event.getValue().getString()
				               << "' can not be converted to type " << item.getType().toStr()
				               << " of item " << item.getId() << endOfMsg();

				eventPos = events.erase(eventPos);
				continue;
			}

			// compare item type with event value type
			if (event.getValue().getType() != item.getType())
			{
				logger.error() << "Event value type " << event.getValue().getType().toStr()
				               << " differs from type " << item.getType().toStr()
				               << " of item " << item.getId() << endOfMsg();

				eventPos = events.erase(eventPos);
				continue;
			}

			// convert event value from external to internal representation
			if (modifier)
				event.setValue(modifier->importValue(event.getValue()));
		}
		else
			event.setValue(Value());

		eventPos++;
	}

	return events;
}

void Link::send(Items& items, const Events& events)
{
	Events modifiedEvents = events;

	for (auto eventPos = modifiedEvents.begin(); eventPos != modifiedEvents.end();)
	{
		// provide event
		auto& event = *eventPos;

		// provide item
		auto itemPos = items.find(event.getItemId());
		if (itemPos == items.end())
		{
			eventPos++;
			continue;
		}
		auto& item = itemPos->second;

		// provide modifier
		auto modifierPos = modifiers.find(event.getItemId());
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : 0;

		// remove READ_REQ and WRITE_REQ in case the link is not the owner of the item
		if (event.getType() != EventType::STATE_IND && item.getOwnerId() != id)
		{
			eventPos = modifiedEvents.erase(eventPos);
			continue;
		}

		// remove STATE_IND in case the link is the owner of the item
		if (event.getType() == EventType::STATE_IND && item.getOwnerId() == id)
		{
			eventPos = modifiedEvents.erase(eventPos);
			continue;
		}

		if (event.getType() != EventType::READ_REQ)
		{
			// convert event value from internal to external representation
			if (modifier)
				event.setValue(modifier->exportValue(event.getValue()));

			// convert event value type
			auto& value = event.getValue();
			if (value.getType() == ValueType::NUMBER && numberAsString)
				event.setValue(Value(cnvToStr(value.getNumber())));
			else if (value.getType() == ValueType::BOOLEAN && booleanAsString)
				if (item.isWritable())
					event.setValue(Value(value.getBoolean() ? trueValue : falseValue));
				else
					event.setValue(Value(value.getBoolean() ? unwritableTrueValue : unwritableFalseValue));
			else if (value.getType() == ValueType::VOID && voidAsString)
				if (item.isWritable())
					event.setValue(Value(voidValue));
				else
					event.setValue(Value(unwritableVoidValue));
		}

		eventPos++;
	}

	pendingEvents = handler->send(items, modifiedEvents);

	if (errorCounter != "")
	{
		// monitor handler state
		HandlerState state = handler->getState();
		if (state.errorCounter != oldHandlerState.errorCounter)
		{
			pendingEvents.add(Event(controlLinkId, errorCounter, EventType::STATE_IND, double(state.errorCounter)));
			oldHandlerState = state;
		}
	}
}


