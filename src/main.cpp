#include <sys/select.h>
#include <signal.h>
 
#include "config.h"
#include "logger.h"

void sighandler(int signo) 
{
}

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

		std::time_t now = std::time(0);
		for (auto& itemPair : items)
		{
			auto& item = itemPair.second;
			auto linkPair = links.find(item.getOwnerId());
			assert(linkPair != links.end());
			auto& link = linkPair->second;

			// manipulate item properties depending on link properties
			if (!link.supports(EventType::READ_REQ))
				item.setReadable(false);
			if (!link.supports(EventType::WRITE_REQ))
				item.setWritable(false);

			// prepare polling
			if (item.isPollingEnabled())
				item.initPolling(now);
		}
	}
	catch (const std::exception& error)
	{
		logger.error() << "Initialization failed: " << error.what() << endOfMsg();
		return 1;
	}

	for (;;)
	{
		// wait for event
		try
		{
			fd_set readFds, writeFds, excpFds;
			FD_ZERO(&readFds);
			FD_ZERO(&writeFds);
			FD_ZERO(&excpFds);
			int maxFd = 0;
			long timeoutMs = 100;
			for (auto& linkPair : links)
				if (linkPair.second.isEnabled())
				{
					long ms = linkPair.second.collectFds(&readFds, &writeFds, &excpFds, &maxFd);
					if (ms != -1)
						timeoutMs = std::min(timeoutMs, ms);
				}

//			cout << "maxFd: " << maxFd << endl;
//			for (int fd = 0; fd < 1000; fd++)
//				if (FD_ISSET(fd, &readFds))
//					cout << "Fd: " << fd << endl;

			struct timespec timeout;
			timeout.tv_sec = timeoutMs / 1000;
			timeout.tv_nsec = (timeoutMs % 1000) * 1000000;
			int rc = pselect(maxFd + 1, &readFds, &writeFds, &excpFds, &timeout, &oldset);
			if (rc == -1)
				if (errno == EINTR)
					break;
				else
					logger.errorX() << unixError("pselect") << endOfMsg();
			//logger.info() << "pselect() done" << endOfMsg();
		}
		catch (const std::exception& error)
		{
			logger.error() << "Error when waiting for event: " << error.what() << endOfMsg();
			continue;
		}

		std::time_t now = std::time(0);

		// receive events
		Events events;
		for (auto& linkPair : links)
			if (linkPair.second.isEnabled())
				try
				{
					events.splice(events.begin(), linkPair.second.receive(items));
				}
				catch (const std::exception& error)
				{
					logger.error() << "Error on link " << linkPair.first << " when receiving events: " << error.what() << endOfMsg();
				}

		// analyze received events
		Events suppressedEvents;
		Events generatedEvents;
		for (auto eventPos = events.begin(); eventPos != events.end();)
		{
			// provide event
			auto& event = *eventPos;

			// provide item
			auto itemPos = items.find(event.getItemId());
			assert(itemPos != items.end());
			auto& item = itemPos->second;

			// provide link
			auto linkPos = links.find(item.getOwnerId());
			assert(linkPos != links.end());
			auto& link = linkPos->second;

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
//			      || item.isSendOnChangeEnabled() // or if only changes are forwarded
			      )
			   )
			{
				suppressedEvents.add(event);
				const Value& value = item.getLastSendValue();
				if (!value.isNull())
				{
					generatedEvents.add(Event("main", item.getId(), EventType::STATE_IND, value));
					eventPos++;
				}
				else
					eventPos = events.erase(eventPos);
				continue;
			}

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
			auto& item = itemPair.second;

			// provide link
			auto linkPos = links.find(item.getOwnerId());
			assert(linkPos != links.end());
			auto& link = linkPos->second;

			if (link.isEnabled())
			{
				// generate STATE_IND depending on send timer
				if (item.isSendRequired(now))
				{
					generatedEvents.add(Event("main", item.getId(), EventType::STATE_IND, item.getLastSendValue()));
					item.setLastSendTime(now);
				}

				// generate READ_REQ depending on polling timer
				if (item.isPollingEnabled() && item.isPollingRequired(now))
				{
					generatedEvents.add(Event("main", item.getId(), EventType::READ_REQ, Value()));
					item.pollingDone(now);
				}
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
					linkPair.second.send(items, events);
				}
				catch (const std::exception& error)
				{
					logger.error() << "Error on link " << linkPair.first << " when sending events: " << error.what() << endOfMsg();
				}
	}

	// shutdown all links
	links.clear();

	// last log message
	logger.info() << "Stopped" << endOfMsg();
}
