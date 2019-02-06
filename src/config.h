#ifndef CONFIG_H
#define CONFIG_H

#include <regex>

#include <regex.h>
#include <rapidjson/document.h>

#include "link.h"

class KnxConfig;
class MqttConfig;
class PortConfig;

class GlobalConfig
{
	private:
	bool logEvents;
	
	public:
	GlobalConfig() : logEvents(false) {}
	GlobalConfig(bool _logEvents) : logEvents(_logEvents) {}
	bool getLogEvents() const { return logEvents; }
};

class Config
{
	private:
	rapidjson::Document document;
	typedef rapidjson::Value::ConstMemberIterator Iterator;
	typedef rapidjson::Value Value;
	
	private:
	int hasMember(const Value& value, string name) const;
	string getString(const Value& value, string name) const;
	string getString(const Value& value, string name, string defaultValue) const;
	int getInt(const Value& value, string name) const;
	int getInt(const Value& value, string name, int defaultValue) const;
	float getFloat(const Value& value, string name) const;
	float getFloat(const Value& value, string name, float defaultValue) const;
	bool getBool(const Value& value, string name) const;
	bool getBool(const Value& value, string name, bool defaultVaue) const;
	const Value& getObject(const Value& value, string name) const;
	const Value& getArray(const Value& value, string name) const;
	std::regex convertPattern(string fieldName, string pattern) const;
	regex_t convertPattern2(string fieldName, string pattern) const;
	std::shared_ptr<MqttConfig> getMqttConfig(const Value& value, const Items& items) const;
	std::shared_ptr<KnxConfig> getKnxConfig(const Value& value, const Items& items) const;
	std::shared_ptr<PortConfig> getPortConfig(const Value& value, const Items& items) const;
	
	public:
	Config(string filename);
	GlobalConfig getGlobalConfig() const;
	Items getItems() const;
	Links getLinks(const Items& items) const;
};

#endif