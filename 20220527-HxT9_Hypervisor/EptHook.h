#pragma once
#include "VmmContext.h"

BOOLEAN EptHookAddHook(PVMM_CONTEXT Context, PVOID TargetAddress, PVOID HookFunction, PVOID TrampolineAddress, UINT32 TargetProcessId, BOOL IsX64);
BOOLEAN EptClearHooks(PVMM_CONTEXT Context, BOOL InvalidateTLB);