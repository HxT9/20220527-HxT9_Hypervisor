#include "Vmm.h"
#include "Logger.h"
#include "ia-32/ia32.h"
#include "ArchIntel.h"
#include "Utilities.h"
#include "WindowsNT.h"
#include "Vmx.h"
#include "Vmcs.h"
#include "Exit.h"
#include "Ept.h"
#include "EptHook.h"

/*
 * Defined in VmxDefs.asm.
 *
 * Saves register contexts and calls InitialzeLogicalProcessor
 */
extern BOOL BeginInitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT Context);

/*
 * Defined in vmxdefs.asm.
 *
 * Saves register contexts and calls HandleVmExit
 */
extern VOID EnterFromGuest();

BYTE IsVmxSupported() {
    SIZE_T FeatureMSR;

    // Check if VMX support is enabled on the processor.
    if (!ArchIsVMXAvailable())
    {
        HxTLog("Vmx is not a feture of this processor.\n");
        return -1;
    }

    // Enable bits in MSR to enable VMXON instruction.
    FeatureMSR = ArchGetHostMSR(IA32_FEATURE_CONTROL);

    // The BIOS will lock the VMX bit on startup.
    if (!BitIsSet(FeatureMSR, IA32_FEATURE_CONTROL_LOCK_BIT_BIT))
    {
        HxTLog("Vmx support was not locked by BIOS.\n");
        return -2;
    }

    // Check disable outside smx bit is not set
    if (!BitIsSet(FeatureMSR, IA32_FEATURE_CONTROL_ENABLE_VMX_OUTSIDE_SMX_BIT))
    {
        HxTLog("VMX support was disabled outside of SMX operation by BIOS.\n");
        return -2;
    }

    HxTLog("Vmx is supported \n");
    return 1;
}

VMXON* AllocateVmxonRegion(PVMM_CONTEXT Context) {
    VMXON* VmxonRegion;

    VmxonRegion = (VMXON*)OsAllocateContiguousAlignedPages(1);
    if (!VmxonRegion) {
        HxTLog("OsAllocateContiguousAlignedPages: Out of memory!\n");
        return NULL;
    }

    VmxonRegion->RevisionId = (UINT32)Context->VmxCapabilities.VmcsRevisionId;

    return VmxonRegion;
}

VMCS* AllocateVmcsRegion(PVMM_CONTEXT Context) {
    VMCS* VmcsRegion;

    VmcsRegion = (VMXON*)OsAllocateContiguousAlignedPages(1);
    if (!VmcsRegion) {
        HxTLog("OsAllocateContiguousAlignedPages: Out of memory!\n");
        return NULL;
    }
    VmcsRegion->RevisionId = (UINT32)Context->VmxCapabilities.VmcsRevisionId;

    return VmcsRegion;
}

PVMM_PROCESSOR_CONTEXT AllocateCPUContext(PVMM_CONTEXT Context) {
    PVMM_PROCESSOR_CONTEXT CpuContext;

    // Allocate some generic memory for our context
    CpuContext = (PVMM_PROCESSOR_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMM_PROCESSOR_CONTEXT));
    if (!CpuContext) {
        HxTLog("OsAllocateNonpagedMemory: Out of memory!\n");
        return NULL;
    }
    OsZeroMemory(CpuContext, sizeof(VMM_PROCESSOR_CONTEXT));

    CpuContext->GlobalContext = Context;

    // Top of the host stack is the global context pointer.
    // See: vmxdefs.h and the structure definition.
    CpuContext->HostStack.GlobalContext = Context;

    // Allocate and setup the VMXON region for this processor
    CpuContext->VmxonRegion = AllocateVmxonRegion(Context);
    if (!CpuContext->VmxonRegion) {
        HxTLog("Error allocating Vmxon Region\n");
        return NULL;
    }
    CpuContext->VmxonRegionPhysical = OsVirtualToPhysical(CpuContext->VmxonRegion);

    // Allocate and setup a blank VMCS region
    CpuContext->VmcsRegion = AllocateVmcsRegion(Context);
    if (!CpuContext->VmcsRegion) {
        HxTLog("Error allocating Vmcs Region\n");
        return NULL;
    }
    CpuContext->VmcsRegionPhysical = OsVirtualToPhysical(CpuContext->VmcsRegion);

    
    // Allocate one page for MSR bitmap, all zeroes because we are not exiting on any MSRs.
    CpuContext->MsrBitmap = OsAllocateContiguousAlignedPages(1);
    if (!CpuContext->MsrBitmap) {
        HxTLog("Error allocating MsrBitmap Region\n");
        return NULL;
    }
    OsZeroMemory(CpuContext->MsrBitmap, PAGE_SIZE);
    CpuContext->MsrBitmapPhysical = OsVirtualToPhysical(CpuContext->MsrBitmap);

    return CpuContext;
}

PVMM_CONTEXT AllocateVmmContext() {
    PVMM_CONTEXT Context;

    // Allocate the global context structure
    Context = (PVMM_CONTEXT)OsAllocateNonpagedMemory(sizeof(VMM_CONTEXT));
    if (!Context) {
        HxTLog("OsAllocateNonpagedMemory: Out of memory!\n");
        return NULL;
    }
    OsZeroMemory(Context, sizeof(VMM_CONTEXT));

    // Get count of all logical processors on the system
    Context->ProcessorCount = OsGetCPUCount();
    HxTLog("Total Processor Count: %i\n", Context->ProcessorCount);

    // Save SYSTEM process DTB
    Context->SystemDirectoryTableBase = __readcr3();

    // Get capability MSRs and add them to the global context.
    Context->VmxCapabilities = ArchGetBasicVmxCapabilities();
    HxTLog("VmcsRevisionNumber: %x\n", Context->VmxCapabilities.VmcsRevisionId);

    // Allocate a logical processor context structure for each processor on the system.
    Context->ProcessorContext = OsAllocateNonpagedMemory(Context->ProcessorCount * sizeof(PVMM_PROCESSOR_CONTEXT));
    if (!Context->ProcessorContext) {
        HxTLog("OsAllocateNonpagedMemory: Out of memory!\n");
        FreeVmmContext(Context);
        return NULL;
    }
    for (SIZE_T ProcessorNumber = 0; ProcessorNumber < Context->ProcessorCount; ProcessorNumber++) {
        Context->ProcessorContext[ProcessorNumber] = AllocateCPUContext(Context);
        if (!Context->ProcessorContext[ProcessorNumber]) {
            HxTLog("Failed to setup cpu %d\n", ProcessorNumber);
            FreeVmmContext(Context);
            return NULL;
        }
    }

    return Context;
}

VOID FreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT Context)
{
    if (Context)
    {
        // Free Vmxon region
        if(Context->VmxonRegion)
            OsFreeContiguousAlignedPages(Context->VmxonRegion);

        // Free Vmcs region
        if (Context->VmcsRegion)
            OsFreeContiguousAlignedPages(Context->VmcsRegion);

        // Free Msr bitmap
        if (Context->MsrBitmap)
            OsFreeContiguousAlignedPages(Context->MsrBitmap);

        // Free actual context
        OsFreeNonpagedMemory(Context);
    }
}

VOID FreeVmmEpt(PVMM_CONTEXT Context) {
    EptClearHooks(Context);

    if(Context->EptState->EptPageTable)
        OsFreeContiguousAlignedPages(Context->EptState->EptPageTable);

    if(Context->EptState)
        OsFreeNonpagedMemory(Context->EptState);
}

VOID FreeVmmContext(PVMM_CONTEXT Context) {
    if (Context)
    {
        // Free each logical processor context
        for (SIZE_T ProcessorNumber = 0; ProcessorNumber < Context->ProcessorCount; ProcessorNumber++)
        {
            FreeLogicalProcessorContext(Context->ProcessorContext[ProcessorNumber]);
        }

        // Free the collection of pointers to processor contexts
        OsFreeNonpagedMemory(Context->ProcessorContext);

        // Free Ept
        FreeVmmEpt(Context);

        // Free the actual context
        OsFreeNonpagedMemory(Context);
    }
}

PVMM_PROCESSOR_CONTEXT GetCurrentCPUContext(PVMM_CONTEXT Context)
{
    SIZE_T CurrentProcessorNumber;
    PVMM_PROCESSOR_CONTEXT CurrentContext;

    // Get the current processor we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    // Get the logical processor context that was allocated for this current processor
    CurrentContext = Context->ProcessorContext[CurrentProcessorNumber];

    return CurrentContext;
}

/*
 * Called by InitializeAllProcessors to initialize VMX on a specific logical processor.
 */
VOID NTAPI InitializeProcessors(_In_ struct _KDPC* Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2) {
    SIZE_T CurrentProcessorNumber;
    PVMM_CONTEXT Context;
    PVMM_PROCESSOR_CONTEXT CpuContext;

    UNREFERENCED_PARAMETER(Dpc);

    // Get the global context
    Context = (PVMM_CONTEXT)DeferredContext;

    // Get the current processor number we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    // Get the logical processor context that was allocated for this current processor
    CpuContext = GetCurrentCPUContext(Context);

    if (BeginInitializeLogicalProcessor(CpuContext)) {
        InterlockedIncrement((volatile LONG*)&Context->SuccessfulInitializationsCount);
        CpuContext->Launched = TRUE;
        HxTLog("Processor %d launched.\n", CurrentProcessorNumber);
    }
    else {
        HxTLog("Processor %d failed to launch.\n", CurrentProcessorNumber);
    }

    // These must be called for GenericDpcCall to signal other processors
    // SimpleVisor code shows how to do this

    // Wait for all DPCs to synchronize at this point
    OsSignalCallDpcSynchronize(SystemArgument2);

    // Mark the DPC as being complete
    OsSignalCallDpcDone(SystemArgument1);
}

/**
 * Initialize VMCS and enter VMX root-mode.
 * This function should never return, except on error. Execution will continue on the guest on success.
 *
 * See: BeginInitializeLogicalProcessor and VmxDefs.asm.
 */
VOID InitializeLogicalProcessor(PVMM_PROCESSOR_CONTEXT CpuContext, SIZE_T GuestRSP, SIZE_T GuestRIP)
{
    SIZE_T CurrentProcessorNumber;

    // Get the current processor we're executing this function on right now
    CurrentProcessorNumber = OsGetCurrentProcessorNumber();

    if (!VmxEnterRootMode(CpuContext))
    {
        HxTLog("[#%i]InitializeLogicalProcessor: Failed to enter VMX Root Mode.\n", CurrentProcessorNumber);
        return;
    }

    // Setup VMCS with all values necessary to begin VMXLAUNCH
    // &Context->HostStack.GlobalContext is also the top of the host stack
    if (!SetupVmcsDefaults(CpuContext, (SIZE_T)&EnterFromGuest, (SIZE_T)&CpuContext->HostStack.GlobalContext, GuestRIP, GuestRSP))
    {
        HxTLog("[#%i]InitializeLogicalProcessor: Failed to setup VMCS.\n", CurrentProcessorNumber);
        VmxExitRootMode(CpuContext);
        return;
    }

    MemoryMapperInitialize(CpuContext);

    // Launch the hypervisor! This function should not return if it is successful, as we continue execution
    // on the guest.
    if (!VmxLaunchProcessor(CpuContext))
    {
        HxTLog("InitializeLogicalProcessor[#%i]: Failed to VmxLaunchProcessor.\n", CurrentProcessorNumber);
        return;
    }
}

PVMM_CONTEXT InitializeVmx() {
    PVMM_CONTEXT Context;

    HxTLog("InitializeVmx: Starting.\n");
    
    // Pre-allocate all logical processor contexts, VMXON regions, VMCS regions
    Context = AllocateVmmContext();
    
    if (!Context)
    {
        return NULL;
    }

    if (!EptInitialize(Context))
    {
        HxTLog("Couldn't initialize EPT\n");
        FreeVmmContext(Context);
        return NULL;
    }

    // Generates a DPC that makes all processors execute the broadcast function.
    OsGenericCallDPC(InitializeProcessors, (PVOID)Context);

    if (Context->SuccessfulInitializationsCount != OsGetCPUCount())
    {
        // TODO: Move to driver uninitalization
        HxTLog("InitializeAllProcessors: Not all processors initialized. [%i successful]\n", Context->SuccessfulInitializationsCount);
        TerminateVmx(Context);
        return NULL;
    }

    return Context;
}

extern void TerminateVmcall();
/**
 * DPC to exit VMX on all processors.
 */
VOID NTAPI ExitRootModeOnAllProcessors(_In_ struct _KDPC* Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2)
{
    PVMM_PROCESSOR_CONTEXT CpuContext;
    PVMM_CONTEXT Context;

    UNREFERENCED_PARAMETER(Dpc);
    Context = (PVMM_CONTEXT)DeferredContext;

    // Get the logical processor context that was allocated for this current processor
    CpuContext = GetCurrentCPUContext(Context);
    
    if (CpuContext->Launched) {
        TerminateVmcall();
    }

    // These must be called for GenericDpcCall to signal other processors
    // SimpleVisor code shows how to do this

    // Wait for all DPCs to synchronize at this point
    OsSignalCallDpcSynchronize(SystemArgument2);

    // Mark the DPC as being complete
    OsSignalCallDpcDone(SystemArgument1);
}

BOOL TerminateVmx(PVMM_CONTEXT Context) {
    if (Context)
    {
        OsGenericCallDPC(ExitRootModeOnAllProcessors, (PVOID)Context);
    }
    FreeVmmContext(Context);

    return TRUE;
}

BOOL HandleVmExit(PVMM_CONTEXT Context, PGPREGISTER_CONTEXT GuestRegisters)
{
    VMEXIT_CONTEXT ExitContext;
    PVMM_PROCESSOR_CONTEXT CpuContext;
    BOOL Success = FALSE;

    // Grab our logical processor context object for this processor
    CpuContext = GetCurrentCPUContext(Context);

    /*
     * Initialize all fields of the exit context, including reading relevant fields from the VMCS.
     */
    InitializeExitContext(&ExitContext, GuestRegisters);

    /*
     * If we tried to enter but failed, we return false here so HandleVmExitFailure is called.
     */
    if (ExitContext.ExitReason.VmEntryFailure == 1)
    {
        return FALSE;
    }

    /*
     * To prevent context switching while enabling interrupts, save IRQL here.
     */
    ExitContext.SavedIRQL = KeGetCurrentIrql();
    if (ExitContext.SavedIRQL < DISPATCH_LEVEL)
    {
        KeRaiseIrqlToDpcLevel();
    }

    /*
     * Handle our exit using the handler code inside of exit.c
     */
    Success = ExitDispatchFunction(CpuContext, &ExitContext);
    if (!Success)
    {
        HxTLog("Failed to handle exit.\n");
    }

    /*
     * If we raised IRQL, lower it before returning to guest.
     */
    if (ExitContext.SavedIRQL < DISPATCH_LEVEL)
    {
        KeLowerIrql(ExitContext.SavedIRQL);
    }

    return Success;
}

UINT64 VmxOffGetRsp(PVMM_CONTEXT Context) {
    return GetCurrentCPUContext(Context)->ExitRSP;
}
UINT64 VmxOffGetRip(PVMM_CONTEXT Context) {
    return GetCurrentCPUContext(Context)->ExitRIP;
}

BOOL HandleVmxOff(PVMM_CONTEXT Context, PGPREGISTER_CONTEXT GuestRegisters)
{
    __debugbreak();
    PVMM_PROCESSOR_CONTEXT ProcessorContext;

    UNREFERENCED_PARAMETER(GuestRegisters);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ProcessorContext);

    // TODO: Fix GlobalContext
    KeBugCheck(0xDEADBEEF);
    /*
    // Grab our logical processor context object for this processor
    ProcessorContext = GetCurrentCPUContext(GlobalContext);

    UtilLogError("HandleVmExitFailure: Encountered vmexit error.");

    // Print information about the error state
    VmxPrintErrorState(ProcessorContext);

    // Exit root mode, since we cannot recover here
    if (!VmxExitRootMode(ProcessorContext))
    {
        // We can't exit root mode? Ut oh. Things are real bad.
        // Returning false here will halt the processor.
        return FALSE;
    }

    // Continue execution with VMX disabled.
    return TRUE;
    */
}