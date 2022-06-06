#include "Logger.h"
#include <stdarg.h>

void HxTLog(LPCSTR Format, ...) {
	va_list Arguments;
	va_start(Arguments, Format);
	vDbgPrintExWithPrefix("[HxT9] ", 0, 0, Format, Arguments);
	va_end(Arguments);
}