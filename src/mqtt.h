#ifndef MQTT_H
#define MQTT_H

#include <ctime>
#include <set>

#include <mosquitto.h>

#include "link.h"
#include "logger.h"

class MqttConfig
{
	public:
	struct Binding
	{
		typedef std::set<string> Topics;
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
	Bindings bindings;

	public:
	MqttConfig(string _clientIdPrefix, string _hostname, int _port, int _reconnectInterval, 
		bool _retainFlag, bool _logMsgs, Bindings _bindings) :
		clientIdPrefix(_clientIdPrefix), hostname(_hostname), port(_port), 
		reconnectInterval(_reconnectInterval), retainFlag(_retainFlag), logMsgs(_logMsgs), bindings(_bindings)
	{}
	string getClientIdPrefix() const { return clientIdPrefix; }
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	int getReconnectInterval() const { return reconnectInterval; }
	bool getRetainFlag() const { return retainFlag; }
	bool getLogMsgs() const {return logMsgs; }
	const Bindings& getBindings() const { return bindings; }
};

class MqttHandler: public Handler
{
	private:
	string id;
	MqttConfig config;
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
	MqttHandler(string _id, MqttConfig _config, Logger _logger);
	virtual ~MqttHandler();
	virtual bool supports(Event::Type eventType) const { return true; }
	virtual int getWriteDescriptor() { return mosquitto_want_write(client) ? mosquitto_socket(client) : -1; }
	virtual int getReadDescriptor() { return mosquitto_socket(client); }
	virtual Events receive(const Items& items);
	virtual void send(const Items& items, const Events& events);

	private:
	bool connect(const Items& items);
	void disconnect();
	Events receiveX(const Items& items);
	void sendX(const Items& items, const Events& events);
	void handleError(string funcName, int errorCode);
	void onMessage(const Msg& msg);
	void sendMessage(string topic, string payload, bool reatain);

	friend void onMqttMessage(struct mosquitto*, void*, const struct mosquitto_message*);
};

#endif

