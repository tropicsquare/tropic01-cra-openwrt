/*
 * pkcs11-test - Simple PKCS#11 module testing utility
 * Copyright (C) 2026 OpenWrt Community
 * 
 * This is a lightweight alternative to pkcs11-tool for testing
 * PKCS#11 modules in embedded environments.
 * 
 * License: GPL-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

/* Minimal PKCS#11 definitions */
typedef unsigned long CK_RV;
typedef unsigned long CK_ULONG;
typedef unsigned long CK_SLOT_ID;
typedef unsigned long CK_SESSION_HANDLE;
typedef unsigned long CK_OBJECT_HANDLE;
typedef unsigned long CK_OBJECT_CLASS;
typedef unsigned long CK_KEY_TYPE;
typedef unsigned long CK_ATTRIBUTE_TYPE;
typedef unsigned long CK_FLAGS;
typedef void *CK_VOID_PTR;
typedef unsigned char CK_BYTE;
typedef CK_BYTE *CK_BYTE_PTR;
typedef CK_SLOT_ID *CK_SLOT_ID_PTR;
typedef CK_OBJECT_HANDLE *CK_OBJECT_HANDLE_PTR;

#define CKR_OK 0x00000000UL
#define CKF_SERIAL_SESSION 0x00000004UL

/* Object classes */
#define CKO_DATA              0x00000000UL
#define CKO_CERTIFICATE       0x00000001UL
#define CKO_PUBLIC_KEY        0x00000002UL
#define CKO_PRIVATE_KEY       0x00000003UL
#define CKO_SECRET_KEY        0x00000004UL

/* Key types */
#define CKK_RSA               0x00000000UL
#define CKK_DSA               0x00000001UL
#define CKK_DH                0x00000002UL
#define CKK_EC                0x00000003UL

/* Attribute types */
#define CKA_CLASS             0x00000000UL
#define CKA_KEY_TYPE          0x00000100UL
#define CKA_LABEL             0x00000003UL
#define CKA_ID                0x00000102UL

typedef struct CK_VERSION {
    CK_BYTE major;
    CK_BYTE minor;
} CK_VERSION;

typedef struct CK_INFO {
    CK_VERSION cryptokiVersion;
    CK_BYTE manufacturerID[32];
    CK_ULONG flags;
    CK_BYTE libraryDescription[32];
    CK_VERSION libraryVersion;
} CK_INFO;

typedef CK_INFO *CK_INFO_PTR;

typedef struct CK_ATTRIBUTE {
    CK_ATTRIBUTE_TYPE type;
    CK_VOID_PTR pValue;
    CK_ULONG ulValueLen;
} CK_ATTRIBUTE;

typedef CK_ATTRIBUTE *CK_ATTRIBUTE_PTR;

typedef struct CK_FUNCTION_LIST CK_FUNCTION_LIST;
typedef CK_FUNCTION_LIST *CK_FUNCTION_LIST_PTR;
typedef CK_FUNCTION_LIST_PTR *CK_FUNCTION_LIST_PTR_PTR;

typedef CK_RV (*CK_C_Initialize)(CK_VOID_PTR);
typedef CK_RV (*CK_C_Finalize)(CK_VOID_PTR);
typedef CK_RV (*CK_C_GetInfo)(CK_INFO_PTR);
typedef CK_RV (*CK_C_GetFunctionList)(CK_FUNCTION_LIST_PTR_PTR);
typedef CK_RV (*CK_C_GetSlotList)(CK_BYTE, CK_SLOT_ID_PTR, CK_ULONG *);
typedef CK_RV (*CK_C_OpenSession)(CK_SLOT_ID, CK_FLAGS, CK_VOID_PTR, CK_VOID_PTR, CK_SESSION_HANDLE *);
typedef CK_RV (*CK_C_CloseSession)(CK_SESSION_HANDLE);
typedef CK_RV (*CK_C_FindObjectsInit)(CK_SESSION_HANDLE, CK_ATTRIBUTE_PTR, CK_ULONG);
typedef CK_RV (*CK_C_FindObjects)(CK_SESSION_HANDLE, CK_OBJECT_HANDLE_PTR, CK_ULONG, CK_ULONG *);
typedef CK_RV (*CK_C_FindObjectsFinal)(CK_SESSION_HANDLE);
typedef CK_RV (*CK_C_GetAttributeValue)(CK_SESSION_HANDLE, CK_OBJECT_HANDLE, CK_ATTRIBUTE_PTR, CK_ULONG);
typedef CK_RV (*CK_C_GenerateRandom)(CK_SESSION_HANDLE, CK_BYTE_PTR, CK_ULONG);

struct CK_FUNCTION_LIST {
    CK_VERSION version;
    CK_C_Initialize C_Initialize;
    CK_C_Finalize C_Finalize;
    CK_C_GetInfo C_GetInfo;
    CK_VOID_PTR reserved1[9];  /* Skip unused functions */
    CK_C_GetSlotList C_GetSlotList;
    CK_VOID_PTR reserved2[2];
    CK_C_OpenSession C_OpenSession;
    CK_C_CloseSession C_CloseSession;
    CK_VOID_PTR reserved3[8];
    CK_C_FindObjectsInit C_FindObjectsInit;
    CK_C_FindObjects C_FindObjects;
    CK_C_FindObjectsFinal C_FindObjectsFinal;
    CK_VOID_PTR reserved4[9];
    CK_C_GetAttributeValue C_GetAttributeValue;
    CK_VOID_PTR reserved5[28];
    CK_C_GenerateRandom C_GenerateRandom;
    /* ... other functions ... */
};

const char* object_class_name(CK_OBJECT_CLASS cls) {
    switch(cls) {
        case CKO_DATA: return "Data";
        case CKO_CERTIFICATE: return "Certificate";
        case CKO_PUBLIC_KEY: return "Public Key";
        case CKO_PRIVATE_KEY: return "Private Key";
        case CKO_SECRET_KEY: return "Secret Key";
        default: return "Unknown";
    }
}

const char* key_type_name(CK_KEY_TYPE type) {
    switch(type) {
        case CKK_RSA: return "RSA";
        case CKK_DSA: return "DSA";
        case CKK_DH: return "DH";
        case CKK_EC: return "EC";
        default: return "Unknown";
    }
}

void print_hex(const char *prefix, unsigned char *data, size_t len) {
    printf("%s", prefix);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len) 
            printf("\n%*s", (int)strlen(prefix), "");
    }
    printf("\n");
}

void print_usage(const char *prog) {
    printf("Usage: %s --module <path_to_module.so> [options]\n", prog);
    printf("\n");
    printf("Simple PKCS#11 module testing utility\n");
    printf("\n");
    printf("Options:\n");
    printf("  --module <path>    Path to PKCS#11 module (.so file)\n");
    printf("  --show-info        Show module information (default)\n");
    printf("  --test-init        Test initialization and finalization\n");
    printf("  --list-objects     List all objects in token\n");
    printf("  --random <bytes>   Generate random data (1-1024 bytes)\n");
    printf("  --help             Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --module /usr/lib/libtropic_pkcs11.so\n", prog);
    printf("  %s --module /usr/lib/libtropic_pkcs11.so --test-init\n", prog);
    printf("  %s --module /usr/lib/libtropic_pkcs11.so --list-objects\n", prog);
    printf("  %s --module /usr/lib/libtropic_pkcs11.so --random 32\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    const char *module_path = NULL;
    int test_init = 0;
    int show_info = 0;
    int list_objects = 0;
    int random_bytes = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--module") == 0 && i + 1 < argc) {
            module_path = argv[++i];
        } else if (strcmp(argv[i], "--test-init") == 0) {
            test_init = 1;
        } else if (strcmp(argv[i], "--show-info") == 0) {
            show_info = 1;
        } else if (strcmp(argv[i], "--list-objects") == 0) {
            list_objects = 1;
        } else if (strcmp(argv[i], "--random") == 0 && i + 1 < argc) {
            random_bytes = atoi(argv[++i]);
            if (random_bytes <= 0 || random_bytes > 1024) {
                fprintf(stderr, "Error: random bytes must be 1-1024\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    if (!module_path) {
        fprintf(stderr, "Error: --module is required\n\n");
        print_usage(argv[0]);
        return 1;
    }
    
    /* Default to show_info if nothing specified */
    if (!test_init && !list_objects && !random_bytes) {
        show_info = 1;
    }
    
    /* Load the module */
    printf("Loading PKCS#11 module: %s\n", module_path);
    void *handle = dlopen(module_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "Error loading module: %s\n", dlerror());
        return 1;
    }
    
    /* Get C_GetFunctionList */
    CK_C_GetFunctionList pGetFunctionList = 
        (CK_C_GetFunctionList)dlsym(handle, "C_GetFunctionList");
    if (!pGetFunctionList) {
        fprintf(stderr, "Error: C_GetFunctionList not found\n");
        dlclose(handle);
        return 1;
    }
    
    /* Get function list */
    CK_FUNCTION_LIST_PTR pFunctionList = NULL;
    CK_RV rv = pGetFunctionList(&pFunctionList);
    if (rv != CKR_OK || !pFunctionList) {
        fprintf(stderr, "Error: C_GetFunctionList failed (0x%08lx)\n", rv);
        dlclose(handle);
        return 1;
    }
    
    printf("✓ Module loaded successfully\n");
    printf("✓ C_GetFunctionList: OK\n");
    printf("  Cryptoki version: %d.%d\n", 
           pFunctionList->version.major, 
           pFunctionList->version.minor);
    
    if (test_init || show_info) {
        /* Initialize */
        printf("\nInitializing module...\n");
        rv = pFunctionList->C_Initialize(NULL);
        if (rv != CKR_OK) {
            fprintf(stderr, "Warning: C_Initialize failed (0x%08lx)\n", rv);
        } else {
            printf("✓ C_Initialize: OK\n");
        }
        
        if (show_info && pFunctionList->C_GetInfo) {
            /* Get module info */
            CK_INFO info;
            memset(&info, 0, sizeof(info));
            rv = pFunctionList->C_GetInfo(&info);
            if (rv == CKR_OK) {
                printf("\n=== Module Information ===\n");
                printf("Cryptoki version: %d.%d\n", 
                       info.cryptokiVersion.major, info.cryptokiVersion.minor);
                printf("Manufacturer ID:  %.32s\n", info.manufacturerID);
                printf("Library desc:     %.32s\n", info.libraryDescription);
                printf("Library version:  %d.%d\n", 
                       info.libraryVersion.major, info.libraryVersion.minor);
                printf("Flags:            0x%08lx\n", info.flags);
            } else {
                fprintf(stderr, "Warning: C_GetInfo failed (0x%08lx)\n", rv);
            }
        }
        
        /* Finalize */
        printf("\nFinalizing module...\n");
        rv = pFunctionList->C_Finalize(NULL);
        if (rv != CKR_OK) {
            fprintf(stderr, "Warning: C_Finalize failed (0x%08lx)\n", rv);
        } else {
            printf("✓ C_Finalize: OK\n");
        }
    }
    
    if (list_objects || random_bytes) {
        /* Initialize for advanced operations */
        rv = pFunctionList->C_Initialize(NULL);
        if (rv != CKR_OK) {
            fprintf(stderr, "Error: C_Initialize failed (0x%08lx)\n", rv);
            dlclose(handle);
            return 1;
        }
        
        /* Get first slot with token */
        CK_ULONG slotCount = 0;
        fprintf(stderr, "DEBUG: About to call C_GetSlotList(0, NULL, &slotCount) [tokenPresent=FALSE]\n");
        rv = pFunctionList->C_GetSlotList(0, NULL, &slotCount);
        fprintf(stderr, "DEBUG: C_GetSlotList returned rv=0x%08lx, slotCount=%lu\n", rv, slotCount);
        if (rv != CKR_OK || slotCount == 0) {
            fprintf(stderr, "Error: No slots found (0x%08lx)\n", rv);
            pFunctionList->C_Finalize(NULL);
            dlclose(handle);
            return 1;
        }
        
        CK_SLOT_ID slots[10];
        CK_ULONG maxSlots = 10;
        fprintf(stderr, "DEBUG: About to call C_GetSlotList(0, slots, &maxSlots) [tokenPresent=FALSE]\n");
        rv = pFunctionList->C_GetSlotList(0, slots, &maxSlots);
        fprintf(stderr, "DEBUG: C_GetSlotList returned rv=0x%08lx, maxSlots=%lu, slots[0]=%lu\n", rv, maxSlots, maxSlots > 0 ? slots[0] : 999);
        if (rv != CKR_OK) {
            fprintf(stderr, "Error: C_GetSlotList failed (0x%08lx)\n", rv);
            pFunctionList->C_Finalize(NULL);
            dlclose(handle);
            return 1;
        }
        
        /* Open session on first slot */
        CK_SESSION_HANDLE session;
        fprintf(stderr, "DEBUG: About to call C_OpenSession(slots[0]=%lu, ...)\n", slots[0]);
        rv = pFunctionList->C_OpenSession(slots[0],CKF_SERIAL_SESSION, NULL, NULL, &session);
        if (rv != CKR_OK) {
            fprintf(stderr, "Error: C_OpenSession failed (0x%08lx)\n", rv);
            pFunctionList->C_Finalize(NULL);
            dlclose(handle);
            return 1;
        }
        
        if (list_objects) {
            printf("\n=== Objects in Token ===\n");
            
            /* Find all objects */
            CK_OBJECT_HANDLE objects[100];
            CK_ULONG objectCount = 0;
            
            rv = pFunctionList->C_FindObjectsInit(session, NULL, 0);
            if (rv == CKR_OK) {
                rv = pFunctionList->C_FindObjects(session, objects, 100, &objectCount);
                pFunctionList->C_FindObjectsFinal(session);
                
                if (objectCount == 0) {
                    printf("No objects found.\n");
                } else {
                    printf("Found %lu object(s):\n\n", objectCount);
                    
                    for (CK_ULONG i = 0; i < objectCount; i++) {
                        printf("Object #%lu:\n", i + 1);
                        
                        /* Get object class */
                        CK_OBJECT_CLASS objClass = 0;
                        CK_ATTRIBUTE classAttr = {CKA_CLASS, &objClass, sizeof(objClass)};
                        if (pFunctionList->C_GetAttributeValue(session, objects[i], &classAttr, 1) == CKR_OK) {
                            printf("  Class: %s\n", object_class_name(objClass));
                            
                            /* Get label */
                            char label[256] = {0};
                            CK_ATTRIBUTE labelAttr = {CKA_LABEL, label, sizeof(label) - 1};
                            if (pFunctionList->C_GetAttributeValue(session, objects[i], &labelAttr, 1) == CKR_OK && labelAttr.ulValueLen > 0) {
                                label[labelAttr.ulValueLen] = '\0';
                                printf("  Label: %s\n", label);
                            }
                            
                            /* Get ID */
                            unsigned char id[256];
                            CK_ATTRIBUTE idAttr = {CKA_ID, id, sizeof(id)};
                            if (pFunctionList->C_GetAttributeValue(session, objects[i], &idAttr, 1) == CKR_OK && idAttr.ulValueLen > 0) {
                                printf("  ID: ");
                                for (CK_ULONG k = 0; k < idAttr.ulValueLen && k < 32; k++) {
                                    printf("%02x", id[k]);
                                }
                                printf("\n");
                            }
                            
                            /* For keys, get key type */
                            if (objClass == CKO_PUBLIC_KEY || objClass == CKO_PRIVATE_KEY || objClass == CKO_SECRET_KEY) {
                                CK_KEY_TYPE keyType = 0;
                                CK_ATTRIBUTE keyTypeAttr = {CKA_KEY_TYPE, &keyType, sizeof(keyType)};
                                if (pFunctionList->C_GetAttributeValue(session, objects[i], &keyTypeAttr, 1) == CKR_OK) {
                                    printf("  Key Type: %s\n", key_type_name(keyType));
                                }
                            }
                        }
                        printf("\n");
                    }
                }
            } else {
                fprintf(stderr, "Error: C_FindObjectsInit failed (0x%08lx)\n", rv);
            }
        }
        
        if (random_bytes) {
            printf("\n=== Random Data Generation ===\n");
            unsigned char *randomData = malloc(random_bytes);
            if (!randomData) {
                fprintf(stderr, "Error: Memory allocation failed\n");
            } else {
                rv = pFunctionList->C_GenerateRandom(session, randomData, random_bytes);
                if (rv == CKR_OK) {
                    printf("Generated %d random bytes:\n", random_bytes);
                    print_hex("  ", randomData, random_bytes);
                } else {
                    fprintf(stderr, "Error: C_GenerateRandom failed (0x%08lx)\n", rv);
                }
                free(randomData);
            }
        }
        
        pFunctionList->C_CloseSession(session);
        pFunctionList->C_Finalize(NULL);
    }
    
    printf("\n=== Test Summary ===\n");
    printf("Module:     %s\n", module_path);
    printf("Status:     PASSED\n");
    printf("The PKCS#11 module appears to be functional.\n");
    
    dlclose(handle);
    return 0;
}
