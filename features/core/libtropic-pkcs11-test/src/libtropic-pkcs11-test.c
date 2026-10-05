/*
 * Comprehensive PKCS#11 Test Utility for libtropic-pkcs11
 * Copyright (C) 2026 OpenWrt.org
 * Licensed under GPL-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pkcs11.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_MODULE "/usr/lib/libtropic_pkcs11.so"

/* Helper function to print hex data */
void print_hex(const char *prefix, unsigned char *data, size_t len) {
    printf("%s", prefix);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
        if ((i + 1) % 32 == 0 && i + 1 < len) 
            printf("\n%*s", (int)strlen(prefix), "");
    }
    printf("\n");
}

/* Test: Module Information */
int test_module_info(CK_FUNCTION_LIST_PTR pFuncs) {
    printf("\n=== Module Information Test ===\n");
    
    CK_INFO info;
    CK_RV rv = pFuncs->C_GetInfo(&info);
    if (rv != CKR_OK) {
        printf("FAILED: C_GetInfo returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Cryptoki version: %d.%d\n", info.cryptokiVersion.major, info.cryptokiVersion.minor);
    printf("Manufacturer:     %.32s\n", info.manufacturerID);
    printf("Library:          %.32s\n", info.libraryDescription);
    printf("Library version:  %d.%d\n", info.libraryVersion.major, info.libraryVersion.minor);
    printf("PASSED\n");
    return 0;
}

/* Test: Slot Information */
int test_slot_info(CK_FUNCTION_LIST_PTR pFuncs) {
    printf("\n=== Slot Information Test ===\n");
    
    CK_ULONG slotCount = 0;
    CK_RV rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &slotCount);
    if (rv != CKR_OK || slotCount == 0) {
        printf("FAILED: No slots found (0x%08lx)\n", rv);
        return 1;
    }
    
    printf("Number of slots: %lu\n", slotCount);
    
    CK_SLOT_ID slots[10];
    CK_ULONG maxSlots = 10;
    rv = pFuncs->C_GetSlotList(CK_FALSE, slots, &maxSlots);
    if (rv != CKR_OK) {
        printf("FAILED: C_GetSlotList returned 0x%08lx\n", rv);
        return 1;
    }
    
    for (CK_ULONG i = 0; i < maxSlots && i < slotCount; i++) {
        CK_SLOT_INFO slotInfo;
        rv = pFuncs->C_GetSlotInfo(slots[i], &slotInfo);
        if (rv == CKR_OK) {
            printf("Slot %lu:\n", slots[i]);
            printf("  Description: %.64s\n", slotInfo.slotDescription);
            printf("  Manufacturer: %.32s\n", slotInfo.manufacturerID);
            printf("  Flags: 0x%08lx", slotInfo.flags);
            if (slotInfo.flags & CKF_TOKEN_PRESENT) printf(" [TOKEN_PRESENT]");
            if (slotInfo.flags & CKF_HW_SLOT) printf(" [HW_SLOT]");
            printf("\n");
        }
    }
    
    printf("PASSED\n");
    return 0;
}

/* Test: Token Information */
int test_token_info(CK_FUNCTION_LIST_PTR pFuncs, CK_SLOT_ID slotID) {
    printf("\n=== Token Information Test ===\n");
    
    CK_TOKEN_INFO tokenInfo;
    CK_RV rv = pFuncs->C_GetTokenInfo(slotID, &tokenInfo);
    if (rv != CKR_OK) {
        printf("FAILED: C_GetTokenInfo returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Token label:      %.32s\n", tokenInfo.label);
    printf("Manufacturer:     %.32s\n", tokenInfo.manufacturerID);
    printf("Model:            %.16s\n", tokenInfo.model);
    printf("Serial number:    %.16s\n", tokenInfo.serialNumber);
    printf("Hardware version: %d.%d\n", tokenInfo.hardwareVersion.major, tokenInfo.hardwareVersion.minor);
    printf("Firmware version: %d.%d\n", tokenInfo.firmwareVersion.major, tokenInfo.firmwareVersion.minor);
    printf("Flags:            0x%08lx", tokenInfo.flags);
    if (tokenInfo.flags & CKF_RNG) printf(" [RNG]");
    if (tokenInfo.flags & CKF_TOKEN_INITIALIZED) printf(" [INITIALIZED]");
    printf("\n");
    printf("Max sessions:     %lu\n", tokenInfo.ulMaxSessionCount);
    printf("Current sessions: %lu\n", tokenInfo.ulSessionCount);
    
    printf("PASSED\n");
    return 0;
}

/* Test: Mechanism List */
int test_mechanisms(CK_FUNCTION_LIST_PTR pFuncs, CK_SLOT_ID slotID) {
    printf("\n=== Mechanism Discovery Test ===\n");
    
    CK_ULONG mechCount = 0;
    CK_RV rv = pFuncs->C_GetMechanismList(slotID, NULL, &mechCount);
    if (rv != CKR_OK) {
        printf("FAILED: C_GetMechanismList returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Number of mechanisms: %lu\n", mechCount);
    
    if (mechCount > 0) {
        CK_MECHANISM_TYPE mechs[100];
        CK_ULONG maxMechs = 100;
        rv = pFuncs->C_GetMechanismList(slotID, mechs, &maxMechs);
        if (rv != CKR_OK) {
            printf("FAILED: C_GetMechanismList(2) returned 0x%08lx\n", rv);
            return 1;
        }
        
        for (CK_ULONG i = 0; i < maxMechs && i < mechCount; i++) {
            printf("  [%lu] Type: 0x%08lx", i+1, mechs[i]);
            
            CK_MECHANISM_INFO mechInfo;
            rv = pFuncs->C_GetMechanismInfo(slotID, mechs[i], &mechInfo);
            if (rv == CKR_OK) {
                printf(" (Key: %lu-%lu bits, Flags: 0x%08lx", 
                       mechInfo.ulMinKeySize, mechInfo.ulMaxKeySize, mechInfo.flags);
                if (mechInfo.flags & CKF_SIGN) printf(" [SIGN]");
                if (mechInfo.flags & CKF_VERIFY) printf(" [VERIFY]");
                if (mechInfo.flags & CKF_GENERATE) printf(" [GENERATE]");
                if (mechInfo.flags & CKF_GENERATE_KEY_PAIR) printf(" [GENERATE_KEY_PAIR]");
                printf(")");
            }
            printf("\n");
        }
    }
    
    printf("PASSED\n");
    return 0;
}

/* Test: Random Number Generation */
int test_random(CK_FUNCTION_LIST_PTR pFuncs, CK_SLOT_ID slotID, int verbose) {
    printf("\n=== Random Number Generation Test ===\n");
    
    CK_SESSION_HANDLE session;
    CK_RV rv = pFuncs->C_OpenSession(slotID, CKF_SERIAL_SESSION, NULL, NULL, &session);
    if (rv != CKR_OK) {
        printf("FAILED: C_OpenSession returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Session opened: 0x%lx\n", (unsigned long)session);
    
    size_t sizes[] = {8, 16, 32, 64, 128, 256};
    int failed = 0;
    
    for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
        unsigned char random[256];
        rv = pFuncs->C_GenerateRandom(session, random, sizes[i]);
        if (rv == CKR_OK) {
            if (verbose) {
                char prefix[32];
                snprintf(prefix, sizeof(prefix), "  %3zu bytes: ", sizes[i]);
                print_hex(prefix, random, sizes[i] > 32 ? 32 : sizes[i]);
                if (sizes[i] > 32) printf("             ... (%zu more bytes)\n", sizes[i] - 32);
            } else {
                printf("  %3zu bytes: OK\n", sizes[i]);
            }
        } else {
            printf("  %3zu bytes: FAILED (0x%08lx)\n", sizes[i], rv);
            failed = 1;
        }
    }
    
    pFuncs->C_CloseSession(session);
    
    if (failed) {
        printf("FAILED\n");
        return 1;
    }
    
    printf("PASSED\n");
    return 0;
}

/* Test: Session Management */
int test_session(CK_FUNCTION_LIST_PTR pFuncs, CK_SLOT_ID slotID) {
    printf("\n=== Session Management Test ===\n");
    
    CK_SESSION_HANDLE session;
    CK_RV rv = pFuncs->C_OpenSession(slotID, CKF_SERIAL_SESSION, NULL, NULL, &session);
    if (rv != CKR_OK) {
        printf("FAILED: C_OpenSession returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Session opened: 0x%lx\n", (unsigned long)session);
    
    CK_SESSION_INFO sessionInfo;
    rv = pFuncs->C_GetSessionInfo(session, &sessionInfo);
    if (rv != CKR_OK) {
        printf("FAILED: C_GetSessionInfo returned 0x%08lx\n", rv);
        pFuncs->C_CloseSession(session);
        return 1;
    }
    
    printf("  Slot ID: %lu\n", sessionInfo.slotID);
    printf("  State: 0x%08lx\n", sessionInfo.state);
    printf("  Flags: 0x%08lx", sessionInfo.flags);
    if (sessionInfo.flags & CKF_SERIAL_SESSION) printf(" [SERIAL_SESSION]");
    if (sessionInfo.flags & CKF_RW_SESSION) printf(" [RW_SESSION]");
    printf("\n");
    
    rv = pFuncs->C_CloseSession(session);
    if (rv != CKR_OK) {
        printf("FAILED: C_CloseSession returned 0x%08lx\n", rv);
        return 1;
    }
    
    printf("Session closed successfully\n");
    printf("PASSED\n");
    return 0;
}

/* Test: Performance (multiple iterations) */
int test_performance(CK_FUNCTION_LIST_PTR pFuncs, CK_SLOT_ID slotID, int iterations) {
    printf("\n=== Performance Test (%d iterations) ===\n", iterations);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int failed = 0;
    for (int i = 0; i < iterations; i++) {
        CK_SESSION_HANDLE session;
        CK_RV rv = pFuncs->C_OpenSession(slotID, CKF_SERIAL_SESSION, NULL, NULL, &session);
        if (rv != CKR_OK) {
            failed++;
            continue;
        }
        
        unsigned char random[32];
        rv = pFuncs->C_GenerateRandom(session, random, 32);
        if (rv != CKR_OK) {
            failed++;
        }
        
        pFuncs->C_CloseSession(session);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double rate = iterations / elapsed;
    
    printf("Completed: %d operations\n", iterations);
    printf("Failed:    %d operations\n", failed);
    printf("Time:      %.3f seconds\n", elapsed);
    printf("Rate:      %.1f ops/sec\n", rate);
    
    if (failed > 0) {
        printf("FAILED\n");
        return 1;
    }
    
    printf("PASSED\n");
    return 0;
}

/* Run all tests */
int run_all_tests(const char *module_path, int verbose) {
    printf("==================================================\n");
    printf("  PKCS#11 Comprehensive Test Suite\n");
    printf("  Module: %s\n", module_path);
    printf("==================================================\n");
    
    void *handle = dlopen(module_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "ERROR: Failed to load module: %s\n", dlerror());
        return 1;
    }
    
    CK_RV (*pGetFunctionList)(CK_FUNCTION_LIST_PTR_PTR) = dlsym(handle, "C_GetFunctionList");
    if (!pGetFunctionList) {
        fprintf(stderr, "ERROR: C_GetFunctionList not found\n");
        dlclose(handle);
        return 1;
    }
    
    CK_FUNCTION_LIST_PTR pFuncs = NULL;
    CK_RV rv = pGetFunctionList(&pFuncs);
    if (rv != CKR_OK) {
        fprintf(stderr, "ERROR: C_GetFunctionList failed: 0x%08lx\n", rv);
        dlclose(handle);
        return 1;
    }
    
    /* Initialize */
    rv = pFuncs->C_Initialize(NULL);
    if (rv != CKR_OK) {
        fprintf(stderr, "ERROR: C_Initialize failed: 0x%08lx\n", rv);
        dlclose(handle);
        return 1;
    }
    
    /* Get first slot */
    CK_ULONG slotCount = 0;
    rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &slotCount);
    if (rv != CKR_OK || slotCount == 0) {
        fprintf(stderr, "ERROR: No slots found\n");
        pFuncs->C_Finalize(NULL);
        dlclose(handle);
        return 1;
    }
    
    CK_SLOT_ID slots[10];
    CK_ULONG maxSlots = 10;
    pFuncs->C_GetSlotList(CK_FALSE, slots, &maxSlots);
    CK_SLOT_ID slotID = slots[0];
    
    /* Run tests */
    int failed = 0;
    failed += test_module_info(pFuncs);
    failed += test_slot_info(pFuncs);
    failed += test_token_info(pFuncs, slotID);
    failed += test_mechanisms(pFuncs, slotID);
    failed += test_random(pFuncs, slotID, verbose);
    failed += test_session(pFuncs, slotID);
    failed += test_performance(pFuncs, slotID, 10);
    
    /* Finalize */
    pFuncs->C_Finalize(NULL);
    dlclose(handle);
    
    printf("\n==================================================\n");
    if (failed == 0) {
        printf("  ALL TESTS PASSED ✓\n");
    } else {
        printf("  %d TEST(S) FAILED ✗\n", failed);
    }
    printf("==================================================\n");
    
    return failed > 0 ? 1 : 0;
}

/* Print usage */
void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Comprehensive PKCS#11 test utility for libtropic-pkcs11\n");
    printf("\n");
    printf("Options:\n");
    printf("  --module <path>    Path to PKCS#11 module (default: %s)\n", DEFAULT_MODULE);
    printf("  --all              Run all tests (default)\n");
    printf("  --info             Show module/slot/token information\n");
    printf("  --random           Test random number generation\n");
    printf("  --mechanisms       Show supported mechanisms\n");
    printf("  --session          Test session management\n");
    printf("  --performance <n>  Run performance test with n iterations\n");
    printf("  --verbose          Show detailed output\n");
    printf("  --help             Show this help message\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                              # Run all tests\n", prog);
    printf("  %s --info                       # Show information only\n", prog);
    printf("  %s --random --verbose           # Test random with details\n", prog);
    printf("  %s --performance 100            # Performance test (100 iterations)\n", prog);
    printf("\n");
}

int main(int argc, char *argv[]) {
    const char *module_path = DEFAULT_MODULE;
    int run_all = 1;
    int test_info = 0;
    int test_rng = 0;
    int test_mech = 0;
    int test_sess = 0;
    int test_perf = 0;
    int perf_iterations = 10;
    int verbose = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--module") == 0 && i + 1 < argc) {
            module_path = argv[++i];
        } else if (strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        } else if (strcmp(argv[i], "--info") == 0) {
            run_all = 0;
            test_info = 1;
        } else if (strcmp(argv[i], "--random") == 0) {
            run_all = 0;
            test_rng = 1;
        } else if (strcmp(argv[i], "--mechanisms") == 0) {
            run_all = 0;
            test_mech = 1;
        } else if (strcmp(argv[i], "--session") == 0) {
            run_all = 0;
            test_sess = 1;
        } else if (strcmp(argv[i], "--performance") == 0 && i + 1 < argc) {
            run_all = 0;
            test_perf = 1;
            perf_iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    /* If --all or no specific test selected, run all */
    if (run_all) {
        return run_all_tests(module_path, verbose);
    }
    
    /* Run specific tests */
    void *handle = dlopen(module_path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "ERROR: Failed to load module: %s\n", dlerror());
        return 1;
    }
    
    CK_RV (*pGetFunctionList)(CK_FUNCTION_LIST_PTR_PTR) = dlsym(handle, "C_GetFunctionList");
    if (!pGetFunctionList) {
        fprintf(stderr, "ERROR: C_GetFunctionList not found\n");
        dlclose(handle);
        return 1;
    }
    
    CK_FUNCTION_LIST_PTR pFuncs = NULL;
    CK_RV rv = pGetFunctionList(&pFuncs);
    if (rv != CKR_OK) {
        fprintf(stderr, "ERROR: C_GetFunctionList failed: 0x%08lx\n", rv);
        dlclose(handle);
        return 1;
    }
    
    rv = pFuncs->C_Initialize(NULL);
    if (rv != CKR_OK) {
        fprintf(stderr, "ERROR: C_Initialize failed: 0x%08lx\n", rv);
        dlclose(handle);
        return 1;
    }
    
    CK_ULONG slotCount = 0;
    rv = pFuncs->C_GetSlotList(CK_FALSE, NULL, &slotCount);
    if (rv != CKR_OK || slotCount == 0) {
        fprintf(stderr, "ERROR: No slots found\n");
        pFuncs->C_Finalize(NULL);
        dlclose(handle);
        return 1;
    }
    
    CK_SLOT_ID slots[10];
    CK_ULONG maxSlots = 10;
    pFuncs->C_GetSlotList(CK_FALSE, slots, &maxSlots);
    CK_SLOT_ID slotID = slots[0];
    
    int failed = 0;
    if (test_info) {
        failed += test_module_info(pFuncs);
        failed += test_slot_info(pFuncs);
        failed += test_token_info(pFuncs, slotID);
    }
    if (test_rng) {
        failed += test_random(pFuncs, slotID, verbose);
    }
    if (test_mech) {
        failed += test_mechanisms(pFuncs, slotID);
    }
    if (test_sess) {
        failed += test_session(pFuncs, slotID);
    }
    if (test_perf) {
        failed += test_performance(pFuncs, slotID, perf_iterations);
    }
    
    pFuncs->C_Finalize(NULL);
    dlclose(handle);
    
    return failed > 0 ? 1 : 0;
}
