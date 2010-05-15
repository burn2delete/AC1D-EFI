/*
 * Copyright (c) 2009 Evan Lojewski. All rights reserved.
 *
 */

#include "libsaio.h"
#include "kernel_patcher.h"

#define NUM_SYMBOLS		3

#define SYMBOL_CPUID_SET_INFO				0
#define SYMBOL_PANIC						1
#define SYMBOL_PMCPUEXITHALTTOOFF			2

#define SYMBOL_CPUID_SET_INFO_STRING		"_cpuid_set_info"
#define SYMBOL_PANIC_STRING					"_panic"
#define SYMBOL_PMCPUEXITHALTTOOFF_STRING	"_pmCPUExitHaltToOff"

char* kernelSymbols[NUM_SYMBOLS] = {
	SYMBOL_CPUID_SET_INFO_STRING,
	SYMBOL_PANIC_STRING,
	SYMBOL_PMCPUEXITHALTTOOFF_STRING
};

UInt32 kernelSymbolAddresses[NUM_SYMBOLS] = {
	0,
	0,
	0
};


UInt32 textSection = 0;
UInt32 textAddress = 0;


extern unsigned long gBinaryAddress;


void patch_kernel(void* kernelData)
{
	switch (locate_symbols((void*)kernelData)) {
		case KERNEL_32:
			patch_kernel_32((void*)kernelData);
			break;
			
		case KERNEL_64:
		default:
			patch_kernel_64((void*)kernelData);
			break;
	}
}

// patches a 64bit kernel.
void patch_kernel_64(void* kernelData)
{
	//  At the moment, the kernel patching code fails when used
	// in 64bit mode, so we don't patch it. This is due to 32bit vs 64bit
	// pointers as well as changes in structure sizes
	printf("Unable to patch 64bit kernel. Please use arch=i386.\n");
}


/**
 ** patch_kernel_32
 **		Due to the way the _cpuid_set_info function is writtin, the first instance of _panic is called
 **			when an unsupported (read: non apple used cpu) is found. This routine locates that first _panic call
 **			and replaces the jump call (0xe8) with no ops (0x90).
 **/
void patch_kernel_32(void* kernelData)
{
	//patch_pmCPUExitHaltToOff(kernelData);	// Not working as intended, disabled for now
	patch_cpuid_set_info(kernelData);

}


/**
 **		This functions located the following in the mach_kernel symbol table
 **			_panic
 **			_cpuid_set_info
 **/
int locate_symbols(void* kernelData)
{
	UInt16 symbolIndexes[NUM_SYMBOLS];

	struct load_command *loadCommand;
	struct symtab_command *symtableData;
	struct nlist *symbolEntry;
	
	char* symbolString;

	UInt32 kernelIndex = 0;
	kernelIndex += sizeof(struct mach_header);
	
	if(((struct mach_header*)kernelData)->magic != MH_MAGIC) return KERNEL_64;
	
	
	//printf("%d load commands beginning at 0x%X\n", (unsigned int)header->ncmds, (unsigned int)kernelIndex);
	//printf("Commands take up %d bytes\n", header->sizeofcmds);
	
	
	int cmd = 0;
	while(cmd < ((struct mach_header*)kernelData)->ncmds)	// TODO: for loop instead
	{
		cmd++;
		
		loadCommand = kernelData + kernelIndex;
		
		UInt cmdSize = loadCommand->cmdsize;
		
		
		// Locate start of _panic and _cpuid_set_info in the symbol tabe.
		// Load commands should be anded with 0x7FFFFFFF to ignore the	LC_REQ_DYLD flag
		if((loadCommand->cmd & 0x7FFFFFFF) == LC_SYMTAB)		// We only care about the symtab segment
		{
			//printf("Located symtable, length is 0x%X, 0x%X\n", (unsigned int)loadCommand->cmdsize, (unsigned int)sizeof(symtableData));
			
			symtableData = kernelData + kernelIndex;
			kernelIndex += sizeof(struct symtab_command);
			
			cmdSize -= sizeof(struct symtab_command);

			// Loop through symbol table untill all of the symbols have been found
			
			symbolString = kernelData + symtableData->stroff; 
			
			
			UInt16 symbolIndex = 0;
			UInt8 numSymbolsFound = 0;

			while(symbolIndex < symtableData->nsyms && numSymbolsFound < NUM_SYMBOLS)	// TODO: for loop
			{
				int i = 0;
				while(i < NUM_SYMBOLS)
				{
					if(strcmp(symbolString, kernelSymbols[i]) == 0)
					{
						symbolIndexes[i] = symbolIndex;
						numSymbolsFound++;				
					} 
					i++;
					
				}
				symbolString += strlen(symbolString) + 1;
				symbolIndex++;
			}
			
			// loop again
			symbolIndex = 0;
			numSymbolsFound = 0;
			while(symbolIndex < symtableData->nsyms && numSymbolsFound < NUM_SYMBOLS)	// TODO: for loop
			{
				
				symbolEntry = kernelData + symtableData->symoff + (symbolIndex * sizeof(struct nlist));
				
				int i = 0;
				while(i < NUM_SYMBOLS)
				{
					if(symbolIndex == (symbolIndexes[i] - 4))
					{
						kernelSymbolAddresses[i] = (UInt32)symbolEntry->n_value;
						numSymbolsFound++;				
					} 
					i++;
					
				}
				
				symbolIndex ++;
			}			
		// Load commands should be anded with 0x7FFFFFFF to ignore the	LC_REQ_DYLD flag
		} else if((loadCommand->cmd & 0x7FFFFFFF) == LC_SEGMENT)		// We only care about the __TEXT segment, any other load command can be ignored
		{
			
			struct segment_command *segCommand;
			
			segCommand = kernelData + kernelIndex;
			
			//printf("Segment name is %s\n", segCommand->segname);
			
			if(strcmp("__TEXT", segCommand->segname) == 0)
			{
				UInt32 sectionIndex;
				
				sectionIndex = sizeof(struct segment_command);
				
				struct section *sect;
				
				while(sectionIndex < segCommand->cmdsize)
				{
					sect = kernelData + kernelIndex + sectionIndex;
					
					sectionIndex += sizeof(struct section);
					
					
					if(strcmp("__text", sect->sectname) == 0)
					{
						// __TEXT,__text found, save the offset and address for when looking for the calls.
						textSection = sect->offset;
						textAddress = sect->addr;
						break;
					}					
				}
			}
			
			
			kernelIndex += cmdSize;
		} else {
			kernelIndex += cmdSize;
		}
	}
	
	return KERNEL_32;
}


/**
 ** Locate the fisrt instance of _panic inside of _cpuid_set_info, and remove it
 **/
void patch_cpuid_set_info(void* kernelData)
{
	UInt8* bytes = (UInt8*)kernelData;
	UInt32 patchLocation = (kernelSymbolAddresses[SYMBOL_CPUID_SET_INFO] - textAddress + textSection);
	UInt32 jumpLocation = 0;
	UInt32 panidAddr = kernelSymbolAddresses[SYMBOL_PANIC] - textAddress;
	if(kernelSymbolAddresses[SYMBOL_CPUID_SET_INFO] == 0)
	{
		printf("Unable to locate _cpuid_set_info\n");
		return;
		
	}
	if(kernelSymbolAddresses[SYMBOL_PANIC] == 0)
	{
		printf("Unable to locate _panic\n");
		return;
	}
	
	//TODO: don't assume it'll always work (Look for *next* function address in symtab and fail once it's been reached)
	while(  
		  (bytes[patchLocation -1] != 0xE8) ||
		  ( ( (UInt32)(panidAddr - patchLocation  - 4) + textSection ) != (UInt32)((bytes[patchLocation + 0] << 0  | 
																					bytes[patchLocation + 1] << 8  | 
																					bytes[patchLocation + 2] << 16 |
																					bytes[patchLocation + 3] << 24)))
		  )
	{
		patchLocation++;
	}
	patchLocation--;
	
/*
 // repace with nops to disable the panic call
	Old patch, nolonger used. This is a safer patch than before, however
	the new patch allows the native pm kext to load without any extra kexts
	bytes[patchLocation] = 0x90;
	bytes[patchLocation + 1] = 0x90;
	bytes[patchLocation + 2] = 0x90;
	bytes[patchLocation + 3] = 0x90;
	bytes[patchLocation + 4] = 0x90;
*/	
	
	// Locate a JMP to patchLocation - sizeof(mov) + 2 (aka 8)
	// ... NOTE: can *ONLY* be up to 0xFF - 8 bytes aways
	jumpLocation = patchLocation - 15;
	while((bytes[jumpLocation - 1] != 0x77 ||
		  bytes[jumpLocation] != (patchLocation - jumpLocation - -8)) &&
		  (patchLocation - jumpLocation) < 0xEF)
	{
		jumpLocation--;
	}
	
	// If found... (not the end of the world if it isn't found, the panic is removed either way.
	// But if it isn't found, then IntelCPUPM.kext might panic if an unknown cpu (atom)
	// Jump to patchLocation - 17
	if((patchLocation - jumpLocation) < 0xEF) bytes[jumpLocation] -= 10; // sizeof(movl	$0x6b5a4cd2,0x00872eb4)
	
	
	// bytes[patchLocation - 17] = 0xC7;	// already here... not needed to be done
	// bytes[patchLocation - 16] = 0x05;	// see above
	UInt32 cpuid_cpufamily_addr =	bytes[patchLocation - 15] << 0  |
									bytes[patchLocation - 14] << 8  |
									bytes[patchLocation - 13] << 16 |
									bytes[patchLocation - 12] << 24;
	
	// NOTE: may change, determined based on cpuid_info struct
	UInt32 cpuid_model_addr = cpuid_cpufamily_addr - 299; 
	
	
	// cpufamily = CPUFAMILY_INTEL_PENRYN
	bytes[patchLocation - 11] = (CPUFAMILY_INTEL_PENRYN & 0x000000FF) >> 0;
	bytes[patchLocation - 10] = (CPUFAMILY_INTEL_PENRYN & 0x0000FF00) >> 8;
	bytes[patchLocation -  9] = (CPUFAMILY_INTEL_PENRYN & 0x00FF0000) >> 16;	
	bytes[patchLocation -  8] = (CPUFAMILY_INTEL_PENRYN & 0xFF000000) >> 24;

	// NOPS, just in case if the jmp call wasn't patched, we'll jump to a
	// nop and continue with the rest of the patch
	// Yay two free bytes :), 10 more can be reclamed if needed, as well as a few
	// from the above code (only cpuid_model needs to be set.
	bytes[patchLocation - 7] = 0x90;
	bytes[patchLocation - 6] = 0x90;

	bytes[patchLocation - 5] = 0xC7;
	bytes[patchLocation - 4] = 0x05;
	bytes[patchLocation - 3] = (cpuid_model_addr & 0x000000FF) >> 0;
	bytes[patchLocation - 2] = (cpuid_model_addr & 0x0000FF00) >> 8;	
	bytes[patchLocation - 1] = (cpuid_model_addr & 0x00FF0000) >> 16;
	bytes[patchLocation - 0] = (cpuid_model_addr & 0xFF000000) >> 24;
	
	// Note: I could have just copied the 8bit cpuid_model in and saved about 4 bytes
	// so if this function need a different patch it's still possible. Also, about ten bytes previous can be freed.
	bytes[patchLocation + 1] = 0x17;	// cpuid_model
	bytes[patchLocation + 2] = 0x01;	// cpuid_extmodel
	bytes[patchLocation + 3] = 0x00;	// cpuid_extfamily
	bytes[patchLocation + 4] = 0x02;	// cpuid_stepping
	
}


/**
 ** SleepEnabler.kext replacement (for those that need it)
 ** Located the KERN_INVALID_ARGUMENT return and replace it with KERN_SUCCESS
 **/
void patch_pmCPUExitHaltToOff(void* kernelData)
{
	UInt8* bytes = (UInt8*)kernelData;
	UInt32 patchLocation = (kernelSymbolAddresses[SYMBOL_PMCPUEXITHALTTOOFF] - textAddress + textSection);

	if(kernelSymbolAddresses[SYMBOL_PMCPUEXITHALTTOOFF] == 0)
	{
		printf("Unable to locate _pmCPUExitHaltToOff\n");
		return;
	}
	
	while(bytes[patchLocation - 1]	!= 0xB8 ||
		  bytes[patchLocation]		!= 0x04 ||	// KERN_INVALID_ARGUMENT (0x00000004)
		  bytes[patchLocation + 1]	!= 0x00 ||	// KERN_INVALID_ARGUMENT
		  bytes[patchLocation + 2]	!= 0x00 ||	// KERN_INVALID_ARGUMENT
		  bytes[patchLocation + 3]	!= 0x00)	// KERN_INVALID_ARGUMENT

	{
		patchLocation++;
	}
	bytes[patchLocation] = 0x00;	// KERN_SUCCESS;
}