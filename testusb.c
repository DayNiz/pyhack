// SPDX-License-Identifier: GPL-2.0-or-later
/* $(CROSS_COMPILE)cc -Wall -Wextra -g -lpthread -o testusb testusb.c */
/* Copyright (c) 2002 by David Brownell
 * Copyright (c) 2010 by Samsung Electronics
 * Author: Michal Nazarewicz <mina86@mina86.com>*/
#include <stdio.h>
#include <string.h>
#include <ftw.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#define	TEST_CASES	30
struct usbtest_param {
	unsigned		test_num;
	unsigned		iterations;
	unsigned		length;
	unsigned		vary;
	unsigned		sglen;
	struct timeval		duration;
};
#define USBTEST_REQUEST	_IOWR('U', 100, struct usbtest_param)
#define USB_DT_DEVICE			0x01
#define USB_DT_INTERFACE		0x04

#define USB_CLASS_PER_INTERFACE		0	/* for DeviceClass */
#define USB_CLASS_VENDOR_SPEC		0xff
struct usb_device_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__u16 idVendor;
	__u16 idProduct;
	__u16 bcdDevice;
	__u8  iManufacturer;
	__u8  iProduct;
	__u8  iSerialNumber;
	__u8  bNumConfigurations;
} __attribute__ ((packed));
struct usb_interface_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bInterfaceNumber;
	__u8  bAlternateSetting;
	__u8  bNumEndpoints;
	__u8  bInterfaceClass;
	__u8  bInterfaceSubClass;
	__u8  bInterfaceProtocol;
	__u8  iInterface;
} __attribute__ ((packed));
enum usb_device_speed {
	USB_SPEED_UNKNOWN = 0,			/* enumerating */
	USB_SPEED_LOW, USB_SPEED_FULL,		/* usb 1.1 */
	USB_SPEED_HIGH,				/* usb 2.0 */
	USB_SPEED_WIRELESS,			/* wireless (usb 2.5) */
	USB_SPEED_SUPER,			/* usb 3.0 */
	USB_SPEED_SUPER_PLUS,			/* usb 3.1 */
};
static char *speed (enum usb_device_speed s)
{
	switch (s) {
	case USB_SPEED_UNKNOWN:		return "unknown";
	case USB_SPEED_LOW:		return "low";
	case USB_SPEED_FULL:		return "full";
	case USB_SPEED_HIGH:		return "high";
	case USB_SPEED_WIRELESS:	return "wireless";
	case USB_SPEED_SUPER:		return "super";
	case USB_SPEED_SUPER_PLUS:	return "super-plus";
	default:			return "??";
	}
}
struct testdev {
	struct testdev		*next;
	char			*name;
	pthread_t		thread;
	enum usb_device_speed	speed;
	unsigned		ifnum : 8;
	unsigned		forever : 1;
	int			test;
	struct usbtest_param	param;
};
static struct testdev		*testdevs;
static int testdev_ffs_ifnum(FILE *fd)
{
	union {
		char buf[255];
		struct usb_interface_descriptor intf;
	} u;
	for (;;) {
		if (fread(u.buf, 1, 1, fd) != 1)
			return -1;
		if (fread(u.buf + 1, (unsigned char)u.buf[0] - 1, 1, fd) != 1)
			return -1;

		if (u.intf.bLength == sizeof u.intf
		 && u.intf.bDescriptorType == USB_DT_INTERFACE
		 && u.intf.bNumEndpoints == 2
		 && u.intf.bInterfaceClass == USB_CLASS_VENDOR_SPEC
		 && u.intf.bInterfaceSubClass == 0
		 && u.intf.bInterfaceProtocol == 0)
			return (unsigned char)u.intf.bInterfaceNumber;
	}
}
static int testdev_ifnum(FILE *fd)
{
	struct usb_device_descriptor dev;
	if (fread(&dev, sizeof dev, 1, fd) != 1)
		return -1;
	if (dev.bLength != sizeof dev || dev.bDescriptorType != USB_DT_DEVICE)
		return -1;
	if (dev.idVendor == 0x0547 && dev.idProduct == 0x1002)
		return 0;
	if (dev.idVendor == 0x0547 && dev.idProduct == 0x2235)
		return 0;
	if (dev.idVendor == 0x04b4 && dev.idProduct == 0x8613)
		return 0;
	if (dev.idVendor == 0x0547 && dev.idProduct == 0x0080)
		return 0;
	if (dev.idVendor == 0x06cd && dev.idProduct == 0x010b)
		return 0;
	if (dev.idVendor == 0x0525 && dev.idProduct == 0xa4a0)
		return 0;
	if (dev.idVendor == 0x0525 && dev.idProduct == 0xa4a4)
		return testdev_ffs_ifnum(fd);
	if (dev.idVendor == 0x0525 && dev.idProduct == 0xa4a3)
		return 0;
	if (dev.idVendor == 0xfff0 && dev.idProduct == 0xfff0)
		return 0;
	if (dev.idVendor == 0x0b62 && dev.idProduct == 0x0059)
		return 0;
	if (dev.idVendor == 0x0525 && dev.idProduct == 0xa4ac
	 && (dev.bDeviceClass == USB_CLASS_PER_INTERFACE
	  || dev.bDeviceClass == USB_CLASS_VENDOR_SPEC))
		return testdev_ffs_ifnum(fd);
	return -1;
}
static int find_testdev(const char *name, const struct stat *sb, int flag)
{
	FILE				*fd;
	int				ifnum;
	struct testdev			*entry;
	(void)sb; /* unused */
	if (flag != FTW_F)
		return 0;
	fd = fopen(name, "rb");
	if (!fd) {
		perror(name);
		return 0;
	}
	ifnum = testdev_ifnum(fd);
	fclose(fd);
	if (ifnum < 0)
		return 0;
	entry = calloc(1, sizeof *entry);
	if (!entry)
		goto nomem;
	entry->name = strdup(name);
	if (!entry->name) {
		free(entry);
nomem:
		perror("malloc");
		return 0;
	}
	entry->ifnum = ifnum;
	entry->next = testdevs;
	testdevs = entry;
	return 0;
}
static int
usbdev_ioctl (int fd, int ifno, unsigned request, void *param)
{
	struct usbdevfs_ioctl	wrapper;

	wrapper.ifno = ifno;
	wrapper.ioctl_code = request;
	wrapper.data = param;

	return ioctl (fd, USBDEVFS_IOCTL, &wrapper);
}
static void *handle_testdev (void *arg)
{
	struct testdev		*dev = arg;
	int			fd, i;
	int			status;
	if ((fd = open (dev->name, O_RDWR)) < 0) {
		perror ("can't open dev file r/w");
		return 0;
	}
	status  =  ioctl(fd, USBDEVFS_GET_SPEED, NULL);
	if (status < 0)
		fprintf(stderr, "USBDEVFS_GET_SPEED failed %d\n", status);
	else
		dev->speed = status;
	fprintf(stderr, "%s speed\t%s\t%u\n",
			speed(dev->speed), dev->name, dev->ifnum);
restart:
	for (i = 0; i < TEST_CASES; i++) {
		if (dev->test != -1 && dev->test != i)
			continue;
		dev->param.test_num = i;
		status = usbdev_ioctl (fd, dev->ifnum,
				USBTEST_REQUEST, &dev->param);
		if (status < 0 && errno == EOPNOTSUPP)
			continue;
		if (status < 0) {
			char	buf [80];
			int	err = errno;
			if (strerror_r (errno, buf, sizeof buf)) {
				snprintf (buf, sizeof buf, "error %d", err);
				errno = err;
			}
			printf ("%s test %d --> %d (%s)\n",
				dev->name, i, errno, buf);
		} else
			printf ("%s test %d, %4d.%.06d secs\n", dev->name, i,
				(int) dev->param.duration.tv_sec,
				(int) dev->param.duration.tv_usec);
		fflush (stdout);
	}
	if (dev->forever)
		goto restart;
	close (fd);
	return arg;
}
static const char *usb_dir_find(void)
{
	static char udev_usb_path[] = "/dev/bus/usb";

	if (access(udev_usb_path, F_OK) == 0)
		return udev_usb_path;

	return NULL;
}
static int parse_num(unsigned *num, const char *str)
{
	unsigned long val;
	char *end;

	errno = 0;
	val = strtoul(str, &end, 0);
	if (errno || *end || val > UINT_MAX)
		return -1;
	*num = val;
	return 0;
}
int main (int argc, char **argv)
{
	int			c;
	struct testdev		*entry;
	char			*device;
	const char		*usb_dir = NULL;
	int			all = 0, forever = 0, not = 0;
	int			test = -1 /* all */;
	struct usbtest_param	param;
	param.iterations = 1000;
	param.length = 1024;
	param.vary = 1024;
	param.sglen = 32;
	device = getenv ("DEVICE");
	while ((c = getopt (argc, argv, "D:aA:c:g:hlns:t:v:")) != EOF)
	switch (c) {
	case 'D':	/* device, if only one */
		device = optarg;
		continue;
	case 'A':	/* use all devices with specified USB dir */
		usb_dir = optarg;
		/* FALL THROUGH */
	case 'a':	/* use all devices */
		device = NULL;
		all = 1;
		continue;
	case 'c':	/* count iterations */
		if (parse_num(&param.iterations, optarg))
			goto usage;
		continue;
	case 'g':	/* scatter/gather entries */
		if (parse_num(&param.sglen, optarg))
			goto usage;
		continue;
	case 'l':	/* loop forever */
		forever = 1;
		continue;
	case 'n':	/* no test running! */
		not = 1;
		continue;
	case 's':	/* size of packet */
		if (parse_num(&param.length, optarg))
			goto usage;
		continue;
	case 't':	/* run just one test */
		test = atoi (optarg);
		if (test < 0)
			goto usage;
		continue;
	case 'v':	/* vary packet size by ... */
		if (parse_num(&param.vary, optarg))
			goto usage;
		continue;
	case '?':
	case 'h':
	default:
usage:
		fprintf (stderr,
			"usage: %s [options]\n"
			"Options:\n"
			"\t-D dev		only test specific device\n"
			"\t-A usb-dir\n"
			"\t-a		test all recognized devices\n"
			"\t-l		loop forever(for stress test)\n"
			"\t-t testnum	only run specified case\n"
			"\t-n		no test running, show devices to be tested\n"
			"Case arguments:\n"
			"\t-c iterations		default 1000\n"
			"\t-s transfer length	default 1024\n"
			"\t-g sglen		default 32\n"
			"\t-v vary			default 1024\n",
			argv[0]);
		return 1;
	}
	if (optind != argc)
		goto usage;
	if (!all && !device) {
		fprintf (stderr, "must specify '-a' or '-D dev', "
			"or DEVICE=/dev/bus/usb/BBB/DDD in env\n");
		goto usage;
	}
	if (!usb_dir) {
		usb_dir = usb_dir_find();
		if (!usb_dir) {
			fputs ("USB device files are missing\n", stderr);
			return -1;
		}
	}
	if (ftw (usb_dir, find_testdev, 3) != 0) {
		fputs ("ftw failed; are USB device files missing?\n", stderr);
		return -1;
	}
	if (!testdevs && !device) {
		fputs ("no test devices recognized\n", stderr);
		return -1;
	}
	if (not)
		return 0;
	if (testdevs && !testdevs->next && !device)
		device = testdevs->name;
	for (entry = testdevs; entry; entry = entry->next) {
		int	status;
		entry->param = param;
		entry->forever = forever;
		entry->test = test;

		if (device) {
			if (strcmp (entry->name, device))
				continue;
			return handle_testdev (entry) != entry;
		}
		status = pthread_create (&entry->thread, 0, handle_testdev, entry);
		if (status)
			perror ("pthread_create");
	}
	if (device) {
		struct testdev		dev;
		fprintf (stderr, "%s: %s may see only control tests\n",
				argv [0], device);
		memset (&dev, 0, sizeof dev);
		dev.name = device;
		dev.param = param;
		dev.forever = forever;
		dev.test = test;
		return handle_testdev (&dev) != &dev;
	}
	for (entry = testdevs; entry; entry = entry->next) {
		void	*retval;

		if (pthread_join (entry->thread, &retval))
			perror ("pthread_join");
	}
	return 0;
}