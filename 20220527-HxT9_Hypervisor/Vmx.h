#pragma once
#include "VmmContext.h"

/*
 * Enter VMX Root Mode on the processor.
 *
 * This function will:
 *	- Enable VMX-enabled bit in CR4
 *	- Ensure the VMX fixed bits are set in CR0 and CR4
 *	- Turn on VMX with VMXON instruction
 *	- Clear the VMCS with VMXCLEAR instruction
 *	- Load the VMCS pointer with VMXPTRLD
 */
BOOL VmxEnterRootMode(PVMM_PROCESSOR_CONTEXT Context);

/*
 * Exits VMX Root Mode on a processor currently in VMX operation mode.
 *
 * This function will:
 *	- Clear the current VMCS
 *	- Execute VMXOFF
 *	- Unset the VMX enabled bit in CR4
 */
BOOL VmxExitRootMode(PVMM_PROCESSOR_CONTEXT Context);

/*
 * Execute VMLAUNCH and launch the processor.
 * 
 * Should never return, except on error.
 */
BOOL VmxLaunchProcessor(PVMM_PROCESSOR_CONTEXT Context);