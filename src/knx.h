#ifndef KNX_H
#define KNX_H

#include <ctime>

#include "basic.h"
#include "logger.h"

typedef unsigned short IpPort; // in host byte order

struct IpAddr
{
	typedef unsigned long Value;
	Value value;

	IpAddr() : value(0) {}
	IpAddr(Value _value) : value(_value) {}
	IpAddr(Byte highHigh, Byte highLow, Byte lowHigh, Byte lowLow) : value(highHigh << 24 | highLow << 16 | lowHigh << 8 | lowLow) {}

	Byte highHigh() const { return value >> 24 & 0xFF; }
	Byte highLow() const { return value >> 16 & 0xFF; }
	Byte lowHigh() const { return value >> 8 & 0xFF; }
	Byte lowLow() const { return value & 0xFF; }

	operator Value() const { return value; }
	string toStr() const;
	static bool fromStr(string ipStr, IpAddr& ip);
};

struct ServiceType 
{
	typedef unsigned short Value;
	Value value;

	ServiceType(Value _value) : value(_value) {}
	ServiceType(Byte high, Byte low) : value(high << 8 | low) {}

	Byte high() const { return value >> 8 & 0xFF; }
	Byte low() const { return value & 0xFF; }
	
	operator Value() const { return value; }
	string toStr() const;
	
	static const Value CONN_REQ = 0x0205;
	static const Value CONN_RESP = 0x0206;
	static const Value CONN_STATE_REQ = 0x0207;
	static const Value CONN_STATE_RESP = 0x0208;
	static const Value DISC_REQ = 0x0209;
	static const Value DISC_RESP = 0x020A;
	static const Value TUNNEL_REQ = 0x0420;
	static const Value TUNNEL_RESP = 0x0421;
};

struct MsgCode 
{
	typedef Byte Value;
	Value value;

	MsgCode(Value _value) : value(_value) {}
	
	operator Value() const { return value; }
	string toStr() const;
	
	static const Value LDATA_REQ = 0x11;
	static const Value LDATA_IND = 0x29;
	static const Value LDATA_CON = 0x2E;
};

struct DatapointType
{
	int mainNo;
	int subNo;
	string unit;

	DatapointType() : mainNo(0), subNo(0) {}
	DatapointType(int _mainNo, int _subNo, string _unit) : mainNo(_mainNo), subNo(_subNo), unit(_unit) {}
	
	string toStr() const;
	static bool fromStr(string dptStr, DatapointType& dpt);
	
	ByteString exportValue(string value) const;
	string importValue(ByteString bytes) const;
};

struct GroupAddr
{
	typedef unsigned short Value;
	Value value;
	bool null;
	
	GroupAddr() : null(true), value(0) {}
	GroupAddr(Value _value) : null(false), value(_value) {}
	GroupAddr(Byte high, Byte low) : null(false), value(high << 8 | low) {}
	GroupAddr(int main, int middle, int sub) : null(false), value(main << 11 | middle << 8 | sub) {}

	bool isNull() const { return null; }

	Byte high() const { assert(!null); return value >> 8 & 0xFF; }
	Byte low() const { assert(!null); return value & 0xFF; }
	
	operator Value() const { assert(!null); return value; }
	string toStr() const;
	static bool fromStr(string gaStr, GroupAddr& ga);
	
	bool operator==(const GroupAddr& x);
};

struct PhysicalAddr
{
	typedef unsigned short Value;
	Value value;
	
	PhysicalAddr() : value(0) {}
	PhysicalAddr(Value _value) : value(_value) {}
	PhysicalAddr(Byte high, Byte low) : value(high << 8 | low) {}
	PhysicalAddr(int area, int line, int device) : value(area << 12 | line << 8 | device) {}

	Byte high() const { return value >> 8 & 0xFF; }
	Byte low() const { return value & 0xFF; }
	
	operator Value() const { return value; }
	string toStr() const;
	static bool fromStr(string paStr, PhysicalAddr& pa);
	
	bool operator==(const PhysicalAddr& x) { return value == x.value; }
};

class KnxConfig
{
	public:
	struct Binding
	{
		string itemId;
		bool owner;
		GroupAddr stateGa;
		GroupAddr writeGa;
		DatapointType dpt;
		Binding(string _itemId, bool _owner, GroupAddr _stateGa, GroupAddr _writeGa, DatapointType _dpt) : 
			itemId(_itemId), owner(_owner), stateGa(_stateGa), writeGa(_writeGa), dpt(_dpt) {};
	};
	class Bindings: public std::map<string, Binding> 
	{
		public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
		bool exists(string itemId) const { return find(itemId) != end(); }
	};
	
	private:
	IpAddr localIpAddr;
	bool natMode;
	IpAddr ipAddr;
	IpPort ipPort;
	int reconnectInterval;
	int connStateReqInterval;
	int controlRespTimeout;
	int ldataConTimeout;
	PhysicalAddr physicalAddr;
	bool logRawMsg;
	bool logData;
	Bindings bindings;

	public:
	KnxConfig(IpAddr _localIpAddr, bool _natMode, IpAddr _ipAddr, IpPort _ipPort, 
		int _reconnectInterval, int _connStateReqInterval, int _controlRespTimeout, int _ldataConTimeout, 
		PhysicalAddr _physicalAddr, bool _logRawMsg, bool _logData, Bindings _bindings) :
		localIpAddr(_localIpAddr), natMode(_natMode), ipAddr(_ipAddr), ipPort(_ipPort), 
		reconnectInterval(_reconnectInterval), connStateReqInterval(_connStateReqInterval), 
		controlRespTimeout(_controlRespTimeout), ldataConTimeout(_ldataConTimeout), 
		physicalAddr(_physicalAddr), logRawMsg(_logRawMsg), logData(_logData), bindings(_bindings)
	{}
	IpAddr getLocalIpAddr() const { return localIpAddr; }
	bool getNatMode() const { return natMode; }
	IpAddr getIpAddr() const { return ipAddr; }
	IpPort getIpPort() const { return ipPort; }
	int getReconnectInterval() const { return reconnectInterval; }
	int getConnStateReqInterval() const { return connStateReqInterval; }
	int getControlRespTimeout() const { return controlRespTimeout; }
	int getLDataConTimeout() const { return ldataConTimeout; }
	PhysicalAddr getPhysicalAddr() const { return physicalAddr; }
	bool getLogRawMsg() const { return logRawMsg; }
	bool getLogData() const { return logData; }
	const Bindings& getBindings() const { return bindings; }
};

class KnxHandler: public Handler
{
	private:
	enum State
	{ 
		DISCONNECTED, 
		WAIT_FOR_CONN_RESP, 
		CONNECTED 
	};
	struct LDataReq
	{
		GroupAddr ga;
		ByteString data;
		LDataReq(GroupAddr _ga, ByteString _data) : ga(_ga), data(_data) {}
	};
	
	private:
	string id;
	KnxConfig config;
	Logger logger;
	int socket;
	IpPort localIpPort;
	IpPort dataIpPort;
	IpAddr dataIpAddr;
    Byte channelId;
	State state;
	std::time_t lastConnectTry;
	bool ongoingConnStateReq;
	std::time_t controlReqSendTime;
	Byte lastReceivedSeqNo;
	Byte lastSentSeqNo;
	bool ongoingLDataReq;
	std::list<LDataReq> waitingLDataReqs;
	
	public:
	KnxHandler(string _id, KnxConfig _config, Logger _logger);
	virtual ~KnxHandler();
	virtual int getReadDescriptor() { return state != DISCONNECTED ? socket : -1; }
	virtual int getWriteDescriptor() { return -1; }
	virtual Events receive();
	virtual void send(const Events& events);
	
	private:
	void disconnect();
	Events receiveX();
	void sendX(const Events& events);
	void sendWaitingLDataReq();
	bool receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port);
	void sendMsg(IpAddr addr, IpPort port, ByteString msg);
	void sendControlMsg(ByteString msg);
	void sendDataMsg(ByteString msg);
	ByteString createConnReq() const;
	ByteString createConnStateReq() const;
	ByteString createDiscReq() const;
	ByteString createTunnelReq(Byte seqNo, GroupAddr ga, ByteString data) const;
	ByteString createTunnelResp(Byte seqNo) const;
	void checkMsg(ByteString msg) const;
	void checkTunnelReq(ByteString msg) const;
	void checkTunnelResp(ByteString msg) const;
	void checkConnResp(ByteString msg) const;
	void checkConnStateResp(ByteString msg, Byte channelId) const;
	void logMsg(ByteString msg, bool received) const;
	void logTunnelReq(ByteString msg) const;
};

#endif