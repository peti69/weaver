#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/pointer.h>
#include <cmath>

#include "link.h"
#include "sml.h"

Value Modifier::mapOutbound(const Value& value) const
{
	for (const auto& [valueRange, newValue] : outMappings)
		if (valueRange.contains(value))
			if (newValue.isString())
			{
				string str = newValue.getString();

				static const string timeTag = "%Time%";
				if (auto pos = str.find(timeTag); pos != string::npos)
					str.replace(pos, timeTag.length(), cnvToStr(std::time(0)));

				static const string valueTag = "%EventValue%";
				if (auto pos = str.find(valueTag); pos != string::npos)
					if (value.isString())
						str.replace(pos, valueTag.length(), value.getString());
					else if (value.isNumber())
						str.replace(pos, valueTag.length(), cnvToStr(value.getNumber()));
					else
						return Value();

				return Value::newString(str);
			}
			else
				return newValue;
	return value;
}

string Modifier::mapInbound(string value) const
{
	if (auto pos = inMappings.find(value); pos != inMappings.end())
		return pos->second;
	else
		return value;
}

Value Modifier::convertOutbound(const Value& value) const
{
	if (value.isNumber())
	{
		Number num = (value.getNumber() / factor) - summand;
		if (round)
			num = std::round(num * pow(10, roundPrecision)) / pow(10, roundPrecision);
		return Value::newNumber(num, value.getUnit());
	}
	else
		return value;
}

Value Modifier::convertInbound(const Value& value) const
{
	if (value.isNumber())
	{
		Number num = (value.getNumber() + summand) * factor;
		if (round)
			num = std::round(num * pow(10, roundPrecision)) / pow(10, roundPrecision);
		return Value::newNumber(num, value.getUnit());
	}
	else
		return value;
}

Link::Link(LinkId id, bool enabled, bool suppressReadEvents,
	ItemId operationalItemId, ItemId errorCounterItemId,
	int maxReceiveDuration, int maxSendDuration,
	bool numberAsString, bool booleanAsString,
	string falseValue, string trueValue,
	string unwritableFalseValue, string unwritableTrueValue,
	bool timePointAsString, string timePointFormat,
	bool voidAsString, string voidValue, string unwritableVoidValue,
	bool voidAsBoolean, bool undefinedAsString, string undefinedValue,
	bool suppressUndefined,
	Modifiers modifiers, std::shared_ptr<HandlerIf> handler, Logger logger) :
	id(id), enabled(enabled), suppressReadEvents(suppressReadEvents),
	operationalItemId(operationalItemId), errorCounterItemId(errorCounterItemId),
	maxReceiveDuration(maxReceiveDuration), maxSendDuration(maxSendDuration),
	numberAsString(numberAsString), booleanAsString(booleanAsString),
	falseValue(falseValue), trueValue(trueValue),
	unwritableFalseValue(unwritableFalseValue), unwritableTrueValue(unwritableTrueValue),
	timePointAsString(timePointAsString), timePointFormat(timePointFormat),
	voidAsString(voidAsString), voidValue(voidValue), unwritableVoidValue(unwritableVoidValue),
	voidAsBoolean(voidAsBoolean), undefinedAsString(undefinedAsString), undefinedValue(undefinedValue),
	suppressUndefined(suppressUndefined),
	modifiers(modifiers), handler(handler), logger(logger)
{
	if (operationalItemId != "")
		pendingEvents.add(Event(controlLinkId, operationalItemId, EventType::STATE_IND, Value::newBoolean(oldHandlerState.operational)));
	if (errorCounterItemId != "")
		pendingEvents.add(Event(controlLinkId, errorCounterItemId, EventType::STATE_IND, Value::newNumber(oldHandlerState.errorCounter)));
}

void Link::validate(Items& items) const
{
	if (operationalItemId != "")
	{
		Item& item = items.validate(operationalItemId);
		item.validateOwnerId(controlLinkId);
		item.validateValueType(ValueType::BOOLEAN);
		item.validatePollingEnabled(false);
		item.setReadable(false);
		item.setWritable(false);
	}
	if (errorCounterItemId != "")
	{
		Item& item = items.validate(errorCounterItemId);
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

		// monitor handler state
		HandlerState state = handler->getState();
		if (operationalItemId != "" && state.operational != oldHandlerState.operational)
			events.add(Event(controlLinkId, operationalItemId, EventType::STATE_IND, Value::newBoolean(state.operational)));
		if (errorCounterItemId != "" &&  state.errorCounter != oldHandlerState.errorCounter)
			events.add(Event(controlLinkId, errorCounterItemId, EventType::STATE_IND, Value::newNumber(state.errorCounter)));
		oldHandlerState = state;
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
//		if (event.getType() == EventType::STATE_IND && event.getValue().getType() == ValueType::VOID)
//		{
//			logger.warn() << event.getType().toStr() << " event received which has "
//			              << event.getValue().getType().toStr() <<  " value" << endOfMsg();
//
//			eventPos = events.erase(eventPos);
//			continue;
//		}

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

			// convert event value (OBIS code based extraction from SML file)
			if (value.isString() && modifier && modifier->inObisCode != "")
			{
				SmlFile file;
				if (!file.parse(cnvFromHexStr(value.getString())))
				{
					logger.error() << "Event value conversion for item " << item.getId() << " - SML parse error in '"
					               << value.getString() << "'" << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				auto sequence = file.searchSequence(cnvFromHexStr(modifier->inObisCode));
				if (!sequence)
				{
					logger.error() << "Event value conversion for item " << item.getId() << " - Sequence for OBIS code "
					               << modifier->inObisCode << " not found in '" << value.getString() << "'" << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				if (sequence->size() < 6)
				{
					logger.error() << "Event value conversion for item " << item.getId() << " - Sequence for OBIS code "
					               << modifier->inObisCode << " too short in '" << value.getString() << "'" << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				auto smlUnit = std::get_if<SmlNode::Integer>(&sequence->at(3)->value);
				auto smlScaler = std::get_if<SmlNode::Integer>(&sequence->at(4)->value);
				auto smlNumber = std::get_if<SmlNode::Integer>(&sequence->at(5)->value);
				if (!smlUnit || !smlScaler || !smlNumber)
				{
					logger.error() << "Event value conversion for item " << item.getId() << " - Sequence for OBIS code "
					               << modifier->inObisCode << " invalid in '" << value.getString() << "'" << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				Unit unit = Unit::UNKNOWN;
				if (*smlUnit == 30)
					unit = Unit::WATTHOUR;
				else if (*smlUnit == 27)
					unit = Unit::WATT;
				else
				{
					logger.error() << "Event value conversion for item " << item.getId() << " - Unknown OBIS unit "
					               << *smlUnit << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
				value = Value::newNumber(std::pow(10.0, *smlScaler) * (*smlNumber), unit);
			}

			// convert event value (JSON pointer extraction)
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
					else
						// ... but no content found
						if (item.hasValueType(ValueType::BOOLEAN))
							value = Value::newBoolean(true);
				}
				else
				{
					// no match
					if (item.hasValueType(ValueType::BOOLEAN))
						value = Value::newBoolean(false);
				}
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
						std::size_t pos;
						Number number = std::stod(value.getString(), &pos);
						if (pos == value.getString().length())
							value = Value::newNumber(number);
					}
					catch (const std::exception& ex)
					{
					}
				}
				if (value.isString() && booleanAsString && item.hasValueType(ValueType::BOOLEAN))
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
				if (value.isString() && timePointAsString && item.hasValueType(ValueType::TIME_POINT))
				{
					if (TimePoint tp; TimePoint::fromStr(value.getString(), tp, timePointFormat))
						value = Value::newTimePoint(tp);
				}
				if (value.isString() && voidAsString && item.hasValueType(ValueType::VOID))
				{
					if (value.getString() == voidValue || value.getString() == unwritableVoidValue)
						value = Value::newVoid();
				}
				if (value.isString() && undefinedAsString && item.hasValueType(ValueType::UNDEFINED))
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
					               << " for item " << item.getId() << endOfMsg();

					eventPos = events.erase(eventPos);
					continue;
				}
			}

			// convert event value (type preserving manipulations - general)
			if (modifier)
				value = modifier->convertInbound(value);

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

			// convert event value (type preserving - general)
			if (modifier)
				value = modifier->convertOutbound(value);

			// convert event value (type preserving - unit)
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
					               << " for item " << item.getId() << endOfMsg();

					eventPos = modifiedEvents.erase(eventPos);
					continue;
				}
			}

			// convert event value (type changing - generic)
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

			// convert event value (type changing - specific)
			if (modifier)
			{
				value = modifier->mapOutbound(value);
				if (value.isNull())
				{
					logger.error() << "Event value " << value.toStr() << " for item "
					               << item.getId() << " cannot be mapped " << endOfMsg();

					eventPos = modifiedEvents.erase(eventPos);
					continue;
				}
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

	// monitor handler state
	HandlerState state = handler->getState();
	if (operationalItemId != "" && state.operational != oldHandlerState.operational)
		pendingEvents.add(Event(controlLinkId, operationalItemId, EventType::STATE_IND, Value::newBoolean(state.operational)));
	if (errorCounterItemId != "" &&  state.errorCounter != oldHandlerState.errorCounter)
		pendingEvents.add(Event(controlLinkId, errorCounterItemId, EventType::STATE_IND, Value::newNumber(state.errorCounter)));
	oldHandlerState = state;
}


