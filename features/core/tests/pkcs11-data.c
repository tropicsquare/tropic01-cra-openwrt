#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#define CK_PTR *
#define CK_DEFINE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION(returnType, name) returnType name
#define CK_DECLARE_FUNCTION_POINTER(returnType, name) returnType (* name)
#define CK_CALLBACK_FUNCTION(returnType, name) returnType (* name)
#ifndef NULL_PTR
#define NULL_PTR 0
#endif

#include <pkcs11.h>

void print_hex(const char *label, unsigned char *data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len) printf("\n%*s", (int)strlen(label) + 2, "");
    }
    printf("\n");
}

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
        case 0x0000001F: return "AES";  /* CKK_AES */
        default: return "Unknown";
    }
}

int main(int argc, char *argv[]) {
    const char *module_path = "/usr/lib/libtropic_pkcs11.so";
    int random_bytes = 32;
    
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: %s [random_bytes]\n", argv[0]);
            printf("  Lists PKCS#11 objects and generates random data\n");
            printf("  random_bytes: number of random bytes (default: 32)\n");
            return 0;
        }
        random_bytes = atoi(argv[1]);
        if (random_bytes <= 0 || random_bytes > 1024) {
            printf("Invalid random bytes count (1-1024)\n");
            return 1;
        }
    }
    
    // Load module
    void *handle = dlopen(module_path, RTLD_NOW);
    if (!handle) {
        printf("Failed to load module: %s\n", dlerror());
        return 1;
    }
    
    CK_RV (*C_GetFunctionList)(CK_FUNCTION_LIST_PTR_PTR);
    *(void **)(&C_GetFunctionList) = dlsym(handle, "C_GetFunctionList");
    
    CK_FUNCTION_LIST_PTR funcs;
    CK_RV rv = C_GetFunctionList(&funcs);
    if (rv != CKR_OK) {
        printf("C_GetFunctionList failed: 0x%lx\n", rv);
        dlclose(handle);
        return 1;
    }
    
    // Initialize
    rv = funcs->C_Initialize(NULL);
    if (rv != CKR_OK) {
        printf("C_Initialize failed: 0x%lx (is device connected?)\n", rv);
        dlclose(handle);
        return 1;
    }
    
    printf("=== PKCS#11 Module: %s ===\n\n", module_path);
    
    // Get slots
    CK_ULONG slotCount;
    rv = funcs->C_GetSlotList(CK_TRUE, NULL, &slotCount);
    if (rv != CKR_OK) {
        printf("C_GetSlotList failed: 0x%lx\n", rv);
        funcs->C_Finalize(NULL);
        dlclose(handle);
        return 1;
    }
    
    printf("Number of slots with tokens: %lu\n\n", slotCount);
    
    if (slotCount == 0) {
        printf("No tokens found.\n");
        funcs->C_Finalize(NULL);
        dlclose(handle);
        return 0;
    }
    
    CK_SLOT_ID *slots = malloc(slotCount * sizeof(CK_SLOT_ID));
    rv = funcs->C_GetSlotList(CK_TRUE, slots, &slotCount);
    
    for (CK_ULONG i = 0; i < slotCount; i++) {
        printf("=== Slot %lu (ID: %lu) ===\n", i, slots[i]);
        
        // Get token info
        CK_TOKEN_INFO tokenInfo;
        rv = funcs->C_GetTokenInfo(slots[i], &tokenInfo);
        if (rv == CKR_OK) {
            printf("Token Label:       %.32s\n", tokenInfo.label);
            printf("Manufacturer:      %.32s\n", tokenInfo.manufacturerID);
            printf("Model:             %.16s\n", tokenInfo.model);
            printf("Serial Number:     %.16s\n", tokenInfo.serialNumber);
            printf("Flags:             0x%08lx\n", tokenInfo.flags);
        }
        
        // Open session
        CK_SESSION_HANDLE session;
        rv = funcs->C_OpenSession(slots[i], CKF_SERIAL_SESSION, NULL, NULL, &session);
        if (rv != CKR_OK) {
            printf("Failed to open session: 0x%lx\n", rv);
            continue;
        }
        
        printf("\n--- Objects in Token ---\n");
        
        // Find all objects
        CK_OBJECT_HANDLE objects[100];
        CK_ULONG objectCount;
        
        rv = funcs->C_FindObjectsInit(session, NULL, 0);
        if (rv == CKR_OK) {
            rv = funcs->C_FindObjects(session, objects, 100, &objectCount);
            funcs->C_FindObjectsFinal(session);
            
            if (objectCount == 0) {
                printf("No objects found in token.\n");
            } else {
                printf("Found %lu object(s):\n\n", objectCount);
                
                for (CK_ULONG j = 0; j < objectCount; j++) {
                    printf("Object #%lu:\n", j + 1);
                    
                    // Get object class
                    CK_OBJECT_CLASS objClass;
                    CK_ATTRIBUTE classAttr = {CKA_CLASS, &objClass, sizeof(objClass)};
                    if (funcs->C_GetAttributeValue(session, objects[j], &classAttr, 1) == CKR_OK) {
                        printf("  Class:      %s (0x%lx)\n", object_class_name(objClass), objClass);
                        
                        // Get label
                        char label[256] = {0};
                        CK_ATTRIBUTE labelAttr = {CKA_LABEL, label, sizeof(label) - 1};
                        if (funcs->C_GetAttributeValue(session, objects[j], &labelAttr, 1) == CKR_OK) {
                            printf("  Label:      %s\n", label);
                        }
                        
                        // Get ID
                        unsigned char id[256];
                        CK_ATTRIBUTE idAttr = {CKA_ID, id, sizeof(id)};
                        if (funcs->C_GetAttributeValue(session, objects[j], &idAttr, 1) == CKR_OK && idAttr.ulValueLen > 0) {
                            printf("  ID:         ");
                            for (CK_ULONG k = 0; k < idAttr.ulValueLen && k < 32; k++) {
                                printf("%02x", id[k]);
                            }
                            printf("\n");
                        }
                        
                        // For keys, get key type
                        if (objClass == CKO_PUBLIC_KEY || objClass == CKO_PRIVATE_KEY || objClass == CKO_SECRET_KEY) {
                            CK_KEY_TYPE keyType;
                            CK_ATTRIBUTE keyTypeAttr = {CKA_KEY_TYPE, &keyType, sizeof(keyType)};
                            if (funcs->C_GetAttributeValue(session, objects[j], &keyTypeAttr, 1) == CKR_OK) {
                                printf("  Key Type:   %s (0x%lx)\n", key_type_name(keyType), keyType);
                            }
                        }
                    }
                    printf("\n");
                }
            }
        } else {
            printf("C_FindObjectsInit failed: 0x%lx\n", rv);
        }
        
        // Generate random data
        printf("--- Random Data Generation ---\n");
        unsigned char *randomData = malloc(random_bytes);
        rv = funcs->C_GenerateRandom(session, randomData, random_bytes);
        if (rv == CKR_OK) {
            print_hex("Random bytes", randomData, random_bytes);
        } else {
            printf("C_GenerateRandom failed: 0x%lx\n", rv);
        }
        free(randomData);
        
        // Close session
        funcs->C_CloseSession(session);
        printf("\n");
    }
    
    free(slots);
    funcs->C_Finalize(NULL);
    dlclose(handle);
    
    return 0;
}
