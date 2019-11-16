#ifndef CONFIG_H
#define CONFIG_H

#include <regex>

#include <regex.h>
#include <rapidjson/document.h>

#include "link.h"

class KnxConfig;
class MqttConfig;
class PortConfig;
class GeneratorConfig;
class StorageConfig;
class Tr064Config;
class HttpConfig;
class TcpConfig;

class GlobalConfig
{
private:
	bool logEvents;
	bool logSuppressedEvents;
	bool logGeneratedEvents;

public:
	GlobalConfig() : logEvents(false), logSuppressedEvents(false), logGeneratedEvents(false) {}
	GlobalConfig(bool _logEvents, bool _logSuppressedEvents, bool _logGeneratedEvents) :
		logEvents(_logEvents), logSuppressedEvents(_logSuppressedEvents), logGeneratedEvents(_logGeneratedEvents)
	{}
	bool getLogEvents() const { return logEvents; }
	bool getLogSuppressedEvents() const { return logSuppressedEvents; }
	bool getLogGeneratedEvents() const { return logGeneratedEvents; }
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
	std::shared_ptr<GeneratorConfig> getGeneratorConfig(const Value& value, const Items& items) const;
	std::shared_ptr<Tr064Config> getTr064Config(const Value& value, const Items& items) const;
	std::shared_ptr<StorageConfig> getStorageConfig(const Value& value, const Items& items) const;
	std::shared_ptr<HttpConfig> getHttpConfig(const Value& value, const Items& items) const;
	std::shared_ptr<TcpConfig> getTcpConfig(const Value& value, const Items& items) const;

public:
	Config() {}
	void read(string fileName);
	GlobalConfig getGlobalConfig() const;
	LogConfig getLogConfig() const;
	Items getItems() const;
	Links getLinks(const Items& items, Log& log) const;
};

#endif
