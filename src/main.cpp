#include <sys/select.h>
#include <signal.h>
#include <chrono>

#include "config.h"
#include "logger.h"

using namespace std::chrono;

void sighandler(int signo) 
{
}

struct Stopwatch
{
	steady_clock::time_point start;
	Stopwatch() : start(steady_clock::now()) {}
	int getRuntime() { return duration_cast<milliseconds>(steady_clock::now() - start).count(); }
};

void logEvent(const Logger& logger, const Event& event, string postfix = "")
{
	LogMsg logMsg = logger.debug();
	logMsg << event.getType().toStr() << " from " << event.getOriginId() << " for " << event.getItemId();
	if (event.getType() != EventType::READ_REQ)
		logMsg << ": " << event.getValue().toStr() << " [" << event.getValue().getType().toStr() << "]";
	logMsg << postfix << endOfMsg();
}

int main(int argc, char* argv[])
{
	// install the signal handler for SIGTERM.
	struct sigaction action;
	action.sa_handler = sighandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	// block SIGTERM
	sigset_t sigset, oldset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, &oldset);

	// read configuration file
	Config configFile;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Configuration file name not specified");
		configFile.read(argv[1]);
	}
	catch (const std::exception& error)
	{
		cout << "Reading configuration file failed: " << error.what() << endl;
		return 1;
	}

	// initialize logging 
	Log log;
	try
	{
		log.init(configFile.getLogConfig());
	}
	catch (const std::exception& error)
	{
		cout << "Logging initialization failed: " << error.what() << endl;
		return 1;
	}
	Logger logger = log.newLogger("main");

	// first log messages
	logger.info() << "Started" << endOfMsg();
	logger.info() << "Using configuration file " << argv[1] << endOfMsg();

	// initialize items and links
	Links links;
	Items items;
	GlobalConfig config;
	try
	{
		config = configFile.getGlobalConfig();
		items = configFile.getItems();
		links = configFile.getLinks(items, log);

		// let links verify and adapt item properties
		for (auto& linkPair : links)
			linkPair.second.validate(items);

		// verify consistency of item properties
		for (auto& itemPair : items)
		{
			Item& item = itemPair.second;

			if (item.getOwnerId() != controlLinkId && !links.exists(item.getOwnerId()))
				throw std::runtime_error("Item " + item.getId() + " is associated with unknown link " + itemPair.second.getOwnerId());

//			if (!item.isWritable() && item.isResponsive())
//				throw std::runtime_error("Item " + item.getId() + " is not writable but responsive");
//			if (!item.isReadable() && item.isPollingEnabled())
//				throw std::runtime_error("Item " + item.getId() + " is not readable but polling is enabled");
		}
	}
	catch (const std::exception& error)
	{
		logger.error() << "Initialization failed: " << error.what() << endOfMsg();
		return 1;
	}

	// prepare polling
	std::time_t start = std::time(0);
	for (auto& itemPair : items)
		if (itemPair.second.isPollingEnabled())
			itemPair.second.initPolling(start);

	Events events;
	for (;;)
	{
		// wait for event
		try
		{
			int maxFd = 0;
			fd_set readFds, writeFds, excpFds;
			FD_ZERO(&readFds);
			FD_ZERO(&writeFds);
			FD_ZERO(&excpFds);
			long timeoutMs = 100;
			for (auto& linkPair : links)
				if (linkPair.second.isEnabled())
				{
					int linkMaxFd = 0;
					fd_set linkReadFds, linkWriteFds, linkExcpFds;
					FD_ZERO(&linkReadFds);
					FD_ZERO(&linkWriteFds);
					FD_ZERO(&linkExcpFds);
					long linkTimeoutMs = linkPair.second.collectFds(&linkReadFds, &linkWriteFds, &linkExcpFds, &linkMaxFd);

					if ((linkMaxFd > 0 || linkTimeoutMs == 0) && config.getLogPSelectCalls())
					{
						LogMsg logMsg = logger.debug();
						bool first = true;
						logMsg << "pselect() - Link " << linkPair.first << " requires timeout " << linkTimeoutMs << " and file descriptor set {";
						for (int fd = 0; fd <= linkMaxFd; fd++)
							if (FD_ISSET(fd, &linkReadFds) || FD_ISSET(fd, &linkWriteFds) || FD_ISSET(fd, &linkExcpFds))
							{
								if (!first)
									logMsg << ",";
								else
									first = false;
								logMsg << fd;
								if (FD_ISSET(fd, &linkReadFds))
									logMsg << "r";
								if (FD_ISSET(fd, &linkWriteFds))
									logMsg << "w";
								if (FD_ISSET(fd, &linkExcpFds))
									logMsg << "e";
							}
						logMsg << "}" << endOfMsg();
					}

					if (linkTimeoutMs != -1)
						timeoutMs = std::min(timeoutMs, linkTimeoutMs);
					for (int fd = 0; fd <= linkMaxFd; fd++)
					{
						if (FD_ISSET(fd, &linkReadFds))
							FD_SET(fd, &readFds);
						if (FD_ISSET(fd, &linkWriteFds))
							FD_SET(fd, &writeFds);
						if (FD_ISSET(fd, &linkExcpFds))
							FD_SET(fd, &excpFds);
					}
				}

			struct timespec timeout;
			timeout.tv_sec = timeoutMs / 1000;
			timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
			int rc = pselect(maxFd + 1, &readFds, &writeFds, &excpFds, &timeout, &oldset);
			if (rc == -1)
				if (errno == EINTR)
					break;
				else
					logger.errorX() << unixError("pselect") << endOfMsg();

			if (config.getLogPSelectCalls())
			{
				LogMsg logMsg = logger.debug();
				bool first = true;
				logMsg << "pselect() - Returns file descriptor set {";
				for (int fd = 0; fd <= maxFd; fd++)
					if (FD_ISSET(fd, &readFds) || FD_ISSET(fd, &writeFds) || FD_ISSET(fd, &excpFds))
					{
						if (!first)
							logMsg << ",";
						else
							first = false;
						logMsg << fd;
						if (FD_ISSET(fd, &readFds))
							logMsg << "r";
						if (FD_ISSET(fd, &readFds))
							logMsg << "w";
						if (FD_ISSET(fd, &excpFds))
							logMsg << "e";
					}
				logMsg << "}" << endOfMsg();
			}
		}
		catch (const std::exception& error)
		{
			logger.error() << "Error when waiting for event: " << error.what() << endOfMsg();
			continue;
		}

		// receive events
		for (auto& linkPair : links)
			if (linkPair.second.isEnabled())
				try
				{
					Stopwatch stopwatch;
					events.splice(events.begin(), linkPair.second.receive(items));
					long runtime = stopwatch.getRuntime();
					if (runtime > 10)
						logger.warn() << "Event receiving on link " << linkPair.first << " took " << runtime << " ms" << endOfMsg();
				}
				catch (const std::exception& error)
				{
					logger.error() << "Error on link " << linkPair.first << " when receiving events: " << error.what() << endOfMsg();
				}

		// only collect received events during the start phase but do not process them
		std::time_t now = std::time(0);
		if (now <= start + 3)
			continue;

		// analyze received events
		Events suppressedEvents;
		Events generatedEvents;
		for (auto eventPos = events.begin(); eventPos != events.end();)
		{
			// provide event
			Event& event = *eventPos;

			// provide item
			auto itemPos = items.find(event.getItemId());
			assert(itemPos != items.end());
			Item& item = itemPos->second;

			// provide link
//			auto linkPos = links.find(item.getOwnerId());
//			assert(linkPos != links.end());
//			Link& link = linkPos->second;

			// remove STATE_IND
			if (  event.getType() == EventType::STATE_IND
			   && !item.isSendRequired(event.getValue()) // if item value did not change (that much)
			   )
				{
					suppressedEvents.add(event);
					eventPos = events.erase(eventPos);
					continue;
				}

			// remove READ_REQ and generate STATE_IND
			if (  event.getType() == EventType::READ_REQ
			   && (  !item.isReadable() // if item can not be read
			      || item.isPollingEnabled()
			      || item.isSendOnChangeEnabled()
			      )
			   )
			{
				suppressedEvents.add(event);
				const Value& value = item.getLastSendValue();
				if (!value.isNull())
					generatedEvents.add(Event(controlLinkId, item.getId(), EventType::STATE_IND, value));
				else
					logger.warn() << "STATE_IND for READ_REQ on item " << event.getItemId()
					              << " can not be generated since its value is unknown" << endOfMsg();
				eventPos = events.erase(eventPos);
				continue;
			}

			// add READ_REQ
			if (  event.getType() == EventType::WRITE_REQ
			   && item.isReadable() // if item can be read
			   && !item.isResponsive() // if item does not react actively
			   )
				generatedEvents.add(Event(controlLinkId, item.getId(), EventType::READ_REQ, Value::newVoid()));

			// store STATE_IND
			if (event.getType() == EventType::STATE_IND)
			{
				item.setLastSendValue(event.getValue());
				item.setLastSendTime(now);
			}

			eventPos++;
		}

		// analyze items
		for (auto& itemPair : items)
		{
			// provide item
			Item& item = itemPair.second;

			// provide link
			if (item.getOwnerId() != controlLinkId)
			{
				auto linkPos = links.find(item.getOwnerId());
				assert(linkPos != links.end());
				if (!linkPos->second.isEnabled())
					continue;
			}

			// generate STATE_IND depending on send timer
			if (item.isSendRequired(now))
			{
				generatedEvents.add(Event(controlLinkId, item.getId(), EventType::STATE_IND, item.getLastSendValue()));
				item.setLastSendTime(now);
			}

			// generate READ_REQ depending on polling timer
			if (item.isPollingEnabled() && item.isPollingRequired(now))
			{
				generatedEvents.add(Event(controlLinkId, item.getId(), EventType::READ_REQ, Value()));
				item.pollingDone(now);
			}
		}

		// log events
		if (config.getLogEvents())
		{
			if (config.getLogSuppressedEvents())
				for (auto& event : suppressedEvents)
					logEvent(logger, event, " (suppressed)");
			for (auto& event : events)
				logEvent(logger, event);
			if (config.getLogGeneratedEvents())
				for (auto& event : generatedEvents)
					logEvent(logger, event, " (generated)");
		}

		// append generated events
		events.splice(events.end(), generatedEvents);

		// send events
		for (auto& linkPair : links)
			if (linkPair.second.isEnabled())
				try
				{
					Stopwatch stopwatch;
					linkPair.second.send(items, events);
					long runtime = stopwatch.getRuntime();
					if (runtime > 10)
						logger.warn() << "Event sending on link " << linkPair.first << " took " << runtime << " ms" << endOfMsg();
				}
				catch (const std::exception& error)
				{
					logger.error() << "Error on link " << linkPair.first << " when sending events: " << error.what() << endOfMsg();
				}

		events.clear();
	}

	// shutdown all links
	links.clear();

	// last log message
	logger.info() << "Stopped" << endOfMsg();
}
