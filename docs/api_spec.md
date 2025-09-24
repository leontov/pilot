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
Execute a decimal expression on Δ-VM.

**Request**
```json
{
  "program": "2+2",
  "gas_limit": 256
}
```

* `program` — arithmetic expression composed of decimal digits and `+`, `-`, `*`, `/`. Alternate keys `formula` and `expression` are accepted. For advanced scenarios a numeric `bytecode` array may be supplied instead.
* `gas_limit` — optional override for the configured VM step limit.

**Response**
```json
{
  "result": "4",
  "stack": ["4"],
  "trace": { "steps": [] },
  "gas_used": 4
}
```

Errors return an object in the common format with codes such as `bad_request` (invalid payload) or `vm_error` (runtime failure).

### GET /api/v1/fkv/get
Retrieve values matching a decimal prefix.

**Query Parameters**
* `prefix` (required): decimal prefix expressed as digits (`12` → keys starting with `1,2`).
* `limit` (optional): maximum entries to return (default 32).

**Response**
```json
{
  "values": [
    { "key": "123", "value": "42" }
  ],
  "programs": [
    { "key": "124", "program": "99" }
  ]
}
```

Keys and payloads are rendered as decimal strings derived from the trie digits.

### POST /api/v1/program/submit
Submit a candidate Δ-VM program for evaluation and storage.

**Request**
```json
{
  "program": "3+4"
}
```

* `program` — decimal expression to evaluate. Synonyms `formula` and `content` are accepted for compatibility. The handler compiles the expression to Δ-VM bytecode before execution.

**Response**
```json
{
  "program_id": "prog-000001",
  "poe": 1.000000,
  "mdl": 4.000000,
  "score": 0.960000,
  "accepted": true
}
```

The Kolibri AI library records accepted formulas (when attached) and heuristic PoE/MDL metrics are derived via the blockchain scoring helpers. Errors follow the common format with `bad_request` or `vm_error` codes.

### POST /api/v1/chain/submit
Publish a previously accepted program ID for inclusion in the knowledge blockchain.

**Request**
```json
{
  "program_id": "prog-000001"
}
```

**Response**
```json
{
  "status": "accepted",
  "block_hash": "000af3...",
  "height": 3,
  "program_id": "prog-000001"
}
```

Errors report `not_found` when the program is unknown or `unavailable` if the blockchain subsystem is detached.

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
