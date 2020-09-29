// -*- c-basic-offset: 8; tab-width: 8; indent-tabs-mode: nil; mode: c++ -*-
/* Copyright (C) 2011 Circuits At Home, LTD. All rights reserved.

This software may be distributed and modified under the terms of the GNU
General Public License version 2 (GPL2) as published by the Free Software
Foundation and appearing in the file GPL2.TXT included in the packaging of
this file. Please note that GPL2 Section 2[b] requires that all works based
on this software must also be made publicly available under the terms of
the GPL2 ("Copyleft").

Contact information
-------------------

Circuits At Home, LTD
Web      :  http://www.circuitsathome.com
e-mail   :  support@circuitsathome.com
 */

#include "hiduniversal.h"

HIDUniversal::HIDUniversal(USB *p) :
USBHID(p),
qNextPollTime(0),
pollInterval(0),
bPollEnable(false),
bHasReportId(false) {
        Initialize();

        if(pUsb)
                pUsb->RegisterDeviceClass(this);
}

uint16_t HIDUniversal::GetHidClassDescrLen(uint8_t type, uint8_t num) {
        for(uint8_t i = 0, n = 0; i < HID_MAX_HID_CLASS_DESCRIPTORS; i++) {
                if(descrInfo[i].bDescrType == type) {
                        if(n == num)
                                return descrInfo[i].wDescriptorLength;
                        n++;
                }
        }
        return 0;
}

void HIDUniversal::Initialize() {
        for(uint8_t i = 0; i < MAX_REPORT_PARSERS; i++) {
                rptParsers[i].rptId = 0;
                rptParsers[i].rptParser = NULL;
        }
        for(uint8_t i = 0; i < HID_MAX_HID_CLASS_DESCRIPTORS; i++) {
                descrInfo[i].bDescrType = 0;
                descrInfo[i].wDescriptorLength = 0;
        }
        for(uint8_t i = 0; i < maxHidInterfaces; i++) {
                hidInterfaces[i].bmInterface = 0;
                hidInterfaces[i].bmProtocol = 0;

                for(uint8_t j = 0; j < maxEpPerInterface; j++)
                        hidInterfaces[i].epIndex[j] = 0;
        }
        for(uint8_t i = 0; i < totalEndpoints; i++) {
                epInfo[i].epAddr = 0;
                epInfo[i].maxPktSize = (i) ? 0 : 8;
                epInfo[i].bmSndToggle = 0;
                epInfo[i].bmRcvToggle = 0;
                epInfo[i].bmNakPower = (i) ? USB_NAK_NOWAIT : USB_NAK_MAX_POWER;
        }
        bNumEP = 1;
        bNumIface = 0;
        bConfNum = 0;
        pollInterval = 0;

#if HID_UNIVERSAL_REMOVE_IDENTICAL_BUFFER
	memset(prevBuf, 0, constBuffLen);
#endif
}

bool HIDUniversal::SetReportParser(uint8_t id, HIDReportParser *prs) {
        for(uint8_t i = 0; i < MAX_REPORT_PARSERS; i++) {
                if(rptParsers[i].rptId == 0 && rptParsers[i].rptParser == NULL) {
                        rptParsers[i].rptId = id;
                        rptParsers[i].rptParser = prs;
                        return true;
                }
        }
        return false;
}

HIDReportParser* HIDUniversal::GetReportParser(uint8_t id) {
        // \todo is this wrong? If an id parameter is given, it will never be used if bHasReportId is false.
        //       and bHasReportId seems to be always false, as it is never set to anything.
        if(!bHasReportId) {
                // \todo is this what is intended?
                //       Or should it return rptParsers[0] is it is not NULL, otherwise use the for loop below?
                return ((rptParsers[0].rptParser) ? rptParsers[0].rptParser : NULL);
        }

        for(uint8_t i = 0; i < MAX_REPORT_PARSERS; i++) {
                if(rptParsers[i].rptId == id)
                        return rptParsers[i].rptParser;
        }
        return NULL;
}

uint8_t HIDUniversal::Init(uint8_t parent, uint8_t port, bool lowspeed) {
        const uint8_t constBufSize = sizeof (USB_DEVICE_DESCRIPTOR);

        uint8_t buf[constBufSize];
        USB_DEVICE_DESCRIPTOR * udd = reinterpret_cast<USB_DEVICE_DESCRIPTOR*>(buf);
        uint8_t rcode;
        UsbDevice *p = NULL;
        EpInfo *oldep_ptr = NULL;
        uint8_t len = 0;

        uint8_t num_of_conf; // number of configurations
        //uint8_t num_of_intf; // number of interfaces

        AddressPool &addrPool = pUsb->GetAddressPool();

        USBTRACE2("HU Init",(int)this);

        if(bAddress)
                return USB_ERROR_CLASS_INSTANCE_ALREADY_IN_USE;

        // Get pointer to pseudo device with address 0 assigned
        p = addrPool.GetUsbDevicePtr(0);

        if(!p)
                return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;

        if(!p->epinfo) {
                USBTRACE("epinfo\n");
                return USB_ERROR_EPINFO_IS_NULL;
        }

        // Save old pointer to EP_RECORD of address 0
        oldep_ptr = p->epinfo;

        // Temporary assign new pointer to epInfo to p->epinfo in order to avoid toggle inconsistence
        p->epinfo = epInfo;

        p->lowspeed = lowspeed;

        // Get device descriptor
        rcode = pUsb->getDevDescr(0, 0, 8, (uint8_t*)buf);

        if(!rcode)
                len = (buf[0] > constBufSize) ? constBufSize : buf[0];

        if(rcode) {
                // Restore p->epinfo
                p->epinfo = oldep_ptr;

                goto FailGetDevDescr;
        }

        // Restore p->epinfo
        p->epinfo = oldep_ptr;

        // Allocate new address according to device class
        bAddress = addrPool.AllocAddress(parent, false, port);

        if(!bAddress)
                return USB_ERROR_OUT_OF_ADDRESS_SPACE_IN_POOL;

        // Extract Max Packet Size from the device descriptor
        epInfo[0].maxPktSize = udd->bMaxPacketSize0;

        // Assign new address to the device
        rcode = pUsb->setAddr(0, 0, bAddress);

        if(rcode) {
                p->lowspeed = false;
                addrPool.FreeAddress(bAddress);
                bAddress = 0;
                USBTRACE2("setAddr:", rcode);
                return rcode;
        }

        //delay(2); //per USB 2.0 sect.9.2.6.3

        USBTRACE2("Addr:", bAddress);

        p->lowspeed = false;

        p = addrPool.GetUsbDevicePtr(bAddress);

        if(!p)
                return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;

        p->lowspeed = lowspeed;

        if(len)
                rcode = pUsb->getDevDescr(bAddress, 0, len, (uint8_t*)buf);

        if(rcode)
                goto FailGetDevDescr;

        VID = udd->idVendor; // Can be used by classes that inherits this class to check the VID and PID of the connected device
        PID = udd->idProduct;

        num_of_conf = udd->bNumConfigurations;

        // Assign epInfo to epinfo pointer
        rcode = pUsb->setEpInfoEntry(bAddress, 1, epInfo);

        if(rcode)
                goto FailSetDevTblEntry;

        USBTRACE2("NC:", num_of_conf);

        for(uint8_t i = 0; i < num_of_conf; i++) {
                //HexDumper<USBReadParser, uint16_t, uint16_t> HexDump;
                ConfigDescParser<USB_CLASS_HID, 0, 0,
                        CP_MASK_COMPARE_CLASS> confDescrParser(this);

                //rcode = pUsb->getConfDescr(bAddress, 0, i, &HexDump);
                rcode = pUsb->getConfDescr(bAddress, 0, i, &confDescrParser);

                if(rcode)
                        goto FailGetConfDescr;

                if(bNumEP > 1)
                        break;
        } // for

        if(bNumEP < 2)
                return USB_DEV_CONFIG_ERROR_DEVICE_NOT_SUPPORTED;

        // Assign epInfo to epinfo pointer
        rcode = pUsb->setEpInfoEntry(bAddress, bNumEP, epInfo);

        USBTRACE2("Cnf:", bConfNum);

        // Set Configuration Value
        rcode = pUsb->setConf(bAddress, 0, bConfNum);

        if(rcode)
                goto FailSetConfDescr;

        for(uint8_t i = 0; i < bNumIface; i++) {
                if(hidInterfaces[i].epIndex[epInterruptInIndex] == 0)
                        continue;

                rcode = SetIdle(hidInterfaces[i].bmInterface, 0, 0);

                if(rcode && rcode != hrSTALL)
                        goto FailSetIdle;
        }

        USBTRACE22("HU configured ", VID, PID);

        OnInitSuccessful();

        bPollEnable = true;
        return 0;

FailGetDevDescr:
#ifdef DEBUG_USB_HOST
        NotifyFailGetDevDescr();
        goto Fail;
#endif

FailSetDevTblEntry:
#ifdef DEBUG_USB_HOST
        NotifyFailSetDevTblEntry();
        goto Fail;
#endif

FailGetConfDescr:
#ifdef DEBUG_USB_HOST
        NotifyFailGetConfDescr();
        goto Fail;
#endif

FailSetConfDescr:
#ifdef DEBUG_USB_HOST
        NotifyFailSetConfDescr();
        goto Fail;
#endif

FailSetIdle:
#ifdef DEBUG_USB_HOST
        USBTRACE("SetIdle:");
#endif

#ifdef DEBUG_USB_HOST
Fail:
        NotifyFail(rcode);
#endif
        Release();
        return rcode;
}

HIDUniversal::HIDInterface* HIDUniversal::FindInterface(uint8_t iface, uint8_t alt, uint8_t proto) {
        for(uint8_t i = 0; i < bNumIface && i < maxHidInterfaces; i++)
                if(hidInterfaces[i].bmInterface == iface && hidInterfaces[i].bmAltSet == alt
                        && hidInterfaces[i].bmProtocol == proto)
                        return hidInterfaces + i;
        return NULL;
}

void HIDUniversal::EndpointXtract(uint8_t conf, uint8_t iface, uint8_t alt, uint8_t proto, const USB_ENDPOINT_DESCRIPTOR *pep) {
        // If the first configuration satisfies, the others are not concidered.
        if(bNumEP > 1 && conf != bConfNum)
	  {
	    USBTRACE("EndpointXtract:no");
                return;
	  }

        //ErrorMessage<uint8_t>(PSTR("\nConf.Val"), conf);
        //ErrorMessage<uint8_t>(PSTR("Iface Num"), iface);
        //ErrorMessage<uint8_t>(PSTR("Alt.Set"), alt);

        bConfNum = conf;

        uint8_t index = 0;
        HIDInterface *piface = FindInterface(iface, alt, proto);

        // Fill in interface structure in case of new interface
        if(!piface) {
                piface = hidInterfaces + bNumIface;
                piface->bmInterface = iface;
                piface->bmAltSet = alt;
                piface->bmProtocol = proto;
                bNumIface++;
		Notifyc(bNumIface+'0',0x80);
		NotifyStr(" new piface\n",0x80);
        }
	else
	  {
	    NotifyStr("use piface\n",0x80);
	  }

        if((pep->bmAttributes & bmUSB_TRANSFER_TYPE) == USB_TRANSFER_TYPE_INTERRUPT)
                index = (pep->bEndpointAddress & 0x80) == 0x80 ? epInterruptInIndex : epInterruptOutIndex;

        if(index) {
                // Fill in the endpoint info structure
                epInfo[bNumEP].epAddr = (pep->bEndpointAddress & 0x0F);
                epInfo[bNumEP].maxPktSize = (uint8_t)pep->wMaxPacketSize;
                epInfo[bNumEP].bmSndToggle = 0;
                epInfo[bNumEP].bmRcvToggle = 0;
                epInfo[bNumEP].bmNakPower = USB_NAK_NOWAIT;

                // Fill in the endpoint index list
                piface->epIndex[index] = bNumEP; //(pep->bEndpointAddress & 0x0F);

                if(pollInterval < pep->bInterval) // Set the polling interval as the largest polling interval obtained from endpoints
                        pollInterval = pep->bInterval;

                bNumEP++;
        }
	else
	  {
	    NotifyStr("!index\n",0x80);
	    //USBTRACE("EndpointXtract:i");
	  }
        //PrintEndpointDescriptor(pep);
}

uint8_t HIDUniversal::Release() {
        for(uint8_t i = 0; i < MAX_REPORT_PARSERS; i++) {
                if (rptParsers[i].rptParser) {
                        rptParsers[i].rptParser->Release();
                }
        }

        pUsb->GetAddressPool().FreeAddress(bAddress);

        bNumEP = 1;
        bAddress = 0;
        qNextPollTime = 0;
        bPollEnable = false;
        return 0;
}

uint8_t HIDUniversal::Poll() {
        uint8_t rcode = 0;

        if(!bPollEnable) {
                //USBTRACE("HIDUniversal::Poll() !bPollEnable\n");
                return 0;
        }

        const uint32_t now = (uint32_t)millis();
        if((int32_t)(now - qNextPollTime) >= 0L) {
                qNextPollTime = now + pollInterval;

                uint8_t buf[constBuffLen];

                for(uint8_t i = 0; i < bNumIface; i++) {
                        const uint8_t index = hidInterfaces[i].epIndex[epInterruptInIndex];
                        uint16_t read = (uint16_t)epInfo[index].maxPktSize;
                        if(read > constBuffLen)
                                read = constBuffLen;

                        memset(buf, 0, constBuffLen);

                        rcode = pUsb->inTransfer(bAddress, epInfo[index].epAddr, &read, buf);
                        if(rcode) {
                                if(rcode != hrNAK) {
                                        USBTRACE22("(hiduniversal.h) Poll:", rcode, hrNAK);
                                }
                                //USBTRACE2("HIDUniversal::Poll() rcode ", (int)rcode);
                                //return rcode;
                                continue;
                        }

                        if(read > constBuffLen)
                                read = constBuffLen;

#if HID_UNIVERSAL_REMOVE_IDENTICAL_BUFFER
                        // \todo should we use the read variable, which could be less than the previously received message?
                        //       or should we use constBuffLen?
                        const bool identical = memcmp(buf, prevBuf, read) == 0;
                        memcpy(prevBuf, buf, read);
                        if(identical) {
                                //USBTRACE2("HIDUniversal::Poll() identical", (int)bConfNum);
                                return 0;
                        }
#endif

#if defined(DEBUG_USB_HOST)
			Notifyc(i+'0', 0x80);
			Notifyc('/', 0x80);
			Notifyc(index+'0', 0x80);
                        NotifyStr("Buf:", 0x80);

                        for(uint8_t i = 0; i < read; i++) {
                                D_PrintHex<uint8_t > (buf[i], 0x80);
                                Notify(PSTR(" "), 0x80);
                        }

                        Notify(PSTR("\n"), 0x80);
#endif
                        ParseHIDData(this, bHasReportId, (uint8_t)read, buf);

                        HIDReportParser *prs = GetReportParser(bHasReportId ? *buf : 0);
                        if(prs) {
                                prs->Parse(this, bHasReportId, (uint8_t)read, buf, bAddress, epInfo[index].epAddr);
                        } else {
                                USBTRACE2("!GetReportParser:",bHasReportId);
                        }
                }
        }
        return rcode;
}

// Send a report to interrupt out endpoint. This is NOT SetReport() request!
uint8_t HIDUniversal::SndRpt(uint16_t nbytes, uint8_t *dataptr) {
        return pUsb->outTransfer(bAddress, epInfo[epInterruptOutIndex].epAddr, nbytes, dataptr);
}
