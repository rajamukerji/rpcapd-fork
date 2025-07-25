/* -*- Mode: c; tab-width: 8; indent-tabs-mode: 1; c-basic-offset: 8; -*- */
/*
 * Copyright (c) 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] __attribute__((unused)) =
    "@(#) $Header: /tcpdump/master/libpcap/inet.c,v 1.75.2.4 2008-04-20 18:19:24 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <pcap-stdinc.h>
#else /* WIN32 */

#include <sys/param.h>
#ifndef MSDOS
#include <sys/file.h>
#endif
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

struct mbuf;		/* Squelch compiler warnings on some platforms for */
struct rtentry;		/* declarations in <net/if.h> */
#include <net/if.h>
#include <netinet/in.h>
#endif /* WIN32 */

#include <ctype.h>
#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(WIN32) && !defined(__BORLANDC__)
#include <unistd.h>
#endif /* !WIN32 && !__BORLANDC__ */
#ifdef HAVE_LIMITS_H
#include <limits.h>
#else
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#endif

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

/* Not all systems have IFF_LOOPBACK */
#ifdef IFF_LOOPBACK
#define ISLOOPBACK(name, flags) ((flags) & IFF_LOOPBACK)
#else
#define ISLOOPBACK(name, flags) ((name)[0] == 'l' && (name)[1] == 'o' && \
    (isdigit((unsigned char)((name)[2])) || (name)[2] == '\0'))
#endif

struct sockaddr *
dup_sockaddr(struct sockaddr *sa, size_t sa_length)
{
	struct sockaddr *newsa;

	if ((newsa = malloc(sa_length)) == NULL)
		return (NULL);
	return (memcpy(newsa, sa, sa_length));
}

static int
get_instance(const char *name)
{
	const char *cp, *endcp;
	int n;

	if (strcmp(name, "any") == 0) {
		/*
		 * Give the "any" device an artificially high instance
		 * number, so it shows up after all other non-loopback
		 * interfaces.
		 */
		return INT_MAX;
	}

	endcp = name + strlen(name);
	for (cp = name; cp < endcp && !isdigit((unsigned char)*cp); ++cp)
		continue;

	if (isdigit((unsigned char)*cp))
		n = atoi(cp);
	else
		n = 0;
	return (n);
}

int
add_or_find_if(pcap_if_t **curdev_ret, pcap_if_t **alldevs, const char *name,
    u_int flags, const char *description, char *errbuf)
{
	pcap_t *p;
	pcap_if_t *curdev, *prevdev, *nextdev;
	int this_instance;

	/*
	 * Is there already an entry in the list for this interface?
	 */
	for (curdev = *alldevs; curdev != NULL; curdev = curdev->next) {
		if (strcmp(name, curdev->name) == 0)
			break;	/* yes, we found it */
	}

	if (curdev == NULL) {
		/*
		 * No, we didn't find it.
		 *
		 * Can we open this interface for live capture?
		 *
		 * We do this check so that interfaces that are
		 * supplied by the interface enumeration mechanism
		 * we're using but that don't support packet capture
		 * aren't included in the list.  Loopback interfaces
		 * on Solaris are an example of this; we don't just
		 * omit loopback interfaces on all platforms because
		 * you *can* capture on loopback interfaces on some
		 * OSes.
		 *
		 * On OS X, we don't do this check if the device
		 * name begins with "wlt"; at least some versions
		 * of OS X offer monitor mode capturing by having
		 * a separate "monitor mode" device for each wireless
		 * adapter, rather than by implementing the ioctls
		 * that {Free,Net,Open,DragonFly}BSD provide.
		 * Opening that device puts the adapter into monitor
		 * mode, which, at least for some adapters, causes
		 * them to deassociate from the network with which
		 * they're associated.
		 *
		 * Instead, we try to open the corresponding "en"
		 * device (so that we don't end up with, for users
		 * without sufficient privilege to open capture
		 * devices, a list of adapters that only includes
		 * the wlt devices).
		 */
#ifdef __APPLE__
		if (strncmp(name, "wlt", 3) == 0) {
			char *en_name;
			size_t en_name_len;

			/*
			 * Try to allocate a buffer for the "en"
			 * device's name.
			 */
			en_name_len = strlen(name) - 1;
			en_name = malloc(en_name_len + 1);
			if (en_name == NULL) {
				(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
				    "malloc: %s", pcap_strerror(errno));
				return (-1);
			}
			strcpy(en_name, "en");
			strcat(en_name, name + 3);
			p = pcap_open_live(en_name, 68, 0, 0, errbuf);
			free(en_name);
		} else
#endif /* __APPLE */
		p = pcap_open_live(name, 68, 0, 0, errbuf);
		if (p == NULL) {
			/*
			 * No.  Don't bother including it.
			 * Don't treat this as an error, though.
			 */
			*curdev_ret = NULL;
			return (0);
		}
		pcap_close(p);

		/*
		 * Yes, we can open it.
		 * Allocate a new entry.
		 */
		curdev = malloc(sizeof(pcap_if_t));
		if (curdev == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			return (-1);
		}

		/*
		 * Fill in the entry.
		 */
		curdev->next = NULL;
		curdev->name = strdup(name);
		if (curdev->name == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			free(curdev);
			return (-1);
		}
		if (description != NULL) {
			/*
			 * We have a description for this interface.
			 */
			curdev->description = strdup(description);
			if (curdev->description == NULL) {
				(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
				    "malloc: %s", pcap_strerror(errno));
				free(curdev->name);
				free(curdev);
				return (-1);
			}
		} else {
			/*
			 * We don't.
			 */
			curdev->description = NULL;
		}
		curdev->addresses = NULL;	/* list starts out as empty */
		curdev->flags = 0;
		if (ISLOOPBACK(name, flags))
			curdev->flags |= PCAP_IF_LOOPBACK;

		/*
		 * Add it to the list, in the appropriate location.
		 * First, get the instance number of this interface.
		 */
		this_instance = get_instance(name);

		/*
		 * Now look for the last interface with an instance number
		 * less than or equal to the new interface's instance
		 * number - except that non-loopback interfaces are
		 * arbitrarily treated as having interface numbers less
		 * than those of loopback interfaces, so the loopback
		 * interfaces are put at the end of the list.
		 *
		 * We start with "prevdev" being NULL, meaning we're before
		 * the first element in the list.
		 */
		prevdev = NULL;
		for (;;) {
			/*
			 * Get the interface after this one.
			 */
			if (prevdev == NULL) {
				/*
				 * The next element is the first element.
				 */
				nextdev = *alldevs;
			} else
				nextdev = prevdev->next;

			/*
			 * Are we at the end of the list?
			 */
			if (nextdev == NULL) {
				/*
				 * Yes - we have to put the new entry
				 * after "prevdev".
				 */
				break;
			}

			/*
			 * Is the new interface a non-loopback interface
			 * and the next interface a loopback interface?
			 */
			if (!(curdev->flags & PCAP_IF_LOOPBACK) &&
			    (nextdev->flags & PCAP_IF_LOOPBACK)) {
				/*
				 * Yes, we should put the new entry
				 * before "nextdev", i.e. after "prevdev".
				 */
				break;
			}

			/*
			 * Is the new interface's instance number less
			 * than the next interface's instance number,
			 * and is it the case that the new interface is a
			 * non-loopback interface or the next interface is
			 * a loopback interface?
			 *
			 * (The goal of both loopback tests is to make
			 * sure that we never put a loopback interface
			 * before any non-loopback interface and that we
			 * always put a non-loopback interface before all
			 * loopback interfaces.)
			 */
			if (this_instance < get_instance(nextdev->name) &&
			    (!(curdev->flags & PCAP_IF_LOOPBACK) ||
			       (nextdev->flags & PCAP_IF_LOOPBACK))) {
				/*
				 * Yes - we should put the new entry
				 * before "nextdev", i.e. after "prevdev".
				 */
				break;
			}

			prevdev = nextdev;
		}

		/*
		 * Insert before "nextdev".
		 */
		curdev->next = nextdev;

		/*
		 * Insert after "prevdev" - unless "prevdev" is null,
		 * in which case this is the first interface.
		 */
		if (prevdev == NULL) {
			/*
			 * This is the first interface.  Pass back a
			 * pointer to it, and put "curdev" before
			 * "nextdev".
			 */
			*alldevs = curdev;
		} else
			prevdev->next = curdev;
	}

	*curdev_ret = curdev;
	return (0);
}

/*
 * XXX - on FreeBSDs that support it, should it get the sysctl named
 * "dev.{adapter family name}.{adapter unit}.%desc" to get a description
 * of the adapter?  Note that "dev.an.0.%desc" is "Aironet PC4500/PC4800"
 * with my Cisco 350 card, so the name isn't entirely descriptive.  The
 * "dev.an.0.%pnpinfo" has a better description, although one might argue
 * that the problem is really a driver bug - if it can find out that it's
 * a Cisco 340 or 350, rather than an old Aironet card, it should use
 * that in the description.
 *
 * Do NetBSD, DragonflyBSD, or OpenBSD support this as well?  OpenBSD
 * lets you get a description, but it's not generated by the OS, it's
 * set with another ioctl that ifconfig supports; we use that to get
 * the description in OpenBSD.
 *
 * In OS X, the System Configuration framework can apparently return
 * names in 10.4 and later; it also appears that freedesktop.org's HAL
 * offers an "info.product" string, but the HAL specification says
 * it "should not be used in any UI" and "subsystem/capability
 * specific properties" should be used instead.  Using that would
 * require that libpcap applications be linked with the frameworks/
 * libraries in question, which would be a bit of a pain unless we
 * offer, for example, a pkg-config:
 *
 *	http://pkg-config.freedesktop.org/wiki/
 *
 * script, so applications can just use that script to find out what
 * libraries you need to link with when linking with libpcap.
 * pkg-config is GPLed; I don't know whether that would prevent its
 * use with a BSD-licensed library such as libpcap.
 *
 * Do any other UN*Xes, or desktop environments support getting a
 * description?
 */
int
add_addr_to_iflist(pcap_if_t **alldevs, const char *name, u_int flags,
    struct sockaddr *addr, size_t addr_size,
    struct sockaddr *netmask, size_t netmask_size,
    struct sockaddr *broadaddr, size_t broadaddr_size,
    struct sockaddr *dstaddr, size_t dstaddr_size,
    char *errbuf)
{
	pcap_if_t *curdev;
	char *description = NULL;
	pcap_addr_t *curaddr, *prevaddr, *nextaddr;
#ifdef SIOCGIFDESCR
	struct ifreq ifrdesc;
	char ifdescr[IFDESCRSIZE];
	int s;
#endif

#ifdef SIOCGIFDESCR
	/*
	 * Get the description for the interface.
	 */
	memset(&ifrdesc, 0, sizeof ifrdesc);
	strlcpy(ifrdesc.ifr_name, name, sizeof ifrdesc.ifr_name);
	ifrdesc.ifr_data = (caddr_t)&ifdescr;
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		if (ioctl(s, SIOCGIFDESCR, &ifrdesc) == 0 &&
		    strlen(ifrdesc.ifr_data) != 0)
			description = ifrdesc.ifr_data;
		close(s);
	}
#endif

	if (add_or_find_if(&curdev, alldevs, name, flags, description,
	    errbuf) == -1) {
		/*
		 * Error - give up.
		 */
		return (-1);
	}
	if (curdev == NULL) {
		/*
		 * Device wasn't added because it can't be opened.
		 * Not a fatal error.
		 */
		return (0);
	}

	/*
	 * "curdev" is an entry for this interface; add an entry for this
	 * address to its list of addresses.
	 *
	 * Allocate the new entry and fill it in.
	 */
	curaddr = malloc(sizeof(pcap_addr_t));
	if (curaddr == NULL) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "malloc: %s", pcap_strerror(errno));
		return (-1);
	}

	curaddr->next = NULL;
	if (addr != NULL) {
		curaddr->addr = dup_sockaddr(addr, addr_size);
		if (curaddr->addr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			free(curaddr);
			return (-1);
		}
	} else
		curaddr->addr = NULL;

	if (netmask != NULL) {
		curaddr->netmask = dup_sockaddr(netmask, netmask_size);
		if (curaddr->netmask == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			if (curaddr->addr != NULL)
				free(curaddr->addr);
			free(curaddr);
			return (-1);
		}
	} else
		curaddr->netmask = NULL;

	if (broadaddr != NULL) {
		curaddr->broadaddr = dup_sockaddr(broadaddr, broadaddr_size);
		if (curaddr->broadaddr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			if (curaddr->netmask != NULL)
				free(curaddr->netmask);
			if (curaddr->addr != NULL)
				free(curaddr->addr);
			free(curaddr);
			return (-1);
		}
	} else
		curaddr->broadaddr = NULL;

	if (dstaddr != NULL) {
		curaddr->dstaddr = dup_sockaddr(dstaddr, dstaddr_size);
		if (curaddr->dstaddr == NULL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "malloc: %s", pcap_strerror(errno));
			if (curaddr->broadaddr != NULL)
				free(curaddr->broadaddr);
			if (curaddr->netmask != NULL)
				free(curaddr->netmask);
			if (curaddr->addr != NULL)
				free(curaddr->addr);
			free(curaddr);
			return (-1);
		}
	} else
		curaddr->dstaddr = NULL;

	/*
	 * Find the end of the list of addresses.
	 */
	for (prevaddr = curdev->addresses; prevaddr != NULL; prevaddr = nextaddr) {
		nextaddr = prevaddr->next;
		if (nextaddr == NULL) {
			/*
			 * This is the end of the list.
			 */
			break;
		}
	}

	if (prevaddr == NULL) {
		/*
		 * The list was empty; this is the first member.
		 */
		curdev->addresses = curaddr;
	} else {
		/*
		 * "prevaddr" is the last member of the list; append
		 * this member to it.
		 */
		prevaddr->next = curaddr;
	}

	return (0);
}

int
pcap_add_if(pcap_if_t **devlist, const char *name, u_int flags,
    const char *description, char *errbuf)
{
	pcap_if_t *curdev;

	return (add_or_find_if(&curdev, devlist, name, flags, description,
	    errbuf));
}


/*
 * Free a list of interfaces.
 */
void
pcap_freealldevs(pcap_if_t *alldevs)
{
	pcap_if_t *curdev, *nextdev;
	pcap_addr_t *curaddr, *nextaddr;

	for (curdev = alldevs; curdev != NULL; curdev = nextdev) {
		nextdev = curdev->next;

		/*
		 * Free all addresses.
		 */
		for (curaddr = curdev->addresses; curaddr != NULL; curaddr = nextaddr) {
			nextaddr = curaddr->next;
			if (curaddr->addr)
				free(curaddr->addr);
			if (curaddr->netmask)
				free(curaddr->netmask);
			if (curaddr->broadaddr)
				free(curaddr->broadaddr);
			if (curaddr->dstaddr)
				free(curaddr->dstaddr);
			free(curaddr);
		}

		/*
		 * Free the name string.
		 */
		free(curdev->name);

		/*
		 * Free the description string, if any.
		 */
		if (curdev->description != NULL)
			free(curdev->description);

		/*
		 * Free the interface.
		 */
		free(curdev);
	}
}

#if !defined(WIN32) && !defined(MSDOS)

/*
 * Return the name of a network interface attached to the system, or NULL
 * if none can be found.  The interface must be configured up; the
 * lowest unit number is preferred; loopback is ignored.
 */
char *
pcap_lookupdev(register char *errbuf)
{
	pcap_if_t *alldevs;
/* for old BSD systems, including bsdi3 */
#ifndef IF_NAMESIZE
#define IF_NAMESIZE IFNAMSIZ
#endif
	static char device[IF_NAMESIZE + 1];
	char *ret;

	if (pcap_findalldevs(&alldevs, errbuf) == -1)
		return (NULL);

	if (alldevs == NULL || (alldevs->flags & PCAP_IF_LOOPBACK)) {
		/*
		 * There are no devices on the list, or the first device
		 * on the list is a loopback device, which means there
		 * are no non-loopback devices on the list.  This means
		 * we can't return any device.
		 *
		 * XXX - why not return a loopback device?  If we can't
		 * capture on it, it won't be on the list, and if it's
		 * on the list, there aren't any non-loopback devices,
		 * so why not just supply it as the default device?
		 */
		(void)strlcpy(errbuf, "no suitable device found",
		    PCAP_ERRBUF_SIZE);
		ret = NULL;
	} else {
		/*
		 * Return the name of the first device on the list.
		 */
		(void)strlcpy(device, alldevs->name, sizeof(device));
		ret = device;
	}

	pcap_freealldevs(alldevs);
	return (ret);
}

int
pcap_lookupnet(register const char *device, register bpf_u_int32 *netp, register bpf_u_int32 *maskp, register char *errbuf)
{
	register int fd;
	register struct sockaddr_in *sin4;
	struct ifreq ifr;

	/*
	 * The pseudo-device "any" listens on all interfaces and therefore
	 * has the network address and -mask "0.0.0.0" therefore catching
	 * all traffic. Using NULL for the interface is the same as "any".
	 */
	if (!device || strcmp(device, "any") == 0
#ifdef HAVE_DAG_API
	    || strstr(device, "dag") != NULL
#endif
#ifdef HAVE_SEPTEL_API
	    || strstr(device, "septel") != NULL
#endif
#ifdef PCAP_SUPPORT_BT
	    || strstr(device, "bluetooth") != NULL
#endif
#ifdef PCAP_SUPPORT_USB
	    || strstr(device, "usb") != NULL
#endif
	    ) {
		*netp = *maskp = 0;
		return 0;
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE, "socket: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	memset(&ifr, 0, sizeof(ifr));
#ifdef linux
	/* XXX Work around Linux kernel bug */
	ifr.ifr_addr.sa_family = AF_INET;
#endif
	(void)strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, (char *)&ifr) < 0) {
		if (errno == EADDRNOTAVAIL) {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "%s: no IPv4 address assigned", device);
		} else {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "SIOCGIFADDR: %s: %s",
			    device, pcap_strerror(errno));
		}
		(void)close(fd);
		return (-1);
	}
	sin4 = (struct sockaddr_in *)&ifr.ifr_addr;
	*netp = sin4->sin_addr.s_addr;
	if (ioctl(fd, SIOCGIFNETMASK, (char *)&ifr) < 0) {
		(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "SIOCGIFNETMASK: %s: %s", device, pcap_strerror(errno));
		(void)close(fd);
		return (-1);
	}
	(void)close(fd);
	*maskp = sin4->sin_addr.s_addr;
	if (*maskp == 0) {
		if (IN_CLASSA(*netp))
			*maskp = IN_CLASSA_NET;
		else if (IN_CLASSB(*netp))
			*maskp = IN_CLASSB_NET;
		else if (IN_CLASSC(*netp))
			*maskp = IN_CLASSC_NET;
		else {
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "inet class for 0x%x unknown", *netp);
			return (-1);
		}
	}
	*netp &= *maskp;
	return (0);
}

#elif defined(WIN32)

/*
 * Return the name of a network interface attached to the system, or NULL
 * if none can be found.  The interface must be configured up; the
 * lowest unit number is preferred; loopback is ignored.
 */
char *
pcap_lookupdev(register char *errbuf)
{
	DWORD dwVersion;
	DWORD dwWindowsMajorVersion;
	dwVersion = GetVersion();	/* get the OS version */
	dwWindowsMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));
	
	if (dwVersion >= 0x80000000 && dwWindowsMajorVersion >= 4) {
		/*
		 * Windows 95, 98, ME.
		 */
		ULONG NameLength = 8192;
		static char AdaptersName[8192];
		
		if (PacketGetAdapterNames(AdaptersName,&NameLength) )
			return (AdaptersName);
		else
			return NULL;
	} else {
		/*
		 * Windows NT (NT 4.0, W2K, WXP). Convert the names to UNICODE for backward compatibility
		 */
		ULONG NameLength = 8192;
		static WCHAR AdaptersName[8192];
		char *tAstr;
		WCHAR *tUstr;
		WCHAR *TAdaptersName = (WCHAR*)malloc(8192 * sizeof(WCHAR));
		int NAdapts = 0;

		if(TAdaptersName == NULL)
		{
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE, "memory allocation failure");
			return NULL;
		}

		if ( !PacketGetAdapterNames((PTSTR)TAdaptersName,&NameLength) )
		{
			(void)snprintf(errbuf, PCAP_ERRBUF_SIZE,
				"PacketGetAdapterNames: %s",
				pcap_win32strerror());
			free(TAdaptersName);
			return NULL;
		}


		tAstr = (char*)TAdaptersName;
		tUstr = (WCHAR*)AdaptersName;

		/*
		 * Convert and copy the device names
		 */
		while(sscanf(tAstr, "%S", tUstr) > 0)
		{
			tAstr += strlen(tAstr) + 1;
			tUstr += wcslen(tUstr) + 1;
			NAdapts ++;
		}

		tAstr++;
		*tUstr = 0;
		tUstr++;

		/*
		 * Copy the descriptions
		 */
		while(NAdapts--)
		{
			char* tmp = (char*)tUstr;
			strcpy(tmp, tAstr);
			tmp += strlen(tAstr) + 1;
			tUstr = (WCHAR*)tmp;
			tAstr += strlen(tAstr) + 1;
		}

		free(TAdaptersName);
		return (char *)(AdaptersName);
	}	
}


int
pcap_lookupnet(register const char *device, register bpf_u_int32 *netp, register bpf_u_int32 *maskp, register char *errbuf)
{
	/* 
	 * We need only the first IPv4 address, so we must scan the array returned by PacketGetNetInfo()
	 * in order to skip non IPv4 (i.e. IPv6 addresses)
	 */
	npf_if_addr if_addrs[MAX_NETWORK_ADDRESSES];
	LONG if_addr_size = 1;
	struct sockaddr_in *t_addr;
	unsigned int i;

	if (!PacketGetNetInfoEx((void *)device, if_addrs, &if_addr_size)) {
		*netp = *maskp = 0;
		return (0);
	}

	for(i=0; i<MAX_NETWORK_ADDRESSES; i++)
	{
		if(if_addrs[i].IPAddress.ss_family == AF_INET)
		{
			t_addr = (struct sockaddr_in *) &(if_addrs[i].IPAddress);
			*netp = t_addr->sin_addr.S_un.S_addr;
			t_addr = (struct sockaddr_in *) &(if_addrs[i].SubnetMask);
			*maskp = t_addr->sin_addr.S_un.S_addr;

			*netp &= *maskp;
			return (0);
		}
				
	}

	*netp = *maskp = 0;
	return (0);
}

#endif /* !WIN32 && !MSDOS */
