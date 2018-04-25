Release Notes for GigE-V Framework for Linux 
Version 2.02.0.0132

For information on installation, hardware and software requirements, and 
complete API documentation, refer to the GigE-V Framework for Linux 
Programmer's Manual available on the Teledyne DALSA website at: 

https://www.teledynedalsa.com/imaging/support/sdks

What's New?
-----------
This release provides support for feature access using GigE Vision compliant 
XML files. (See history details below).

Supported Hardware Platforms
---------------------------- 
 x86    : Intel/AMD 32-bit and 64-bit CPUs
 ARM7hf : 32-bit ARM7 with hard-float (hardware floating point)
 ARMsf  : 32-bit ARM with soft-float (software emulated floating point)
 aarch64: 64-bit ARM 

System Requirements
-------------------
 - Linux OS support for Gigabit NIC hardware (kernel 2.6.24 and later)
 - Support for PF_PACKET with RX_RING capability recommended for best 
    performance (usually available with the Wireshark application and/or 
    the libpcap package which is widely installed by default).
 - libcap-dev package is required to use Linux "capabilities" when running 
   as "root" is not desired.
 - libglade2-dev package is required for building and using the 
   GigE Vision Device Status tool (which uses gtk).
 - libx11-dev / libxext-dev packages are required for using the X11 display 
   window in the example programs.


V2.00.0.0108
------------
Base release

V2.01.0.0120
------------
Release : Highlights 
	- Optimized TurboDrive support for Intel/AMD (automatically enabled).
	- Basic TurboDrive support for ARM.
	- Support for BiColor pixel format.
	- Zipped XML files are now deflated using Public Domain code from "miniz.c"
	  rather than relying on the target system to have "gunzip" installed.
	- X11 display function in example programs no longer relies on a specific named
	  font being present.
	- X11 display function in example programs now handles images that are much
	  larger than the desktop,


V2.02.0.0132
------------
Release : Highlights 
	- Fixes for delivery of frames in order of their frame id. (GigeVision Block ID).
	- Fixes for TurboDrive decoding error for 8bit Bayer data.
	- Adds SSE3 support for unpacking mono10Packed/mono12Packed.
	- Adds aarch64 and 32-bit armv8.




