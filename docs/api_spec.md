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
Execute explicit Δ-VM bytecode.

**Request**
```json
{
  "program": "PUSHd 2; PUSHd 2; ADD10; HALT",
  "input_stack": ["optional decimals"],
  "gas_limit": 1024
}
```

**Response**
```json
{
  "result": "4",
  "stack": ["4"],
  "trace": { ... },
  "gas_used": 4
}
```

Errors include `invalid_opcode`, `gas_exhausted`, `sandbox_violation`.

### GET /api/v1/fkv/get
Retrieve values matching a decimal prefix.

**Query Parameters**
* `prefix` (required): decimal namespace prefix, e.g. `S/geo/country/RU`.
* `limit` (optional): maximum entries to return (default 32).

**Response**
```json
{
  "values": [
    { "key": "S/geo/country/RU", "value": "Москва", "poe": "0.99", "mdl": "8.1" }
  ],
  "programs": [
    { "program_id": "prog_12345", "score": "0.93" }
  ],
  "topk": [ ... ]
}
```

### POST /api/v1/program/submit
Submit a candidate Δ-VM program for evaluation.

**Request**
```json
{
  "bytecode": "PUSHd 2; PUSHd 3; MUL10; HALT",
  "metadata": {
    "origin": "synthesis", "description": "multiply 2 and 3"
  }
}
```

**Response**
```json
{
  "program_id": "prog_67890",
  "poe": "0.95",
  "mdl": "10.2",
  "score": "0.71",
  "accepted": true
}
```

On rejection the response includes `accepted: false` and `reasons: []` with MDL/PoE commentary.

### POST /api/v1/chain/submit
Publish a program ID for inclusion in the PoU blockchain.

**Request**
```json
{
  "program_id": "prog_67890",
  "nonce": "123456",
  "signature": "<Ed25519 signature>"
}
```

**Response**
```json
{
  "status": "accepted",
  "block_hash": "0009ABC...",
  "height": 42
}
```

Failures may include `poe_below_threshold`, `invalid_signature`, `stale_parent`.

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
