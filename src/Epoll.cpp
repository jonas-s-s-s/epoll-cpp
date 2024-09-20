#include "Epoll.h"
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>

Epoll::Epoll(bool isEdgeTriggered) : _epollFd(epoll_create1(0)), _isEdgeTriggered(isEdgeTriggered) {
    if (_epollFd == -1) {
        throw std::runtime_error("Epoll::Epoll: ERROR - Failed to create epoll file descriptor.");
    }

    _eventsVector.reserve(_maxEventsNum * sizeof(epoll_event));
}

Epoll::~Epoll() {
    close(_epollFd);
}

// # Epoll class public interface
// ######################################################################################################################

void Epoll::addDescriptor(int fd) {
    _monitoredFds.try_emplace(fd, fd);

    if (_isEdgeTriggered) {
        _setNonBlocking(fd);
    }
}

void Epoll::removeDescriptor(int monitoredFd) {
    if (_monitoredFds.count(monitoredFd) != 0) {
        _epollCtlDelete(monitoredFd);
        _monitoredFds.erase(monitoredFd);
    }
}

void Epoll::waitForEvents(int timeout) {
    // Start waiting for descriptor events
    int numOfEvents = epoll_wait(_epollFd, &_eventsVector[0], _maxEventsNum, timeout);

    for (int i = 0; i < numOfEvents; i++) {
        uint32_t events = _eventsVector[i].events;
        int fd = _eventsVector[i].data.fd;

        // Check for all possible event types
        for (uint32_t evt: allEventTypes) {
            // The monitored descriptor can be removed during the event handling process, protect against this
            if (_monitoredFds.count(fd) == 0)
                return;

            // Check if the handler for this event exists
            if (_monitoredFds.at(fd).hasHandler(events & evt)) {
                // Call the handler function
                _monitoredFds.at(fd).getHandler(events & evt)(fd);
            }
        }

        // Remove this descriptor if it's closing (this will work only if EPOLLRDHUP or EPOLLHUP events are listened for)
        if (events & (EPOLLRDHUP | EPOLLHUP)) {
            removeDescriptor(fd);
        }
    }
}

void Epoll::addEventHandler(int monitoredFd, uint32_t eventType, std::function<void(int)> eventHandler) {
    if (_monitoredFds.count(monitoredFd) == 0) {
        throw std::runtime_error("Epoll::addEventHandler: ERROR - file descriptor must first be added to Epoll before adding event handler.");
    }

    MonitoredDescriptor &md = _monitoredFds.at(monitoredFd);

    // Check for all possible event types
    for (uint32_t evt: allEventTypes) {
        if (eventType & evt) {
            // Set the handler, if eventType includes this evt
            md.setHandler(evt, eventHandler);
        }
    }

    // After all handlers are set, register the events for listening with the OS kernel
    _reloadEventHandlers(md);
}

void Epoll::removeEventHandler(int monitoredFd, uint32_t eventType) {
    auto &md = _monitoredFds.at(monitoredFd);

    // Check for all possible event types
    for (uint32_t evt: allEventTypes) {
        // Check if the handler for this event exists
        if (md.hasHandler(eventType & evt)) {
            // remove the handler function
            md.setHandler(eventType & evt, nullptr);
        }
    }

    // Make sure that removed events aren't listened for by the OS kernel
    _reloadEventHandlers(md);
}

// # Epoll class getters
// ######################################################################################################################

const std::unordered_map<int, MonitoredDescriptor> &Epoll::getMonitoredFds() const {
    return _monitoredFds;
}

int Epoll::getEpollFd() const {
    return _epollFd;
}

int Epoll::isEdgeTriggered() const {
    return _isEdgeTriggered;
}

// # Epoll class private members
// ######################################################################################################################

void Epoll::_reloadEventHandlers(MonitoredDescriptor &md) const {
    uint32_t resultingEvents = 0;

    // Construct a resultingEvents variable for all registered event handlers of monitoredDescriptor
    bool firstEvt = true;
    for (uint32_t evt: allEventTypes) {
        if (md.hasHandler(evt)) {
            if (firstEvt) {
                resultingEvents = evt;
                firstEvt = false;
            } else {
                resultingEvents |= evt;
            }
        }
    }

    if (_isEdgeTriggered) {
        if (firstEvt)
            resultingEvents = EPOLLET;
        else
            resultingEvents |= EPOLLET;
    }

    //"EPOLL_CTL_ADD" can be called for a single FD only once
    if (md.isInitialized) {
        _epollCtlModify(md.monitoredFd, resultingEvents);
    } else {
        _epollCtlAdd(md.monitoredFd, resultingEvents);
        md.isInitialized = true;
    }
}

void Epoll::_epollCtlAdd(int fd, uint32_t events) const {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw std::runtime_error("Epoll::_epollCtlAdd: ERROR - Failed adding event to descriptor.");
    }
}

void Epoll::_epollCtlModify(int fd, uint32_t events) const {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(_epollFd, EPOLL_CTL_MOD, fd, &ev) == -1) {
        throw std::runtime_error("Epoll::_epollCtlModify: ERROR - Failed modifying file descriptor events.");
    }
}

void Epoll::_setNonBlocking(int fd) {
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
        throw std::runtime_error("Epoll::_setNonBlocking: ERROR - Failed to set descriptor into non-blocking mode.");
    }
}

void Epoll::_epollCtlDelete(int fd) const {
    struct epoll_event ev{};
    ev.data.fd = fd;
    epoll_ctl(_epollFd, EPOLL_CTL_DEL, fd, &ev);
}

// # MonitoredDescriptor members
// ######################################################################################################################

MonitoredDescriptor::MonitoredDescriptor(int monitoredFd) : monitoredFd(monitoredFd) {}

bool MonitoredDescriptor::hasHandler(uint32_t eventType) const {
    switch (eventType) {
        case EPOLLIN:
            return IN_handler != nullptr;
        case EPOLLOUT:
            return OUT_handler != nullptr;
        case EPOLLRDHUP:
            return RDHUP_handler != nullptr;
        case EPOLLPRI:
            return PRI_handler != nullptr;
        case EPOLLERR:
            return ERR_handler != nullptr;
        case EPOLLHUP:
            return HUP_handler != nullptr;
        default:
            return false;
    }
}

void MonitoredDescriptor::setHandler(uint32_t eventType, std::function<void(int)> handler) {
    switch (eventType) {
        case EPOLLIN:
            IN_handler = std::move(handler);
            return;
        case EPOLLOUT:
            OUT_handler = std::move(handler);
            return;
        case EPOLLRDHUP:
            RDHUP_handler = std::move(handler);
            return;
        case EPOLLPRI:
            PRI_handler = std::move(handler);
            return;
        case EPOLLERR:
            ERR_handler = std::move(handler);
            return;
        case EPOLLHUP:
            HUP_handler = std::move(handler);
            return;
        default:
            return;
    }
}

std::function<void(int)> &MonitoredDescriptor::getHandler(uint32_t eventType) {
    switch (eventType) {
        case EPOLLIN:
            return IN_handler;
        case EPOLLOUT:
            return OUT_handler;
        case EPOLLRDHUP:
            return RDHUP_handler;
        case EPOLLPRI:
            return PRI_handler;
        case EPOLLERR:
            return ERR_handler;
        case EPOLLHUP:
            return HUP_handler;
        default:
            throw std::runtime_error("Epoll::MonitoredDescriptor::getHandler: ERROR - passed eventType is invalid.");
    }
}
