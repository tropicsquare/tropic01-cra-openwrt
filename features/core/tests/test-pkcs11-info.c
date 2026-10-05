#include <stdio.h>
#include <dlfcn.h>
#include <pkcs11.h>
#include <string.h>

void print_hex(const char *prefix, unsigned char *data, size_t len) {
    printf("%s", prefix);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len) 
            printf("\n%*s", (int)strlen(prefix), "");
    }
    printf("\n");
}

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
    
    printf("\n=== PKCS#11 Module Information ===\n");
    rv = pFuncs->C_Initialize(NULL);
    if (rv != CKR_OK) {
        fprintf(stderr, "C_Initialize failed: 0x%08lx\n", rv);
        return 1;
    }
    
    CK_INFO info;
    rv = pFuncs->C_GetInfo(&info);
    if (rv == CKR_OK) {
        printf("Cryptoki version: %d.%d\n", info.cryptokiVersion.major, info.cryptokiVersion.minor);
        printf("Manufacturer:     %.32s\n", info.manufacturerID);
        printf("Library:          %.32s\n", info.libraryDescription);
        printf("Library version:  %d.%d\n", info.libraryVersion.major, info.libraryVersion.minor);
    }
    
    printf("\n=== Slot Information ===\n");
    CK_ULONG slotCount = 0;
    rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &slotCount);
    printf("Number of slots: %lu\n", slotCount);
    
    if (slotCount > 0) {
        CK_SLOT_ID slots[10];
        CK_ULONG maxSlots = 10;
        pFuncs->C_GetSlotList(CK_FALSE, slots, &maxSlots);
        
        CK_SLOT_INFO slotInfo;
        rv = pFuncs->C_GetSlotInfo(slots[0], &slotInfo);
        if (rv == CKR_OK) {
            printf("Slot description: %.64s\n", slotInfo.slotDescription);
            printf("Manufacturer:     %.32s\n", slotInfo.manufacturerID);
            printf("Flags:            0x%08lx\n", slotInfo.flags);
            if (slotInfo.flags & CKF_TOKEN_PRESENT) printf("  - Token present\n");
            if (slotInfo.flags & CKF_HW_SLOT) printf("  - Hardware slot\n");
        }
        
        printf("\n=== Token Information ===\n");
        CK_TOKEN_INFO tokenInfo;
        rv = pFuncs->C_GetTokenInfo(slots[0], &tokenInfo);
        if (rv == CKR_OK) {
            printf("Token label:      %.32s\n", tokenInfo.label);
            printf("Manufacturer:     %.32s\n", tokenInfo.manufacturerID);
            printf("Model:            %.16s\n", tokenInfo.model);
            printf("Serial number:    %.16s\n", tokenInfo.serialNumber);
            printf("Flags:            0x%08lx\n", tokenInfo.flags);
            if (tokenInfo.flags & CKF_RNG) printf("  - Hardware RNG available\n");
            if (tokenInfo.flags & CKF_TOKEN_INITIALIZED) printf("  - Token initialized\n");
            printf("Max sessions:     %lu\n", tokenInfo.ulMaxSessionCount);
            printf("Open sessions:    %lu\n", tokenInfo.ulSessionCount);
        }
        
        printf("\n=== Supported Mechanisms ===\n");
        CK_ULONG mechCount = 0;
        rv = pFuncs->C_GetMechanismList(slots[0], NULL, &mechCount);
        if (rv == CKR_OK) {
            printf("Number of mechanisms: %lu\n", mechCount);
            if (mechCount > 0) {
                CK_MECHANISM_TYPE mechs[100];
                CK_ULONG maxMechs = 100;
                rv = pFuncs->C_GetMechanismList(slots[0], mechs, &maxMechs);
                if (rv == CKR_OK) {
                    for (CK_ULONG i = 0; i < maxMechs && i < mechCount; i++) {
                        printf("  [%lu] Mechanism type: 0x%08lx\n", i+1, mechs[i]);
                        
                        CK_MECHANISM_INFO mechInfo;
                        rv = pFuncs->C_GetMechanismInfo(slots[0], mechs[i], &mechInfo);
                        if (rv == CKR_OK) {
                            printf("       Key size: %lu-%lu bits\n", 
                                   mechInfo.ulMinKeySize, mechInfo.ulMaxKeySize);
                            printf("       Flags: 0x%08lx ", mechInfo.flags);
                            if (mechInfo.flags & CKF_SIGN) printf("[SIGN] ");
                            if (mechInfo.flags & CKF_VERIFY) printf("[VERIFY] ");
                            if (mechInfo.flags & CKF_GENERATE) printf("[GENERATE] ");
                            if (mechInfo.flags & CKF_GENERATE_KEY_PAIR) printf("[GENERATE_KEY_PAIR] ");
                            printf("\n");
                        }
                    }
                }
            }
        } else {
            printf("C_GetMechanismList returned: 0x%08lx\n", rv);
        }
        
        printf("\n=== Random Number Generation Test ===\n");
        CK_SESSION_HANDLE session;
        rv = pFuncs->C_OpenSession(slots[0], CKF_SERIAL_SESSION, NULL, NULL, &session);
        if (rv == CKR_OK) {
            printf("Session opened: 0x%lx\n", (unsigned long)session);
            
            // Test different sizes
            size_t sizes[] = {8, 16, 32, 64, 128, 256};
            for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
                unsigned char random[256];
                rv = pFuncs->C_GenerateRandom(session, random, sizes[i]);
                if (rv == CKR_OK) {
                    char prefix[32];
                    snprintf(prefix, sizeof(prefix), "  %3zu bytes: ", sizes[i]);
                    print_hex(prefix, random, sizes[i] > 32 ? 32 : sizes[i]);
                    if (sizes[i] > 32) printf("             ... (%zu more bytes)\n", sizes[i] - 32);
                } else {
                    printf("  %zu bytes: FAILED (0x%08lx)\n", sizes[i], rv);
                }
            }
            
            pFuncs->C_CloseSession(session);
        } else {
            printf("C_OpenSession failed: 0x%08lx\n", rv);
        }
    }
    
    pFuncs->C_Finalize(NULL);
    dlclose(handle);
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
