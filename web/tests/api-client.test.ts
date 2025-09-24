import { describe, expect, afterEach, it, vi } from "vitest";

import { resolveApiBaseUrl } from "../src/api/client";

declare global {
  interface ImportMetaEnv {
    VITE_API_BASE?: string;
  }
}

afterEach(() => {
  delete (import.meta.env as ImportMetaEnv).VITE_API_BASE;
  vi.unstubAllGlobals();
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
