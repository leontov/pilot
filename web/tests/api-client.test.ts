import { describe, expect, afterEach, it, vi } from "vitest";

import { KolibriClient, resolveApiBaseUrl } from "../src/api/client";

declare global {
  interface ImportMetaEnv {
    VITE_API_BASE?: string;
  }
}

afterEach(() => {
  delete (import.meta.env as ImportMetaEnv).VITE_API_BASE;
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

describe("resolveApiBaseUrl", () => {
  it("returns base URL from environment when provided", () => {
    (import.meta.env as ImportMetaEnv).VITE_API_BASE = "https://env.example";

    expect(resolveApiBaseUrl()).toBe("https://env.example");
  });

  it("falls back to window.location.origin when environment is not set", () => {
    vi.stubGlobal("window", { location: { origin: "https://window.example" } });

    expect(resolveApiBaseUrl()).toBe("https://window.example");
  });

  it("returns empty string when neither environment nor window origin are available", () => {
    expect(resolveApiBaseUrl()).toBe("");
  });
});

describe("KolibriClient", () => {
  const okResponse = () =>
    new Response("{}", {
      status: 200,
      headers: { "Content-Type": "application/json" }
    });

  it("merges base URL paths without duplicating slashes", async () => {
    const fetchImpl = vi.fn().mockResolvedValue(okResponse());
    const client = new KolibriClient({
      baseUrl: "https://api.example/base",
      fetchImpl,
      defaultHeaders: { Authorization: "Bearer secret" }
    });

    await client.dialog({ input: "ping" });

    expect(fetchImpl).toHaveBeenCalledTimes(1);
    const [url, init] = fetchImpl.mock.calls[0];
    expect(url).toBe("https://api.example/base/api/v1/dialog");
    const headers = (init as RequestInit).headers as Headers;
    expect(headers.get("Authorization")).toBe("Bearer secret");
    expect(headers.get("Content-Type")).toBe("application/json");
  });

  it("falls back to relative paths when base URL is empty", async () => {
    const fetchImpl = vi.fn().mockResolvedValue(okResponse());
    const client = new KolibriClient({ fetchImpl });

    await client.health();

    expect(fetchImpl).toHaveBeenCalledWith(
      "/api/v1/health",
      expect.objectContaining({ method: "GET" })
    );
  });

  it("aborts requests when the configured timeout is exceeded", async () => {
    vi.useFakeTimers();
    const fetchImpl = vi.fn((_, init?: RequestInit) =>
      new Promise<Response>((_, reject) => {
        init?.signal?.addEventListener("abort", () => {
          reject(init.signal?.reason);
        });
      })
    );

    const client = new KolibriClient({
      baseUrl: "https://api.example",
      fetchImpl,
      timeoutMs: 5
    });

    const requestPromise = client.metrics();
    const expectation = expect(requestPromise).rejects.toMatchObject({
      name: expect.stringMatching(/TimeoutError|AbortError/)
    });
    await vi.advanceTimersByTimeAsync(6);

    await expectation;
    expect(fetchImpl).toHaveBeenCalledTimes(1);
  });
});
