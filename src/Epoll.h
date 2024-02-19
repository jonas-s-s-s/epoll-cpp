#pragma once

#include <functional>
#include <set>
#include <sys/epoll.h>
#include <unordered_map>

constexpr static const std::array<uint32_t, 6> allEventTypes{EPOLLIN, EPOLLOUT, EPOLLRDHUP, EPOLLPRI, EPOLLERR, EPOLLHUP};

class MonitoredDescriptor {
public:
    explicit MonitoredDescriptor(int monitoredFd);

    bool isInitialized = false;
    const int monitoredFd;

    /**
     * Checks if this eventType has a handler function assigned to it
     */
    bool hasHandler(uint32_t eventType) const;

    /**
     * Sets an event handler of a SINGLE eventType (don't use | bitwise or notation)
     */
    void setHandler(uint32_t eventType, std::function<void(int)> handler);

    /**
     * Gets the events handler associated with this SINGLE eventType
     */
    std::function<void(int)>& getHandler(uint32_t eventType);

private:
    // No need to use unordered_map since there are only 6 possible event types
    std::function<void(int)> IN_handler = nullptr;
    std::function<void(int)> OUT_handler = nullptr;
    std::function<void(int)> RDHUP_handler = nullptr;
    std::function<void(int)> PRI_handler = nullptr;
    std::function<void(int)> ERR_handler = nullptr;
    std::function<void(int)> HUP_handler = nullptr;
};

class Epoll {
public:
    Epoll(bool isEdgeTriggered);

    /**
     * Will add a file descriptor to this epoll.
     * Fd will be set to non-blocking if epoll is in edge triggered mode.
     * @param fd the file descriptor number
     */
    void addDescriptor(int fd);

    /**
     * This method is called automatically if you've added event handlers for "EPOLLRDHUP | EPOLLHUP".
     * Otherwise in order to free memory you have to call this manually once your fd closes.
     */
    void removeDescriptor(int monitoredFd);

    /**
     * Blocks thread until event occurs.
     */
    void waitForEvents();

    /**
     * Will add a handler function to event of certain fd which is monitored by this epoll.
     * The "| bitwise or notation" can be used to add handler to multiple events at once, for example: "EPOLLIN | EPOLLOUT".
     * @param monitoredFd fd which was previously registered by addDescriptor()
     * @param eventType the event unit32_t as specified in linux header <sys/epoll.h>
     * @param eventHandler a function which will be called once this event occurs
     */
    void addEventHandler(int monitoredFd, uint32_t eventType, std::function<void(int)> eventHandler);

    void removeEventHandler(int monitoredFd, uint32_t eventType);

    const std::unordered_map<int, MonitoredDescriptor>& getMonitoredFds() const;

    int getEpollFd() const;

    int isEdgeTriggered() const;

private:
    std::unordered_map<int, MonitoredDescriptor> _monitoredFds{};
    const int _epollFd;
    const int _isEdgeTriggered;

    const int _maxEventsNum = 10;
    std::vector<epoll_event> _eventsVector{};

    void _reloadEventHandlers(MonitoredDescriptor& md) const;

    /**
     * ADDS events to a NEW fd. If the FD is not new, _epollCtlModify must be used instead.
     */
    void _epollCtlAdd(int fd, uint32_t events) const;

    /**
     * REWRITES the events of certain FD. All previously added events will be REMOVED.
     */
    void _epollCtlModify(int fd, uint32_t events) const;

    static void _setNonBlocking(int fd);

    void _epollCtlDelete(int fd) const;

public:
    virtual ~Epoll();
};
