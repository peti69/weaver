#ifndef CONFIG_H
#define CONFIG_H

#include <regex>

#include <rapidjson/document.h>

#include "link.h"

class KnxConfig;
namespace mqtt
{
class Config;
}
class PortConfig;
class GeneratorConfig;
namespace storage
{
class Config;
}
class Tr064Config;
class HttpConfig;
class TcpConfig;
namespace calculator
{
class Config;
}

class GlobalConfig
{
private:
	bool logPSelectCalls;
	bool logEvents;
	bool logSuppressedEvents;
	bool logGeneratedEvents;

public:
	GlobalConfig() :
		logPSelectCalls(false), logEvents(false),
		logSuppressedEvents(false), logGeneratedEvents(false)
	{}
	GlobalConfig(bool logPSelectCalls, bool logEvents, bool logSuppressedEvents, bool logGeneratedEvents) :
		logPSelectCalls(logPSelectCalls), logEvents(logEvents),
		logSuppressedEvents(logSuppressedEvents), logGeneratedEvents(logGeneratedEvents)
	{}

	bool getLogPSelectCalls() const { return logPSelectCalls; }
	bool getLogEvents() const { return logEvents; }
	bool getLogSuppressedEvents() const { return logSuppressedEvents; }
	bool getLogGeneratedEvents() const { return logGeneratedEvents; }
};

class Config
{
private:
	rapidjson::Document document;

private:
	mqtt::Config getMqttConfig(const rapidjson::Value& value) const;
	KnxConfig getKnxConfig(const rapidjson::Value& value) const;
	PortConfig getPortConfig(const rapidjson::Value& value) const;
	GeneratorConfig getGeneratorConfig(const rapidjson::Value& value) const;
	calculator::Config getCalculatorConfig(const rapidjson::Value& value) const;
	Tr064Config getTr064Config(const rapidjson::Value& value) const;
	storage::Config getStorageConfig(const rapidjson::Value& value) const;
	HttpConfig getHttpConfig(const rapidjson::Value& value) const;
	TcpConfig getTcpConfig(const rapidjson::Value& value) const;

public:
	Config() {}
	void read(string fileName);
	GlobalConfig getGlobalConfig() const;
	LogConfig getLogConfig() const;
	Items getItems() const;
	Links getLinks(const Items& items, Log& log) const;
};

#endif
