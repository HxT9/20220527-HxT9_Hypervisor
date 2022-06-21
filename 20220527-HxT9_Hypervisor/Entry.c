#include <ntddk.h>
#include "Logger.h"
#include "Vmm.h"
#include "WindowsNT.h"

PVMM_CONTEXT GlobalContext;

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	HxTLog("Hypervisor entry\n");
	PVMM_CONTEXT Context; //For debug
	NTSTATUS status = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = DriverUnload;

	if (!IsVmxSupported()) {
		status = STATUS_NOT_SUPPORTED;
	}

	if (NT_SUCCESS(status)) {
		Context = InitializeVmx();
		GlobalContext = Context;
		if (!GlobalContext) {
			status = STATUS_DRIVER_INTERNAL_ERROR;
		}
	}

	if (NT_SUCCESS(status))
		HxTLog("Hypervisor launched\n");

	return status;
}


#include "EptHook.h"
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);

	TerminateVmx(GlobalContext);

	HxTLog("Unloaded\n");
}