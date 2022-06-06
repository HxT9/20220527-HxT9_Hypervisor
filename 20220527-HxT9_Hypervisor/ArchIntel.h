#pragma once
#include "ia-32/ia32.h"

/*
 * CPUID Function identifier to check if VMX is enabled.
 *
 * CPUID.1:ECX.VMX[bit 5] = 1
 */
#define CPUID_VMX_ENABLED_FUNCTION 1

 /*
  * CPUID Subfunction identifier to check if VMX is enabled.
  *
  * CPUID.1:ECX.VMX[bit 5] = 1
  */
#define CPUID_VMX_ENABLED_SUBFUNCTION 0

#define CPUID_REGISTER_EAX 0
#define CPUID_REGISTER_EBX 1
#define CPUID_REGISTER_ECX 2
#define CPUID_REGISTER_EDX 3

  /*
  * CPUID VMX support enabled bit.
  *
  * CPUID.1:ECX.VMX[bit 5] = 1
  */
#define CPUID_VMX_ENABLED_BIT 5

/**
 * Get an MSR by its address and convert it to the specified type.
 */
SIZE_T ArchGetHostMSR(ULONG MsrAddress);

/*
 * Check if VMX support is enabled on the processor.
 */
BOOL ArchIsVMXAvailable();

/*
 * Get the IA32_VMX_BASIC MSR.
 *
 * Reporting Register of Basic VMX Capabilities.
 */
IA32_VMX_BASIC_REGISTER ArchGetBasicVmxCapabilities();

/*
 * Enables "Virtual Machine Extensions Enable" bit in CR4 (bit 13)
 */
VOID ArchEnableVmxe();

/*
 * Disable "Virtual Machine Extensions Enable" bit in CR4 (bit 13)
 */
VOID ArchDisableVmxe();

/*
 * Literally the contents of ntoskrnl's RtlCaptureContext to capture CPU register state.
 */
extern VOID ArchCaptureContext(CONTEXT* RegisterContext);

/*
 * Get the segment selector for the task selector segment (TSS)
 */
extern SEGMENT_SELECTOR ArchReadTaskRegister();

/*
 * Get the segment selector for the Local Descriptor Table (LDT)
 */
extern SEGMENT_SELECTOR ArchReadLocalDescriptorTableRegister();

typedef struct _IA32_SPECIAL_REGISTERS
{
	CR0 ControlRegister0;
	CR3 ControlRegister3;
	CR4 ControlRegister4;
	SEGMENT_DESCRIPTOR_REGISTER_64 GlobalDescriptorTableRegister;
	SEGMENT_DESCRIPTOR_REGISTER_64 InterruptDescriptorTableRegister;
	DR7 DebugRegister7;
	EFLAGS RflagsRegister;
	SEGMENT_SELECTOR TaskRegister;
	SEGMENT_SELECTOR LocalDescriptorTableRegister;
	IA32_DEBUGCTL_REGISTER DebugControlMsr;
	IA32_SYSENTER_CS_REGISTER SysenterCsMsr;
	SIZE_T SysenterEspMsr;
	SIZE_T SysenterEipMsr;
	SIZE_T GlobalPerfControlMsr;
	IA32_PAT_REGISTER PatMsr;
	IA32_EFER_REGISTER EferMsr;
} IA32_SPECIAL_REGISTERS, * PIA32_SPECIAL_REGISTERS;

VOID ArchCaptureSpecialRegisters(PIA32_SPECIAL_REGISTERS Registers);

typedef struct DECLSPEC_ALIGN(16) _REGISTER_CONTEXT {

	//
	// Register parameter home addresses.
	//
	// N.B. These fields are for convience - they could be used to extend the
	//      context record in the future.
	//

	ULONG64 P1Home;
	ULONG64 P2Home;
	ULONG64 P3Home;
	ULONG64 P4Home;
	ULONG64 P5Home;
	ULONG64 P6Home;

	//
	// Control flags.
	//

	ULONG ContextFlags;
	ULONG MxCsr;

	//
	// Segment Registers and processor flags.
	//

	SEGMENT_SELECTOR SegCS;
	SEGMENT_SELECTOR SegDS;
	SEGMENT_SELECTOR SegES;
	SEGMENT_SELECTOR SegFS;
	SEGMENT_SELECTOR SegGS;
	SEGMENT_SELECTOR SegSS;
	ULONG EFlags;

	//
	// Debug registers
	//

	ULONG64 Dr0;
	ULONG64 Dr1;
	ULONG64 Dr2;
	ULONG64 Dr3;
	ULONG64 Dr6;
	ULONG64 Dr7;

	//
	// Integer registers.
	//

	ULONG64 Rax;
	ULONG64 Rcx;
	ULONG64 Rdx;
	ULONG64 Rbx;
	ULONG64 Rsp;
	ULONG64 Rbp;
	ULONG64 Rsi;
	ULONG64 Rdi;
	ULONG64 R8;
	ULONG64 R9;
	ULONG64 R10;
	ULONG64 R11;
	ULONG64 R12;
	ULONG64 R13;
	ULONG64 R14;
	ULONG64 R15;

	//
	// Program counter.
	//

	ULONG64 Rip;

	//
	// Floating point state.
	//

	union {
		XMM_SAVE_AREA32 FltSave;
		struct {
			M128A Header[2];
			M128A Legacy[8];
			M128A Xmm0;
			M128A Xmm1;
			M128A Xmm2;
			M128A Xmm3;
			M128A Xmm4;
			M128A Xmm5;
			M128A Xmm6;
			M128A Xmm7;
			M128A Xmm8;
			M128A Xmm9;
			M128A Xmm10;
			M128A Xmm11;
			M128A Xmm12;
			M128A Xmm13;
			M128A Xmm14;
			M128A Xmm15;
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;

	//
	// Vector registers.
	//

	M128A VectorRegister[26];
	ULONG64 VectorControl;

	//
	// Special debug control registers.
	//

	ULONG64 DebugControl;
	ULONG64 LastBranchToRip;
	ULONG64 LastBranchFromRip;
	ULONG64 LastExceptionToRip;
	ULONG64 LastExceptionFromRip;
} REGISTER_CONTEXT, * PREGISTER_CONTEXT;

/*
 * From vmxdefs.asm:
 *
 * Saved GP register context before calling into the vmexit handler.
 *
 * 	pop	rax
	pop	rcx
	pop	rdx
	pop	rbx
	add	rsp, 8
	pop	rbp
	pop	rsi
	pop	rdi
	pop	r8
	pop	r9
	pop	r10
	pop	r11
	pop	r12
	pop	r13
	pop	r14
	pop	r15
 */
 typedef struct _GPREGISTER_CONTEXT
 {
	 /* Populated from vmxdefs.asm */
	 SIZE_T GuestRAX;
	 SIZE_T GuestRCX;
	 SIZE_T GuestRDX;
	 SIZE_T GuestRBX;

	 /* Populated from VMCS */
	 SIZE_T GuestRSP;

	 /* Populated from vmxdefs.asm */
	 SIZE_T GuestRBP;
	 SIZE_T GuestRSI;
	 SIZE_T GuestRDI;
	 SIZE_T GuestR8;
	 SIZE_T GuestR9;
	 SIZE_T GuestR10;
	 SIZE_T GuestR11;
	 SIZE_T GuestR12;
	 SIZE_T GuestR13;
	 SIZE_T GuestR14;
	 SIZE_T GuestR15;
 } GPREGISTER_CONTEXT, * PGPREGISTER_CONTEXT;