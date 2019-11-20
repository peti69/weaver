#ifndef KNX_H
#define KNX_H

#include <ctime>
#include <set>

#include "link.h"
#include "logger.h"

typedef unsigned short IpPort;

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
	static const Value TUNNEL_ACK = 0x0421;
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
	
	ByteString exportValue(const Value& value) const;
	Value importValue(ByteString bytes) const;
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
		GroupAddr stateGa;
		GroupAddr writeGa;
		DatapointType dpt;
		Binding(string _itemId, GroupAddr _stateGa, GroupAddr _writeGa, DatapointType _dpt) : 
			itemId(_itemId), stateGa(_stateGa), writeGa(_writeGa), dpt(_dpt) {}
	};
	class Bindings: public std::map<string, Binding> 
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
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
		CONNECTED,
	};
	string id;
	KnxConfig config;
	Logger logger;
	int socket;
	IpPort localIpPort;
	IpPort dataIpPort;
	IpAddr dataIpAddr;
	State state;

	// Channel id returned in CONNECTION RESPONSE and used for TUNNEL REQUEST.
	Byte channelId;

	// Physical address returned in CONNECTION RESPONSE or the configured one.
	PhysicalAddr physicalAddr;

	// Time when last connect attempt has been started.
	std::time_t lastConnectTry;

	// Has a CONNECTION STATE REQUEST been sent for which a CONNECTION STATE RESPONSE
	// is pending?
	bool ongoingConnStateReq;

	// Time when the last CONNECTION REQUEST or CONNECTION STATE REQUST has been sent.
	std::time_t lastControlReqSendTime;

	// Sequence number of last received and accepted TUNNEL REQUEST.
	Byte lastReceivedSeqNo;

	// Sequence number of last sent TUNNEL REQUEST.
	Byte lastSentSeqNo;

	struct LDataReq
	{
		string itemId;
		GroupAddr ga;
		ByteString data;
		int attempts;
		LDataReq() : attempts(0) {}
		LDataReq(string _itemId, GroupAddr _ga, ByteString _data) :
			itemId(_itemId), ga(_ga), data(_data), attempts(0) {}
	};

	// L_Data.req messages which are waiting to be sent as TUNNEL REQUEST or to be
	// confirmed by a TUNNEL ACK.
	std::list<LDataReq> waitingLDataReqs;

	// Last TUNNEL REQUEST which has been sent.
	ByteString lastSentTunnelReq;

	// Time when the last TUNNEL REQUEST has been sent. It is set to 0 when the
	// TUNNEL ACK is received.
	std::time_t lastTunnelReqSendTime;

	// Number of times the last TUNNEL REQUEST has already been sent.
	int lastTunnelReqSendAttempts;

	// L_Data.req messages which have been sent but for which no L_Data.con has
	// been received so far.
	struct SentLDataReq
	{
		LDataReq ldataReq;
		std::time_t time; // time when TUNNEL REQUEST has been sent
		SentLDataReq(const LDataReq& _ldataReq, std::time_t _time) : ldataReq(_ldataReq), time(_time) {}
	};
	std::list<SentLDataReq> sentLDataReqs;

	// READ_REQ events which have been received and for which so far no STATE_IND has
	// been received.
	// Attention: Timeouts are currently not detected.
	std::set<string> receivedReadReqs;

public:
	KnxHandler(string _id, KnxConfig _config, Logger _logger);
	virtual ~KnxHandler();
	virtual bool supports(EventType eventType) const override { return true; }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;
	
private:
	void close();
	void disconnect();
	Events receiveX(const Items& items);
	Events sendX(const Items& items, const Events& events);
	void sendTunnelReq(ByteString msg);
	void sendTunnelReq(const LDataReq& ldataReq);
	void processReceivedLDataCon(ByteString msg);
	void processReceivedTunnelAck(ByteString msg);
	void processPendingLDataCon();
	void processPendingTunnelAck();
	void processWaitingLDataReqs();
	bool receiveMsg(ByteString& msg, IpAddr& addr, IpPort& port) const;
	void sendMsg(IpAddr addr, IpPort port, ByteString msg) const;
	void sendControlMsg(ByteString msg) const;
	void sendDataMsg(ByteString msg) const;
	ByteString createConnReq() const;
	ByteString createConnStateReq() const;
	ByteString createDiscReq() const;
	ByteString createDiscResp() const;
	ByteString createTunnelReq(Byte seqNo, GroupAddr ga, ByteString data) const;
	ByteString createTunnelAck(Byte seqNo) const;
	void checkMsg(ByteString msg) const;
	void checkTunnelReq(ByteString msg) const;
	void checkTunnelAck(ByteString msg) const;
	void checkConnResp(ByteString msg) const;
	void checkConnStateResp(ByteString msg, Byte channelId) const;
	void logMsg(ByteString msg, bool received) const;
	void logTunnelReq(ByteString msg) const;
	string getStatusCodeName(Byte statusCode) const;
	string getStatusCodeExplanation(Byte statusCode) const;
	string getStatusCodeText(Byte statusCode) const;
};

#endif
