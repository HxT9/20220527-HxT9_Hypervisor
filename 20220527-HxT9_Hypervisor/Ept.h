#pragma once
#include "ArchIntel.h"

typedef struct _VMM_CONTEXT VMM_CONTEXT, * PVMM_CONTEXT;
typedef struct _VMM_PROCESSOR_CONTEXT VMM_PROCESSOR_CONTEXT, * PVMM_PROCESSOR_CONTEXT;
typedef struct _VMEXIT_CONTEXT VMEXIT_CONTEXT, * PVMEXIT_CONTEXT;

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

typedef struct _MTRR_RANGE_DESCRIPTOR
{
	SIZE_T PhysicalBaseAddress;
	SIZE_T PhysicalEndAddress;
	UCHAR MemoryType;
} MTRR_RANGE_DESCRIPTOR, * PMTRR_RANGE_DESCRIPTOR;

typedef EPT_PML4 EPT_PML4_POINTER, * PEPT_PML4_POINTER;
typedef EPDPTE   EPT_PML3_POINTER, * PEPT_PML3_POINTER;
typedef EPDE_2MB EPT_PML2_ENTRY, * PEPT_PML2_ENTRY;
typedef EPDE     EPT_PML2_POINTER, * PEPT_PML2_POINTER;
typedef EPTE     EPT_PML1_ENTRY, * PEPT_PML1_ENTRY;

typedef struct _VMM_EPT_PAGE_TABLE
{
	/**
	 * @brief 28.2.2 Describes 512 contiguous 512GB memory regions each with 512 1GB regions.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE)
	EPT_PML4_POINTER PML4[VMM_EPT_PML4E_COUNT];

	/**
	 * @brief Describes exactly 512 contiguous 1GB memory regions within a our singular 512GB PML4 region.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE)
	EPT_PML3_POINTER PML3[VMM_EPT_PML3E_COUNT];

	/**
	 * @brief For each 1GB PML3 entry, create 512 2MB entries to map identity.
	 * NOTE: We are using 2MB pages as the smallest paging size in our map, so we do not manage individiual 4096 byte pages.
	 * Therefore, we do not allocate any PML1 (4096 byte) paging structures.
	 */
	DECLSPEC_ALIGN(PAGE_SIZE)
	EPT_PML2_ENTRY PML2[VMM_EPT_PML3E_COUNT][VMM_EPT_PML2E_COUNT];

} VMM_EPT_PAGE_TABLE, * PVMM_EPT_PAGE_TABLE;

typedef struct _EPT_STATE
{
	EPT_POINTER EptPointer;
	MTRR_RANGE_DESCRIPTOR MemoryRanges[9];
	ULONG NumberOfEnabledMemoryRanges;
	PVMM_EPT_PAGE_TABLE EptPageTable;
	LIST_ENTRY DynamicSplitList;
	LIST_ENTRY HookedPagesList;
} EPT_STATE, *PEPT_STATE;

/**
 * @brief Split 2MB granularity to 4 KB granularity
 *
 */
typedef struct _VMM_EPT_DYNAMIC_SPLIT
{
	/**
	 * @brief The 4096 byte page table entries that correspond to the split 2MB table entry
	 *
	 */
	DECLSPEC_ALIGN(PAGE_SIZE) EPT_PML1_ENTRY PML1[VMM_EPT_PML1E_COUNT];

	/**
	* @brief The pointer to the 2MB entry in the page table which this split is servicing.
	*
	*/
	union
	{
		PEPT_PML2_ENTRY   Entry;
		PEPT_PML2_POINTER Pointer;
	};

	/**
	 * @brief Linked list entries for each dynamic split
	 *
	 */
	LIST_ENTRY DynamicSplitList;

} VMM_EPT_DYNAMIC_SPLIT, * PVMM_EPT_DYNAMIC_SPLIT;

/**
 * @brief Structure to save the state of each hooked pages
 *
 */
typedef struct _EPT_HOOKED_PAGE_DETAIL
{
	DECLSPEC_ALIGN(PAGE_SIZE) BYTE FakePageContents[PAGE_SIZE];

	/**
	 * @brief Linked list entries for each page hook.
	 */
	LIST_ENTRY PageHookList;

	/**
	* @brief The virtual address from the caller prespective view (cr3)
	*/
	UINT64 VirtualAddress;

	/**
	* @brief The virtual address of it's entry
	* this way we can de-allocate the list whenever the hook is finished
	*/
	UINT64 AddressOfEptHook2sDetourListEntry;

	/**
	 * @brief The base address of the page. Used to find this structure in the list of page hooks
	 * when a hook is hit.
	 */
	SIZE_T PhysicalBaseAddress;

	/**
	* @brief The base address of the page with fake contents. Used to swap page with fake contents
	* when a hook is hit.
	*/
	SIZE_T PhysicalBaseAddressOfFakePageContents;

	/*
	 * @brief The page entry in the page tables that this page is targetting.
	 */
	PEPT_PML1_ENTRY EntryAddress;

	/**
	 * @brief The original page entry. Will be copied back when the hook is removed
	 * from the page.
	 */
	EPT_PML1_ENTRY OriginalEntry;

	/**
	 * @brief The original page entry. Will be copied back when the hook is remove from the page.
	 */
	EPT_PML1_ENTRY ExecuteEntry;

	/**
	 * @brief The original page entry. Will be copied back when the hook is remove from the page.
	 */
	EPT_PML1_ENTRY RWEntry;

	/**
	* @brief The buffer of the trampoline function which is used in the inline hook.
	*/
	PCHAR Trampoline;

} EPT_HOOKED_PAGE_DETAIL, * PEPT_HOOKED_PAGE_DETAIL;

/**
 * Initializes any EPT components that are not local to a particular processor.
 *
 * Checks to ensure EPT is supported by the processor and builds a map of system memory from
 * the MTRR registers.
 */
BOOL EptInitialize(PVMM_CONTEXT Context);

/**
 * Handle VM exits for EPT violations. Violations are thrown whenever an operation is performed
 * on an EPT entry that does not provide permissions to access that page.
 */
VOID ExitHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);

/**
 * @brief Split 2MB (LargePage) into 4kb pages
 *
 * @param EptPageTable The EPT Page Table
 * @param PreAllocatedBuffer The address of pre-allocated buffer
 * @param PhysicalAddress Physical address of where we want to split
 * @param CoreIndex The index of core
 * @return BOOLEAN Returns true if it was successfull or false if there was an error
 */
BOOLEAN EptSplitLargePage(PVMM_CONTEXT Context, SIZE_T PhysicalAddress);

/**
 * @brief Get the PML1 entry for this physical address if the page is split
 *
 * @param EptPageTable The EPT Page Table
 * @param PhysicalAddress Physical address that we want to get its PML1
 * @return PEPT_PML1_ENTRY Return NULL if the address is invalid or the page wasn't already split
 */
PEPT_PML1_ENTRY EptGetPml1Entry(PVMM_EPT_PAGE_TABLE EptPageTable, SIZE_T PhysicalAddress);

/**
 * @brief This function set the specific PML1 entry in a spinlock protected area then invalidate the TLB
 * @details This function should be called from vmx root-mode
 *
 * @param EntryAddress PML1 entry information (the target address)
 * @param EntryValue The value of pm1's entry (the value that should be replaced)
 * @param InvalidationType type of invalidation
 * @return VOID
 */
VOID EptSetPML1AndInvalidateTLB(PVMM_CONTEXT Context, PEPT_PML1_ENTRY EntryAddress, EPT_PML1_ENTRY EntryValue, INVEPT_TYPE InvalidationType);
VOID EptSetPML1(PVMM_CONTEXT Context, PEPT_PML1_ENTRY EntryAddress, EPT_PML1_ENTRY EntryValue);

BOOLEAN EptHandleEptViolation(PVMM_PROCESSOR_CONTEXT ProcessorContext, PVMEXIT_CONTEXT ExitContext);