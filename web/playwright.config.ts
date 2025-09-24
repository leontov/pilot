// Copyright (c) 2024 Кочуров Владислав Евгеньевич
import { defineConfig, devices } from "@playwright/test";

const PORT = Number(process.env.KOLIBRI_WEB_PORT ?? 4173);

export default defineConfig({
  testDir: "./tests/e2e",
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  workers: process.env.CI ? 2 : undefined,
  reporter: [
    ["list"],
    ["html", { outputFolder: "playwright-report", open: "never" }]
  ],
  use: {
    baseURL: process.env.KOLIBRI_WEB_BASE_URL ?? `http://127.0.0.1:${PORT}`,
    trace: "on-first-retry"
  },
  webServer: {
    command: `npm run dev -- --host 0.0.0.0 --port ${PORT}`,
    port: PORT,
    timeout: 120_000,
    reuseExistingServer: !process.env.CI
  },
  projects: [
    {
      name: "chromium",
      use: { ...devices["Desktop Chrome"] }
    }
  ]
});
