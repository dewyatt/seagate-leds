seagate-leds
============
Linux utility to control Seagate/Maxtor external hard disk LEDs. Supports both the standard activity LED and capacity LEDs.
Also provides information on:
* Supported features
* Supported interfaces
* Serial number
* Download Finder URL (for firmware updates, etc)

See the [Supported Devices] [supported-devices] page for information on what devices are known to be compatible.

[supported-devices]: ../../wiki/Supported-Devices "Supported Devices"

Build
-----
You will need sg3_utils (for libsgutils2).
```
make
```
or
```
g++ seagate-leds.cpp -oseagate-leds -lsgutils2 -O3
```

Usage
-----
Three different commands are supported: info, led, capacity-led.

Info is the only command that will probably work without special prvileges.

### info
```
$ ./seagate-led /dev/sdb info
0BC2:50A7 Seagate GoFlex Desk (SB22)
Supported VPD Pages:
  [0x00] Standard Inquiry
	[0x80] Serial Number
	[0x83] Device Identification
	[0xC1] Features
	[0xC2] Interfaces
Serial # NA0LRYV7
Download Finder URL: https://apps1.seagate.com/downloads/request.html?userPreferredLocaleCookie=en_EN_&fryqrp=QYSAQEAN5YELI27568
Features:
	[0x08] Power
	[0x31] T10 SAT
	[0x37] LED Control
	[0x38] LED Capacity Control
Interfaces:
	[0x08] USBMiniB [active]
```
### led
Get
```
# ./seagate-led /dev/sdb led
led: on
```
Set
```
# ./seagate-led /dev/sdb led off
```
Available arguments are 'on, off, 1, 0'.

### capacity-led
Get
```
# ./seagate-led /dev/sdb capacity-led
0000
```
Set
```
# ./seagate-led /dev/sdb capacity-led 1001
```
This command accepts 3 types of arguments:
* Percent (as in, 90% full)
* Binary (requires 4 digits: 1010)
* Decimal input (ex: 15)
