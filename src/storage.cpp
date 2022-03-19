#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include "rapidjson/filewritestream.h"
#include <rapidjson/prettywriter.h>

#include <utility>
#include <set>

#include "storage.h"
#include "finally.h"

using namespace std::rel_ops;

namespace storage
{

Handler::Handler(LinkId id, Config config, Logger logger) :
	id(id), config(config), logger(logger)
{
}

void Handler::validate(Items& items)
{
	bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id)
		{
			item.setReadable(false);
			item.setWritable(true);
			item.setResponsive(true);

			if (!bindings.count(itemId))
				bindings.add(Binding(itemId, Value::newUndefined(), false));
		}

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		item.validateOwnerId(id);
		item.validateValueType(binding.initialValue.getType());
	}
}

Events Handler::receiveX(const Items& items)
{
	Events newEvents;

	// if not yet done restore persisted owned items
	if (!fileRead)
	{
		// shell we perform another attempt to read the file?
		TimePoint now = Clock::now();
		if (now < lastFileReadTry + rereadInterval)
			return newEvents;
		lastFileReadTry = now;

		// open file
		FILE* file = fopen(config.getFileName().c_str(), "r");
		if (!file)
			logger.errorX() << "Can not open file " << config.getFileName() << " for reading" << endOfMsg();
		auto autoClose = finally([file] { fclose(file); });

		// read file and translate it to a DOM tree
		char buffer[4096];
		rapidjson::FileReadStream stream(file, buffer, sizeof(buffer));
		rapidjson::Document document;
		rapidjson::ParseResult result = document.ParseStream<rapidjson::kParseCommentsFlag|rapidjson::kParseTrailingCommasFlag>(stream);
		if (result.IsError())
			logger.errorX() << "JSON parse error '" << rapidjson::GetParseError_En(result.Code()) << "' at offset "
							<< result.Offset() << " in file " << config.getFileName() << endOfMsg();
		if (!document.IsObject())
			logger.errorX() << "JSON document from file " << config.getFileName() << " is not an object" << endOfMsg();

		// analyze DOM tree
		ItemIds itemsInFile;
		for (auto iter = document.MemberBegin(); iter != document.MemberEnd(); iter++)
		{
			ItemId itemId = iter->name.GetString();
			itemsInFile.insert(itemId);

			// verify item identifier
			auto itemPos = items.find(itemId);
			if (itemPos == items.end())
				logger.errorX() << "Item " << itemId << " is unknown" << endOfMsg();
			auto& item = itemPos->second;

			// verify that the item is owned
			if (item.getOwnerId() != id)
				logger.errorX() << "Item " << itemId << " is not owned by the link" << endOfMsg();

			// determine item value
			Value value;
			if (iter->value.IsString() && item.hasValueType(ValueType::TIME_POINT))
			{
				if (TimePoint tp; TimePoint::fromStr(iter->value.GetString(), tp))
					value = Value::newTimePoint(tp);
			}
			else if (iter->value.IsString() && item.hasValueType(ValueType::STRING))
				value = Value::newString(iter->value.GetString());
			else if (iter->value.IsBool() && item.hasValueType(ValueType::BOOLEAN))
				value = Value::newBoolean(iter->value.GetBool());
			else if (iter->value.IsNumber() && item.hasValueType(ValueType::NUMBER))
				value = Value::newNumber(iter->value.GetDouble());
			else if (iter->value.IsNull() && item.hasValueType(ValueType::UNDEFINED))
				value = Value::newUndefined();
			if (value.isNull())
				logger.errorX() << "Value for item " << itemId << " is not supported" << endOfMsg();

			// generate STATE_IND for item
			newEvents.add(Event(id, itemId, EventType::STATE_IND, value));
		}

		// generate STATE_IND for all items not found in file
		for (const auto& [itemId, item] : items)
			if (item.getOwnerId() == id && !itemsInFile.count(itemId))
				newEvents.add(Event(id, itemId, EventType::STATE_IND, bindings.at(itemId).initialValue));

		fileRead = true;
	}

	return newEvents;
}

Events Handler::receive(const Items& items)
{
	try
	{
		return receiveX(items);
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	return Events();
}

Events Handler::send(const Items& items, const Events& events)
{
	if (!fileRead)
		return Events();

	// analyze WRITE_REQ and determine changed values
	std::unordered_map<ItemId, Value> newValues;
	for (auto& event : events)
		if (  event.getType() == EventType::WRITE_REQ
		   && items.get(event.getItemId()).getLastValue() != event.getValue()
		   )
			newValues[event.getItemId()] = event.getValue();

	// if a value changed persist all owned items
	if (newValues.size())
	{
		// translate owned items to a DOM tree
		rapidjson::Document document;
		auto& allocator = document.GetAllocator();
		document.SetObject();
		for (auto& [itemId, item] : items)
			if (item.getOwnerId() == id && bindings.at(itemId).persistent)
			{
				auto newValuePos = newValues.find(itemId);
				const Value& value = newValuePos != newValues.end() ? newValuePos->second : item.getLastValue();
				rapidjson::Value jsonValue;
				if (value.isString())
					jsonValue.SetString(value.getString(), allocator);
				else if (value.isBoolean())
					jsonValue.SetBool(value.getBoolean());
				else if (value.isTimePoint())
					jsonValue.SetString(value.getTimePoint().toStr(), allocator);
				else if (value.isNumber())
					jsonValue.SetDouble(value.getNumber());
				else if (value.isUndefined())
					jsonValue.SetNull();
				rapidjson::Value memberName(itemId, allocator);
				document.AddMember(memberName, jsonValue, allocator);
			}

		// create file
		FILE* file = fopen(config.getFileName().c_str(), "w");
		if (!file)
		{
			logger.error() << "Can not open file " << config.getFileName() << " for writing" << endOfMsg();
			return Events();
		}
		auto autoClose = finally([file] { fclose(file); });

		// write DOM tree to file
		char buffer[4096];
		rapidjson::FileWriteStream stream(file, buffer, sizeof(buffer));
		rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(stream);
		document.Accept(writer);
	}

	// generate for every changed value a corresponding STATE_IND
	Events newEvents;
	for (auto& [itemId, value] : newValues)
		newEvents.add(Event(id, itemId, EventType::STATE_IND, value));

	return newEvents;
}

}
