/*
 * This file is part of the MAVLink Router project
 *
 * Copyright (C) 2016  Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <string>

#include <dns_sd.h>

class Zeroconf {
public:
        virtual                         ~Zeroconf();

        static Zeroconf                 &init(uint16_t port, const std::string& regType);
        static Zeroconf                 &get_instance();

        int                             sockFd();
        void                            processResult();

private:
        Zeroconf() = default;
        Zeroconf(const Zeroconf &) = delete;
        Zeroconf &operator=(const Zeroconf &) = delete;

        int                             createSharedConnection();
	void                            registerZeroconf(uint16_t port, const std::string& regType);
	void                            deregisterZeroconf();
        void                            browseQGroundcontrol();

        static void                     registerCallBack(DNSServiceRef serviceRef,
                                                         DNSServiceFlags flags,
                                                         DNSServiceErrorType errorCode,
                                                         const char * name,
                                                         const char * regtype,
                                                         const char * domain,
                                                         void * context);

        static void                     browserCallBack(DNSServiceRef serviceRef,
                                                        DNSServiceFlags flags,
                                                        uint32_t interfaceIndex,
                                                        DNSServiceErrorType errorCode,
                                                        const char * name,
                                                        const char * regtype,
                                                        const char * domain,
                                                        void * context);

        static void                     resolveCallBack(DNSServiceRef serviceRef,
                                                        DNSServiceFlags flags,
                                                        uint32_t interfaceIndex,
                                                        DNSServiceErrorType errorCode,
                                                        const char * fullname,
                                                        const char * hosttarget,
							uint16_t port,
							uint16_t txtLen,
							const unsigned char *txtRecord,
                                                        void * context);

        DNSServiceRef                   _dnsSharedServiceRef;
        DNSServiceRef                   _dnsServiceRef1; // For register transaction
        DNSServiceRef                   _dnsServiceRef2; // For browse transaction
        DNSServiceRef                   _dnsServiceRef3; // For resolve transaction

        static Zeroconf                 _instance;
        static bool                     _initialized;
};
