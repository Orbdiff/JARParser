#include "include.h"
#include "SeDebugPrivilege.h"

int wmain(void) {
    EnableDebugPrivilege();
    system("chcp 65001 > nul");
    JARParser();
    system("pause");
    return 0;
}