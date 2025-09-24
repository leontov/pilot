import { afterEach, describe, expect, it, vi } from "vitest";

const clearWindow = () => {
  delete (globalThis as { window?: unknown }).window;
};

describe("resolveApiBaseUrl", () => {
  afterEach(() => {
    vi.unstubAllEnvs();
    vi.resetModules();
    clearWindow();
  });

  it("returns environment base URL when defined", async () => {
    vi.stubEnv("VITE_API_BASE", "https://custom.example");
    const { resolveApiBaseUrl } = await import("./client");

    expect(resolveApiBaseUrl()).toBe("https://custom.example");
  });

  it("falls back to window origin when environment is missing", async () => {
    vi.stubEnv("VITE_API_BASE", "");
    (globalThis as { window?: { location: { origin: string } } }).window = {
      location: { origin: "https://window.example" }
    };
    const { resolveApiBaseUrl } = await import("./client");

    expect(resolveApiBaseUrl()).toBe("https://window.example");
  });

  it("returns empty string when nothing is available", async () => {
    vi.stubEnv("VITE_API_BASE", "");
    const { resolveApiBaseUrl } = await import("./client");

    expect(resolveApiBaseUrl()).toBe("");
  });
});
