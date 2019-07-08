#define RAPIDJSON_HAS_STDSTRING 1

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/error/en.h>
#include "rapidjson/filewritestream.h"
#include <rapidjson/prettywriter.h>

#include <utility>

#include "storage.h"
#include "finally.h"

using namespace std::rel_ops;

Storage::Storage(string _id, StorageConfig _config, Logger _logger) :
	id(_id), config(_config), logger(_logger), fileRead(false), lastFileReadTry(0)
{
}

Events Storage::receiveX(const Items& items)
{
	Events newEvents;

	// if not yet done restore persisted owned items
	if (!fileRead)
	{
		// shell we perform another attempt to read the file?
		std::time_t now = std::time(0);
		if (lastFileReadTry + 60 > now)
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
		for (auto iter = document.MemberBegin(); iter != document.MemberEnd(); iter++)
		{
			string itemId = iter->name.GetString();

			// verify item identifier
			auto itemPos = items.find(itemId);
			if (itemPos == items.end())
				logger.errorX() << "Item " << itemId << " is unknown" << endOfMsg();

			// verify that the item is owned
			if (itemPos->second.getOwnerId() != id)
				logger.errorX() << "Item " << itemId << " is not owned by the link" << endOfMsg();

			// determine item value
			Value value;
			if (iter->value.IsString())
				value = Value(iter->value.GetString());
			else if (iter->value.IsBool())
				value = Value(iter->value.GetBool());
			else if (iter->value.IsNumber())
				value = Value(iter->value.GetDouble());
			else
				logger.errorX() << "Value for item " << itemId << " is not supported" << endOfMsg();

			// generate STATE_IND for item
			newEvents.add(Event(id, itemId, EventType::STATE_IND, value));
		}

		fileRead = true;
	}

	return newEvents;
}

Events Storage::receive(const Items& items)
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

Events Storage::send(const Items& items, const Events& events)
{
	// analyze WRITE_REQ and determine changed values
	std::map<string, Value> newValues;
	for (auto& event : events)
		if (event.getType() == EventType::WRITE_REQ)
		{
			auto itemPos = items.find(event.getItemId());
			if (itemPos != items.end() && itemPos->second.getValue() != event.getValue())
				newValues[event.getItemId()] = event.getValue();
		}

	// if a value changed persist all owned items
	if (newValues.size())
	{
		// translate owned items to a DOM tree
		rapidjson::Document document;
		auto& allocator = document.GetAllocator();
		document.SetObject();
		for (auto& itemPair : items)
		{
			const Item& item = itemPair.second;
			auto newValuePos = newValues.find(item.getId());
			const Value& value = newValuePos != newValues.end() ? newValuePos->second : item.getValue();
			if (item.getOwnerId() == id)
			{
				rapidjson::Value name(item.getId().c_str(), allocator);
				if (!value.isNull())
					if (value.getType() == ValueType::STRING)
						document.AddMember(name, rapidjson::Value(value.getString(), allocator), allocator);
					else if (value.getType() == ValueType::BOOLEAN)
						document.AddMember(name, value.getBoolean(), allocator);
					else if (value.getType() == ValueType::NUMBER)
						document.AddMember(name, value.getNumber(), allocator);
			}
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
	for (auto& newValuePair : newValues)
		newEvents.add(Event(id, newValuePair.first, EventType::STATE_IND, newValuePair.second));

	return newEvents;
}
