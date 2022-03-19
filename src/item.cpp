#include "item.h"

void Item::addToHistory(TimePoint now, const Value& value)
{
	if (!value.isNumber() || historyPeriod == Seconds::zero())
		return;
	history.emplace_back(now, value.getNumber());
	while (!history.empty() && history.front().timePoint < now - historyPeriod)
		history.pop_front();
}

Value Item::calcMinFromHistory(TimePoint start) const
{
	if (lastValue.isNull() || !lastValue.isNumber())
		return Value::newUndefined();
	auto number = lastValue.getNumber();
	for (auto pos = history.rbegin(); pos != history.rend() && pos->timePoint >= start; pos++)
		if (pos->number < number)
			number = pos->number;
	return Value::newNumber(number);
}

Value Item::calcMaxFromHistory(TimePoint start) const
{
	if (lastValue.isNull() || !lastValue.isNumber())
		return Value::newUndefined();
	auto number = lastValue.getNumber();
	for (auto pos = history.rbegin(); pos != history.rend() && pos->timePoint >= start; pos++)
		if (pos->number > number)
			number = pos->number;
	return Value::newNumber(number);
}

bool Item::isSendOnTimerRequired(TimePoint now) const
{
	return sendOnTimerParams.active && !lastValue.isNull() && lastSendTime + sendOnTimerParams.interval <= now;
}

bool Item::isSendOnChangeRequired(const Value& value) const
{
	if (!sendOnChangeParams.active)
		return true;

	if (lastValue == value)
		return false;

	if (value.isNumber() && lastValue.isNumber())
	{
		Number oldNum = lastValue.getNumber();
		Number num = value.getNumber();
		if (  num >= sendOnChangeParams.minimum
		   && num <= sendOnChangeParams.maximum
		   && num >= oldNum * (1.0 - sendOnChangeParams.relVariation / 100.0) - sendOnChangeParams.absVariation
		   && num <= oldNum * (1.0 + sendOnChangeParams.relVariation / 100.0) + sendOnChangeParams.absVariation
		   )
			return false;
	}

	return true;
}

bool Item::isPollingRequired(TimePoint now) const
{
	assert(pollingInterval != Seconds::zero());
	return lastPollingTime + pollingInterval <= now;
}

void Item::initPolling(TimePoint now)
{
	assert(pollingInterval != Seconds::zero());
	lastPollingTime = now - Seconds(std::rand() % pollingInterval.count());
}

void Item::pollingDone(TimePoint now)
{
	assert(pollingInterval != Seconds::zero());
	lastPollingTime = now;
}

void Item::validateReadable(bool _readable) const
{
	if (readable && !_readable)
		throw std::runtime_error("Item " + id + " must not be readable");
	if (!readable && _readable)
		throw std::runtime_error("Item " + id + " must be readable");
}

void Item::validateWritable(bool _writable) const
{
	if (writable && !_writable)
		throw std::runtime_error("Item " + id + " must not be writable");
	if (!writable && _writable)
		throw std::runtime_error("Item " + id + " must be writable");
}

void Item::validateResponsive(bool _responsive) const
{
	if (responsive && !_responsive)
		throw std::runtime_error("Item " + id + " must not be responsive");
	if (!responsive && _responsive)
		throw std::runtime_error("Item " + id + " must be responsive");
}

void Item::validatePollingEnabled(bool _enabled) const
{
	if (pollingInterval != Seconds::zero() && !_enabled)
		throw std::runtime_error("Item " + id + " must not be polled");
	if (pollingInterval == Seconds::zero() && _enabled)
		throw std::runtime_error("Item " + id + " must be polled");
}

void Item::validateHistory() const
{
	if (historyPeriod == Seconds::zero())
		throw std::runtime_error("Item " + id + " must be historized");
}

void Item::validateValueType(ValueType _valueType) const
{
	if (!hasValueType(_valueType))
		throw std::runtime_error("Item " + id + " must have value type " + _valueType.toStr());
}

void Item::validateValueTypeNot(ValueType _valueType) const
{
	if (hasValueType(_valueType))
		throw std::runtime_error("Item " + id + " must not have value type " + _valueType.toStr());
}

void Item::validateUnitType(UnitType _unitType) const
{
	if (  unit.getType() != _unitType
	   || unit.getType() == UnitType::UNKNOWN
	   || _unitType == UnitType::UNKNOWN
	   )
		throw std::runtime_error("Item " + id + " must have unit type " + _unitType.toStr());
}

void Item::validateOwnerId(LinkId _ownerId) const
{
	if (ownerId != _ownerId)
		throw std::runtime_error("Item " + id + " must be owned by link " + _ownerId);
}

Item& Items::validate(ItemId itemId)
{
	auto pos = find(itemId);
	if (pos == end())
		throw std::runtime_error("Item " + itemId + " referenced but not defined");
	return pos->second;
}

