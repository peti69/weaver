#ifndef MQTT_H
#define MQTT_H

#include <ctime>
#include <set>
#include <regex>

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
		std::regex inPattern;
		string outPattern;
		Binding(string itemId, Topics stateTopics, string writeTopic, string readTopic,
				std::regex inPattern, string outPattern) :
			itemId(itemId), stateTopics(stateTopics), writeTopic(writeTopic), readTopic(readTopic),
			inPattern(inPattern), outPattern(outPattern)
		{}
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
		bool exists(string itemId) const { return find(itemId) != end(); }
	};

private:
	string clientId;
	string hostname;
	int port;
	bool tlsFlag;
	string caFile;
	string caPath;
	string ciphers;
	int reconnectInterval;
	string username;
	string password;
	bool retainFlag;
	TopicPattern stateTopicPattern;
	TopicPattern writeTopicPattern;
	TopicPattern readTopicPattern;
	Topics subTopics;
	bool logMsgs;
	bool logLibEvents;
	Bindings bindings;

public:
	Config(string clientId, string hostname, int port, bool tlsFlag, string caFile, string caPath, string ciphers,
		int reconnectInterval, string username, string password, bool retainFlag,
		TopicPattern stateTopicPattern, TopicPattern writeTopicPattern, TopicPattern readTopicPattern,
		Topics subTopics, bool logMsgs, bool logLibEvents, Bindings bindings) :
		clientId(clientId), hostname(hostname), port(port), tlsFlag(tlsFlag), caFile(caFile), caPath(caPath), ciphers(ciphers),
		reconnectInterval(reconnectInterval), username(username), password(password), retainFlag(retainFlag),
		stateTopicPattern(stateTopicPattern), writeTopicPattern(writeTopicPattern), readTopicPattern(readTopicPattern),
		subTopics(subTopics), logMsgs(logMsgs), logLibEvents(logLibEvents), bindings(bindings)
	{}
	string getClientId() const { return clientId; }
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	bool getTlsFlag() const { return tlsFlag; }
	string getCaFile() const { return caFile; }
	string getCaPath() const { return caPath; }
	string getCiphers() const { return ciphers; }
	int getReconnectInterval() const { return reconnectInterval; }
	string getUsername() const { return username; }
	string getPassword() const { return password; }
	bool getRetainFlag() const { return retainFlag; }
	TopicPattern getStateTopicPattern() const { return stateTopicPattern; }
	TopicPattern getWriteTopicPattern() const { return writeTopicPattern; }
	TopicPattern getReadTopicPattern() const { return readTopicPattern; }
	const Topics& getSubTopics() const {return subTopics; }
	bool getLogMsgs() const {return logMsgs; }
	bool getLogLibEvents() const { return logLibEvents; }
	const Bindings& getBindings() const { return bindings; }
};

class Handler: public HandlerIf
{
private:
	enum State
	{
		DISCONNECTED,
		CONNECTING,
		CONNECTING_FAILED,
		CONNECTING_SUCCEEDED,
		CONNECTED,
	};
	State state;
	string id;
	Config config;
	Logger logger;
	struct mosquitto* client;
	std::time_t lastConnectTry;
	struct Msg 
	{
		string topic;
		string payload;
		Msg(string topic, string payload) : topic(topic), payload(payload) {}
	};
	std::list<Msg> receivedMsgs;

	// External state of handler.
	HandlerState handlerState;

public:
	Handler(string _id, Config _config, Logger _logger);
	virtual ~Handler();
	virtual bool supports(EventType eventType) const override { return true; }
	virtual HandlerState getState() const override { return handlerState; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	void disconnect();
	Events receiveX(const Items& items);
	Events sendX(const Items& items, const Events& events);
	void handleError(string funcName, int errorCode);
	void onLog(int level, string text);
	void onConnect(int rc);
	void onMessage(const Msg& msg);
	void sendMessage(string topic, string payload, bool reatainFlag);

	friend void onConnect(struct mosquitto*, void*, int);
	friend void onMessage(struct mosquitto*, void*, const struct mosquitto_message*);
	friend void onLog(struct mosquitto*, void*, int, const char*);
};

}

#endif

