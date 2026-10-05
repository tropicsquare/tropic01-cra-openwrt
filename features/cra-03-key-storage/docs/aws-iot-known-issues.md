# AWS IoT telemetry: known issues

**Status: not working. It is not part of cra-03's claim.** The `tropic01-aws` package is built and installed, but
its default configuration has an empty endpoint, so on a fresh image it does nothing and logs
`AWS endpoint not configured`. The work to make it reliable is tracked in the
[CRA backlog](../../../docs/cra/backlog.md), item 3.1.

## What it is meant to do

Publish telemetry to AWS IoT Core over MQTT with mutual TLS, authenticating with the same TROPIC01-held key as the
VPN: the private key stays in the chip and signs through the PKCS#11 module, and the device certificate is read from
R-MEM. The client is built on the coreMQTT library.

## What was observed

On an OpenWrt One, publishing failed with `rc=1`. Two independent causes were found.

### 1. The chip is held by another process

TROPIC01 supports only **one SPI session at a time**. `tropic01-aws-connect` stops OpenVPN, waits a fixed 2 seconds,
and then starts the publisher. That is sometimes too short: the publisher's PKCS#11 initialisation then fails with a
GPIO "resource busy" error, surfaced through `pkcs11-provider` as
`C_Initialize: Some problem has occurred with the token and/or slot`. Waiting longer, or checking that
`lt-util -i` succeeds twice in a row before publishing, made this error go away.

*Fix to implement:* poll for the chip (retry a cheap read such as `lt-util -i`, with a timeout) instead of sleeping a
fixed time.

### 2. The MQTT connect is rejected after TLS

With the first problem avoided, the PKCS#11 side worked (the private key was found in ECC slot 0) and the TLS
connection reached `SSL_read`, but the broker then closed the connection:

```
tropic01-aws-pub: SSL_read error 1
tropic01-aws-pub: MQTT_Connect failed: MQTTRecvFailed
```

This pattern usually means AWS IoT Core rejected the `CONNECT`, which is decided on the AWS side:

- the device certificate is not registered and **ACTIVE** in AWS IoT;
- it is not attached to a policy that allows `iot:Connect` for the client ID and `iot:Publish` on the topic used;
- the client ID or topic in `/etc/config/tropic01-aws` does not match the policy.

Nothing on the router can fix this; check the certificate status and the attached policy in the AWS IoT console.

## How to retest

1. Register the certificate stored in R-MEM slot 1 (the one for ECC slot 0) in AWS IoT, activate it, and attach a policy
   that allows `iot:Connect` and `iot:Publish` for your client ID and topic.
2. Configure the unit:

   ```sh
   uci set tropic01-aws.main.endpoint=<your-endpoint>.iot.<region>.amazonaws.com
   uci set tropic01-aws.main.client_id=<client id>
   uci set tropic01-aws.main.topic=<topic>
   uci commit tropic01-aws
   ```

3. Stop OpenVPN (it holds the chip), then publish once: `service tropic01-aws publish`, and read the result with
   `logread | grep tropic01-aws`.
