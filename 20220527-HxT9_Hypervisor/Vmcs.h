#pragma once
#include "ia-32/ia32.h"
#include "VmmContext.h"

/*
 * Used to write a vmcs field using vmwrite and an ia32-doc register type.
 */
#define VmxVmwriteFieldFromRegister(_FIELD_DEFINE_, _REGISTER_VAR_) VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _REGISTER_VAR_.Flags);

 /*
  * Used to write a vmcs field using vmwrite and an immediate value.
  */
#define VmxVmwriteFieldFromImmediate(_FIELD_DEFINE_, _IMMEDIATE_) VmError |= __vmx_vmwrite(_FIELD_DEFINE_, _IMMEDIATE_);

  /*
   * Reads a value from the VMCS to a ia32-doc register type.
   */
#define VmxVmreadFieldToRegister(_FIELD_DEFINE_, _REGISTER_VAR_) VmError |= __vmx_vmread(_FIELD_DEFINE_, _REGISTER_VAR_.Flags);

   /*
	* Reads a value from the VMCS to an immediate value.
	*/
#define VmxVmreadFieldToImmediate(_FIELD_DEFINE_, _IMMEDIATE_) VmError |= __vmx_vmread(_FIELD_DEFINE_, _IMMEDIATE_);

BOOL SetupVmcsDefaults(PVMM_PROCESSOR_CONTEXT Context, SIZE_T HostRIP, SIZE_T HostRSP, SIZE_T GuestRIP, SIZE_T GuestRSP);

typedef struct _SEGMENT_DESCRIPTOR
{
	/*
	 * Selector (16 bits)
	 */
	SIZE_T Selector;

	/*
	 * Base address (64 bits; 32 bits on processors that do not support Intel 64 architecture). The base-address
	 * fields for CS, SS, DS, and ES have only 32 architecturally-defined bits; nevertheless, the corresponding
	 * VMCS fields have 64 bits on processors that support Intel 64 architecture.
	 */
	SIZE_T BaseAddress;

	/*
	 * Segment limit (32 bits). The limit field is always a measure in bytes.
	 */
	UINT32 SegmentLimit;

	/*
	 * Access rights (32 bits). The format of this field is given in Table 24-2 and detailed as follows:
	 *
	 * • The low 16 bits correspond to bits 23:8 of the upper 32 bits of a 64-bit segment descriptor. While bits
	 *   19:16 of code-segment and data-segment descriptors correspond to the upper 4 bits of the segment
	 *   limit, the corresponding bits (bits 11:8) are reserved in this VMCS field.
	 *
	 * • Bit 16 indicates an unusable segment. Attempts to use such a segment fault except in 64-bit mode.
	 *   In general, a segment register is unusable if it has been loaded with a null selector.
	 *
	 * • Bits 31:17 are reserved.
	 */
	VMX_SEGMENT_ACCESS_RIGHTS AccessRights;
} VMX_SEGMENT_DESCRIPTOR, * PVMX_SEGMENT_DESCRIPTOR;