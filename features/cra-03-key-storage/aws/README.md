# AWS IoT Credential Files

This directory holds the AWS IoT trust-anchor certificates required by
`tropic01-aws-pub` to verify the AWS IoT Core endpoint.

## Files in this directory

| File | Description | Source |
|------|-------------|--------|
| `AmazonRootCA1.pem` | Amazon Root CA 1 (RSA 2048) | Public — download from Amazon |
| `AmazonRootCA3.pem` | Amazon Root CA 3 (ECC 256) | Public — download from Amazon |

## Device certificate files (NOT committed)

Device-specific certificates are generated per-device during provisioning
and **must never be committed to this repository**.  They follow this naming
convention used by the AWS Console bulk-download:

```
<certificate-id>-certificate.pem.crt
<certificate-id>-private.pem.key
```

These files are excluded from git via `.gitignore`.

## Re-downloading the Root CAs

```sh
curl -o AmazonRootCA1.pem https://www.amazontrust.com/repository/AmazonRootCA1.pem
curl -o AmazonRootCA3.pem https://www.amazontrust.com/repository/AmazonRootCA3.pem
```

## Provisioning

Run the `tropic01-aws-provision` script on the router to generate a device
key inside TROPIC01 and create a CSR.  Register the CSR in AWS IoT Core to
obtain a device certificate, then deploy it as described in the
[TROPIC01 AWS IoT guide](../docs/solution-description.md).
