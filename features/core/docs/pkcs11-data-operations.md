# Getting Data from PKCS#11 Module

## Overview

The libtropic-pkcs11 module provides access to cryptographic operations and data through the PKCS#11 (Cryptoki) API.

## Current Status

✅ Module: `/usr/lib/libtropic_pkcs11.so`  
✅ Device: `/dev/spidev2.0` + `/dev/gpiochip0` pin 25 (TROPIC01 on SPI)  
✅ Initialized: Working

## Available Operations

### 1. Query Slots and Tokens

```c
// Get number of slots with tokens present
CK_ULONG slotCount;
rv = C_GetSlotList(CK_TRUE, NULL, &slotCount);

// Get slot IDs
CK_SLOT_ID slots[slotCount];
rv = C_GetSlotList(CK_TRUE, slots, &slotCount);

// Get slot information
CK_SLOT_INFO slotInfo;
rv = C_GetSlotInfo(slots[0], &slotInfo);
// Returns: description, manufacturer ID, hardware/firmware version

// Get token information
CK_TOKEN_INFO tokenInfo;
rv = C_GetTokenInfo(slots[0], &tokenInfo);
// Returns: label, manufacturer, model, serial number, flags
```

### 2. List Objects (Keys, Certificates)

```c
// Open a session
CK_SESSION_HANDLE session;
rv = C_OpenSession(slotId, CKF_SERIAL_SESSION, NULL, NULL, &session);

// Find all objects
CK_OBJECT_HANDLE objects[100];
CK_ULONG objectCount;
rv = C_FindObjectsInit(session, NULL, 0);
rv = C_FindObjects(session, objects, 100, &objectCount);
rv = C_FindObjectsFinal(session);

// Get object attributes
for (int i = 0; i < objectCount; i++) {
    // Get object class (key, certificate, data)
    CK_OBJECT_CLASS objClass;
    CK_ATTRIBUTE attr = {CKA_CLASS, &objClass, sizeof(objClass)};
    rv = C_GetAttributeValue(session, objects[i], &attr, 1);
    
    // Get key type (for keys)
    CK_KEY_TYPE keyType;
    attr.type = CKA_KEY_TYPE;
    attr.pValue = &keyType;
    rv = C_GetAttributeValue(session, objects[i], &attr, 1);
    
    // Get label
    char label[256];
    attr.type = CKA_LABEL;
    attr.pValue = label;
    attr.ulValueLen = sizeof(label);
    rv = C_GetAttributeValue(session, objects[i], &attr, 1);
}
```

### 3. Query Supported Mechanisms

```c
// Get mechanism list
CK_ULONG mechCount;
rv = C_GetMechanismList(slotId, NULL, &mechCount);

CK_MECHANISM_TYPE mechanisms[mechCount];
rv = C_GetMechanismList(slotId, mechanisms, &mechCount);

// Get mechanism info
for (int i = 0; i < mechCount; i++) {
    CK_MECHANISM_INFO mechInfo;
    rv = C_GetMechanismInfo(slotId, mechanisms[i], &mechInfo);
    // Returns: min/max key size, flags (sign, verify, encrypt, decrypt, etc.)
}
```

### 4. Read Certificates

```c
// Find certificates
CK_OBJECT_CLASS certClass = CKO_CERTIFICATE;
CK_ATTRIBUTE certTemplate[] = {
    {CKA_CLASS, &certClass, sizeof(certClass)}
};

rv = C_FindObjectsInit(session, certTemplate, 1);
rv = C_FindObjects(session, &certHandle, 1, &objectCount);
rv = C_FindObjectsFinal(session);

// Get certificate value
CK_ATTRIBUTE valueAttr = {CKA_VALUE, NULL, 0};
rv = C_GetAttributeValue(session, certHandle, &valueAttr, 1);

unsigned char *certData = malloc(valueAttr.ulValueLen);
valueAttr.pValue = certData;
rv = C_GetAttributeValue(session, certHandle, &valueAttr, 1);
// certData now contains the DER-encoded certificate
```

### 5. Get Public Key Data

```c
// Find public keys
CK_OBJECT_CLASS keyClass = CKO_PUBLIC_KEY;
CK_ATTRIBUTE pubKeyTemplate[] = {
    {CKA_CLASS, &keyClass, sizeof(keyClass)}
};

rv = C_FindObjectsInit(session, pubKeyTemplate, 1);
rv = C_FindObjects(session, &keyHandle, 1, &objectCount);
rv = C_FindObjectsFinal(session);

// For EC public key
CK_ATTRIBUTE ecAttrs[] = {
    {CKA_EC_PARAMS, NULL, 0},  // Curve parameters (OID)
    {CKA_EC_POINT, NULL, 0}    // Public key point
};
rv = C_GetAttributeValue(session, keyHandle, ecAttrs, 2);
```

### 6. Perform Cryptographic Operations

The module implements three mechanisms: `CKM_EC_KEY_PAIR_GEN`, `CKM_ECDSA`
and `CKM_EDDSA`.

#### Sign Data
```c
// Initialize signing operation
// 32-byte SHA-256 hash; longer input is cut to 32 bytes, shorter is CKR_DATA_LEN_RANGE
CK_MECHANISM mechanism = {CKM_ECDSA, NULL, 0};
rv = C_SignInit(session, &mechanism, privateKeyHandle);

// Sign the hash
unsigned char hash[32];  // SHA-256 of the data, computed on the host
unsigned char signature[64];
CK_ULONG signatureLen = sizeof(signature);
rv = C_Sign(session, hash, sizeof(hash), signature, &signatureLen);
```

#### Generate Random Data
```c
unsigned char randomData[32];
rv = C_GenerateRandom(session, randomData, sizeof(randomData));
```

#### Verify, Encrypt, Decrypt, Digest

Verify, encrypt, decrypt and digest are not implemented by the module; verify on the host with OpenSSL.

## Quick Test Script

Here's what you can do right now on your device:

```bash
# Check what the module supports
ssh root@<router-ip> 'pkcs11-test --module /usr/lib/libtropic_pkcs11.so --test-init'
```

## Common PKCS#11 Return Codes

```c
CKR_OK                          = 0x00000000  // Success
CKR_DEVICE_ERROR                = 0x00000030  // Device not connected
CKR_DEVICE_REMOVED              = 0x000000E1  // Device unplugged
CKR_TOKEN_NOT_PRESENT           = 0x000000E0  // No token in slot
CKR_FUNCTION_NOT_SUPPORTED      = 0x00000054  // Operation not supported
CKR_KEY_HANDLE_INVALID          = 0x00000060  // Invalid key handle
CKR_MECHANISM_INVALID           = 0x00000070  // Unsupported mechanism
CKR_PIN_INCORRECT               = 0x000000A0  // Wrong PIN
CKR_SESSION_CLOSED              = 0x000000B0  // Session not open
CKR_CRYPTOKI_NOT_INITIALIZED    = 0x00000190  // Not initialized
```

## Full Example: List All Objects

```c
#include <stdio.h>
#include <dlfcn.h>
#include "pkcs11.h"

int main() {
    // Load module
    void *handle = dlopen("/usr/lib/libtropic_pkcs11.so", RTLD_NOW);
    
    // Get function list
    CK_RV (*C_GetFunctionList)(CK_FUNCTION_LIST_PTR_PTR);
    *(void **)(&C_GetFunctionList) = dlsym(handle, "C_GetFunctionList");
    
    CK_FUNCTION_LIST_PTR funcs;
    C_GetFunctionList(&funcs);
    
    // Initialize
    funcs->C_Initialize(NULL);
    
    // Get first slot
    CK_SLOT_ID slots[10];
    CK_ULONG slotCount = 10;
    funcs->C_GetSlotList(CK_TRUE, slots, &slotCount);
    
    if (slotCount > 0) {
        // Open session
        CK_SESSION_HANDLE session;
        funcs->C_OpenSession(slots[0], CKF_SERIAL_SESSION, NULL, NULL, &session);
        
        // Find all objects
        CK_OBJECT_HANDLE objects[100];
        CK_ULONG objectCount;
        
        funcs->C_FindObjectsInit(session, NULL, 0);
        funcs->C_FindObjects(session, objects, 100, &objectCount);
        funcs->C_FindObjectsFinal(session);
        
        printf("Found %lu objects:\n", objectCount);
        
        for (CK_ULONG i = 0; i < objectCount; i++) {
            // Get object class
            CK_OBJECT_CLASS objClass;
            CK_ATTRIBUTE attr[] = {
                {CKA_CLASS, &objClass, sizeof(objClass)}
            };
            
            if (funcs->C_GetAttributeValue(session, objects[i], attr, 1) == CKR_OK) {
                printf("  Object %lu: ", i);
                switch(objClass) {
                    case CKO_DATA: printf("Data"); break;
                    case CKO_CERTIFICATE: printf("Certificate"); break;
                    case CKO_PUBLIC_KEY: printf("Public Key"); break;
                    case CKO_PRIVATE_KEY: printf("Private Key"); break;
                    case CKO_SECRET_KEY: printf("Secret Key"); break;
                    default: printf("Unknown (0x%lx)", objClass);
                }
                printf("\n");
            }
        }
        
        funcs->C_CloseSession(session);
    }
    
    funcs->C_Finalize(NULL);
    dlclose(handle);
    return 0;
}
```

## Integration Examples

### OpenSSH with PKCS#11

```bash
# Generate SSH key using PKCS#11
ssh-keygen -D /usr/lib/libtropic_pkcs11.so

# Use PKCS#11 key for SSH authentication
ssh -I /usr/lib/libtropic_pkcs11.so user@server
```

### OpenVPN with PKCS#11

```
# In OpenVPN config
pkcs11-providers /usr/lib/libtropic_pkcs11.so
pkcs11-id 'serialized-id-of-key'
```

### OpenSSL with PKCS#11

Use `pkcs11-provider`, as `cra-03-key-storage` configures it:

```bash
# Sign a 32-byte hash with the key in ECC slot 1
OPENSSL_CONF=/etc/ssl/tropic01-aws/openssl-aws.cnf \
  openssl pkeyutl -sign -inkey "pkcs11:id=%01;type=private" -in hash.bin -out sig.bin
```

## Documentation References

- **PKCS#11 v2.40 Specification**: http://docs.oasis-open.org/pkcs11/pkcs11-base/v2.40/
- **libtropic-pkcs11 GitHub**: https://github.com/tropicsquare/libtropic-pkcs11
- **OpenSC PKCS#11 Guide**: https://github.com/OpenSC/OpenSC/wiki/PKCS11-tool

## Next Steps

To get started with data operations:

1. **Check what objects exist** on the token
2. **Query supported mechanisms** (key generation, ECDSA, EdDSA)
3. **Read public keys/certificates** if available
4. **Perform cryptographic operations** (sign, verify, etc.)

The module is working - you can now use it for:
- Digital signatures (ECDSA P-256, EdDSA Ed25519)
- Key generation
- Random number generation
- Reading certificates and data from R-MEM
