#pragma once

/**
 * Linked list for-each macro for traversing LIST_ENTRY structures.
 *
 * _LISTHEAD_ is a pointer to the struct that the list head belongs to.
 * _LISTHEAD_NAME_ is the name of the variable which contains the list head. Should match the same name as the list entry struct member in the actual record.
 * _TARGET_TYPE_ is the type name of the struct of each item in the list
 * _TARGET_NAME_ is the name which will contain the pointer to the item each iteration
 *
 * Example:
 * FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, DynamicSplitList, VMM_EPT_DYNAMIC_SPLIT, Split)
 * 		OsFreeNonpagedMemory(Split);
 * }
 *
 * ProcessorContext->EptPageTable->DynamicSplitList is the head of the list.
 * VMM_EPT_DYNAMIC_SPLIT is the struct of each item in the list.
 * Split is the name of the local variable which will hold the pointer to the item.
 */
#define FOR_EACH_LIST_ENTRY(_LISTHEAD_, _LISTHEAD_NAME_, _TARGET_TYPE_, _TARGET_NAME_) \
	for (PLIST_ENTRY Entry = _LISTHEAD_->_LISTHEAD_NAME_.Flink; Entry != &_LISTHEAD_->_LISTHEAD_NAME_; Entry = Entry->Flink) { \
	P##_TARGET_TYPE_ _TARGET_NAME_ = CONTAINING_RECORD(Entry, _TARGET_TYPE_, _LISTHEAD_NAME_);

 /**
  * The braces for the block are messy due to the need to define a local variable in the for loop scope.
  * Therefore, this macro just ends the for each block without messing up code editors trying to detect
  * the block indent level.
  */
# define FOR_EACH_LIST_ENTRY_END() }

/**
 * Check if a bit is set in the bit field.
 */
BOOL BitIsSet(SIZE_T BitField, SIZE_T BitPosition);

/**
 * Set a bit in a bit field.
 */
SIZE_T BitSetBit(SIZE_T BitField, SIZE_T BitPosition);

/*
 * Clear a bit in a bit field.
 */
SIZE_T BitClearBit(SIZE_T BitField, SIZE_T BitPosition);

/*
 * Certain control MSRs in VMX will ask that certain bits always be 0, and some always be 1.
 *
 * In these MSR formats, the lower 32-bits specify the "must be 1" bits.
 * These bits are OR'd to ensure they are always 1, no matter what DesiredValue was set to.
 *
 * The high 32-bits specify the "must be 0" bits.
 * These bits are AND'd to ensure these bits are always 0, no matter what DesiredValue was set to.
 */
SIZE_T EncodeMustBeBits(SIZE_T DesiredValue, SIZE_T ControlMSR);