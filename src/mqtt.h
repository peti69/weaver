#ifndef MQTT_H
#define MQTT_H

#include <ctime>
#include <set>

#include <mosquitto.h>

#include "link.h"
#include "logger.h"

namespace Mqtt
{

class TopicPattern
{
private:
	string topicPatternStr;
	static const string variable;

	TopicPattern(string topicPatternStr) : topicPatternStr(topicPatternStr) {}

public:
	// Constructs a non-usable TopicPattern.
	TopicPattern() {}

	// Indicates whether self is usable or not.
	bool isNull() { return topicPatternStr.empty(); }

	// Extracts the item id from the passed topic according to the pattern.
	string getItemId(string topic) const;

	// Constructs from the pattern and the passed item id the appropriate MQTT topic for publishing.
	string createPubTopic(string itemId) const;

	// Constructs from the pattern the appropriate MQTT topic pattern for subscribing.
	string createSubTopicPattern() const;

	// Constructs a TopicPattern from the passed pattern string. The string must be a valid MQTT topic
	// without + and #. One of its level must be %ItemId%.
	static TopicPattern fromStr(string topicPatternStr);
};

class Config
{
public:
	typedef std::set<string> Topics;
	struct Binding
	{
		string itemId;
		Topics stateTopics;
		string writeTopic;
		string readTopic;

		Binding(string _itemId, Topics _stateTopics, string _writeTopic, string _readTopic) :
			itemId(_itemId), stateTopics(_stateTopics), writeTopic(_writeTopic), readTopic(_readTopic)
		{}
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
		bool exists(string itemId) const { return find(itemId) != end(); }
	};

private:
	string clientIdPrefix;
	string hostname;
	int port;
	int reconnectInterval;
	bool retainFlag;
	bool logMsgs;
	TopicPattern stateTopicPattern;
	TopicPattern writeTopicPattern;
	TopicPattern readTopicPattern;
	Topics subTopics;
	Bindings bindings;

public:
	Config(string _clientIdPrefix, string _hostname, int _port, int _reconnectInterval,
		bool _retainFlag, bool _logMsgs, Topics _subTopics, TopicPattern _stateTopicPattern,
		TopicPattern _writeTopicPattern, TopicPattern _readTopicPattern, Bindings _bindings) :
		clientIdPrefix(_clientIdPrefix), hostname(_hostname), port(_port), 
		reconnectInterval(_reconnectInterval), retainFlag(_retainFlag), logMsgs(_logMsgs), 
		subTopics(_subTopics), stateTopicPattern(_stateTopicPattern),
		writeTopicPattern(_writeTopicPattern), readTopicPattern(_readTopicPattern), bindings(_bindings)
	{}
	string getClientIdPrefix() const { return clientIdPrefix; }
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	int getReconnectInterval() const { return reconnectInterval; }
	bool getRetainFlag() const { return retainFlag; }
	bool getLogMsgs() const {return logMsgs; }
	const Topics& getSubTopics() const {return subTopics; }
	TopicPattern getStateTopicPattern() const { return stateTopicPattern; }
	TopicPattern getWriteTopicPattern() const { return writeTopicPattern; }
	TopicPattern getReadTopicPattern() const { return readTopicPattern; }
	const Bindings& getBindings() const { return bindings; }
};

class Handler: public HandlerIf
{
private:
	string id;
	Config config;
	Logger logger;
	struct mosquitto* client;
	bool connected;
	std::time_t lastConnectTry;
	struct Msg 
	{
		string topic;
		string payload;
		Msg(string _topic, string _payload) : topic(_topic), payload(_payload) {}
	};
	std::list<Msg> receivedMsgs;

public:
	Handler(string _id, Config _config, Logger _logger);
	virtual ~Handler();
	virtual bool supports(EventType eventType) const override { return true; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	bool connect(const Items& items);
	void disconnect();
	Events receiveX(const Items& items);
	Events sendX(const Items& items, const Events& events);
	void handleError(string funcName, int errorCode);
	void onMessage(const Msg& msg);
	void sendMessage(string topic, string payload, bool reatainFlag);

	friend void onMqttMessage(struct mosquitto*, void*, const struct mosquitto_message*);
};

}

#endif

