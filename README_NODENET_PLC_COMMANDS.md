# NodeNet PLC Commands

This document describes the NodeNet PLC commands implemented by the firmware for browsing point definitions, browsing point states, creating or updating points, and deleting points.

These commands are handled in the NodeNet core and operate on the local point catalog.

## Overview

Supported commands:

- `pointDefsReq`
- `pointStatesReq`
- `pointUpsert`
- `pointDelete`
- `updateProperty` for writable local properties and writable Modbus coil points

The point identity model is hierarchical:

- `deviceId`
- `feature`
- `pointId`

The optional `path` field used by `pointDefsReq` and `pointStatesReq` is built like this:

```text
deviceId.feature.pointId
```

Examples:

- empty path: browse devices
- `gb9fao5yk4f`: browse one device and its features
- `gb9fao5yk4f.modbus0.eurotherm6100`: browse points under one feature
- `gb9fao5yk4f.modbus0.eurotherm6100.ch1`: browse one exact point

Both browse commands support pagination:

- `offset`: first element to return
- `limit`: maximum number of elements to return
- `count`: number of elements returned in this response
- `total`: total number of matching elements
- `hasMore`: `true` when more elements are available

Transport note:

- when requests are sent from a PC through the NodeNet master bridge, use `from = 255`
- the firmware routes replies back to `request.from`
- `from = 255` is treated as the desktop endpoint and the reply is re-emitted so the PC can receive it through the master
- using `from = 5` or another regular NodeNet address sends the reply to that NodeNet node, not back to the PC link

## pointDefsReq

Requests point definitions from the local catalog.

### Request

```json
{
  "cmd": "pointDefsReq",
  "from": 5,
  "to": 4,
  "path": "",
  "offset": 0,
  "limit": 8
}
```

### Device-level response

With an empty path, or with a `deviceId`, the response kind is `devices`.

```json
{
  "cmd": "pointDefsRes",
  "to": 5,
  "path": "",
  "offset": 0,
  "kind": "devices",
  "count": 1,
  "total": 1,
  "hasMore": false,
  "devices": [
    {
      "deviceId": "gb9fao5yk4f",
      "features": [
        "core",
        "modbus0",
        "modbus0.waveshare8ch",
        "modbus0.eurotherm6100"
      ]
    }
  ]
}
```

### Feature-level response

With a `deviceId.feature` path, the response kind is `points`.

```json
{
  "cmd": "pointDefsRes",
  "to": 5,
  "path": "gb9fao5yk4f.modbus0.eurotherm6100",
  "offset": 0,
  "kind": "points",
  "count": 3,
  "total": 3,
  "hasMore": false,
  "points": [
    {
      "deviceId": "gb9fao5yk4f",
      "feature": "modbus0.eurotherm6100",
      "pointId": "ch1",
      "displayName": "Eurotherm CH1 PV",
      "backend": 1,
      "direction": 0,
      "valueType": 2,
      "refreshMs": 1000,
      "timeoutMs": 3000,
      "stringCapacity": 0,
      "portIndex": 0,
      "slaveAddress": 2,
      "address": 41433,
      "registerCount": 1,
      "table": 3,
      "access": 1
    }
  ]
}
```

## pointStatesReq

Requests live point states from the local catalog.

### Request

```json
{
  "cmd": "pointStatesReq",
  "from": 5,
  "to": 4,
  "path": "gb9fao5yk4f.modbus0.eurotherm6100",
  "offset": 0,
  "limit": 4
}
```

### Point-state response

With a `deviceId.feature` or `deviceId.feature.pointId` path, the response kind is `points` and the returned array is `pointStates`.

`lastUpdateAgeMs` is the elapsed time in milliseconds since the last poll or write attempt for that point.

`lastGoodUpdateAgeMs` is the elapsed time in milliseconds since the last successful poll or write for that point. It stays `0` if the point has never had a good value yet.

```json
{
  "cmd": "pointStatesRes",
  "to": 5,
  "path": "gb9fao5yk4f.modbus0.eurotherm6100",
  "offset": 0,
  "kind": "points",
  "count": 3,
  "total": 3,
  "hasMore": false,
  "pointStates": [
    {
      "deviceId": "gb9fao5yk4f",
      "feature": "modbus0.eurotherm6100",
      "pointId": "ch1",
      "quality": 1,
      "lastUpdateAgeMs": 183,
      "lastGoodUpdateAgeMs": 183,
      "value": 215
    },
    {
      "deviceId": "gb9fao5yk4f",
      "feature": "modbus0.eurotherm6100",
      "pointId": "ch2",
      "quality": 1,
      "lastUpdateAgeMs": 1186,
      "lastGoodUpdateAgeMs": 1186,
      "value": 198
    }
  ]
}
```

### Quality values

`quality` is a numeric `PointQuality` value:

- `0`: `Unknown`
- `1`: `Good`
- `2`: `UncertainInitialValue`
- `3`: `BadNotConnected`
- `4`: `BadNodeMissing`
- `5`: `BadTimeout`
- `6`: `BadProtocolError`
- `7`: `BadConfigError`
- `8`: `BadInvalidValue`
- `9`: `BadWriteRejected`

## pointUpsert

Creates a new point definition or updates an existing one.

### Request

The request payload must contain a `definition` object compatible with the firmware serializer.

```json
{
  "cmd": "pointUpsert",
  "from": 5,
  "to": 4,
  "definition": {
    "deviceId": "gb9fao5yk4f",
    "feature": "modbus0.waveshare8ch",
    "pointId": "output1",
    "displayName": "Output Channel 1",
    "backend": 1,
    "direction": 2,
    "valueType": 0,
    "refreshMs": 1000,
    "timeoutMs": 3000,
    "stringCapacity": 0,
    "portIndex": 0,
    "slaveAddress": 1,
    "address": 0,
    "registerCount": 1,
    "table": 1,
    "access": 3
  }
}
```

### Success response

```json
{
  "cmd": "pointUpsert",
  "ok": true
}
```

### Failure response

```json
{
  "cmd": "pointUpsert",
  "ok": false,
  "error": "invalidDefinition"
}
```

Possible `pointUpsert` errors:

- `invalidDefinition`
- `upsertFailed`

### Numeric enum values used in definition payloads

`backend`:

- `0`: `Local`
- `1`: `Modbus`
- `2`: `NodeNet`

`direction`:

- `0`: `Input`
- `1`: `Output`
- `2`: `InOut`

`valueType`:

- `0`: `Bool`
- `1`: `Uint16`
- `2`: `Int16`
- `3`: `Uint32`
- `4`: `Int32`
- `5`: `Float`
- `6`: `Enum`
- `7`: `String`

For `backend = 1` (`Modbus`), the Modbus-specific fields are:

- `portIndex`
- `slaveAddress`
- `address`
- `registerCount`
- `table`
- `access`

Modbus `table` values:

- `1`: `Coils`
- `2`: `DiscreteInputs`
- `3`: `HoldingRegisters`
- `4`: `InputRegisters`

Modbus `access` values:

- `1`: `Read`
- `2`: `Write`
- `3`: `ReadWrite`

For `backend = 2` (`NodeNet`), use these remote reference fields instead:

- `remoteDeviceId`
- `remoteFeature`
- `remotePointId`

## pointDelete

Deletes one point definition by explicit identity.

### Request

```json
{
  "cmd": "pointDelete",
  "from": 5,
  "to": 4,
  "deviceId": "gb9fao5yk4f",
  "feature": "modbus0.waveshare8ch",
  "pointId": "output8"
}
```

### Success response

```json
{
  "cmd": "pointDelete",
  "ok": true
}
```

### Failure response

```json
{
  "cmd": "pointDelete",
  "ok": false,
  "error": "notFound"
}
```

Possible `pointDelete` errors:

- `missingIdentity`
- `notFound`
- `saveFailed`

## plcStatusReq

Requests detailed PLC runtime status for one slot.

### Request

```json
{
  "cmd": "plcStatusReq",
  "from": 255,
  "to": 4,
  "slotId": 0
}
```

### Response

```json
{
  "cmd": "plcStatusRes",
  "to": 5,
  "ok": true,
  "slotId": 0,
  "state": "running",
  "loaded": true,
  "status": 2,
  "pc": 7,
  "cycleCounter": 885,
  "faultCode": 0,
  "faultInfo": 0,
  "bytecodeBase": 537067584,
  "bytecodeSize": 7,
  "maxInstructionsPerScan": 16,
  "maxScanTimeUs": 5000,
  "source": "local",
  "params": {
    "inputChannel": 1,
    "outputChannel": 1
  },
  "runtimeMapOk": true,
  "inputRuntimeIndex": 0,
  "outputRuntimeIndex": 1,
  "runtimeStoreEpoch": 1,
  "runtimePublishedCount": 21,
  "runtimeHeaderAddr": 537919744
}
```

Possible `state` values:

- `empty`
- `loaded`
- `running`
- `faulted`

## plcSlotsReq

Requests a compact inventory of PLC slots with pagination.

### Request

```json
{
  "cmd": "plcSlotsReq",
  "from": 255,
  "to": 4,
  "offset": 0,
  "limit": 4
}
```

### Response

```json
{
  "cmd": "plcSlotsRes",
  "to": 5,
  "ok": true,
  "offset": 0,
  "count": 4,
  "total": 16,
  "hasMore": true,
  "runtimeStoreEpoch": 1,
  "runtimePublishedCount": 21,
  "slots": [
    {
      "slotId": 0,
      "state": "running",
      "loaded": true,
      "source": "local",
      "cycleCounter": 885,
      "faultCode": 0,
      "bytecodeSize": 7,
      "status": 2
    },
    {
      "slotId": 1,
      "state": "loaded",
      "loaded": true,
      "source": "unknown",
      "cycleCounter": 0,
      "faultCode": 0,
      "bytecodeSize": 6,
      "status": 1
    }
  ]
}
```

## plcLoadReq

Loads the built-in boolean mirror PLC program into a chosen slot.

This is a firmware-side service command intended for runtime validation before the full raw PLC upload flow exists.

### Request

```json
{
  "cmd": "plcLoadReq",
  "from": 255,
  "to": 4,
  "slotId": 0,
  "programType": "mirrorBool",
  "params": {
    "inputChannel": 1,
    "outputChannel": 1
  },
  "persistToFlash": true
}
```

Fields:

- `slotId`: target PLC slot, `0..15`
- `programType`: currently `mirrorBool`
- `params.inputChannel`: Waveshare input channel, `1..8`
- `params.outputChannel`: Waveshare output channel, `1..8`
- `persistToFlash`: optional, only accepted for `slotId = 0`

### Response

```json
{
  "cmd": "plcLoadRes",
  "to": 5,
  "ok": true,
  "slotId": 0,
  "programType": "mirrorBool",
  "params": {
    "inputChannel": 1,
    "outputChannel": 1
  },
  "persistToFlash": true,
  "loadStatus": 0,
  "flashStatus": 0,
  "source": "flash",
  "runtimeMapOk": true,
  "inputRuntimeIndex": 0,
  "outputRuntimeIndex": 1,
  "state": "loaded",
  "cycleCounter": 0,
  "faultCode": 0
}
```

Possible errors:

- `runtimeUnavailable`
- `slotOutOfRange`
- `channelOutOfRange`
- `flashPersistSlot0Only`
- `loadFailed`
- `flashPersistFailed`

## updateProperty for point writes

The existing `updateProperty` command can also be used to write some runtime PLC values.

Two write categories are currently supported:

- local NodeNetCore properties such as `instrumentName`, `master`, and `modbus0.*`
- Modbus coil points addressed by their full point path

For Modbus point writes, the current implementation is intentionally narrow:

- backend must be `Modbus`
- table must be `Coils`
- value type must be `Bool`
- access must be `Write` or `ReadWrite`

That matches Waveshare outputs such as `output1`.

### Example: set Waveshare output1 ON

```json
{
  "cmd": "updateProperty",
  "from": 5,
  "to": 4,
  "propertyName": "gb9fao5yk4f.modbus0.waveshare8ch.output1",
  "value": true
}
```

### Example: set Waveshare output1 OFF

```json
{
  "cmd": "updateProperty",
  "from": 5,
  "to": 4,
  "propertyName": "gb9fao5yk4f.modbus0.waveshare8ch.output1",
  "value": false
}
```

When the Modbus write succeeds, the point state is updated locally to the commanded boolean value.

### Example: change Modbus0 speed

```json
{
  "cmd": "updateProperty",
  "from": 5,
  "to": 4,
  "propertyName": "modbus0.speed",
  "value": 9600
}
```

## Notes

- `pointDefsReq` returns definitions in the `points` array.
- `pointStatesReq` returns runtime values in the `pointStates` array.
- `pointUpsert` persists the updated point catalog.
- `pointDelete` removes the point from the catalog and then persists the catalog.
- Responses are size-limited by the NodeNet payload maximum, so large result sets may be split across multiple pages.