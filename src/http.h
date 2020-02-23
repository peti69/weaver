#ifndef HTTP_H
#define HTTP_H

#include <curl/curl.h>

#include <regex>

#include "link.h"
#include "logger.h"

class HttpConfig
{
public:
	struct Binding {
		string itemId;
		string url;
		std::list<string> headers;
		string request;
		std::regex responsePattern;
		Binding(string _itemId, string _url, std::list<string> _headers, string _request, std::regex _responsePattern) :
			itemId(_itemId), url(_url), headers(_headers), request(_request), responsePattern(_responsePattern) {}
	};
	class Bindings: public std::map<string, Binding>
	{
	public:
		void add(Binding binding) { insert(value_type(binding.itemId, binding)); }
	};

private:
	bool logTransfers;
	bool verboseMode;
	Bindings bindings;

public:
	HttpConfig(bool _logTransfers, bool _verboseMode, Bindings _bindings) :
		logTransfers(_logTransfers), verboseMode(_verboseMode), bindings(_bindings) {}
	bool getLogTransfers() const { return logTransfers; }
	bool getVerboseMode() const { return verboseMode; }
	const Bindings& getBindings() const { return bindings; }
};

class HttpHandler: public HandlerIf
{
private:
	string id;
	HttpConfig config;
	Logger logger;

	// Multi handle used for all easy handles
	CURLM* handle;

	// Information stored per ongoing transfer
	struct Transfer
	{
		string itemId;
		curl_slist* headers;
		std::shared_ptr<string> response;
		char errorBuffer[CURL_ERROR_SIZE];
		Transfer() : response(new string()), headers(0) {}
		Transfer(string _itemId) : itemId(_itemId), headers(0), response(new string()) {}
	};

	// Mapping from easy handle to transfer information
	std::map<CURL*, Transfer> transfers;

public:
	HttpHandler(string _id, HttpConfig _config, Logger _logger);
	virtual ~HttpHandler();
	virtual bool supports(EventType eventType) const override { return eventType != EventType::STATE_IND; }
	virtual HandlerState getState() const override { return HandlerState(); }
	virtual long collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd) override;
	virtual Events receive(const Items& items) override;
	virtual Events send(const Items& items, const Events& events) override;

private:
	Events receiveX();
	Events sendX(const Events& events);
	void handleError(string funcName, CURLcode errorCode, LogMsg logMsg) const;
	void handleError(string funcName, CURLcode errorCode) const;
	void handleMultiError(string funcName, CURLMcode errorCode, LogMsg logMsg) const;
	void handleMultiError(string funcName, CURLMcode errorCode) const;
};

#endif
