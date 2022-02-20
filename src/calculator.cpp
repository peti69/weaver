#include "calculator.h"

namespace calculator
{

Handler::Handler(string id, Config config, Logger logger) :
	id(id), config(config), logger(logger)
{
}

void Handler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		item.validateOwnerId(id);
		item.validateType(ValueType::NUMBER);
		item.validateType(ValueType::UNDEFINED);
		item.validatePollingEnabled(false);
		item.setWritable(false);
		item.setReadable(false);

		Item& sourceItem = items.validate(binding.sourceItemId);
		sourceItem.validateType(ValueType::NUMBER);
		sourceItem.validateHistory();
		dependants[binding.sourceItemId].insert(itemId);

		Item& periodItem = items.validate(binding.periodItemId);
		periodItem.validateType(ValueType::NUMBER);
		dependants[binding.periodItemId].insert(itemId);
	}
}

Events Handler::receive(const Items& items)
{
	return Events();
}

Events Handler::send(const Items& items, const Events& events)
{
	Events newEvents;

	TimePoint now = Clock::now();

	auto createEvent = [&](const Binding& binding)
	{
		Value value = Value::newUndefined();
		const Value& period = items.get(binding.periodItemId).getLastValue();
		if (period.isNumber())
		{
			const Item& sourceItem = items.get(binding.sourceItemId);
			switch (binding.function)
			{
				case Function::MAXIMUM:
					value = sourceItem.calcMaxFromHistory(now - Seconds(static_cast<int>(period.getNumber())));
					break;
				case Function::MINIMUM:
					value = sourceItem.calcMinFromHistory(now - Seconds(static_cast<int>(period.getNumber())));
					break;
			}
		}
		return Event(id, binding.itemId, EventType::STATE_IND, value);
	};

	for (const Event& event : events)
		if (auto dependantPos = dependants.find(event.getItemId()); dependantPos != dependants.end())
			for (ItemId itemId : dependantPos->second)
				newEvents.add(createEvent(config.getBindings().at(itemId)));

	if (now >= lastCalculation + 10s)
	{
		for (auto& [itemId, binding]: config.getBindings())
			newEvents.add(createEvent(binding));
		lastCalculation = now;
	}

	return newEvents;
}

}
