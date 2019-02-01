#ifndef MQTT_H
#define MQTT_H

#include <ctime>

#include <mosquitto.h>

#include "link.h"
#include "logger.h"

class MqttConfig
{
	public:
	struct Binding
	{
		typedef std::list<string> Topics;
		string itemId;
		bool owner;
		Topics stateTopics;
		string writeTopic;
		string readTopic;
		
		Binding(string _itemId, bool _owner, Topics _stateTopics, string _writeTopic, string _readTopic) :
			itemId(_itemId), owner(_owner), stateTopics(_stateTopics), writeTopic(_writeTopic), readTopic(_readTopic)
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
	int reconnectInterval;
	bool retainFlag;
	Bindings bindings;

	public:
	MqttConfig(string _clientId, string _hostname, int _port, int _reconnectInterval, 
		bool _retainFlag, Bindings _bindings) :
		clientId(_clientId), hostname(_hostname), port(_port), reconnectInterval(_reconnectInterval), 
		retainFlag(_retainFlag), bindings(_bindings)
	{}
	string getClientId() const { return clientId; }
	string getHostname() const { return hostname; }
	int getPort() const { return port; }
	int getReconnectInterval() const { return reconnectInterval; }
	bool getRetainFlag() const { return retainFlag; }
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
	Events receivedEvents;
	
	public:
	MqttHandler(string _id, MqttConfig _config, Logger _logger);
	virtual ~MqttHandler();
	virtual int getWriteDescriptor() { return mosquitto_want_write(client) ? mosquitto_socket(client) : -1; }
	virtual int getReadDescriptor() { return mosquitto_socket(client); }
	virtual Events receive();
	virtual void send(const Events& events);

	private:
	bool connect();
	void disconnect();
	Events receiveX();
	void sendX(const Events& events);
	void handleError(string funcName, int errorCode);
	void onMessage(const mosquitto_message* msg);

	friend void onMqttMessage(struct mosquitto*, void*, const struct mosquitto_message*);
};

#endif

