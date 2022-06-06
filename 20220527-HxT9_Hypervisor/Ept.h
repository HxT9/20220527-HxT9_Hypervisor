#pragma once
#include "ArchIntel.h"

typedef struct _VMM_CONTEXT VMM_CONTEXT, * PVMM_CONTEXT;
typedef struct _VMM_PROCESSOR_CONTEXT VMM_PROCESSOR_CONTEXT, * PVMM_PROCESSOR_CONTEXT;
typedef struct _VMEXIT_CONTEXT VMEXIT_CONTEXT, * PVMEXIT_CONTEXT;

typedef struct _MTRR_RANGE_DESCRIPTOR
{
	SIZE_T PhysicalBaseAddress;
	SIZE_T PhysicalEndAddress;
	UCHAR MemoryType;
} MTRR_RANGE_DESCRIPTOR, * PMTRR_RANGE_DESCRIPTOR;

/**
 * Okay, you can totally shoot me here but I *hate* the naming scheme of PDE PTE PDPTE and PML4 that Intel uses.
 * It just makes way more sense to me to simply annotate each level of the table by its number.
 *
 * PML4 == The 4th level of page table translation.
 * PML3 == The 3rd level of page table translation... etc.
 *
 * An "entry" is defined as a paging structure which describes backing memory. In this project, there are two types
 * of "entries" used, the PML2 (2MB) entry by default and the PML1 (4096 byte) entry when split. The rest of the paging structures are pointer
 * types.
 *
 * A "pointer" is defined as a paging structure which points to additional, smaller paging structures. In this project,
 * the PML4 and PDPTE are "PML4 pointer" and "PML3 pointer" respectively. If a 2MB page has been split, that entry will
 * become a "PML2 pointer" to multiple "PML1 entries".
 */

/**
 * The number of 512GB PML4 entries in the page table
 */
#define VMM_EPT_PML4E_COUNT 512

/**
 * The number of 1GB PDPT entries in the page table per 512GB PML4 entry.
 */
#define VMM_EPT_PML3E_COUNT 512

/**
 * Then number of 2MB Page Directory entries in the page table per 1GB PML3 entry.
 */
#define VMM_EPT_PML2E_COUNT 512

/**
 * Then number of 4096 byte Page Table entries in the page table per 2MB PML2 entry when dynamically split.
 */
#define VMM_EPT_PML1E_COUNT 512

/**
 * Integer 2MB
 */
#define SIZE_2_MB ((SIZE_T)(512 * PAGE_SIZE))

/**
 * Offset into the 1st paging structure (4096 byte)
 */
#define ADDRMASK_EPT_PML1_OFFSET(_VAR_) (_VAR_ & 0xFFFULL)

/**
 * Index of the 1st paging structure (4096 byte)
 */
#define ADDRMASK_EPT_PML1_INDEX(_VAR_) ((_VAR_ & 0x1FF000ULL) >> 12)

/**
 * Index of the 2nd paging structure (2MB)
 */
#define ADDRMASK_EPT_PML2_INDEX(_VAR_) ((_VAR_ & 0x3FE00000ULL) >> 21)

/**
 * Index of the 3rd paging structure (1GB)
 */
#define ADDRMASK_EPT_PML3_INDEX(_VAR_) ((_VAR_ & 0x7FC0000000ULL) >> 30)

/**
 * Index of the 4th paging structure (512GB)
 */
#define ADDRMASK_EPT_PML4_INDEX(_VAR_) ((_VAR_ & 0xFF8000000000ULL) >> 39)

typedef struct _VMM_EPT_PAGE_TABLE
{
	/**
	 * 28.2.2 Describes 512 contiguous 512GB memory regions each with 512 1GB regions.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPT_PML4 PML4[VMM_EPT_PML4E_COUNT];

	/**
	 * Describes exactly 512 contiguous 1GB memory regions within a our singular 512GB PML4 region.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPDPTE PML3[VMM_EPT_PML3E_COUNT];

	/**
	 * For each 1GB PML3 entry, create 512 2MB entries to map identity.
	 * NOTE: We are using 2MB pages as the smallest paging size in our map, so we do not manage individiual 4096 byte pages.
	 * Therefore, we do not allocate any PML1 (4096 byte) paging structures.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPDE_2MB PML2[VMM_EPT_PML3E_COUNT][VMM_EPT_PML2E_COUNT];

	/**
	 * List of all allocated dynamic splits. Used to free dynamic entries at the end of execution.
	 * A dynamic split is a 2MB page that's been split into 512 4096 size pages.
	 * This is used only on request when a specific page's protections need to be split.
	 */
	LIST_ENTRY DynamicSplitList;

	/**
	 * List of currently active page hooks. Page hooks are a shadow hooking mechanism which is capable of transparently swapping
	 * instructions when instructions of a page are executed. But, when the page is read or written to, the shadow page will be updated
	 * to reflect the changes without affecting the hook. This creates an invisible hook that the kernel cannot detect through typical means,
	 * such as code checksum analysis.
	 */
	LIST_ENTRY PageHookList;

} VMM_EPT_PAGE_TABLE, * PVMM_EPT_PAGE_TABLE;

#pragma warning(push, 0)
typedef struct _VMM_EPT_DYNAMIC_SPLIT
{
	/*
	 * The 4096 byte page table entries that correspond to the split 2MB table entry.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPTE PML1[VMM_EPT_PML1E_COUNT];

	/*
	 * The pointer to the 2MB entry in the page table which this split is servicing.
	 */
	union
	{
		EPDE_2MB* Entry;
		EPDE* Pointer;
	};

	/*
	 * Linked list entries for each dynamic split
	 */
	LIST_ENTRY DynamicSplitList;

} VMM_EPT_DYNAMIC_SPLIT, * PVMM_EPT_DYNAMIC_SPLIT;
#pragma warning(pop, 0)

typedef struct _VMM_EPT_PAGE_HOOK
{
	/*
	 * The fake page we copied from physical memory. This page will be swapped in
	 * with our changes when executed and swapped out when read.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) CHAR FakePage[PAGE_SIZE];

	/**
	 * Linked list entires for each page hook.
	 */
	LIST_ENTRY PageHookList;

	/**
	 * The base address of the page. Used to find this structure in the list of page hooks
	 * when a hook is hit.
	 */
	SIZE_T PhysicalBaseAddress;

	/*
	 * The page entry in the page tables that this page is targetting.
	 */
	EPTE* TargetPage;

	/**
	 * The original page entry. Will be copied back when the hook is removed
	 * from the page.
	 */
	EPTE OriginalEntry;

	/**
	 * The fake entry which points the page entry to FakePage in this structure. This is
	 * the entry that will be installed when the shadow entry is not currently swapped in.
	 * This page is marked executable only. If this page is read or written to, the HookedEntry
	 * will be swapped in.
	 */
	EPTE ShadowEntry;

	/**
	 * This entry points back to the original page of physical memory. This entry is modified
	 * to not be executable. When this entry is swapped in and instructions are fetched from this
	 * page, the ShadowEntry will be swapped in.
	 */
	EPTE HookedEntry;

	/**
	 * The buffer of the trampoline function which is used in the inline hook.
	 */
	PBYTE Trampoline;
} VMM_EPT_PAGE_HOOK, * PVMM_EPT_PAGE_HOOK;

/**
 * Initializes any EPT components that are not local to a particular processor.
 *
 * Checks to ensure EPT is supported by the processor and builds a map of system memory from
 * the MTRR registers.
 */
BOOL EptGlobalInitialize(PVMM_CONTEXT Context);

/**
 * Initialize EPT for an individual logical processor.
 *
 * Creates an identity mapped page table and sets up an EPTP to be applied to the VMCS later.
 */
BOOL EptLogicalProcessorInitialize(PVMM_PROCESSOR_CONTEXT ProcessorContext);

/*
 * Free memory allocated by EPT functions.
 */
VOID EptFreeLogicalProcessorContext(PVMM_PROCESSOR_CONTEXT ProcessorContext);

/**
 * Handle VM exits for EPT violations. Violations are thrown whenever an operation is performed
 * on an EPT entry that does not provide permissions to access that page.
 */
VOID ExitHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);

BOOL EptAddPageHook(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVOID TargetFunction, PVOID HookFunction, PVOID* OrigFunction);

//VmxDefs.asm
extern VOID __invept(SIZE_T Type, INVEPT_DESCRIPTOR* Descriptor);