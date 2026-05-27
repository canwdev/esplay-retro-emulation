#pragma once

#ifdef _MSC_VER
#include <direct.h>
#include <io.h>

#define getcwd _getcwd
#define unlink _unlink
#define access _access
#endif
