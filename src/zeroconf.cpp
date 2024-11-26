#include <memory>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>

#include "mainloop.h"
#include "zeroconf.h"

Zeroconf Zeroconf::_instance{};
bool Zeroconf::_initialized{false};

Zeroconf& Zeroconf::init(uint16_t port, const std::string& regtype)
{
    if (_instance.createSharedConnection() == 0) {
        _instance.registerZeroconf(port, regtype);
        _instance.browseQGroundcontrol();
    }
    return _instance;
}

Zeroconf& Zeroconf::get_instance()
{
    return _instance;
}

Zeroconf::~Zeroconf()
{
    deregisterZeroconf();
}

int Zeroconf::createSharedConnection()
{
    DNSServiceErrorType error;

    if (!_initialized) {
        error = DNSServiceCreateConnection(&_dnsSharedServiceRef);
        if (error != kDNSServiceErr_NoError) {
            log_info("Failed to create shared connection:%d", error);
            return -1;
        }
        _initialized = true;
    }
    return 0;
}

void Zeroconf::registerZeroconf(uint16_t port, const std::string& regType)
{
    DNSServiceErrorType error;

    if (!_initialized)
        return;

    if (port != 0) {
        //// COPY the primary shared DNSServiceRef
        _dnsServiceRef1 = _dnsSharedServiceRef;

        error = DNSServiceRegister(&_dnsServiceRef1,
	                           kDNSServiceFlagsShareConnection,// Shared connection.
                                   0,
                                   0,
                                   regType.c_str(),
                                   nullptr,
                                   nullptr,
                                   htons(port),
                                   0,
                                   nullptr,
                                   Zeroconf::registerCallBack,
                                   nullptr);

        if (error != kDNSServiceErr_NoError) {
            log_info("DNSServiceRegister returned error %d", error);
        }
    }
}

void Zeroconf::deregisterZeroconf()
{
    if (_initialized) {
        if (_dnsServiceRef1) {
            DNSServiceRefDeallocate(_dnsServiceRef1);
            _dnsServiceRef1 = nullptr;
        }
        if (_dnsServiceRef2) {
            DNSServiceRefDeallocate(_dnsServiceRef2);
            _dnsServiceRef2 = nullptr;
        }
        if (_dnsServiceRef3) {
            DNSServiceRefDeallocate(_dnsServiceRef3);
            _dnsServiceRef3 = nullptr;
        }

        DNSServiceRefDeallocate(_dnsSharedServiceRef);
        _dnsSharedServiceRef = nullptr;
    }
}

int Zeroconf::sockFd()
{
    if (_initialized) {
        return DNSServiceRefSockFD(_dnsSharedServiceRef);
    }
    return -1;
}

void Zeroconf::registerCallBack(DNSServiceRef serviceRef,
                                DNSServiceFlags flags,
                                DNSServiceErrorType errorCode,
                                const char * name,
                                const char * regtype,
                                const char * domain,
                                void * context)
{
    if (errorCode == kDNSServiceErr_NoError) {
        if (flags & kDNSServiceFlagsAdd)
            log_info("Service %s.%s%s registerred and active", name, regtype, domain);
    } else if (errorCode == kDNSServiceErr_NameConflict) {
        log_info("Service %s.%s%s is already in-used", name, regtype, domain);
    } else {
        log_info("Failed to register service %s.%s%s: %d",
                 name, regtype, domain, errorCode);
    }
}

void Zeroconf::browserCallBack(DNSServiceRef serviceRef,
                               DNSServiceFlags flags,
                               uint32_t interfaceIndex,
                               DNSServiceErrorType errorCode,
                               const char * name,
                               const char * regtype,
                               const char * domain,
                               void * context)
{
    Zeroconf *thiz = reinterpret_cast<Zeroconf*>(context);
    DNSServiceErrorType err;

    if (errorCode == kDNSServiceErr_NoError) {
        const char * action;

        if (flags & kDNSServiceFlagsAdd) action = "ADD";
        else action  = "RMV";

        log_info("%s %30s.%s%s on interface %d\n", action, name, regtype, domain, (int) interfaceIndex);

        if (flags & kDNSServiceFlagsAdd) {
            thiz->_dnsServiceRef3 = thiz->_dnsSharedServiceRef;

            err = DNSServiceResolve(&thiz->_dnsServiceRef3,
                                    kDNSServiceFlagsShareConnection,
                                    interfaceIndex,
                                    name,
                                    regtype,
                                    domain,
                                    Zeroconf::resolveCallBack,
                                    context);
            if (err != kDNSServiceErr_NoError) {
                log_info("DNSServiceResolve failed: %d\n", err);
            }
        }
    } else {
        log_info("Bonjour browser error occurred: %d\n", errorCode);
    }

    DNSServiceRefDeallocate(thiz->_dnsServiceRef2);
    thiz->_dnsServiceRef2 = nullptr;
}

void Zeroconf::resolveCallBack(DNSServiceRef serviceRef,
                               DNSServiceFlags flags,
                               uint32_t interfaceIndex,
                               DNSServiceErrorType errorCode,
                               const char * fullname,
                               const char * hosttarget,
                               uint16_t port, // in network byte order
                               uint16_t txtLen,
                               const unsigned char *txtRecord,
                               void * context)
{
    Zeroconf *thiz = reinterpret_cast<Zeroconf*>(context);

    if (errorCode == kDNSServiceErr_NoError) {
        struct addrinfo *addrs = nullptr;
        int err = getaddrinfo(hosttarget, NULL, NULL, &addrs);
        if (err) {
            log_info("getaddrinfo failed, error=%d", err);
        } else {
            struct addrinfo *addrInfo = nullptr;

            for (addrInfo = addrs; addrInfo; addrInfo = addrInfo->ai_next) {
                if (addrInfo->ai_family == AF_INET) {
                    std::string name = std::string(hosttarget);
                    auto udp = std::make_shared<UdpEndpoint>(name);
                    char* ip = inet_ntoa(((struct sockaddr_in *)addrInfo->ai_addr)->sin_addr);
                    int fd = udp->open_ipv4(ip, ntohs(port), UdpEndpointConfig::Mode::Client);
                    if (fd > -1) {
                        log_info("Found %s at %s:%d", hosttarget, ip, ntohs(port));
                        Mainloop::get_instance().add_endpoint(udp);
                        break;
                    }
                }
            }

            if (addrs)
                freeaddrinfo(addrs);
        }
    }

    DNSServiceRefDeallocate(thiz->_dnsServiceRef3);
    thiz->_dnsServiceRef3 = nullptr;
}

void Zeroconf::browseQGroundcontrol()
{
    DNSServiceErrorType err;

    if (!_initialized)
        return;

    //// COPY the primary shared DNSServiceRef
    _dnsServiceRef2 = _dnsSharedServiceRef;

    err = DNSServiceBrowse(
                &_dnsServiceRef2,              // Receives reference to Bonjour browser object.
                kDNSServiceFlagsShareConnection,// Shared connection.
                kDNSServiceInterfaceIndexAny,   // Browse on all network interfaces.
                "_qgroundcontrol._udp",         // Browse for QGroundControl service types.
                nullptr,                        // Browse on the default domain (e.g. local.).
                Zeroconf::browserCallBack,      // Callback function when Bonjour events occur.
                this);

    if (err != kDNSServiceErr_NoError) {
        log_info("DNSServiceBrowse failed: %d\n", err);
    }
}

void Zeroconf::processResult()
{
    if (_initialized) {
        DNSServiceErrorType err = DNSServiceProcessResult(_dnsSharedServiceRef);
        if (err != kDNSServiceErr_NoError) {
            log_info("DNSServiceProcessResult failed: %d\n", err);
        }
    }
}
