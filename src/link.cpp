#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/pointer.h>

#include "link.h"

using namespace std::chrono;

struct Stopwatch
{
	steady_clock::time_point start;
	Stopwatch() : start(steady_clock::now()) {}
	int getRuntime() { return duration_cast<milliseconds>(steady_clock::now() - start).count(); }
};

string Modifier::mapOutbound(string value) const
{
	auto pos = outMappings.find(value);
	if (pos != outMappings.end())
		return pos->second;
	else
		return value;
}

string Modifier::mapInbound(string value) const
{
	auto pos = inMappings.find(value);
	if (pos != inMappings.end())
		return pos->second;
	else
		return value;
}

Value Modifier::convertOutbound(const Value& value) const
{
	if (value.isNumber())
		return Value::newNumber((value.getNumber() / factor) - summand);
	else
		return value;
}

Value Modifier::convertInbound(const Value& value) const
{
	if (value.isNumber())
		return Value::newNumber((value.getNumber() + summand) * factor);
	else
		return value;
}

void Link::validate(Items& items) const
{
	if (errorCounter != "")
	{
		Item& item = items.validate(errorCounter);
		item.validateOwnerId(controlLinkId);
		item.validateValueType(ValueType::NUMBER);
		item.validatePollingEnabled(false);
		item.setReadable(false);
		item.setWritable(false);
	}

	for (auto& [itemId, modifier] : modifiers)
	{
		Item& item = items.validate(itemId);
		if (modifier.unit != Unit::UNKNOWN)
			item.validateUnitType(modifier.unit.getType());
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
		Stopwatch stopwatch;
		events = handler->receive(items);
		long runtime = stopwatch.getRuntime();
		if (runtime > maxReceiveDuration)
			logger.warn() << "Event receiving took " << runtime << " ms" << endOfMsg();

		if (errorCounter != "")
		{
			// monitor handler state
			HandlerState state = handler->getState();
			if (state.errorCounter != oldHandlerState.errorCounter)
			{
				events.add(Event(controlLinkId, errorCounter, EventType::STATE_IND, Value::newNumber(state.errorCounter)));
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
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : nullptr;

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

		// remove STATE_IND with void values
		if (event.getType() == EventType::STATE_IND && event.getValue().getType() == ValueType::VOID)
		{
			logger.warn() << event.getType().toStr() << " event received which has "
			              << event.getValue().getType().toStr() <<  " value" << endOfMsg();

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

		// remove READ_REQ depending on configuration
		if (suppressReadEvents && event.getType() == EventType::READ_REQ)
		{
			eventPos = events.erase(eventPos);
			continue;
		}

		if (event.getType() != EventType::READ_REQ)
		{
			Value value = event.getValue();

			// remove undefined values depending on configuration
			if (suppressUndefined && value.isUndefined())
			{
				eventPos = events.erase(eventPos);
				continue;
			}

			// convert event value (JSON Pointer extraction)
			if (value.isString() && modifier && modifier->inJsonPointer != "")
			{
				rapidjson::Document document;
				rapidjson::ParseResult result = document.Parse(value.getString().c_str());
				if (result.IsError())
				{
					logger.error() << "JSON parse error '" << rapidjson::GetParseError_En(result.Code())
					               << "' when converting event STRING value '" << value.getString()
					               << "' of item " << item.getId() << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				else
				{
					if (rapidjson::Value* jsonValue = rapidjson::Pointer(modifier->inJsonPointer.c_str()).Get(document))
					{
						if (jsonValue->IsBool())
							value = Value::newBoolean(jsonValue->GetBool());
						else if (jsonValue->IsString())
							value = Value::newString(jsonValue->GetString());
						else if (jsonValue->IsNumber())
							value = Value::newNumber(jsonValue->GetDouble());
						else if (jsonValue->IsNull())
							value = Value::newUndefined();
					}
					else
					{
						logger.error() << "JSON pointer " << modifier->inJsonPointer << " can't be resolved "
						               << "when converting event STRING value '" << value.getString()
						               << "' of item " << item.getId() << endOfMsg();

						eventPos = events.erase(eventPos);
						continue;
					}
				}
			}

			// convert event value (Regular expression matching)
			if (value.isString() && modifier)
			{
				std::smatch match;
				if (std::regex_search(value.getString(), match, modifier->inPattern))
				{
					// match
					if (match.size() > 1)
					{
						// ... and content found
						int i = 1;
						while (i < match.size() && !match[i].matched) i++;
						if (i < match.size()) // this should always be true
							value = Value::newString(match[i]);
					}
//					else
//						// ... but no content found
//						if (item.hasType(ValueType::BOOLEAN))
//							value = Value::newBoolean(true);
//						else
//							value = Value::newVoid();
				}
//				else
//				{
//					// no match
//					if (item.hasType(ValueType::BOOLEAN))
//						// special handling for boolean items
//						value = Value::newBoolean(false);
//				}
			}

			// convert event value (mapping)
			if (value.isString() && modifier)
				value = Value::newString(modifier->mapInbound(value.getString()));

			// convert event value (type)
			if (value.isString() && !item.hasValueType(ValueType::STRING))
			{
				if (value.isString() && numberAsString && item.hasValueType(ValueType::NUMBER))
				{
					try
					{
						value = Value::newNumber(std::stod(value.getString()));
					}
					catch (const std::exception& ex)
					{
					}
				}
				else if (value.isString() && booleanAsString && item.hasValueType(ValueType::BOOLEAN))
				{
					if (item.isWritable())
					{
						if (value.getString() == falseValue)
							value = Value::newBoolean(false);
						else if (value.getString() == trueValue)
							value = Value::newBoolean(true);
					}
					else
					{
						if (value.getString() == unwritableFalseValue)
							value = Value::newBoolean(false);
						else if (value.getString() == unwritableTrueValue)
							value = Value::newBoolean(true);
					}
				}
				else if (value.isString() && timePointAsString && item.hasValueType(ValueType::TIME_POINT))
				{
					if (TimePoint tp; TimePoint::fromStr(value.getString(), tp, timePointFormat))
						value = Value::newTimePoint(tp);
				}
				else if (value.isString() && voidAsString && item.hasValueType(ValueType::VOID))
				{
					if (value.getString() == voidValue || value.getString() == unwritableVoidValue)
						value = Value::newVoid();
				}
				else if (value.isString() && undefinedAsString && item.hasValueType(ValueType::UNDEFINED))
				{
					if (value.getString() == undefinedValue)
						value = Value::newUndefined();
				}
				if (value.isString())
				{
					logger.error() << "Event STRING value '" << value.getString()
					               << "' not convertible to type " << item.getValueTypes().toStr()
					               << " of item " << item.getId() << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
			}
			else if (value.isBoolean() && !item.hasValueType(ValueType::BOOLEAN))
			{
				if (voidAsBoolean)
					value = Value::newVoid();
			}

			// compare item types with event value type
			if (!item.hasValueType(value.getType()))
			{
				logger.error() << "Event value type " << value.getType().toStr()
				               << " not compatible with type(s) " << item.getValueTypes().toStr()
				               << " of item " << item.getId() << endOfMsg();

				eventPos = events.erase(eventPos);
				continue;
			}

			// convert event value (type preserving manipulations - general)
			if (modifier)
				value = modifier->convertInbound(value);

			// convert event value (type preserving manipulations - unit)
			if (value.isNumber())
			{
				Unit targetUnit = item.getUnit();
				Unit sourceUnit = value.getUnit();
				if (sourceUnit == Unit::UNKNOWN && modifier)
					sourceUnit = modifier->unit;
				if (sourceUnit == Unit::UNKNOWN)
					sourceUnit = targetUnit;
				if (sourceUnit.canConvertTo(targetUnit))
					value = Value::newNumber(sourceUnit.convertTo(value.getNumber(), targetUnit), targetUnit);
				else
				{
					logger.error() << "Event value unit " << sourceUnit.toStr()
					               << " can not be converted to unit " << targetUnit.toStr()
					               << " of item " << item.getId() << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
			}

			event.setValue(value);
		}
		else
			event.setValue(Value::newVoid());

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
		auto modifier = modifierPos != modifiers.end() ? &modifierPos->second : nullptr;

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

		// remove READ_REQ depending on configuration
		if (suppressReadEvents && event.getType() == EventType::READ_REQ)
		{
			eventPos = modifiedEvents.erase(eventPos);
			continue;
		}

		if (event.getType() != EventType::READ_REQ)
		{
			Value value = event.getValue();

			// remove undefined values depending on configuration
			if (suppressUndefined && value.isUndefined())
			{
				eventPos = modifiedEvents.erase(eventPos);
				continue;
			}

			// convert event value (type preserving manipulations - unit)
			if (value.isNumber())
			{
				Unit sourceUnit = value.getUnit();
				Unit targetUnit = sourceUnit;
				if (modifier && modifier->unit != Unit::UNKNOWN)
					targetUnit = modifier->unit;
				if (sourceUnit.canConvertTo(targetUnit))
					value = Value::newNumber(sourceUnit.convertTo(value.getNumber(), targetUnit), targetUnit);
				else
				{
					logger.error() << "Event value unit " << sourceUnit.toStr()
					               << " can not be converted to unit " << targetUnit.toStr()
					               << " of item " << item.getId() << endOfMsg();

					eventPos = modifiedEvents.erase(eventPos);
					continue;
				}
			}

			// convert event value (type preserving manipulations - general)
			if (modifier)
				value = modifier->convertOutbound(value);

			// convert event value (type)
			if (value.isNumber() && numberAsString)
				value = Value::newString(cnvToStr(value.getNumber()));
			else if (value.isBoolean() && booleanAsString)
				if (item.isWritable())
					value = Value::newString(value.getBoolean() ? trueValue : falseValue);
				else
					value = Value::newString(value.getBoolean() ? unwritableTrueValue : unwritableFalseValue);
			else if (value.isTimePoint() && timePointAsString)
				value = Value::newString(value.getTimePoint().toStr(timePointFormat));
			else if (value.isVoid() && voidAsString)
				if (item.isWritable())
					value = Value::newString(voidValue);
				else
					value = Value::newString(unwritableVoidValue);
			else if (value.isVoid() && voidAsBoolean)
				value = Value::newBoolean(true);
			else if (value.isUndefined() && undefinedAsString)
				value = Value::newString(undefinedValue);

			// convert event value (mapping)
			if (value.isString() && modifier)
				value = Value::newString(modifier->mapOutbound(value.getString()));

			// convert event value (formatting)
			if (value.isString() && modifier)
			{
//				string pattern = modifier->outPattern;
//				string::size_type pos = pattern.find("%time");
//				if (pos != string::npos)
//					pattern.replace(pos, 5, cnvToStr(std::time(0)));
//
//				string str;
//				str.resize(100);
//				int n = snprintf(&str[0], str.capacity(), pattern.c_str(), value.getString().c_str());
//				if (n >= str.capacity())
//				{
//					str.resize(n + 1);
//					n = snprintf(&str[0], str.capacity(), pattern.c_str(), value.getString().c_str());
//				}
//				str.resize(n);
//
//				value = Value::newString(str);

				string str = modifier->outPattern;

				static const string timeTag = "%Time%";
				if (auto pos = str.find(timeTag); pos != string::npos)
					str.replace(pos, timeTag.length(), cnvToStr(std::time(0)));

				static const string valueTag = "%Value%";
				if (auto pos = str.find(valueTag); pos != string::npos)
					str.replace(pos, valueTag.length(), value.toStr());

				value = Value::newString(str);
			}

			event.setValue(value);
		}

		eventPos++;
	}

	Stopwatch stopwatch;
	pendingEvents = handler->send(items, modifiedEvents);
	long runtime = stopwatch.getRuntime();
	if (runtime > maxReceiveDuration)
		logger.warn() << "Event sending took " << runtime << " ms" << endOfMsg();

	if (errorCounter != "")
	{
		// monitor handler state
		HandlerState state = handler->getState();
		if (state.errorCounter != oldHandlerState.errorCounter)
		{
			pendingEvents.add(Event(controlLinkId, errorCounter, EventType::STATE_IND, Value::newNumber(state.errorCounter)));
			oldHandlerState = state;
		}
	}
}


