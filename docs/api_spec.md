# Kolibri Ω HTTP API Specification (v1)

## Overview
Kolibri Ω exposes a deterministic REST interface for dialog, program execution, memory access, program submission, blockchain operations, and observability. All endpoints speak JSON over HTTP/1.1. Decimal strings are used whenever numeric precision matters.

* **Base URL:** `http://localhost:9000`
* **Content-Type:** `application/json`
* **Authentication:** Sprint A runs without auth; future revisions add API keys with HMAC signing.
* **Versioning:** The prefix `/api/v1` denotes the stable surface. Breaking changes increment the version.
* **Idempotency:** POST endpoints accept `X-Request-Id` for safe retries.

## Common Objects
| Field | Type | Description |
|-------|------|-------------|
| `trace` | object | JSON trace emitted by Δ-VM (array of steps with stack state). |
| `poe` | number/string | Proof-of-Effectiveness score (decimal string recommended). |
| `mdl` | number/string | Minimum Description Length contribution. |
| `gas_used` | integer | Total gas spent while executing the request. |
| `program_id` | string | Stable identifier for stored programs. |

Errors follow the structure:
```json
{
  "error": {
    "code": "string",
    "message": "human readable",
    "details": {...}
  }
}
```
Common error codes: `bad_request`, `not_found`, `conflict`, `rate_limited`, `internal_error`.

## Endpoint Catalogue

### POST /api/v1/dialog
Initiate a decimal-first dialogue turn.

**Request**
```json
{
  "input": "2+2",
  "context": {
    "session_id": "optional"
  }
}
```

**Response**
```json
{
  "answer": "4",
  "trace": {
    "steps": [ { "pc": 0, "op": "PUSHd", "stack": [2], "gas": 999 }, ... ]
  },
  "poe": "0.92",
  "mdl": "12.3",
  "gas_used": 12
}
```

### POST /api/v1/vm/run
Execute explicit Δ-VM bytecode or compile a decimal expression on the fly.

**Request**
```json
{
  "program": "PUSHd 2; PUSHd 2; ADD10; HALT",
  "gas_limit": 1024
}
```

The field `program` may also be an array of opcode integers (0–255). When omitted the endpoint returns `400 bad_request`.

**Response**
```json
{
  "status": "ok",
  "result": "4",
  "stack": ["4"],
  "halted": true,
  "steps": 4,
  "gas_used": 4,
  "program_source": "PUSHd 2; PUSHd 2; ADD10; HALT",
  "trace": [
    { "step": 0, "ip": 0, "opcode": 1, "stack_top": 2, "gas_left": 1024 },
    { "step": 1, "ip": 1, "opcode": 1, "stack_top": 2, "gas_left": 1023 }
  ]
}
```

Failures return the VM status in the `status` field (`invalid_opcode`, `div_by_zero`, `gas_exhausted`, …) while preserving a `200 OK` HTTP code.

### GET /api/v1/fkv/get
Retrieve values matching a decimal prefix.

**Query Parameters**
* `prefix` (required): decimal namespace prefix encoded as digits (e.g. `123`).
* `limit` (optional): maximum entries to return (default 32).

**Response**
```json
{
  "values": [
    { "key": "123", "value": "45" }
  ],
  "programs": [
    { "key": "880", "program": "987" }
  ]
}
```

Missing prefixes return `400 bad_request`. Prefixes that match no entries respond with empty arrays and `200 OK`.

### POST /api/v1/program/submit
Submit a candidate Δ-VM program for evaluation. The payload accepts either `program` (string or opcode array) or `bytecode` (opcode array). Unsupported payloads are rejected with `400 bad_request`.

**Request**
```json
{
  "program": "PUSHd 2; PUSHd 3; MUL10; HALT"
}
```

**Response**
```json
{
  "program_id": "program-42",
  "poe": 0.94,
  "mdl": 18.0,
  "score": 0.76,
  "accepted": true,
  "vm_status": "ok"
}
```

The endpoint stores accepted programs in-memory for later blockchain submission and updates the Kolibri AI library when available.

### POST /api/v1/chain/submit
Publish a previously accepted program ID for inclusion in the PoU blockchain.

**Request**
```json
{
  "program_id": "program-42"
}
```

**Response**
```json
{
  "status": "accepted",
  "block_hash": "0009ABC...",
  "height": 42,
  "poe": 0.94,
  "mdl": 18.0,
  "score": 0.76
}
```

If the program ID is unknown the server returns `404 not_found`. When the blockchain subsystem is not attached the endpoint returns `503 service_unavailable`.

### GET /api/v1/health
Report readiness/liveness information.

**Response**
```json
{
  "status": "ok",
  "uptime_seconds": 1234,
  "version": "1.0.0",
  "peers": 4,
  "blocks": 128
}
```

### GET /api/v1/metrics
Expose Prometheus-compatible counters.

**Response**
```
# HELP kolibri_vm_latency_ms Δ-VM latency in milliseconds
kolibri_vm_latency_ms{quantile="0.5"} 12
kolibri_vm_latency_ms{quantile="0.95"} 45
...
```

## WebSocket (Planned)
Future releases expose `/api/v1/events` streaming VM traces, block offers, and synthesis updates. Investors can subscribe during demos for live dashboards.

## Rate Limits & Quotas
Sprint A enforces conservative per-IP limits: 60 dialog calls/min, 30 program submissions/min, 10 chain submissions/min. Rate-limit violations return HTTP 429 with `Retry-After` header.

## Deployment Checklist
1. Run `./kolibri.sh up` to build native and web components.
2. Ensure `VITE_API_BASE` is set for Studio builds (default `http://localhost:9000`).
3. Configure firewall rules to expose port 9000 to demo network.
4. Load investor demo dataset via F-KV seeding scripts (optional).

## Change Log
* **v1.0:** Initial release with dialog, VM run, F-KV, program submit, chain submit, health, metrics.
