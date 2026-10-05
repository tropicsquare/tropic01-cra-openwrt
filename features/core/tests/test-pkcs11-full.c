#include <stdio.h>
#include <dlfcn.h>
#include <pkcs11.h>

int main(void) {
    void *handle = dlopen("/usr/lib/libtropic_pkcs11.so", RTLD_NOW);
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
    
    printf("\n=== C_Initialize ===\n");
    rv = pFuncs->C_Initialize(NULL);
    printf("Result: 0x%08lx\n", rv);
    if (rv != CKR_OK) {
        return 1;
    }
    
    printf("\n=== C_GetSlotList (tokenPresent=FALSE) ===\n");
    CK_ULONG count = 0;
    rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &count);
    printf("Result: 0x%08lx, count=%lu\n", rv, count);
    
    if (count > 0) {
        CK_SLOT_ID slots[10];
        CK_ULONG maxSlots = 10;
        rv = pFuncs->C_GetSlotList(CK_FALSE, slots, &maxSlots);
        printf("Slots: %lu, slotID[0]=%lu\n", maxSlots, maxSlots > 0 ? slots[0] : 999);
        
        printf("\n=== C_OpenSession (slot=%lu) ===\n", slots[0]);
        CK_SESSION_HANDLE session;
        rv = pFuncs->C_OpenSession(slots[0], CKF_SERIAL_SESSION, NULL, NULL, &session);
        printf("Result: 0x%08lx, session=%lu\n", rv, (unsigned long)session);
        
        if (rv == CKR_OK) {
            printf("\n=== C_GenerateRandom (32 bytes) ===\n");
            unsigned char random[32];
            rv = pFuncs->C_GenerateRandom(session, random, 32);
            printf("Result: 0x%08lx\n", rv);
            if (rv == CKR_OK) {
                printf("Random data: ");
                for (int i = 0; i < 32; i++) {
                    printf("%02x", random[i]);
                }
                printf("\n");
            }
            
            pFuncs->C_CloseSession(session);
        }
    }
    
    pFuncs->C_Finalize(NULL);
    dlclose(handle);
    return 0;
}
