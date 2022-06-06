#include "Utilities.h"

BOOL BitIsSet(SIZE_T BitField, SIZE_T BitPosition)
{
	return (BitField >> BitPosition) & 1UL;
}

SIZE_T BitSetBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField | (1ULL << BitPosition);
}

SIZE_T BitClearBit(SIZE_T BitField, SIZE_T BitPosition)
{
	return BitField & ~(1ULL << BitPosition);
}

SIZE_T EncodeMustBeBits(SIZE_T DesiredValue, SIZE_T ControlMSR)
{
	LARGE_INTEGER ControlMSRLargeInteger;

	// LARGE_INTEGER provides a nice interface to get the top 32 bits of a 64-bit integer
	ControlMSRLargeInteger.QuadPart = ControlMSR;

	DesiredValue &= ControlMSRLargeInteger.HighPart;
	DesiredValue |= ControlMSRLargeInteger.LowPart;

	return DesiredValue;
}