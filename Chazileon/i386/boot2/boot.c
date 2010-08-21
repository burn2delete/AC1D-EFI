/*
 * Copyright (c) 1999-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 2.0 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1990 Carnegie-Mellon University
 * Copyright (c) 1989 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 *          INTEL CORPORATION PROPRIETARY INFORMATION
 *
 *  This software is supplied under the terms of a license  agreement or 
 *  nondisclosure agreement with Intel Corporation and may not be copied 
 *  nor disclosed except in accordance with the terms of that agreement.
 *
 *  Copyright 1988, 1989 by Intel Corporation
 */

/*
 * Copyright 1993 NeXT Computer, Inc.
 * All rights reserved.
 */
 
/*
 * Completely reworked by Sam Streeper (sam_s@NeXT.com)
 * Reworked again by Curtis Galloway (galloway@NeXT.com)
 */

//#define DEBUG 1 //Azi:temp

//Azi:include
//#include "boot.h" - included on graphics.h, which is included on gui.h
//#include "bootstruct.h" - same as above
#include "fake_efi.h"
#include "gui.h"
//#include "libsa.h" - included on libsaio.h, which is included on sl.h
#include "platform.h"
#include "ramdisk.h"
#include "sl.h"

#include "edid.h" // Autoresolution
#include "autoresolution.h" // "was" included on boot.h, which is everywere!! -> gui.h -> graphics.h

long gBootMode; /* defaults to 0 == kBootModeNormal */
bool gOverrideKernel;
static char gBootKernelCacheFile[512];
static char gCacheNameAdler[64 + 256];
//char *gPlatformName = gCacheNameAdler; disabled
char gRootDevice[512];
char gMKextName[512];
bool gEnableCDROMRescan;
bool gScanSingleDrive;

int     bvCount = 0;
//int		menucount = 0;
int     gDeviceCount = 0;

BVRef   bvr;
BVRef   menuBVR;
BVRef   bvChain;
bool    useGUI;

//static void selectBiosDevice(void);
static unsigned long Adler32(unsigned char *buffer, long length);

static bool gUnloadPXEOnExit = false;

/*
 * How long to wait (in seconds) to load the
 * kernel after displaying the "boot:" prompt.
 */
#define kBootErrorTimeout 5

/*
 * Default path to kernel cache file
 */
#define kDefaultCachePath "/System/Library/Caches/com.apple.kernelcaches/kernelcache"

//==========================================================================
// Zero the BSS.

static void zeroBSS(void)
{
	extern char _DATA__bss__begin, _DATA__bss__end;
	extern char _DATA__common__begin, _DATA__common__end;

	bzero(&_DATA__bss__begin, (&_DATA__bss__end - &_DATA__bss__begin));
	bzero(&_DATA__common__begin, (&_DATA__common__end - &_DATA__common__begin));
}

//==========================================================================
// Malloc error function

static void malloc_error(char *addr, size_t size, const char *file, int line)
{
	stop("\nMemory allocation error! Addr=0x%x, Size=0x%x, File=%s, Line=%d\n", (unsigned)addr, (unsigned)size, file, line);
}

//==========================================================================
//Initializes the runtime.  Right now this means zeroing the BSS and initializing malloc.
//
void initialize_runtime(void)
{
	zeroBSS();
	malloc_init(0, 0, 0, malloc_error);
}

//==========================================================================
// execKernel - Load the kernel image (mach-o) and jump to its entry point.

static int ExecKernel(void *binary)
{
    entry_t                   kernelEntry;
    int                       ret;

    bootArgs->kaddr = bootArgs->ksize = 0;

    ret = DecodeKernel(binary,
                       &kernelEntry,
                       (char **) &bootArgs->kaddr,
                       (int *)&bootArgs->ksize );

    if ( ret != 0 )
        return ret;

    // Reserve space for boot args
    reserveKernBootStruct();

    // Load boot drivers from the specifed root path.

    if (!gHaveKernelCache) {
          LoadDrivers("/");
    }

    clearActivityIndicator();

    if (gErrors) {
        printf("Errors encountered while starting up the computer.\n");
        printf("Pausing %d seconds...\n", kBootErrorTimeout);
        sleep(kBootErrorTimeout);
    }

    setupFakeEfi();

    md0Ramdisk();

    verbose("Starting Darwin %s\n",( archCpuType == CPU_TYPE_I386 ) ? "x86" : "x86_64");

    // Cleanup the PXE base code.

    if ( (gBootFileType == kNetworkDeviceType) && gUnloadPXEOnExit ) {
		if ( (ret = nbpUnloadBaseCode()) != nbpStatusSuccess )
        {
        	printf("nbpUnloadBaseCode error %d\n", (int) ret);
            sleep(2);
        }
    }

    bool dummyVal;

	//Azi: Wait=y is breaking other keys when typed "after them" at boot prompt.
	// Works properly if typed in first place or used on Boot.plist.
	if (getBoolForKey(kWaitForKeypressKey, &dummyVal, &bootInfo->bootConfig) && dummyVal) {
		pause();
	}

	usb_loop();

    // If we were in text mode, switch to graphics mode.
    // This will draw the boot graphics unless we are in
    // verbose mode.

    if(gVerboseMode)
      setVideoMode( GRAPHICS_MODE, 0 );
    else
      drawBootGraphics();
	
	setupBooterLog();
	
    finalizeBootStruct();
    
    // Jump to kernel's entry point. There's no going back now.

    startprog( kernelEntry, bootArgs );

    // Not reached

    return 0;
}

//==========================================================================
// This is the entrypoint from real-mode which functions exactly as it did
// before. Multiboot does its own runtime initialization, does some of its
// own things, and then calls common_boot.
void boot(int biosdev)
{
	initialize_runtime();
	// Enable A20 gate before accessing memory above 1Mb.
	enableA20();
	common_boot(biosdev);
}

//==========================================================================
// The 'main' function for the booter. Called by boot0 when booting
// from a block device, or by the network booter.
//
// arguments:
//   biosdev - Value passed from boot1/NBP to specify the device
//             that the booter was loaded from.
//
// If biosdev is kBIOSDevNetwork, then this function will return if
// booting was unsuccessful. This allows the PXE firmware to try the
// next boot device on its list.
void common_boot(int biosdev)
{
	bool			instantMenu, quiet, rescanPrompt;
	bool			firstRun = true;
	char			*bootFile;
	int				status;
	unsigned int	allowBVFlags = kBVFlagSystemVolume|kBVFlagForeignBoot;
	unsigned int	denyBVFlags = kBVFlagEFISystem;
	unsigned long	adler32;
	
	// Set reminder to unload the PXE base code. Neglect to unload
	// the base code will result in a hang or kernel panic.
	gUnloadPXEOnExit = true;
	
	// Record the device that the booter was loaded from.
	gBIOSDev = biosdev & kBIOSDevMask;
	
	// Initialize boot info structure.
	initKernBootStruct();
	
	initBooterLog();
	
	// Setup VGA text mode.
	// Not sure if it is safe to call setVideoMode() before the
	// config table has been loaded. Call video_mode() instead.
#if DEBUG
	printf("before video_mode\n");
#endif
	video_mode( 2 );  // 80x25 mono text mode.
#if DEBUG
	printf("after video_mode\n");
#endif
	
	// Scan and record the system's hardware information.
	scan_platform();
	
	// First get info for boot volume.
	scanBootVolumes(gBIOSDev, 0);
	bvChain = getBVChainForBIOSDev(gBIOSDev);
	setBootGlobals(bvChain);
	
	// Load boot.plist config file
	status = loadSystemConfig(&bootInfo->bootConfig);
	
	if (getBoolForKey(kQuietBootKey, &quiet, &bootInfo->bootConfig) && quiet)
	{
		gBootMode |= kBootModeQuiet;
	}
	
	// Override firstRun to get to the boot menu instantly by setting "Instant Menu"=y in system config
	if (getBoolForKey(kInstantMenuKey, &instantMenu, &bootInfo->bootConfig) && instantMenu)
	{
		firstRun = false;
	}
	
	// Loading preboot ramdisk if exists.
	loadPrebootRAMDisk();
	
	// Disable rescan option by default
	gEnableCDROMRescan = false;
	
	// Enable it with Rescan=y in system config
	if (getBoolForKey(kRescanKey, &gEnableCDROMRescan, &bootInfo->bootConfig) && gEnableCDROMRescan)
	{
		gEnableCDROMRescan = true;
	}
	
	// Ask the user for Rescan option by setting "Rescan Prompt"=y in system config.
	rescanPrompt = false;
	if (getBoolForKey(kRescanPromptKey, &rescanPrompt , &bootInfo->bootConfig)
		&& rescanPrompt && biosDevIsCDROM(gBIOSDev))
	{
		gEnableCDROMRescan = promptForRescanOption();
	}
	
	// Enable touching a single BIOS device only if "Scan Single Drive"=y is set in system config.
	if (getBoolForKey(kScanSingleDriveKey, &gScanSingleDrive, &bootInfo->bootConfig) && gScanSingleDrive)
	{
		gScanSingleDrive = true;
	}
	
	// Create a list of partitions on device(s).
	if (gScanSingleDrive)
	{
		scanBootVolumes(gBIOSDev, &bvCount);
	}
	else
	{
		scanDisks(gBIOSDev, &bvCount);
	}
	
	// Create a separated bvr chain using the specified filters.
	bvChain = newFilteredBVChain(0x80, 0xFF, allowBVFlags, denyBVFlags, &gDeviceCount);
	
	gBootVolume = selectBootVolume(bvChain);
	
#if DEBUG
	printf(" Default: %d, ->biosdev: %d, ->part_no: %d ->flags: %d\n",
			gBootVolume, gBootVolume->biosdev, gBootVolume->part_no, gBootVolume->flags);
	printf(" bt(0,0): %d, ->biosdev: %d, ->part_no: %d ->flags: %d\n",
			gBIOSBootVolume, gBIOSBootVolume->biosdev, gBIOSBootVolume->part_no, gBIOSBootVolume->flags);
	pause(); //getc();
#endif
	
	useGUI = true;
	
	// Override useGUI default
	getBoolForKey(kGUIKey, &useGUI, &bootInfo->bootConfig);

//Azi:autoresolution begin
	// Before initGui, patch the video bios with the correct resolution
	UInt32 params[4];
	params[3] = 0;
	
	// default to "false" as it doesn't work for everyone atm.
	// http://forum.voodooprojects.org/index.php/topic,1227.0.html
	gAutoResolution = false;
	
	getBoolForKey(kAutoResolutionKey, &gAutoResolution, &bootInfo->bootConfig);
	
	//Open the VBios and store VBios or Tables
	map = openVbios(CT_UNKWN);
	
	if (gAutoResolution == true)
	{
		//Get Resolution from Graphics Mode key or EDID
		int count = getNumberArrayFromProperty(kGraphicsModeKey, params, 4);
		if (count < 3)
			getResolution(params);
		else
		{
			if ( params[2] == 256 ) params[2] = 8;
			if ( params[2] == 555 ) params[2] = 16;
			if ( params[2] == 888 ) params[2] = 32;
		}
		
#ifdef AUTORES_DEBUG
	printf("Resolution: %dx%d\n",params[0], params[1]);
#endif	
		
		//perfom the actual VBIOS patching
		if (params[0] != 0 && params[1] != 0)
			patchVbios(map, params[0], params[1], params[2], 0, 0);
	}
//Azi:autoresolution end

	if (useGUI && initGUI())
	{
		// initGUI() returned with an error, disabling GUI.
		useGUI = false;
	}
	
	setBootGlobals(bvChain);
	
	// Parse args, load and start kernel.
	while (1)
	{
		bool		forceresume, tryresume, tryresumedefault; 
		const char	*val;
		int			len, trycache;
		int			ret = -1;
		long		flags, cachetime, kerneltime, exttime, sleeptime, time;
		void		*binary = (void *)kLoadAddr;
		
		// additional variable for testing alternate kernel image locations on boot helper partitions.
		char		bootFileSpec[512]; //Azi:HelperConfig - kernel
		
		// Initialize globals.
		
		sysConfigValid = false;
		gErrors		   = false;
		
		status = getBootOptions(firstRun);
		firstRun = false;
		if ( status == -1 ) continue;
		
		//Azi: i'm now almost sure that here is the right place to do this! - test (gBootVolume == NULL)
		// Turn off any GUI elements, draw background and update VRAM.
		if ( bootArgs->Video.v_display == GRAPHICS_MODE )
		{
			gui.devicelist.draw = false;
			gui.bootprompt.draw = false;
			gui.menu.draw = false;
			gui.infobox.draw = false;
			gui.logo.draw = false;
			drawBackground();
			updateVRAM();
		}
		
//Azi:autoresolution begin
		
		//
		//AutoResolution - Reapply the patch or cancel if Graphics Mode was incorrect
		//					or EDID Info was insane
		getBoolForKey(kAutoResolutionKey, &gAutoResolution, &bootInfo->bootConfig);
		
		//Restore the vbios for Cancelation
		if ((gAutoResolution == false) && map)
		{
			restoreVbios(map);
			closeVbios(map);	
		}
		
		if ((gAutoResolution == true) && map)
		{
			// If mode has been switched during boot menu
			// use the new resolution
			if (map->hasSwitched == true)
			{
				params[0] = map->currentX;
				params[1] = map->currentY;
				params[2] = 32;
			}
			else
			{
				//or get resolution from Graphics Mode or EDID
				int count = getNumberArrayFromProperty(kGraphicsModeKey, params, 4);
				if (count < 3)
					getResolution(params);
				else
				{
					if ( params[2] == 256 ) params[2] = 8;
					if ( params[2] == 555 ) params[2] = 16;
					if ( params[2] == 888 ) params[2] = 32;
				}
			}
			
			//Resolution has changed, reapply the patch
			if ((params[0] != 0) && (params[1] != 0) && (params[0] != map->currentX) &&
				(params[1] != map->currentY))
			{
				patchVbios(map, params[0], params[1], params[2], 0, 0);
			}
			closeVbios(map);
		}
		
//Azi:autoresolution end
		
		status = processBootOptions();
		// Status == 1 means to chainboot
		if ( status ==	1 ) break;
		// Status == -1 means that gBootVolume is NULL. Config file is not mandatory anymore! 
		if ( status == -1 )
		{
			// gBootVolume == NULL usually means the user hit escape.
			if (gBootVolume == NULL)
			{
				freeFilteredBVChain(bvChain);
				
				if (gEnableCDROMRescan)
					rescanBIOSDevice(gBIOSDev);
				
				bvChain = newFilteredBVChain(0x80, 0xFF, allowBVFlags, denyBVFlags, &gDeviceCount);
				setBootGlobals(bvChain);
				setupDeviceList(&bootInfo->themeConfig);
			}
			continue;
		}
		
		// Other status (e.g. 0) means that we should proceed with boot.

		// If cpu doesn't handle 64 bit instructions,...
		if (!platformCPUFeature(CPU_FEATURE_EM64T) ||
			// ... user forced i386 kernel architecture on cpu with "em64t"...
			getValueForKey(kArchI386Flag, &val, &len, &bootInfo->bootConfig) ||
			// ... or forced Legacy Mode...
			getValueForKey(kLegacyModeFlag, &val, &len, &bootInfo->bootConfig))
		{
			// ... use i386 kernel arch.
			archCpuType = CPU_TYPE_I386;
		}
		else
		{
			// Else use x86_64 kernel arch.
			archCpuType = CPU_TYPE_X86_64;
		}
		// Override i386/-legacy, if flagged on Boot.plist.
		if (getValueForKey(kArchX86_64Flag, &val, &len, &bootInfo->bootConfig))
		{
			archCpuType = CPU_TYPE_X86_64;
		}
		
		if (!getBoolForKey (kWakeKey, &tryresume, &bootInfo->bootConfig))
		{
			tryresume = true;
			tryresumedefault = true;
		}
		else
		{
			tryresumedefault = false;
		}
		
		if (!getBoolForKey (kForceWakeKey, &forceresume, &bootInfo->bootConfig))
		{
			forceresume = false;
		}
		
		if (forceresume)
		{
			tryresume = true;
			tryresumedefault = false;
		}
		
		while (tryresume)
		{
			const char *tmp;
			BVRef bvr;
			if (!getValueForKey(kWakeImageKey, &val, &len, &bootInfo->bootConfig))
				val="/private/var/vm/sleepimage";
			
			// Do this first to be sure that root volume is mounted
			ret = GetFileInfo(0, val, &flags, &sleeptime);
			
			if ((bvr = getBootVolumeRef(val, &tmp)) == NULL)
				break;
			
			// Can't check if it was hibernation Wake=y is required
			if (bvr->modTime == 0 && tryresumedefault)
				break;
			
			if ((ret != 0) || ((flags & kFileTypeMask) != kFileTypeFlat))
				break;
			
			if (!forceresume && ((sleeptime+3)<bvr->modTime))
			{
				printf("Hibernate image is too old by %d seconds. Use ForceWake=y to override\n",
						bvr->modTime-sleeptime);
				break;
			}
			HibernateBoot((char *)val);
			break;
		}
		
		// Reset cache name.
		bzero(gCacheNameAdler + 64, sizeof(gCacheNameAdler) - 64);
		
		sprintf(gCacheNameAdler + 64, "%s,%s", gRootDevice, bootInfo->bootFile);
		
		adler32 = Adler32((unsigned char *)gCacheNameAdler, sizeof(gCacheNameAdler));
		
		if (getValueForKey(kKernelCacheKey, &val, &len, &bootInfo->bootConfig))
		{
			strlcpy(gBootKernelCacheFile, val, len+1);
		}
		else
		{
			sprintf(gBootKernelCacheFile, "%s.%08lX", kDefaultCachePath, adler32);
		}
		
		// Check for cache file.
		trycache = (((gBootMode & kBootModeSafe) == 0) &&
					!gOverrideKernel &&
					(gBootFileType == kBlockDeviceType) &&
					(gMKextName[0] == '\0') &&
					(gBootKernelCacheFile[0] != '\0'));

		verbose("Loading Darwin %s\n", gMacOSVersion);
		
		if (trycache) do
		{
			// if we haven't found the kernel yet, don't use the cache
			ret = GetFileInfo(NULL, bootInfo->bootFile, &flags, &kerneltime);
			if ((ret != 0) || ((flags & kFileTypeMask) != kFileTypeFlat))
			{
				trycache = 0;
				break;
			}
			
			ret = GetFileInfo(NULL, gBootKernelCacheFile, &flags, &cachetime);
			if ((ret != 0) || ((flags & kFileTypeMask) != kFileTypeFlat)
				|| (cachetime < kerneltime))
			{
				trycache = 0;
				break;
			}
			
			ret = GetFileInfo("/System/Library/", "Extensions", &flags, &exttime);
			if ((ret == 0) && ((flags & kFileTypeMask) == kFileTypeDirectory)
				&& (cachetime < exttime))
			{
				trycache = 0;
				break;
			}
			
			if (kerneltime > exttime)
			{
				exttime = kerneltime;
			}
			
			if (cachetime != (exttime + 1))
			{
				trycache = 0;
				break;
			}
		} while (0);
		
		do
		{
			if (trycache)
			{
				bootFile = gBootKernelCacheFile;
				verbose("Loading kernel cache %s\n", bootFile); //Azi: i never saw this!! check!!!
				ret = LoadFile(bootFile);
				binary = (void *)kLoadAddr;
				if (ret >= 0)
				{
					break;
				}
			}
			
			bootFile = bootInfo->bootFile;
			
			// Try to load kernel image from alternate locations on boot helper partitions.
			sprintf(bootFileSpec, "com.apple.boot.P/%s", bootFile); //Azi:HelperConfig - kernel
			
			ret = GetFileInfo(NULL, bootFileSpec, &flags, &time); 
			if (ret == -1)
			{
				sprintf(bootFileSpec, "com.apple.boot.R/%s", bootFile);
				
				ret = GetFileInfo(NULL, bootFileSpec, &flags, &time); 
				if (ret == -1)
				{
					sprintf(bootFileSpec, "com.apple.boot.S/%s", bootFile);
					
					ret = GetFileInfo(NULL, bootFileSpec, &flags, &time); 
					if (ret == -1)
					{
						// Not found any alternate locations, using the original kernel image path.
						strcpy(bootFileSpec, bootFile);
					}
				}
			}
						
			verbose("Loading kernel %s\n", bootFileSpec);
			
			ret = LoadThinFatFile(bootFileSpec, &binary);
			if (ret <= 0 && archCpuType == CPU_TYPE_X86_64)
			{
				archCpuType = CPU_TYPE_I386;
				ret = LoadThinFatFile(bootFileSpec, &binary);				
			}
		
		} while (0);
		
		clearActivityIndicator();
/*#if DEBUG
		printf("Pausing...");
		sleep(8);
#endif Azi: annoying! Can't see the point atm... */
		
		if (ret <= 0)
		{
			printf("Can't find %s\n", bootFile);

			sleep(1);

			if (gBootFileType == kNetworkDeviceType) {
				// Return control back to PXE. Don't unload PXE base code.
				gUnloadPXEOnExit = false;
				break;
			}
		}
		else
		{
			/* Won't return if successful. */
			ret = ExecKernel(binary);
		}
	}
	
	// chainboot
	if (status==1)
	{
		if (getVideoMode() == GRAPHICS_MODE)
		{	// if we are already in graphics-mode,
			setVideoMode(VGA_TEXT_MODE, 0); // switch back to text mode
		}
	}
	
	if ((gBootFileType == kNetworkDeviceType) && gUnloadPXEOnExit)
	{
		nbpUnloadBaseCode();
	}
}

/*!
    Selects a new BIOS device, taking care to update the global state appropriately.
 */
/*
static void selectBiosDevice(void)
{
    struct DiskBVMap *oldMap = diskResetBootVolumes(gBIOSDev);
    CacheReset();
    diskFreeMap(oldMap);
    oldMap = NULL;

    int dev = selectAlternateBootDevice(gBIOSDev);

    BVRef bvchain = scanBootVolumes(dev, 0);
    BVRef bootVol = selectBootVolume(bvchain);
    gBootVolume = bootVol;
    setRootVolume(bootVol);
    gBIOSDev = dev;
}
*/

#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5000
// NMAX (was 5521) the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1

#define DO1(buf,i)  {s1 += buf[i]; s2 += s1;}
#define DO2(buf,i)  DO1(buf,i); DO1(buf,i+1);
#define DO4(buf,i)  DO2(buf,i); DO2(buf,i+2);
#define DO8(buf,i)  DO4(buf,i); DO4(buf,i+4);
#define DO16(buf)   DO8(buf,0); DO8(buf,8);

unsigned long Adler32(unsigned char *buf, long len)
{
    unsigned long s1 = 1; // adler & 0xffff;
    unsigned long s2 = 0; // (adler >> 16) & 0xffff;
    unsigned long result;
    int k;

    while (len > 0) {
        k = len < NMAX ? len : NMAX;
        len -= k;
        while (k >= 16) {
            DO16(buf);
            buf += 16;
            k -= 16;
        }
        if (k != 0) do {
            s1 += *buf++;
            s2 += s1;
        } while (--k);
        s1 %= BASE;
        s2 %= BASE;
    }
    result = (s2 << 16) | s1;
    return OSSwapHostToBigInt32(result);
}