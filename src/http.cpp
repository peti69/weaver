#include <cstring>

#include "http.h"

HttpHandler::HttpHandler(string _id, HttpConfig _config, Logger _logger) :
	id(_id), config(_config), logger(_logger), handle(0)
{
	// boot curl
	curl_global_init(CURL_GLOBAL_ALL);

	// log cURL version and capabilities
	curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
	if (info)
	{
		LogMsg msg = logger.info();
		msg << "Using cURL " << info->version << ", ";
		if (info->ssl_version)
			msg << "TLS/SSL support (" << info->ssl_version << "), ";
		else
			msg << "no TTS/SSL support, ";
		if (info->features & CURL_VERSION_ASYNCHDNS)
			msg << "async DNS";
		else
			msg << "sync DNS";
		msg << endOfMsg();
	}

	// allocate multi handle
	handle = curl_multi_init();
}

HttpHandler::~HttpHandler()
{
	// free multi handle
	curl_multi_cleanup(handle);

	// shutdown curl
	curl_global_cleanup();
}

void HttpHandler::validate(Items& items)
{
	auto& bindings = config.getBindings();

	for (auto& [itemId, item] : items)
		if (item.getOwnerId() == id && !bindings.count(itemId))
			throw std::runtime_error("Item " + itemId + " has no binding for link " + id);

	for (auto& [itemId, binding] : bindings)
	{
		auto& item = items.validate(itemId);
		if (item.getOwnerId() == id)
		{
			if (item.isReadable())
				item.validatePollingEnabled(true);
		}
	}
}

long HttpHandler::collectFds(fd_set* readFds, fd_set* writeFds, fd_set* excpFds, int* maxFd)
{
	long timeout;
	CURLMcode mcode = curl_multi_timeout(handle, &timeout);
	handleMultiError("curl_multi_timeout", mcode, logger.error());

	int curlMaxFd = -1;
	mcode = curl_multi_fdset(handle, readFds, writeFds, excpFds, &curlMaxFd);
	handleMultiError("curl_multi_fdset", mcode, logger.error());
	*maxFd = std::max(*maxFd, curlMaxFd);

	return timeout;
}

Events HttpHandler::receive(const Items& items)
{
	try
	{
		return receiveX();
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	return Events();
}

Events HttpHandler::send(const Items& items, const Events& events)
{
	try
	{
		return sendX(events);
	}
	catch (const std::exception& ex)
	{
		logger.error() << ex.what() << endOfMsg();
	}

	return Events();
}

//static size_t readCallback(char* buffer, size_t size, size_t nitems, string* userdata)
//{
//	size_t totalSize = size * nitems;
//	if (totalSize > userdata->length())
//		totalSize = userdata->length();
//	memcpy(buffer, userdata->data(), totalSize);
//	userdata->erase(0, totalSize);
//	return totalSize;
//}

static size_t writeCallback(char* buffer, size_t size, size_t nmemb, string* userdata)
{
    size_t totalSize = size * nmemb;
    userdata->append(buffer, totalSize);
    return totalSize;
}

static int debugCallback(CURL* handle, curl_infotype type, char* data, size_t size, Logger* logger)
{
	string logData(data, size);
	if (logData[logData.length() - 1] == '\n')
		logData.erase(logData.length() - 1);
	string prefix = "D: ";
	if (type == CURLINFO_HEADER_OUT || type == CURLINFO_DATA_OUT)
		prefix = "S: ";
	else if (type == CURLINFO_HEADER_IN || type == CURLINFO_DATA_IN)
		prefix = "R: ";
	logger->debug() << prefix << logData << endOfMsg();

	return 0;
}

Events HttpHandler::receiveX()
{
	Events events;

	// process ongoing transfers
	int activeHandles;
	CURLMcode mcode = curl_multi_perform(handle, &activeHandles);
	handleMultiError("curl_multi_perform", mcode);

	// check for finished transfers
	CURLMsg* msg;
	int waitingMessages;
	while ((msg = curl_multi_info_read(handle, &waitingMessages)) != NULL)
		if (msg->msg == CURLMSG_DONE)
		{
			auto transferPos = transfers.find(msg->easy_handle);
			if (transferPos != transfers.end())
			{
				string itemId = transferPos->second.event.getItemId();
				string response = *transferPos->second.response;

				// examine transfer result
				CURLcode code = msg->data.result;
				if (code != CURLE_OK)
					logger.error() << "Transfer for item " << itemId << " failed with error code "
					               << code << " (" << curl_easy_strerror(code) << ") and error details '"
					               << transferPos->second.errorBuffer << "'" << endOfMsg();
				else
				{
					if (config.getLogTransfers())
						logger.debug() << "Transfer for item " << itemId << " completed with response '"
						               << response << "'" << endOfMsg();

					auto bindingPos = config.getBindings().find(itemId);
					if (bindingPos != config.getBindings().end())
					{
						auto& binding = bindingPos->second;

						// compare returned response with response pattern
						if (std::regex_search(response, binding.responsePattern))
						{
							if (transferPos->second.event.getType() == EventType::READ_REQ)
								events.add(Event(id, itemId, EventType::STATE_IND, Value::newString(response)));
						}
						else
							// no match
							logger.error() << "Response '" << response << "' for item "
							               << itemId << " not expected" << endOfMsg();
					}
				}

				curl_slist_free_all(transferPos->second.headers);

				// remove finished transfer from the list of ongoing transfers
				transfers.erase(transferPos);
			}

			// remove easy handle from multi handle
			curl_multi_remove_handle(handle, msg->easy_handle);

			// deallocate easy handle
			curl_easy_cleanup(msg->easy_handle);
		}

	return events;
}

Events HttpHandler::sendX(const Events& events)
{
	auto& bindings = config.getBindings();

	for (auto& event : events)
	{
		string itemId = event.getItemId();

		auto bindingPos = bindings.find(itemId);
		if (bindingPos != bindings.end())
		{
			auto& binding = bindingPos->second;

			// new transfer is required
			Transfer transfer(event);

			// allocate easy handle
			CURL* easyHandle = curl_easy_init();
			if (!easyHandle)
				logger.errorX() << "Function curl_easy_init() failed" << endOfMsg();

			// instruct cULR to enable TCP keep alive probes
			CURLcode code = curl_easy_setopt(easyHandle, CURLOPT_TCP_KEEPALIVE , 1L);
			handleError("curl_easy_setopt", code);

			// construct URL
			string url = binding.url;
			static const string valueTag = "%EventValue%";
			if (auto pos = url.find(valueTag); pos != string::npos)
				url.replace(pos, valueTag.length(), event.getValue().toStr());

			// pass URL to cURL
			code = curl_easy_setopt(easyHandle, CURLOPT_URL, url.c_str());
			handleError("curl_easy_setopt", code);

			// tell cURL that a POST instead of a GET is required
			if (binding.request.length())
			{
				code = curl_easy_setopt(easyHandle, CURLOPT_POSTFIELDS, binding.request.c_str());
				handleError("curl_easy_setopt", code);
			}

			// pass header to cURL
			for (string header : binding.headers)
			{
				transfer.headers = curl_slist_append(transfer.headers, header.c_str());
				if (!transfer.headers)
					logger.errorX() << "Function curl_slist_append() failed" << endOfMsg();
			}
			code = curl_easy_setopt(easyHandle, CURLOPT_HTTPHEADER, transfer.headers);
			handleError("curl_easy_setopt", code);

			// instruct cULR how to authenticate
			if (config.getUser() != "" && config.getPassword() != "")
			{
				string userPassword = config.getUser() + ":" + config.getPassword();
				code = curl_easy_setopt(easyHandle, CURLOPT_USERPWD, userPassword.c_str());
				handleError("curl_easy_setopt", code);
				code = curl_easy_setopt(easyHandle, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
				handleError("curl_easy_setopt", code);
			}

			// instruct cURL not perform any validation of the server certificate
			code = curl_easy_setopt(easyHandle, CURLOPT_SSL_VERIFYPEER, 0L);
			handleError("curl_easy_setopt", code);
			code = curl_easy_setopt(easyHandle, CURLOPT_SSL_VERIFYHOST, 0L);
			handleError("curl_easy_setopt", code);

			// instruct cURL how to handle the response
			code = curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, writeCallback);
			handleError("curl_easy_setopt", code);
			code = curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, transfer.response.get());
			handleError("curl_easy_setopt", code);

			// instruct cURL how to do debug logging
			code = curl_easy_setopt(easyHandle, CURLOPT_VERBOSE, config.getVerboseMode());
			handleError("curl_easy_setopt", code);
			code = curl_easy_setopt(easyHandle, CURLOPT_DEBUGFUNCTION, debugCallback);
			handleError("curl_easy_setopt", code);
			code = curl_easy_setopt(easyHandle, CURLOPT_DEBUGDATA, &logger);
			handleError("curl_easy_setopt", code);

			transfer.errorBuffer[0] = 0;
			code = curl_easy_setopt(easyHandle, CURLOPT_ERRORBUFFER , transfer.errorBuffer);
			handleError("curl_easy_setopt", code);

			// add easy handle to multi handle
			CURLMcode mcode = curl_multi_add_handle(handle, easyHandle);
			handleMultiError("curl_multi_add_handle", mcode);

			if (config.getLogTransfers())
				logger.debug() << "Transfer for item " << itemId << " to URL " << url
				               << " started with request '" << binding.request << "'" << endOfMsg();

			// add new transfer to the list of ongoing transfers
			transfers.insert({easyHandle, transfer});
		}
	}

	return Events();
}

void HttpHandler::handleError(string funcName, CURLcode errorCode, LogMsg logMsg) const
{
	if (errorCode != CURLE_OK)
	{
		logMsg << "Function " << funcName << "() returned error "
		       << errorCode << " (" << curl_easy_strerror(errorCode) << ")";
		logMsg << endOfMsg();
	}
}

void HttpHandler::handleError(string funcName, CURLcode errorCode) const
{
	handleError(funcName, errorCode, logger.errorX());
}

void HttpHandler::handleMultiError(string funcName, CURLMcode errorCode, LogMsg logMsg) const
{
	if (errorCode != CURLM_OK)
	{
		logMsg << "Function " << funcName << "() returned error "
		       << errorCode << " (" << curl_multi_strerror(errorCode) << ")";
		logMsg << endOfMsg();
	}
}

void HttpHandler::handleMultiError(string funcName, CURLMcode errorCode) const
{
	handleMultiError(funcName, errorCode, logger.errorX());
}

