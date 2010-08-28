	/*
 * Copyright (c) 2009 Evan Lojewski. All rights reserved.
 *
 */

#include "libsaio.h"
#include "kernel_patcher.h"
#include "platform.h"
#include "modules.h"
extern PlatformInfo_t    Platform;

patchRoutine_t* patches = NULL;
kernSymbols_t* kernelSymbols = NULL;


UInt32 textSection = 0;
UInt32 textAddress = 0;


void KernelPatcher_start()
{
	register_kernel_patch(patch_cpuid_set_info, KERNEL_32, CPUID_MODEL_ATOM);		// TODO: CPUFAMILY_INTEL_PENRYN, CPUID_MODEL_PENRYN
	register_kernel_patch(patch_cpuid_set_info, KERNEL_32, CPUID_MODEL_UNKNOWN);	// 0, 0 
	
	register_kernel_patch(patch_commpage_stuff_routine, KERNEL_32, CPUID_MODEL_ANY);
	
	register_kernel_patch(patch_lapic_init, KERNEL_32, CPUID_MODEL_ANY);
	
	register_kernel_symbol(KERNEL_32, "_panic");
	register_kernel_symbol(KERNEL_32, "_cpuid_set_info");
	register_kernel_symbol(KERNEL_32, "_pmCPUExitHaltToOff");
	register_kernel_symbol(KERNEL_32, "_lapic_init");
	register_kernel_symbol(KERNEL_32, "_commpage_stuff_routine");
		
	
	// TODO: register needed symbols
	
	
	register_hook_callback("ExecKernel", &patch_kernel); 
}

/*
 * Register a kerenl patch
 */
void register_kernel_patch(void* patch, int arch, int cpus)
{
	// TODO: only insert valid patches based on current cpuid and architecture
	// AKA, don't at 64bit patches if it's a 32bit only machine
	patchRoutine_t* entry;
	
	// TODO: verify Platform.CPU.Model is populated this early in bootup
	// Check to ensure that the patch is valid on this machine
	// If it is not, exit early form this function
	if(cpus != Platform.CPU.Model)
	{
		if(cpus != CPUID_MODEL_ANY)
		{
			if(cpus == CPUID_MODEL_UNKNOWN)
			{
				switch(Platform.CPU.Model)
				{
					case 13:
					case CPUID_MODEL_YONAH:
					case CPUID_MODEL_MEROM:
					case CPUID_MODEL_PENRYN:
					case CPUID_MODEL_NEHALEM:
					case CPUID_MODEL_FIELDS:
					case CPUID_MODEL_DALES:
					case CPUID_MODEL_NEHALEM_EX:
						// Known cpu's we don't want to add the patch
						return;
						break;

					default:
						// CPU not in supported list, so we are going to add
						// The patch will be applied
						break;
						
				}
			}
			else
			{
				// Invalid cpuid for current cpu. Ignoring patch
				return;
			}

		}
	}
		
	if(patches == NULL)
	{
		patches = entry = malloc(sizeof(patchRoutine_t));
	}
	else
	{
		entry = patches;
		while(entry->next)
		{
			entry = entry->next;
		}
		
		entry->next = malloc(sizeof(patchRoutine_t));
		entry = entry->next;
	}
	
	entry->next = NULL;
	entry->patchRoutine = patch;
	entry->validArchs = arch;
	entry->validCpu = cpus;
}

void register_kernel_symbol(int kernelType, const char* name)
{
	if(kernelSymbols == NULL)
	{
		kernelSymbols = malloc(sizeof(kernSymbols_t));
		kernelSymbols->next = NULL;
		kernelSymbols->symbol = (char*)name;
		kernelSymbols->addr = 0;
	}
	else {
		kernSymbols_t *symbol = kernelSymbols;
		while(symbol->next != NULL)
		{
			symbol = symbol->next;
		}
		
		symbol->next = malloc(sizeof(kernSymbols_t));
		symbol = symbol->next;

		symbol->next = NULL;
		symbol->symbol = (char*)name;
		symbol->addr = 0;
	}
}

kernSymbols_t* lookup_kernel_symbol(const char* name)
{
	if(kernelSymbols == NULL)
	{
		return NULL;
	}
	kernSymbols_t *symbol = kernelSymbols;


	while(symbol && strcmp(symbol->symbol, name) !=0)
	{
		symbol = symbol->next;
	}
	
	if(!symbol)
	{
		return NULL;
	}
	else
	{
		return symbol;
	}

}

void patch_kernel(void* kernelData, void* arg2, void* arg3, void *arg4)
{
	patchRoutine_t* entry = patches;
	
	printf("Patching kernel located at 0x%X\n", kernelData);
	locate_symbols(kernelData);
	printf("Symbols located\n", kernelData);
	getc();
	
	int arch = determineKernelArchitecture(kernelData);
	
	// TODO:locate all symbols
	
	
	if(patches != NULL)
	{
		while(entry->next)
		{
			if(entry->validArchs == KERNEL_ANY || arch == entry->validArchs)
			{
				entry->patchRoutine(kernelData);
			}
			entry = entry->next;
		}
		
	}
}

int determineKernelArchitecture(void* kernelData)
{	
	if(((struct mach_header*)kernelData)->magic == MH_MAGIC)
	{
		return KERNEL_32;
	}
	if(((struct mach_header*)kernelData)->magic == MH_MAGIC_64)
	{
		return KERNEL_64;
	}
	else
	{
		return KERNEL_ERR;
	}
}


/**
 **		This functions located the requested symbols in the mach-o file.
 **			as well as determines the start of the __TEXT segment and __TEXT,__text sections
 **/
int locate_symbols(void* kernelData)
{

	struct load_command *loadCommand;
	struct symtab_command *symtableData;
	//	struct nlist *symbolEntry;
	
	char* symbolString;

	UInt32 kernelIndex = 0;
	kernelIndex += sizeof(struct mach_header);
	
	if(((struct mach_header*)kernelData)->magic != MH_MAGIC) return KERNEL_64;
	
	
	int cmd = 0;
	while(cmd < ((struct mach_header*)kernelData)->ncmds)	// TODO: for loop instead
	{
		cmd++;
		
		loadCommand = kernelData + kernelIndex;
		
		UInt cmdSize = loadCommand->cmdsize;
		
		
		if((loadCommand->cmd & 0x7FFFFFFF) == LC_SYMTAB)		// We only care about the symtab segment
		{
			//printf("Located symtable, length is 0x%X, 0x%X\n", (unsigned int)loadCommand->cmdsize, (unsigned int)sizeof(symtableData));
			
			symtableData = kernelData + kernelIndex;
			kernelIndex += sizeof(struct symtab_command);
		
			symbolString = kernelData + symtableData->stroff;
		}
		else if((loadCommand->cmd & 0x7FFFFFFF) == LC_SEGMENT)		// We only care about the __TEXT segment, any other load command can be ignored
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
	
	printf("Parseing symtabl.\n");
	handle_symtable((UInt32)kernelData, symtableData, &symbol_handler);
	getc();
}

void* symbol_handler(char* symbolName, void* addr)
{
	// Locate the symbol in the list, if it exists, update it's address
	kernSymbols_t *symbol = lookup_kernel_symbol(symbolName);
	
	
	if(symbol)
	{
		printf("Located registered symbol %s at 0x%X\n", symbolName, addr);
		getc();
		symbol->addr = (UInt32)addr;
	}
	return (void*)0xFFFFFFFF;
}


/**
 ** Locate the fisrt instance of _panic inside of _cpuid_set_info, and either remove it
 ** Or replace it so that the cpuid is set to a valid value.
 **/
void patch_cpuid_set_info(void* kernelData/*, UInt32 impersonateFamily, UInt8 impersonateModel*/)
{
	printf("patch_cpuid_set_info\n");
	getc();
	UInt32 impersonateFamily = 0;
	UInt8 impersonateModel = 0;
	
	UInt8* bytes = (UInt8*)kernelData;
	
	kernSymbols_t *symbol = lookup_kernel_symbol("_cpuid_set_info");
	UInt32 patchLocation = symbol ? symbol->addr : 0; //	(kernelSymbolAddresses[SYMBOL_CPUID_SET_INFO] - textAddress + textSection);
	
	UInt32 jumpLocation = 0;
	
	symbol = lookup_kernel_symbol("_panic");
	UInt32 panicAddr = symbol ? symbol->addr : 0; //kernelSymbolAddresses[SYMBOL_PANIC] - textAddress;
	if(patchLocation == 0)
	{
		printf("Unable to locate _cpuid_set_info\n");
		return;
		
	}
	if(panicAddr == 0)
	{
		printf("Unable to locate _panic\n");
		return;
	}
	
	//TODO: don't assume it'll always work (Look for *next* function address in symtab and fail once it's been reached)
	while(  
		  (bytes[patchLocation -1] != 0xE8) ||
		  ( ( (UInt32)(panicAddr - patchLocation  - 4) + textSection ) != (UInt32)((bytes[patchLocation + 0] << 0  | 
																					bytes[patchLocation + 1] << 8  | 
																					bytes[patchLocation + 2] << 16 |
																					bytes[patchLocation + 3] << 24)))
		  )
	{
		patchLocation++;
	}
	patchLocation--;
	
	
	// Remove panic call, just in case the following patch routines fail
	bytes[patchLocation + 0] = 0x90;
	bytes[patchLocation + 1] = 0x90;
	bytes[patchLocation + 2] = 0x90;
	bytes[patchLocation + 3] = 0x90;
	bytes[patchLocation + 4] = 0x90;
	
	
	// Locate the jump call, so that 10 bytes can be reclamed.
	// NOTE: This will *NOT* be located on pre 10.6.2 kernels
	jumpLocation = patchLocation - 15;
	while((bytes[jumpLocation - 1] != 0x77 ||
		   bytes[jumpLocation] != (patchLocation - jumpLocation - -8)) &&
		  (patchLocation - jumpLocation) < 0xF0)
	{
		jumpLocation--;
	}
	
	// If found... AND we want to impersonate a specific cpumodel / family...
	if(impersonateFamily &&
	   impersonateModel  &&
	   ((patchLocation - jumpLocation) < 0xF0))
	{
		
		bytes[jumpLocation] -= 10;		// sizeof(movl	$0x6b5a4cd2,0x00872eb4) = 10bytes
		
		/* 
		 * Inpersonate the specified CPU FAMILY and CPU Model
		 */

		// bytes[patchLocation - 17] = 0xC7;	// already here... not needed to be done
		// bytes[patchLocation - 16] = 0x05;	// see above
		UInt32 cpuid_cpufamily_addr =	bytes[patchLocation - 15] << 0  |
										bytes[patchLocation - 14] << 8  |
										bytes[patchLocation - 13] << 16 |
										bytes[patchLocation - 12] << 24;
		
		// NOTE: may change, determined based on cpuid_info struct
		UInt32 cpuid_model_addr = cpuid_cpufamily_addr - 299; 
		
		
		// cpufamily = CPUFAMILY_INTEL_PENRYN
		bytes[patchLocation - 11] = (impersonateFamily & 0x000000FF) >> 0;
		bytes[patchLocation - 10] = (impersonateFamily & 0x0000FF00) >> 8;
		bytes[patchLocation -  9] = (impersonateFamily & 0x00FF0000) >> 16;	
		bytes[patchLocation -  8] = (impersonateFamily & 0xFF000000) >> 24;
		
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
		bytes[patchLocation + 1] = impersonateModel;	// cpuid_model
		bytes[patchLocation + 2] = 0x01;	// cpuid_extmodel
		bytes[patchLocation + 3] = 0x00;	// cpuid_extfamily
		bytes[patchLocation + 4] = 0x02;	// cpuid_stepping
		
	}
	else if(impersonateFamily && impersonateModel)
	{
		// pre 10.6.2 kernel
		// Locate the jump to directly *after* the panic call,
		jumpLocation = patchLocation - 4;
		while((bytes[jumpLocation - 1] != 0x77 ||
			   bytes[jumpLocation] != (patchLocation - jumpLocation + 4)) &&
			  (patchLocation - jumpLocation) < 0x20)
		{
			jumpLocation--;
		}
		// NOTE above isn't needed (I was going to use it, but I'm not, so instead,
		// I'll just leave it to verify the binary stucture.
		
		// NOTE: the cpumodel_familt data is not set in _cpuid_set_info
		// so we don't need to set it here, I'll get set later based on the model
		// we set now.
		
		if((patchLocation - jumpLocation) < 0x20)
		{
			UInt32 cpuid_model_addr =	(bytes[patchLocation - 14] << 0  |
											bytes[patchLocation - 13] << 8  |
											bytes[patchLocation - 12] << 16 |
											bytes[patchLocation - 11] << 24);
			// Remove jump
			bytes[patchLocation - 9] = 0x90;		///  Was a jump if supported cpu
			bytes[patchLocation - 8] = 0x90;		// jumped past the panic call, we want to override the panic

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
			bytes[patchLocation + 1] = impersonateModel;	// cpuid_model
			bytes[patchLocation + 2] = 0x01;	// cpuid_extmodel
			bytes[patchLocation + 3] = 0x00;	// cpuid_extfamily
			bytes[patchLocation + 4] = 0x02;	// cpuid_stepping
			
			
			
			patchLocation = jumpLocation;
			// We now have 14 bytes available for a patch
			
		}
		else 
		{
			// Patching failed, using NOP replacement done initialy
		}
	}
	else 
	{
		// Either We were unable to change the jump call due to the function's sctructure
		// changing, or the user did not request a patch. As such, resort to just 
		// removing the panic call (using NOP replacement above). Note that the
		// IntelCPUPM kext may still panic due to the cpu's Model ID not being patched
	}
}


/**
 ** SleepEnabler.kext replacement (for those that need it)
 ** Located the KERN_INVALID_ARGUMENT return and replace it with KERN_SUCCESS
 **/
void patch_pmCPUExitHaltToOff(void* kernelData)
{
	printf("patch_pmCPUExitHaltToOff\n");
	getc();
	UInt8* bytes = (UInt8*)kernelData;

	kernSymbols_t *symbol = lookup_kernel_symbol("_PmCpuExitHaltToOff");
	UInt32 patchLocation = symbol ? symbol->addr : 0; //(kernelSymbolAddresses[SYMBOL_PMCPUEXITHALTTOOFF] - textAddress + textSection);

	if(patchLocation == 0)
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

void patch_lapic_init(void* kernelData)
{
	printf("patch_lapic_init\n");
	getc();

	UInt8 panicIndex = 0;
	UInt8* bytes = (UInt8*)kernelData;
	kernSymbols_t *symbol = lookup_kernel_symbol("_lapic_init");
	UInt32 patchLocation = symbol ? symbol->addr : 0; 
	
	// (kernelSymbolAddresses[SYMBOL_LAPIC_INIT] - textAddress + textSection);
	// kernelSymbolAddresses[SYMBOL_PANIC] - textAddress;
	
	symbol = lookup_kernel_symbol("_panic");
	UInt32 panicAddr = symbol ? symbol->addr : 0; 

	if(patchLocation == 0)
	{
		printf("Unable to locate %s\n", "_lapic_init");
		return;
		
	}
	if(panicAddr == 0)
	{
		printf("Unable to locate %s\n", "_panic");
		return;
	}
	
	
	
	// Locate the (panicIndex + 1) panic call
	while(panicIndex < 3)	// Find the third panic call
	{
		while(  
			  (bytes[patchLocation -1] != 0xE8) ||
			  ( ( (UInt32)(panicAddr - patchLocation  - 4) + textSection ) != (UInt32)((bytes[patchLocation + 0] << 0  | 
																						bytes[patchLocation + 1] << 8  | 
																						bytes[patchLocation + 2] << 16 |
																						bytes[patchLocation + 3] << 24)))
			  )
		{
			patchLocation++;
		}
		patchLocation++;
		panicIndex++;
	}
	patchLocation--;	// Remove extra increment from the < 3 while loop
	
	bytes[--patchLocation] = 0x90;	
	bytes[++patchLocation] = 0x90;
	bytes[++patchLocation] = 0x90;
	bytes[++patchLocation] = 0x90;
	bytes[++patchLocation] = 0x90;
	
	
}


void patch_commpage_stuff_routine(void* kernelData)
{
	printf("patch_commpage_stuff_routine\n");
	getc();

	UInt8* bytes = (UInt8*)kernelData;
	kernSymbols_t *symbol = lookup_kernel_symbol("_commpage_stuff_routine");
	UInt32 patchLocation = symbol ? symbol->addr : 0; 

	
	// (kernelSymbolAddresses[SYMBOL_COMMPAGE_STUFF_ROUTINE] - textAddress + textSection);
	// kernelSymbolAddresses[SYMBOL_PANIC] - textAddress;
	
	symbol = lookup_kernel_symbol("_panic");
	UInt32 panicAddr = symbol ? symbol->addr : 0; 

	//if(kernelSymbolAddresses[SYMBOL_COMMPAGE_STUFF_ROUTINE] == 0)
	{
		//	printf("Unable to locate %s\n", SYMBOL_COMMPAGE_STUFF_ROUTINE_STRING);
		return;
		
	}
	//if(kernelSymbolAddresses[SYMBOL_PANIC] == 0)
	{
		//	printf("Unable to locate %s\n", SYMBOL_PANIC_STRING);
		return;
	}
	
	
	while(  
		  (bytes[patchLocation -1] != 0xE8) ||
		  ( ( (UInt32)(panicAddr - patchLocation  - 4) + textSection ) != (UInt32)((bytes[patchLocation + 0] << 0  | 
																					bytes[patchLocation + 1] << 8  | 
																					bytes[patchLocation + 2] << 16 |
																					bytes[patchLocation + 3] << 24)))
		  )
	{
		patchLocation++;
	}
	patchLocation--;
	
	
	// Remove panic call, just in case the following patch routines fail
	bytes[patchLocation + 0] = 0x90;
	bytes[patchLocation + 1] = 0x90;
	bytes[patchLocation + 2] = 0x90;
	bytes[patchLocation + 3] = 0x90;
	bytes[patchLocation + 4] = 0x90;
	
	
}
