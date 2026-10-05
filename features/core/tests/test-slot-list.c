#include <stdio.h>
#include <dlfcn.h>
#include <pkcs11.h>

int main(void) {
    void *handle = dlopen("/usr/lib/libtropic_pkcs11.so.spi", RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    
    CK_RV (*pGetFunctionList)(CK_FUNCTION_LIST_PTR_PTR) = dlsym(handle, "C_GetFunctionList");
    if (!pGetFunctionList) {
        fprintf(stderr, "C_GetFunctionList not found\n");
        return 1;
    }
    
    CK_FUNCTION_LIST_PTR pFuncs = NULL;
    CK_RV rv = pGetFunctionList(&pFuncs);
    if (rv != CKR_OK) {
        fprintf(stderr, "C_GetFunctionList failed: 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Calling C_Initialize...\n");
    rv = pFuncs->C_Initialize(NULL);
    printf("C_Initialize returned: 0x%08lx\n", rv);
    if (rv != CKR_OK) {
        return 1;
    }
    
    printf("\nCalling C_GetSlotList(CK_FALSE, NULL, &count)...\n");
    CK_ULONG count = 0;
    rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &count);
    printf("C_GetSlotList returned: 0x%08lx, count=%lu\n", rv, count);
    
    printf("\nCalling C_GetSlotList(CK_TRUE, NULL, &count)...\n");
    count = 0;
    rv = pFuncs->C_GetSlotList(CK_TRUE, NULL, &count);
    printf("C_GetSlotList returned: 0x%08lx, count=%lu\n", rv, count);
    
    if (count > 0) {
        CK_SLOT_ID slots[10];
        CK_ULONG maxSlots = 10;
        printf("\nCalling C_GetSlotList(CK_TRUE, slots, &maxSlots)...\n");
        rv = pFuncs->C_GetSlotList(CK_TRUE, slots, &maxSlots);
        printf("C_GetSlotList returned: 0x%08lx, maxSlots=%lu\n", rv, maxSlots);
        if (rv == CKR_OK && maxSlots > 0) {
            printf("First slot ID: %lu\n", slots[0]);
        }
    }
    
    pFuncs->C_Finalize(NULL);
    dlclose(handle);
    return 0;
}
