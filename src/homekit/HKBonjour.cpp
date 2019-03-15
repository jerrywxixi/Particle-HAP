//  Copyright (C) 2010 Georg Kaindl
//  http://gkaindl.com
//
//  Particle.io Port Copyright (c) 2017 Mobile FLow LLC
//  http://github/moflome
//
//  This file is part of Arduino EthernetBonjour.
//
//  EthernetBonjour is free software: you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public License
//  as published by the Free Software Foundation, either version 3 of
//  the License, or (at your option) any later version.
//
//  EthernetBonjour is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with EthernetBonjour. If not, see
//  <http://www.gnu.org/licenses/>.
//

#define  HAS_SERVICE_REGISTRATION      1  // disabling saves about 1.25 kilobytes
#define  HAS_NAME_BROWSING             1  // disable together with above, additionally saves about 4.3 kilobytes

#include <string.h>
#include <stdlib.h>

#include "HKBonjour.h"

#define  MDNS_DEFAULT_NAME       "arduino"
#define  MDNS_TLD                ".local"
#define  DNS_SD_SERVICE          "_services._dns-sd._udp.local"
#define  MDNS_SERVER_PORT        5353
#define  MDNS_NQUERY_RESEND_TIME 1000   // 1 second, name query resend timeout
#define  MDNS_SQUERY_RESEND_TIME 1000  // 10 seconds, service query resend timeout
#define  MDNS_RESPONSE_TTL       120    //120 two minutes (in seconds)

#define  MDNS_MAX_SERVICES_PER_PACKET  6
#define  MSNS_ANNOUNCE_TIME_SEC  1 //Send announce packet every 5 seconds

static uint8_t mdnsMulticastIPAddr[] = { 224, 0, 0, 251 };
//static uint8_t mdnsHWAddr[] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb };



typedef enum _MDNSPacketType_t {
   MDNSPacketTypeMyIPAnswer,
   MDNSPacketTypeNoIPv6AddrAvailable,
   MDNSPacketTypeServiceRecord,
   MDNSPacketTypeServiceRecordRelease,
   MDNSPacketTypeNameQuery,
   MDNSPacketTypeServiceQuery,
} MDNSPacketType_t;

typedef struct _DNSHeader_t {
   uint16_t    xid;
   uint8_t     recursionDesired:1;
   uint8_t     truncated:1;
   uint8_t     authoritiveAnswer:1;
   uint8_t     opCode:4;
   uint8_t     queryResponse:1;
   uint8_t     responseCode:4;
   uint8_t     checkingDisabled:1;
   uint8_t     authenticatedData:1;
   uint8_t     zReserved:1;
   uint8_t     recursionAvailable:1;
   uint16_t    queryCount;
   uint16_t    answerCount;
   uint16_t    authorityCount;
   uint16_t    additionalCount;
} __attribute__((__packed__)) DNSHeader_t;

typedef enum _DNSOpCode_t {
   DNSOpQuery     = 0,
   DNSOpIQuery    = 1,
   DNSOpStatus    = 2,
   DNSOpNotify    = 4,
   DNSOpUpdate    = 5
} DNSOpCode_t;


HKBonjour::HKBonjour()
{
   memset(&this->_mdnsData, 0, sizeof(MDNSDataInternal_t));
   memset(&this->_serviceRecords, 0, sizeof(this->_serviceRecords));

   this->_state = MDNSStateIdle;
//   this->_sock = -1;

   this->_bonjourName = NULL;
   this->_resolveNames[0] = NULL;
   this->_resolveNames[1] = NULL;

   this->_lastAnnounceMillis = 0;
}

HKBonjour::~HKBonjour()
{
	this->stop();
}


int HKBonjour::setUDP( UDP * localUDP )
{
      this->_localUDP = localUDP;
      return 0;
}
int HKBonjour::stop()
{
    UDP *udp = this->_localUDP;

	udp->stop();

      return 0;
}
int HKBonjour::beginMulticast(uint8_t *IPAddr, uint16_t port)
{
    UDP *udp = this->_localUDP;

	udp->begin(5353);    // was... 4097
	udp->beginPacket(IPAddr, 5353);

      return 0;
}
int HKBonjour::write(uint8_t *buf, int size)
{
    UDP *udp = this->_localUDP;

	udp->write(buf, size);

      return 0;
}
int HKBonjour::read(uint8_t *buf, int size)
{
    UDP *udp = this->_localUDP;

      udp->setBuffer(size, buf); // application provided buffer

      return udp->read(buf, size);

}
int HKBonjour::beginPacket(uint8_t * IPAddr, uint16_t port)
{
    UDP *udp = this->_localUDP;
    udp->beginPacket(IPAddr, 5353);
    return 0;
}
int HKBonjour::endPacket()
{
    UDP *udp = this->_localUDP;

	udp->endPacket();

      return 0;
}
int HKBonjour::parsePacket()
{
    UDP *udp = this->_localUDP;

    return udp->parsePacket();
}
int HKBonjour::flush()
{
    UDP *udp = this->_localUDP;

    udp->flush();

      return 0;
}
int HKBonjour::remotePort()
{
    UDP *udp = this->_localUDP;

    return udp->remotePort();
}
int HKBonjour::remoteIP()
{
    UDP *udp = this->_localUDP;

    return udp->remoteIP();
}
/*unsigned long HKBonjour::localIP()
{
  IPAddress localIP = WiFi.localIP();

  return (unsigned long)localIP;
}*/

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::begin(const char* bonjourName)
{
	// if we were called very soon after the board was booted, we need to give the
	// EthernetShield (WIZnet) some time to come up. Hence, we delay until millis() is at
	// least 5000. This is necessary, so that if we need to add a service record directly
	// after begin, the announce packet does not get lost in the bowels of the WIZnet chip.
	while (millis() < 5000) delay(100);

	int statusCode = 0;
	statusCode = this->setBonjourName(bonjourName);
	if (statusCode)
	statusCode = this->beginMulticast(mdnsMulticastIPAddr, MDNS_SERVER_PORT);

	return statusCode;
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::begin()
{
   return this->begin(MDNS_DEFAULT_NAME);
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::_initQuery(uint8_t idx, const char* name, unsigned long timeout)
{
   int statusCode = 0;

   if (NULL == this->_resolveNames[idx] && NULL != ((0==idx) ? (void*)this->_nameFoundCallback :
                                                               (void*)this->_serviceFoundCallback)) {
      this->_resolveNames[idx] = (uint8_t*)name;

      if (timeout)
         this->_resolveTimeouts[idx] = millis() + timeout;
      else
         this->_resolveTimeouts[idx] = 0;

      statusCode = (MDNSSuccess == this->_sendMDNSMessage(0,
                                             0,
                                             (idx == 0) ? MDNSPacketTypeNameQuery :
                                                          MDNSPacketTypeServiceQuery,
                                             0));
   } else
      free((void*)name);

   return statusCode;
}

void HKBonjour::_cancelQuery(uint8_t idx)
{
   if (NULL != this->_resolveNames[idx]) {
      free(this->_resolveNames[idx]);
      this->_resolveNames[idx] = NULL;
   }
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::resolveName(const char* name, unsigned long timeout)
{
   this->cancelResolveName();

   char* n = (char*)malloc(strlen(name) + 7 + 1);
   if (NULL == n)
      return 0;
   memset(n, 0, strlen(name) + 7 + 1);
   strcpy(n, name);
   strcat(n, MDNS_TLD);

   return this->_initQuery(0, n, timeout);
}

void HKBonjour::setNameResolvedCallback(BonjourNameFoundCallback newCallback)
{
   this->_nameFoundCallback = newCallback;
}

void HKBonjour::cancelResolveName()
{
   this->_cancelQuery(0);
}

int HKBonjour::isResolvingName()
{
   return (NULL != this->_resolveNames[0]);
}

void HKBonjour::setServiceFoundCallback(BonjourServiceFoundCallback newCallback)
{
   this->_serviceFoundCallback = newCallback;
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::startDiscoveringService(const char* serviceName,
                                                  MDNSServiceProtocol_t proto,
                                                  unsigned long timeout)
{
   this->stopDiscoveringService();

   char* n = (char*)malloc(strlen(serviceName) + 13 + 1);
   if (NULL == n)
      return 0;
   memset(n, 0, strlen(serviceName) + 13 + 1);
   strcpy(n, serviceName);

   const uint8_t* srv_type = this->_postfixForProtocol(proto);
   if (srv_type)
      strcat(n, (const char*)srv_type);

   this->_resolveServiceProto = proto;

   return this->_initQuery(1, n, timeout);
}

void HKBonjour::stopDiscoveringService()
{
   this->_cancelQuery(1);
}

int HKBonjour::isDiscoveringService()
{
   return (NULL != this->_resolveNames[1]);
}

// return value:
// A DNSError_t (DNSSuccess on success, something else otherwise)
// in "int" mode: positive on success, negative on error
MDNSError_t HKBonjour::_sendMDNSMessage(uint32_t peerAddress, uint32_t xid, int type,
                                                   int serviceRecord)
{
   MDNSError_t statusCode = MDNSSuccess;
   uint16_t ptr = 0;
   DNSHeader_t* dnsHeader = NULL;
   uint8_t* buf;



   dnsHeader = (DNSHeader_t*)malloc(sizeof(DNSHeader_t));
   if (NULL == dnsHeader) {
      statusCode = MDNSOutOfMemory;
      goto errorReturn;
   }

   memset(dnsHeader, 0, sizeof(DNSHeader_t));

   dnsHeader->xid = ethutil_htons(xid);
   dnsHeader->opCode = DNSOpQuery;

   switch (type) {
      case MDNSPacketTypeServiceRecordRelease:
      case MDNSPacketTypeMyIPAnswer:
         dnsHeader->answerCount = ethutil_htons(1);
         dnsHeader->queryResponse = 1;
         dnsHeader->authoritiveAnswer = 1;
         break;
      case MDNSPacketTypeServiceRecord:
         dnsHeader->answerCount = ethutil_htons(4);
         dnsHeader->additionalCount = ethutil_htons(1);
         dnsHeader->queryResponse = 1;
         dnsHeader->authoritiveAnswer = 1;
         break;
      case MDNSPacketTypeNameQuery:
      case MDNSPacketTypeServiceQuery:
         dnsHeader->queryCount = ethutil_htons(1);
         break;
      case MDNSPacketTypeNoIPv6AddrAvailable:
         dnsHeader->queryCount = ethutil_htons(1);
         dnsHeader->additionalCount = ethutil_htons(1);
         dnsHeader->responseCode = 0x03;
         dnsHeader->authoritiveAnswer = 1;
         dnsHeader->queryResponse = 1;
         break;
   }




   this->beginPacket(mdnsMulticastIPAddr,MDNS_SERVER_PORT);
   this->write((uint8_t*)dnsHeader,sizeof(DNSHeader_t));

   ptr += sizeof(DNSHeader_t);
   buf = (uint8_t*)dnsHeader;

   // construct the answer section
   switch (type) {
      case MDNSPacketTypeMyIPAnswer: {
         this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));
         break;
      }

#if defined(HAS_SERVICE_REGISTRATION) && HAS_SERVICE_REGISTRATION

      case MDNSPacketTypeServiceRecord: {

         // SRV location record
         this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);

         buf[0] = 0x00;
         buf[1] = 0x21;    // SRV record
         buf[2] = 0x80;    // cache flush
         buf[3] = 0x01;    // class IN

         // ttl
         *((uint32_t*)&buf[4]) = ethutil_htonl(MDNS_RESPONSE_TTL);

         // data length
         *((uint16_t*)&buf[8]) = ethutil_htons(8 + strlen((char*)this->_bonjourName));

         this->write((uint8_t*)buf,10);
         ptr += 10;
         // priority and weight
         buf[0] = buf[1] = buf[2] = buf[3] = 0;

         // port
         *((uint16_t*)&buf[4]) = ethutil_htons(this->_serviceRecords[serviceRecord]->port);

         this->write((uint8_t*)buf,6);
         ptr += 6;
         // target
         this->_writeDNSName(this->_bonjourName, &ptr, buf, sizeof(DNSHeader_t), 1);

         // TXT record
         this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);

         buf[0] = 0x00;
         buf[1] = 0x10;    // TXT record
         buf[2] = 0x80;    // cache flush
         buf[3] = 0x01;    // class IN

         // ttl
         *((uint32_t*)&buf[4]) = ethutil_htonl(MDNS_RESPONSE_TTL);

         this->write((uint8_t*)buf,8);
         ptr += 8;

         // data length && text
         if (NULL == this->_serviceRecords[serviceRecord]->textContent) {
            buf[0] = 0x00;
            buf[1] = 0x01;
            buf[2] = 0x00;

            this->write((uint8_t*)buf,3);
            ptr += 3;
         } else {
            int slen = strlen((char*)this->_serviceRecords[serviceRecord]->textContent);
            *((uint16_t*)buf) = ethutil_htons(slen);
            this->write((uint8_t*)buf,2);
            ptr += 2;

            this->write((uint8_t*)this->_serviceRecords[serviceRecord]->textContent,slen);
            ptr += slen;
         }

         // PTR record (for the dns-sd service in general)
         this->_writeDNSName((const uint8_t*)DNS_SD_SERVICE, &ptr, buf,
                                          sizeof(DNSHeader_t), 1);

         buf[0] = 0x00;
         buf[1] = 0x0c;    // PTR record
         buf[2] = 0x00;    // no cache flush
         buf[3] = 0x01;    // class IN

         // ttl
         *((uint32_t*)&buf[4]) = ethutil_htonl(MDNS_RESPONSE_TTL);

         // data length.
         uint16_t dlen = strlen((char*)this->_serviceRecords[serviceRecord]->servName) + 2;
         *((uint16_t*)&buf[8]) = ethutil_htons(dlen);

         this->write((uint8_t*)buf, 10);
         ptr += 10;

         this->_writeServiceRecordName(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 1);

         // PTR record (our service)
         this->_writeServiceRecordPTR(serviceRecord, &ptr, buf, sizeof(DNSHeader_t),
                                      MDNS_RESPONSE_TTL);

         // finally, our IP address as additional record
         this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));

         break;
      }

      case MDNSPacketTypeServiceRecordRelease: {
         // just send our service PTR with a TTL of zero
         this->_writeServiceRecordPTR(serviceRecord, &ptr, buf, sizeof(DNSHeader_t), 0);
         break;
      }

#endif // defined(HAS_SERVICE_REGISTRATION) && HAS_SERVICE_REGISTRATION

#if defined(HAS_NAME_BROWSING) && HAS_NAME_BROWSING

      case MDNSPacketTypeNameQuery:
      case MDNSPacketTypeServiceQuery:
      {
         // construct a query for the currently set _resolveNames[0]
         this->_writeDNSName(
               (type == MDNSPacketTypeServiceQuery) ? this->_resolveNames[1] :
                                                      this->_resolveNames[0],
               &ptr, buf, sizeof(DNSHeader_t), 1);

         buf[0] = buf[2] = 0x0;
         buf[1] = (type == MDNSPacketTypeServiceQuery) ? 0x0c : 0x01;
         buf[3] = 0x1;

         this->write((uint8_t*)buf, sizeof(DNSHeader_t));
         ptr += sizeof(DNSHeader_t);

         this->_resolveLastSendMillis[(type == MDNSPacketTypeServiceQuery) ? 1 : 0] = millis();

         break;
      }

#endif // defined(HAS_NAME_BROWSING) && HAS_NAME_BROWSING

      case MDNSPacketTypeNoIPv6AddrAvailable: {
         // since the WIZnet doesn't have IPv6, we will respond with a Not Found message
         this->_writeDNSName(this->_bonjourName, &ptr, buf, sizeof(DNSHeader_t), 1);

         buf[0] = buf[2] = 0x0;
         buf[1] = 0x1c; // AAAA record
         buf[3] = 0x01;

         this->write((uint8_t*)buf, 4);
         ptr += 4;

         // send our IPv4 address record as additional record, in case the peer wants it.
         this->_writeMyIPAnswerRecord(&ptr, buf, sizeof(DNSHeader_t));

         break;
      }
   }


   this->endPacket();

errorReturn:

   if (NULL != dnsHeader)
      free(dnsHeader);

   Serial.printf("Bonjour send message status: %d\n", statusCode);
   return statusCode;
}

// return value:
// A DNSError_t (DNSSuccess on success, something else otherwise)
// in "int" mode: positive on success, negative on error
MDNSError_t HKBonjour::_processMDNSQuery()
{
   MDNSError_t statusCode = MDNSSuccess;
   DNSHeader_t* dnsHeader = NULL;
   int i, j;
   uint8_t* buf;
   uint32_t xid;
   uint16_t udp_len, qCnt, aCnt, aaCnt, addCnt;
   uint8_t recordsAskedFor[NumMDNSServiceRecords+2];
   uint8_t recordsFound[2];
   uint8_t wantsIPv6Addr = 0;
   uint8_t * udpBuffer = NULL;
   uintptr_t ptr;

   memset(recordsAskedFor, 0, sizeof(uint8_t)*(NumMDNSServiceRecords+2));
   memset(recordsFound, 0, sizeof(uint8_t)*2);


   udp_len = this->parsePacket();
   if (0 == udp_len) {
      statusCode = MDNSTryLater;
      goto errorReturn;
   }
    Serial.println("Processing MNDS Query packet");
   udpBuffer = (uint8_t*) malloc(udp_len);  //allocate memory to hold _remaining UDP packet
   if (NULL == udpBuffer) {
      this->flush();
      statusCode = MDNSOutOfMemory;
      goto errorReturn;
   }
   this->read((uint8_t*)udpBuffer, udp_len);//read _remaining UDP packet from W5100/W5200 into memory
   ptr = (uintptr_t)udpBuffer;

   dnsHeader = (DNSHeader_t*)malloc(sizeof(DNSHeader_t));
   if (NULL == dnsHeader) {
      statusCode = MDNSOutOfMemory;
      goto errorReturn;
   }

   buf = (uint8_t*)dnsHeader;
   memcpy((uint8_t*)buf, (uint16_t*)ptr ,sizeof(DNSHeader_t));

   xid = ethutil_ntohs(dnsHeader->xid);
   qCnt = ethutil_ntohs(dnsHeader->queryCount);
   aCnt = ethutil_ntohs(dnsHeader->answerCount);
   aaCnt = ethutil_ntohs(dnsHeader->authorityCount);
   addCnt = ethutil_ntohs(dnsHeader->additionalCount);

   if (0 == dnsHeader->queryResponse &&
       DNSOpQuery == dnsHeader->opCode &&
       MDNS_SERVER_PORT == this->remotePort())
	  {
      // process an MDNS query
      int offset = sizeof(DNSHeader_t);
      uint8_t* buf = (uint8_t*)dnsHeader;
      int rLen = 0, tLen = 0;

      // read over the query section
      for (i=0; i<qCnt; i++) {
         // construct service name data structures for comparison
         const uint8_t* servNames[NumMDNSServiceRecords+2];
         int servLens[NumMDNSServiceRecords+2];
         uint8_t servNamePos[NumMDNSServiceRecords+2];
         uint8_t servMatches[NumMDNSServiceRecords+2];

         // first entry is our own MDNS name, the rest are our services
         servNames[0] = (const uint8_t*)this->_bonjourName;
         servNamePos[0] = 0;
         servLens[0] = strlen((char*)this->_bonjourName);
         servMatches[0] = 1;

         // second entry is our own the general DNS-SD service
         servNames[1] = (const uint8_t*)DNS_SD_SERVICE;
         servNamePos[1] = 0;
         servLens[1] = strlen((char*)DNS_SD_SERVICE);
         servMatches[1] = 1;

         for (j=2; j<NumMDNSServiceRecords+2; j++)
            if (NULL != this->_serviceRecords[j-2] && NULL != this->_serviceRecords[j-2]->servName) {
               servNames[j] = this->_serviceRecords[j-2]->servName;
               servLens[j] = strlen((char*)servNames[j]);
               servMatches[j] = 1;
               servNamePos[j] = 0;
            } else {
               servNames[j] = NULL;
               servLens[j] = 0;
               servMatches[j] = 0;
               servNamePos[j] = 0;
            }

         tLen = 0;
         do {

        	memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,1);
            offset += 1;

            rLen = buf[0];
            tLen += 1;

            if (rLen > 128) {// handle DNS name compression, kinda, sorta


            	memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,1);
            	offset += 1;

               for (j=0; j<NumMDNSServiceRecords+2; j++) {
                  if (servNamePos[j] && servNamePos[j] != buf[0]) {
                     servMatches[j] = 0;
                  }
               }

               tLen += 1;
            } else if (rLen > 0) {
               int tr = rLen, ir;

               while (tr > 0) {
                  ir = (tr > sizeof(DNSHeader_t)) ? sizeof(DNSHeader_t) : tr;

                  memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,ir);
                  offset += ir;
                  tr -= ir;

                  for (j=0; j<NumMDNSServiceRecords+2; j++) {
                     if (!recordsAskedFor[j] && servMatches[j])
                        servMatches[j] &= this->_matchStringPart(&servNames[j], &servLens[j], buf,
                                                                 ir);
                  }
               }

               tLen += rLen;
            }
         } while (rLen > 0 && rLen <= 128);

         // if this matched a name of ours (and there are no characters left), then
         // check whether this is an A record query (for our own name) or a PTR record query
         // (for one of our services).
         // if so, we'll note to send a record

         memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,4);
         offset += 4;

         for (j=0; j<NumMDNSServiceRecords+2; j++) {
            if (!recordsAskedFor[j] && servNames[j] && servMatches[j] && 0 == servLens[j]) {
               if (0 == servNamePos[j])
                  servNamePos[j] = offset - 4 - tLen;

               if (buf[0] == 0 && buf[3] == 0x01 &&
                  (buf[2] == 0x00 || buf[2] == 0x80)) {

                  if ((0 == j && 0x01 == buf[1]) || (0 < j && (0x0c == buf[1] || 0x10 == buf[1] || 0x21 == buf[1])))
                     recordsAskedFor[j] = 1;
                  else if (0 == j && 0x1c == buf[1])
                     wantsIPv6Addr = 1;
               }
            }
         }
      }
   }

#if (defined(HAS_SERVICE_REGISTRATION) && HAS_SERVICE_REGISTRATION) || (defined(HAS_NAME_BROWSING) && HAS_NAME_BROWSING)

   else if (1 == dnsHeader->queryResponse &&
              DNSOpQuery == dnsHeader->opCode &&
              MDNS_SERVER_PORT == remotePort() &&
              (NULL != this->_resolveNames[0] || NULL != this->_resolveNames[1]))
	     {
         int offset = sizeof(DNSHeader_t);
         uint8_t* buf = (uint8_t*)dnsHeader;
         int rLen = 0, tLen = 0;

         uint8_t* ptrNames[MDNS_MAX_SERVICES_PER_PACKET];
         uint16_t ptrOffsets[MDNS_MAX_SERVICES_PER_PACKET];
         uint16_t ptrPorts[MDNS_MAX_SERVICES_PER_PACKET];
         uint8_t ptrIPs[MDNS_MAX_SERVICES_PER_PACKET];
         uint8_t servIPs[MDNS_MAX_SERVICES_PER_PACKET][5];
         uint8_t* servTxt[MDNS_MAX_SERVICES_PER_PACKET];
         memset(servIPs, 0, sizeof(uint8_t)*MDNS_MAX_SERVICES_PER_PACKET*5);
         memset(servTxt, 0, sizeof(uint8_t*)*MDNS_MAX_SERVICES_PER_PACKET);

         const uint8_t* ptrNamesCmp[MDNS_MAX_SERVICES_PER_PACKET];
         int ptrLensCmp[MDNS_MAX_SERVICES_PER_PACKET];
         uint8_t ptrNamesMatches[MDNS_MAX_SERVICES_PER_PACKET];

         uint8_t checkAARecords = 0;
         memset(ptrNames, 0, sizeof(uint8_t*)*MDNS_MAX_SERVICES_PER_PACKET);

         const uint8_t* servNames[2];
         uint8_t servNamePos[2];
         int servLens[2];
         uint8_t servMatches[2];
         uint8_t firstNamePtrByte = 0;
         uint8_t partMatched[2];
         uint8_t lastWasCompressed[2];
         uint8_t servWasCompressed[2];

         servNamePos[0] = servNamePos[1] = 0;

         for (i=0; i<qCnt+aCnt+aaCnt+addCnt; i++) {

            for (j=0; j<2; j++) {
               if (NULL != this->_resolveNames[j]) {
                  servNames[j] = this->_resolveNames[j];
                  servLens[j] = strlen((const char*)this->_resolveNames[j]);
                  servMatches[j] = 1;
               } else {
                  servNames[j] = NULL;
                  servLens[j] = servMatches[j] = 0;
               }
            }

            for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
               if (NULL != ptrNames[j]) {
                  ptrNamesCmp[j] = ptrNames[j];
                  ptrLensCmp[j] = strlen((const char*)ptrNames[j]);
                  ptrNamesMatches[j] = 1;
               }
            }

            partMatched[0] = partMatched[1] = 0;
            lastWasCompressed[0] = lastWasCompressed[1] = 0;
            servWasCompressed[0] = servWasCompressed[1] = 0;
            firstNamePtrByte = 0;
            tLen = 0;

            do {
            	memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,1);
               offset += 1;
               rLen = buf[0];
               tLen += 1;

               if (rLen > 128) { // handle DNS name compression, kinda, sorta...

            	   memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,1);
                  offset += 1;

                  for (j=0; j<2; j++) {
                     if (servNamePos[j] && servNamePos[j] != buf[0])
                        servMatches[j] = 0;
                     else
                        servWasCompressed[j] = 1;

                     lastWasCompressed[j] = 1;
                  }

                  tLen += 1;

                  if (0 == firstNamePtrByte)
                     firstNamePtrByte = buf[0];
               } else if (rLen > 0) {
                  if (i < qCnt)
                     offset += rLen;
                  else {
                     int tr = rLen, ir;

                     if (0 == firstNamePtrByte)
                        firstNamePtrByte = offset-1; // -1, since we already read length (1 byte)

                     while (tr > 0) {
                        ir = (tr > sizeof(DNSHeader_t)) ? sizeof(DNSHeader_t) : tr;
                        memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,ir);
                        offset += ir;
                        tr -= ir;

                        for (j=0; j<2; j++) {
                           if (!recordsFound[j] && servMatches[j] && servNames[j])
                              servMatches[j] &= this->_matchStringPart(&servNames[j], &servLens[j],
                                                                       buf, ir);
                              if (!partMatched[j])
                                 partMatched[j] = servMatches[j];

                              lastWasCompressed[j] = 0;
                        }

                        for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
                           if (NULL != ptrNames[j] && ptrNamesMatches[j]) {
                              // only compare the part we have. this is incorrect, but good enough,
                              // since actual MDNS implementations won't go here anyways, as they
                              // should use name compression. This is just so that multiple Arduinos
                              // running this MDNSResponder code should be able to find each other's
                              // services.
                              if (ptrLensCmp[j] >= ir)
                                 ptrNamesMatches[j] &= this->_matchStringPart(&ptrNamesCmp[j],
                                                            &ptrLensCmp[j], buf, ir);
                           }
                        }
                     }

                     tLen += rLen;
                  }
               }
            } while (rLen > 0 && rLen <= 128);

            // if this matched a name of ours (and there are no characters left), then
            // check wether this is an A record query (for our own name) or a PTR record query
            // (for one of our services).
            // if so, we'll note to send a record
            if (i < qCnt)
               offset += 4;
            else if (i >= qCnt) {
               if (i >= qCnt + aCnt && !checkAARecords)
                  break;

               uint8_t packetHandled = 0;

               memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,4);
               offset += 4;
               if (i < qCnt+aCnt) {
                  for (j=0; j<2; j++) {
                     if (0 == servNamePos[j])
                        servNamePos[j] = offset - 4 - tLen;

                     if (servNames[j] &&
                         ((servMatches[j] && 0 == servLens[j]) ||
                         (partMatched[j] && lastWasCompressed[j]) ||
                         (servWasCompressed[j] && servMatches[j]))) { // somewhat handle compression by guessing

                        if (buf[0] == 0 && buf[1] == ((0 == j) ? 0x01 : 0x0c) &&
                           (buf[2] == 0x00 || buf[2] == 0x80) && buf[3] == 0x01) {
                           recordsFound[j] = 1;

                           // this is an A or PTR type response. Parse it as such.

                           memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,6);
                           offset += 6;
                           //uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
                           uint16_t dataLen = ethutil_ntohs(*(uint16_t*)&buf[4]);

                           if (0 == j && 4 == dataLen) {
                              // ok, this is the IP address. report it via callback.

                        	   memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,4);

                              this->_finishedResolvingName((char*)this->_resolveNames[0],
                                                           (const byte*)buf);
                           } else if (1 == j) {
                              uint8_t k;
                              for (k=0; k<MDNS_MAX_SERVICES_PER_PACKET; k++)
                                 if (NULL == ptrNames[k])
                                    break;

                              if (k < MDNS_MAX_SERVICES_PER_PACKET) {
                                 int l = dataLen - 2; // -2: data compression of service postfix

                                 uint8_t* ptrName = (uint8_t*)malloc(l);

                                 if (ptrName) {

                                	 memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,1);
                                	 memcpy((uint8_t*)ptrName, (uint16_t*)(ptr+offset+1) ,l-1);

                                    if (buf[0] < l-1)
                                       ptrName[buf[0]]; // this catches uncompressed names
                                    else
                                       ptrName[l-1] = '\0';

                                    ptrNames[k] = ptrName;
                                    ptrOffsets[k] = (uint16_t)(offset);

                                    checkAARecords = 1;
                                 }
                              }
                           }
                           offset += dataLen;
                           packetHandled = 1;
                        }
                     }
                  }
               } else if (i >= qCnt+aCnt+aaCnt) {
                  //  check whether we find a service description
                  if (buf[1] == 0x21) {
                     for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
                        if (ptrNames[j] &&
                              ((firstNamePtrByte && firstNamePtrByte == ptrOffsets[j]) ||
                              (0 == ptrLensCmp[j] && ptrNamesMatches[j]))) {
                           // we have found the matching SRV location packet to a previous SRV domain

                        	memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,6);
                           offset += 6;

                           //uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
                           uint16_t dataLen = ethutil_ntohs(*(uint16_t*)&buf[4]);

                           if (dataLen >= 8) {

                        	   memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,8);
                              ptrPorts[j] = ethutil_ntohs(*(uint16_t*)&buf[4]);

                              if (buf[6] > 128) { // target is a compressed name
                                 ptrIPs[j] = buf[7];
                              } else { // target is uncompressed
                                 ptrIPs[j] = offset+6;
                              }
                           }
                           offset += dataLen;
                           packetHandled = 1;

                           break;
                        }
                     }
                 } else if (buf[1] == 0x10) { // txt record
                     for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
                        if (ptrNames[j] &&
                              ((firstNamePtrByte && firstNamePtrByte == ptrOffsets[j]) ||
                              (0 == ptrLensCmp[j] && ptrNamesMatches[j]))) {


                        	memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,6);
                           offset += 6;

                           //uint32_t ttl = ethutil_ntohl(*(uint32_t*)buf);
                           uint16_t dataLen = ethutil_ntohs(*(uint16_t*)&buf[4]);

                           // if there's a content to this txt record, save it for delivery
                           if (dataLen > 1 && NULL == servTxt[j]) {
                              servTxt[j] = (uint8_t*)malloc(dataLen+1);
                              if (NULL != servTxt[j]) {

                            	  memcpy((uint8_t*)servTxt[j], (uint16_t*)(ptr+offset) ,dataLen);

                                 // zero-terminate
                                 servTxt[j][dataLen] = '\0';
                              }
                           }
                           offset += dataLen;
                           packetHandled = 1;

                           break;
                        }
                     }
                  } else if (buf[1] == 0x01) { // A record (IPv4 address)
                     for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
                        if (0 == servIPs[j][0]) {
                           servIPs[j][0] = firstNamePtrByte ? firstNamePtrByte : 255;

                           memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset) ,6);
                           offset += 6;

                           uint16_t dataLen = ethutil_ntohs(*(uint16_t*)&buf[4]);
                           if (4 == dataLen) {
                        	  memcpy((uint8_t*)&servIPs[j][1], (uint16_t*)(ptr+offset) ,4);
                           }
                           offset += dataLen;
                           packetHandled = 1;

                           break;
                        }
                     }
                  }
               }

               // eat the answer
               if (!packetHandled) {
            	   offset += 4; // ttl
            	   memcpy((uint8_t*)buf, (uint16_t*)(ptr+offset), 2);
            	   offset += 2 + ethutil_ntohs(*(uint16_t*)buf); // skip over content
               }
            }
         }

         // deliver the services discovered in this packet
         if (NULL != this->_resolveNames[1]) {
            char* typeName = (char*)this->_resolveNames[1];
            char* p = (char*)this->_resolveNames[1];
            while(*p && *p != '.')
               p++;
            *p = '\0';

            for (i=0; i<MDNS_MAX_SERVICES_PER_PACKET; i++)
               if (ptrNames[i]) {
                  const uint8_t* ipAddr = NULL;
                  const uint8_t* fallbackIpAddr = NULL;

                  for (j=0; j<MDNS_MAX_SERVICES_PER_PACKET; j++) {
                     if (servIPs[j][0] == ptrIPs[i] || servIPs[j][0] == 255) {
                        // the || part is such a hack, but it will work as long as there's only
                        // one A record per MDNS packet. fucking DNS name compression.
                        ipAddr = &servIPs[j][1];

                        break;
                     } else if (NULL == fallbackIpAddr && 0 != servIPs[j][0])
                        fallbackIpAddr = &servIPs[j][1];
                  }

                  // if we can't find a matching IP, we try to use the first one we found.
                  if (NULL == ipAddr) ipAddr = fallbackIpAddr;

                  if (ipAddr && this->_serviceFoundCallback) {
                     this->_serviceFoundCallback(typeName,
                                                this->_resolveServiceProto,
                                                (const char*)ptrNames[i],
                                                (const byte*)ipAddr,
                                                (unsigned short)ptrPorts[i],
                                                (const char*)servTxt[i]);
                  }
               }
            *p = '.';
         }

         uint8_t k;
         for (k=0; k<MDNS_MAX_SERVICES_PER_PACKET; k++)
            if (NULL != ptrNames[k]) {
               free(ptrNames[k]);
               if (NULL != servTxt[k])
                  free(servTxt[k]);
            }
   }

#endif // (defined(HAS_SERVICE_REGISTRATION) && HAS_SERVICE_REGISTRATION) || (defined(HAS_NAME_BROWSING) && HAS_NAME_BROWSING)

   free(udpBuffer);

errorReturn:

   if (NULL != dnsHeader)
      free(dnsHeader);

   // now, handle the requests
   for (j=0; j<NumMDNSServiceRecords+2; j++) {
      if (recordsAskedFor[j]) {
         if (0 == j)
            (void)this->_sendMDNSMessage(this->remoteIP(), xid, (int)MDNSPacketTypeMyIPAnswer, 0);
         else if (1 == j) {
            uint8_t k = 2;
            for (k=0; k<NumMDNSServiceRecords; k++)
               recordsAskedFor[k+2] = 1;
         } else if (NULL != this->_serviceRecords[j-2])
            (void)this->_sendMDNSMessage(this->remoteIP(), xid, (int)MDNSPacketTypeServiceRecord, j-2);
      }
   }

   // if we were asked for our IPv6 address, say that we don't have any
   if (wantsIPv6Addr)
      (void)this->_sendMDNSMessage(this->remoteIP(), xid, (int)MDNSPacketTypeNoIPv6AddrAvailable, 0);

   return statusCode;
}

void HKBonjour::run()
{
   uint8_t i;
   unsigned long now = millis();

   // first, look for MDNS queries to handle
   (void)_processMDNSQuery();

   // are we querying a name or service? if so, should we resend the packet or time out?
   for (i=0; i<2; i++) {
      if (NULL != this->_resolveNames[i]) {
         // Hint: _resolveLastSendMillis is updated in _sendMDNSMessage
         if (now - this->_resolveLastSendMillis[i] > ((i == 0) ? (uint32_t)MDNS_NQUERY_RESEND_TIME :
                                                                 (uint32_t)MDNS_SQUERY_RESEND_TIME))
            (void)this->_sendMDNSMessage(0,
                                         0,
                                         (0 == i) ? MDNSPacketTypeNameQuery :
                                                    MDNSPacketTypeServiceQuery,
                                         0);

         if (this->_resolveTimeouts[i] > 0 && now > this->_resolveTimeouts[i]) {
            if (i == 0)
               this->_finishedResolvingName((char*)this->_resolveNames[0], NULL);
            else if (i == 1) {
               if (this->_serviceFoundCallback) {
                  char* typeName = (char*)this->_resolveNames[1];
                  char* p = (char*)this->_resolveNames[1];
                  while(*p && *p != '.')
                     p++;
                  *p = '\0';

                  this->_serviceFoundCallback(typeName,
                                              this->_resolveServiceProto,
                                              NULL,
                                              NULL,
                                              0,
                                              NULL);
               }
            }

            if (NULL != this->_resolveNames[i]) {
               free(this->_resolveNames[i]);
               this->_resolveNames[i] = NULL;
            }
         }
      }
   }

   // now, should we re-announce our services again?
   if ((now - this->_lastAnnounceMillis) > 1000*MSNS_ANNOUNCE_TIME_SEC) {
      for (i=0; i<NumMDNSServiceRecords; i++) {
         if (NULL != this->_serviceRecords[i])
            (void)this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecord, i);
      }

      this->_lastAnnounceMillis = now;
   }
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::setBonjourName(const char* bonjourName)
{
   if (NULL == bonjourName)
      return 0;

   if (this->_bonjourName != NULL)
      free(this->_bonjourName);

   this->_bonjourName = (uint8_t*)malloc(strlen(bonjourName) + 7 + 1);
   if (NULL == this->_bonjourName)
      return 0;
   memset(this->_bonjourName, 0, strlen(bonjourName) + 7 + 1);
   strcpy((char*)this->_bonjourName, bonjourName);
   strcpy((char*)this->_bonjourName+strlen(bonjourName), MDNS_TLD);

   return 1;
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::addServiceRecord(const char* name, uint16_t port,
                                           MDNSServiceProtocol_t proto)
{
#if defined(__MK20DX128__) || defined(__MK20DX256__)
	 return this->addServiceRecord(name, port, proto, NULL); //works for Teensy 3 (32-bit Arm Cortex)
#else
   return this->addServiceRecord(name, port, proto, ""); //works for Teensy 2 (8-bit Atmel)
#endif
}

// return values:
// 1 on success
// 0 otherwise
int HKBonjour::addServiceRecord(const char* name, uint16_t port,
                                           MDNSServiceProtocol_t proto, const char* textContent)
{
   int i, status = 0;
   MDNSServiceRecord_t* record = NULL;

   if (NULL != name && 0 != port) {
      for (i=0; i < NumMDNSServiceRecords; i++) {
         if (NULL == this->_serviceRecords[i]) {
            record = (MDNSServiceRecord_t*)malloc(sizeof(MDNSServiceRecord_t));
            if (NULL != record) {
               record->name = record->textContent = NULL;

               record->name = (uint8_t*)malloc(strlen((char*)name) + 1);
               memset(record->name, 0, strlen((char*)name) + 1);
               if (NULL == record->name)
                  goto errorReturn;

               if (NULL != textContent) {
                  record->textContent = (uint8_t*)malloc(strlen((char*)textContent) + 1);
                  memset(record->textContent, 0, strlen((char*)textContent) + 1);
                  if (NULL == record->textContent)
                     goto errorReturn;

                  strcpy((char*)record->textContent, textContent);
               }

               record->port = port;
               record->proto = proto;
               strcpy((char*)record->name, name);

               uint8_t* s = this->_findFirstDotFromRight(record->name);
               record->servName = (uint8_t*)malloc(strlen((char*)s) + 12 + 1);
               memset(record->servName, 0, strlen((char*)s) + 12 + 1);
               if (record->servName) {
                  strcpy((char*)record->servName, (const char*)s);

                  const uint8_t* srv_type = this->_postfixForProtocol(proto);
                  if (srv_type)
                     strcat((char*)record->servName, (const char*)srv_type);
               }

               this->_serviceRecords[i] = record;

               status = (MDNSSuccess ==
                           this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecord, i));

               break;
            }
         }
      }
   }

   return status;

errorReturn:
   if (NULL != record) {
      if (NULL != record->name)
         free(record->name);
      if (NULL != record->servName)
         free(record->servName);
      if (NULL != record->textContent)
         free(record->textContent);

      free(record);
   }

   return 0;
}

void HKBonjour::_removeServiceRecord(int idx)
{
   if (NULL != this->_serviceRecords[idx]) {
      (void)this->_sendMDNSMessage(0, 0, (int)MDNSPacketTypeServiceRecordRelease, idx);

      if (NULL != this->_serviceRecords[idx]->textContent)
         free(this->_serviceRecords[idx]->textContent);

      if (NULL != this->_serviceRecords[idx]->servName)
         free(this->_serviceRecords[idx]->servName);

      free(this->_serviceRecords[idx]->name);
      free(this->_serviceRecords[idx]);

      this->_serviceRecords[idx] = NULL;
   }
}

void HKBonjour::removeServiceRecord(uint16_t port, MDNSServiceProtocol_t proto)
{
   this->removeServiceRecord(NULL, port, proto);
}

void HKBonjour::removeServiceRecord(const char* name, uint16_t port,
                                               MDNSServiceProtocol_t proto)
{
   int i;
   for (i=0; i<NumMDNSServiceRecords; i++)
      if (port == this->_serviceRecords[i]->port &&
          proto == this->_serviceRecords[i]->proto &&
          (NULL == name || 0 == strcmp((char*)this->_serviceRecords[i]->name, name))) {
             this->_removeServiceRecord(i);
             break;
          }
}

void HKBonjour::removeAllServiceRecords()
{
   int i;
   for (i=0; i<NumMDNSServiceRecords; i++)
      this->_removeServiceRecord(i);
}

void HKBonjour::_writeDNSName(const uint8_t* name, uint16_t* pPtr,
                                         uint8_t* buf, int bufSize, int zeroTerminate)
{
   uint16_t ptr = *pPtr;
   uint8_t* p1 = (uint8_t*)name, *p2, *p3;
   int i, c, len;

   while(*p1) {
      c = 1;
      p2 = p1;
      while (0 != *p2 && '.' != *p2) { p2++; c++; };

      p3 = buf;
      i = c;
      len = bufSize-1;
      *p3++ = (uint8_t)--i;
      while (i-- > 0) {
         *p3++ = *p1++;

         if (--len <= 0) {
            this->write((uint8_t*)buf, bufSize);
            ptr += bufSize;
            len = bufSize;
            p3 = buf;
         }
      }

      while ('.' == *p1)
         ++p1;

      if (len != bufSize) {
    	  this->write((uint8_t*)buf, bufSize-len);
         ptr += bufSize-len;
      }
   }

   if (zeroTerminate) {
      buf[0] = 0;
      this->write((uint8_t*)buf, 1);
      ptr += 1;
   }

   *pPtr = ptr;
}

void HKBonjour::_writeMyIPAnswerRecord(uint16_t* pPtr, uint8_t* buf, int bufSize)
{
   uint16_t ptr = *pPtr;

   this->_writeDNSName(this->_bonjourName, &ptr, buf, bufSize, 1);

   buf[0] = 0x00;
   buf[1] = 0x01;
   buf[2] = 0x80; // cache flush: true
   buf[3] = 0x01;
   this->write((uint8_t*)buf, 4);
   ptr += 4;

   *((uint32_t*)buf) = ethutil_htonl(MDNS_RESPONSE_TTL);
   *((uint16_t*)&buf[4]) = ethutil_htons(4);      // data length

   uint8_t myIp[4];
   IPAddress myIpBuf;
#if !defined(PARTICLE)
   myIpBuf = Ethernet.localIP();
#else
   myIpBuf = WiFi.localIP();
#endif
   myIp[0] = myIpBuf [0];
   myIp[1] = myIpBuf [1];
   myIp[2] = myIpBuf [2];
   myIp[3] = myIpBuf [3];

   memcpy(&buf[6], &myIp, 4);              // our IP address

   this->write((uint8_t*)buf, 10);
   ptr += 10;

   *pPtr = ptr;
}

void HKBonjour::_writeServiceRecordName(int recordIndex, uint16_t* pPtr, uint8_t* buf,
                                                   int bufSize, int tld)
{
   uint16_t ptr = *pPtr;

   uint8_t* name = tld ? this->_serviceRecords[recordIndex]->servName :
                         this->_serviceRecords[recordIndex]->name;

   this->_writeDNSName(name, &ptr, buf, bufSize, tld);

   if (0 == tld) {
      const uint8_t* srv_type =
         this->_postfixForProtocol(this->_serviceRecords[recordIndex]->proto);

      if (NULL != srv_type) {
         srv_type++; // eat the dot at the beginning
         this->_writeDNSName(srv_type, &ptr, buf, bufSize, 1);
      }
   }

   *pPtr = ptr;
}

void HKBonjour::_writeServiceRecordPTR(int recordIndex, uint16_t* pPtr, uint8_t* buf,
                                                  int bufSize, uint32_t ttl)
{
   uint16_t ptr = *pPtr;

   this->_writeServiceRecordName(recordIndex, &ptr, buf, bufSize, 1);

   buf[0] = 0x00;
   buf[1] = 0x0c;    // PTR record
   buf[2] = 0x00;    // no cache flush
   buf[3] = 0x01;    // class IN

   // ttl
   *((uint32_t*)&buf[4]) = ethutil_htonl(ttl);

   // data length (+13 = "._tcp.local" or "._udp.local" + 1  byte zero termination)
   *((uint16_t*)&buf[8]) =
         ethutil_htons(strlen((char*)this->_serviceRecords[recordIndex]->name) + 13);

   this->write((uint8_t*)buf, 10);
   ptr += 10;

   this->_writeServiceRecordName(recordIndex, &ptr, buf, bufSize, 0);

   *pPtr = ptr;
}

uint8_t* HKBonjour::_findFirstDotFromRight(const uint8_t* str)
{
   const uint8_t* p = str + strlen((char*)str);
   while (p > str && '.' != *p--);
   return (uint8_t*)&p[2];
}

int HKBonjour::_matchStringPart(const uint8_t** pCmpStr, int* pCmpLen, const uint8_t* buf,
                                           int dataLen)
{
   int matches = 1;

   if (*pCmpLen >= dataLen)
      matches &= (0 == memcmp(*pCmpStr, buf, dataLen));
   else
      matches = 0;

   *pCmpStr += dataLen;
   *pCmpLen -= dataLen;
   if ('.' == **pCmpStr)
      (*pCmpStr)++, (*pCmpLen)--;

   return matches;
}

const uint8_t* HKBonjour::_postfixForProtocol(MDNSServiceProtocol_t proto)
{
   const uint8_t* srv_type = NULL;
   switch(proto) {
      case MDNSServiceTCP:
         srv_type = (uint8_t*)"._tcp" MDNS_TLD;
         break;
      case MDNSServiceUDP:
         srv_type = (uint8_t*)"._udp" MDNS_TLD;
         break;
   }

   return srv_type;
}

void HKBonjour::_finishedResolvingName(char* name, const byte ipAddr[4])
{
   if (NULL != this->_nameFoundCallback) {
      if (NULL != name) {
         uint8_t* n = this->_findFirstDotFromRight((const uint8_t*)name);
         *(n-1) = '\0';
      }

      this->_nameFoundCallback((const char*)name, ipAddr);
   }

   free(this->_resolveNames[0]);
   this->_resolveNames[0] = NULL;
}


uint16_t ethutil_swaps(uint16_t i);
uint32_t ethutil_swapl(uint32_t l);

extern uint16_t ethutil_htons(unsigned short hostshort)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return ethutil_swaps(hostshort);
#else
	return hostshort;
#endif
}

extern uint32_t ethutil_htonl(unsigned long hostlong)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return ethutil_swapl(hostlong);
#else
	return hostlong;
#endif
}

extern uint16_t ethutil_ntohs(unsigned short netshort)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return ethutil_swaps(netshort);
#else
	return netshort;
#endif
}

extern uint32_t ethutil_ntohl(unsigned long netlong)
{
#if ( SYSTEM_ENDIAN == _ENDIAN_LITTLE_ )
	return ethutil_swapl(netlong);
#else
	return netlong;
#endif
}

uint16_t ethutil_swaps(uint16_t i)
{
	uint16_t ret=0;
	ret = (i & 0xFF) << 8;
	ret |= ((i >> 8)& 0xFF);
	return ret;
}

uint32_t ethutil_swapl(uint32_t l)
{
	uint32_t ret=0;
	ret = (l & 0xFF) << 24;
	ret |= ((l >> 8) & 0xFF) << 16;
	ret |= ((l >> 16) & 0xFF) << 8;
	ret |= ((l >> 24) & 0xFF);
	return ret;
}
