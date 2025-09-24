// Copyright (c) 2024 Кочуров Владислав Евгеньевич
import { test, expect } from "@playwright/test";

test.beforeEach(async ({ page }) => {
  const now = new Date().toISOString();
  const peers = [
    { id: "peer-1", status: "online", latency: 12.5, score: 0.98, address: "10.0.0.1", role: "validator" },
    { id: "peer-2", status: "quarantine", latency: 38.2, score: 0.56, address: "10.0.0.2", role: "learner" }
  ];

  await page.route("**/api/v1/dialog", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ answer: "4", trace: [{ step: 1, op: "ADD10" }], timestamp: now })
    });
  });

  await page.route("**/api/v1/vm/run", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ status: "ok", result: 4, trace: [{ step: 1, op: "ADD10" }] })
    });
  });

  await page.route("**/api/v1/control/tasks", async (route) => {
    if (route.request().method() === "GET") {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify([
          {
            id: "task-1",
            name: "vm.run",
            status: "running",
            priority: 5,
            progress: 0.5,
            nextRunAt: now
          }
        ])
      });
      return;
    }
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ acknowledged: true })
    });
  });

  await page.route("**/api/v1/control/monitoring", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({
        metrics: { blocks: 42, tasksInFlight: 2, lastBlockTime: now, peers },
        health: {
          uptime: 86_400,
          memory: { total: 2048, used: 1024 },
          peers,
          blocks: 42,
          version: "1.0.0"
        },
        alerts: [
          { id: "alert-1", severity: "warning", title: "PoU drop", description: "PoU below target", raisedAt: now }
        ],
        timeline: [
          { timestamp: now, label: "Block sealed", value: 1, metadata: { blockId: "blk-1" } }
        ]
      })
    });
  });

  await page.route("**/api/v1/control/monitoring/alerts/**", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ acknowledged: true })
    });
  });

  await page.route("**/api/v1/metrics", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ blocks: 42, tasksInFlight: 2, lastBlockTime: now, peers })
    });
  });

  await page.route("**/api/v1/chain/submit", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ status: "accepted", blockId: "blk-99", position: 1, poe: 0.95, mdlDelta: -0.12 })
    });
  });

  await page.route("**/api/v1/control/cluster/peers/**", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/json",
      body: JSON.stringify({ acknowledged: true })
    });
  });
});

test("navigates across core Studio flows", async ({ page }) => {
  await page.goto("/");

  await expect(page.getByRole("heading", { name: "Диалог с Δ-VM" })).toBeVisible();
  await page.getByLabel("Запрос").fill("2+2");
  await page.getByRole("button", { name: "Отправить" }).click();
  await expect(page.getByText("4", { exact: true })).toBeVisible();

  await page.getByRole("tab", { name: "Программы" }).click();
  await expect(page.getByRole("heading", { name: "Δ-VM Runner" })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Редактор программ Δ-VM" })).toBeVisible();

  await page.getByRole("tab", { name: "Задачи" }).click();
  await expect(page.getByRole("heading", { name: "Планировщик задач" })).toBeVisible();
  await page.getByRole("button", { name: "Запланировать" }).click();

  await page.getByRole("tab", { name: "Мониторинг" }).click();
  await expect(page.getByRole("heading", { name: "Мониторинг Kolibri Ω" })).toBeVisible();
  await expect(page.getByText("Block sealed")).toBeVisible();

  await page.getByRole("tab", { name: "Блокчейн" }).click();
  await expect(page.getByRole("heading", { name: "PoU / MDL мониторинг" })).toBeVisible();
  await page.getByLabel("ID программы").fill("prog-42");
  await page.getByRole("button", { name: "Отправить в цепочку" }).click();
  await expect(page.getByText("PoU: 0.950")).toBeVisible();

  await page.getByRole("tab", { name: "Кластер" }).click();
  await expect(page.getByRole("heading", { name: "Менеджер кластера" })).toBeVisible();
  await page.getByRole("button", { name: "Настроить" }).first().click();
  await expect(page.getByText("Поместить в карантин")).toBeVisible();
});
