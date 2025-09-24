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
Execute explicit Δ-VM bytecode provided as decimal opcodes.

**Request**
```json
{
  "bytecode": [1, 2, 1, 2, 2, 18],
  "max_steps": 256,
  "max_stack": 128,
  "trace": true
}
```

**Response**
```json
{
  "status": "ok",
  "result": "4",
  "steps": 4,
  "halted": true,
  "trace": [
    { "step": 0, "ip": 0, "opcode": 1, "stack_top": 0, "gas_left": 256 }
  ]
}
```

Errors include `invalid_opcode`, `stack_underflow`, `stack_overflow`, and `gas_exhausted`.

### GET /api/v1/fkv/get
Retrieve values matching a decimal prefix.

**Query Parameters**
* `prefix` (required): decimal digits representing the trie prefix, e.g. `123`.
* `limit` (optional): maximum entries to return (default 16, capped at 128).

**Response**
```json
{
  "prefix": "123",
  "values": [
    { "key": "123", "value": "456" }
  ],
  "programs": [
    { "key": "1234", "bytecode": "10221018" }
  ]
}
```

### POST /api/v1/program/submit
Submit a candidate Δ-VM program for evaluation.

**Request**
```json
{
  "program_id": "prog-1",
  "content": "PUSHd 2; PUSHd 2; ADD10; HALT",
  "representation": "text",
  "effectiveness": 0.82
}
```

**Response**
```json
{
  "program_id": "prog-1",
  "poe": 0.82,
  "mdl": 24.0,
  "score": 0.58,
  "accepted": true
}
```

If the candidate fails validation the response contains `"accepted": false` with an explanatory score payload.

### POST /api/v1/chain/submit
Publish a previously submitted program ID for inclusion in the PoU blockchain.

**Request**
```json
{
  "program_id": "prog-1"
}
```

**Response**
```json
{
  "status": "accepted",
  "block_hash": "0009ABC...",
  "height": 3
}
```

If the program was not submitted earlier the handler returns `404 not_found`.

### GET /api/v1/health
Report readiness/liveness information.

**Response**
```json
{
  "status": "ok",
  "uptime_ms": 1234,
  "requests": 42,
  "errors": 1,
  "blockchain_attached": true,
  "ai_attached": true
}
```

### GET /api/v1/metrics
Expose Prometheus-compatible counters. The response is UTF-8 plaintext following the `text/plain; version=0.0.4` format with
`kolibri_http_requests_total`, `kolibri_http_route_requests_total`, `kolibri_http_route_errors_total`,
`kolibri_http_request_duration_ms_sum`, and `kolibri_blockchain_blocks` metrics.

**Example**
```
# HELP kolibri_http_requests_total Total HTTP requests handled by Kolibri
# TYPE kolibri_http_requests_total counter
kolibri_http_requests_total 7
```

### GET /api/v1/ai/state
Return a serialized snapshot of the Kolibri AI orchestrator state.

### GET /api/v1/ai/formulas
List the top formulas tracked by the AI library. Accepts `limit` query parameter (default: all available entries).

### GET /api/v1/ai/snapshot
Export the full AI snapshot (memory + dataset) as JSON. Useful for backups or debugging.

### POST /api/v1/ai/snapshot
Import a previously exported snapshot. The body must contain the JSON blob returned by the GET variant. On success the handler
returns `{ "status": "ok" }`.

### GET /api/v1/studio/state
Return aggregated telemetry for Kolibri Studio dashboards, including HTTP route counters, uptime, blockchain height, and AI
attachment status.

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
