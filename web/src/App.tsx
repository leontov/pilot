// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { Suspense, lazy, useCallback, useEffect, useMemo, useState } from "react";
import { NotificationProvider } from "./components/NotificationCenter";
import { SkeletonLines } from "./components/Skeleton";
import { TabNavigation, type TabItem } from "./components/TabNavigation";

const DialogView = lazy(() => import("./views/DialogView").then((module) => ({ default: module.DialogView })));
const MemoryView = lazy(() => import("./views/MemoryView").then((module) => ({ default: module.MemoryView })));
const ProgramsView = lazy(() => import("./views/ProgramsView").then((module) => ({ default: module.ProgramsView })));
const SynthView = lazy(() => import("./views/SynthView").then((module) => ({ default: module.SynthView })));
const ChainView = lazy(() => import("./views/ChainView").then((module) => ({ default: module.ChainView })));
const StatusView = lazy(() => import("./views/StatusView").then((module) => ({ default: module.StatusView })));
const ClusterView = lazy(() => import("./views/ClusterView").then((module) => ({ default: module.ClusterView })));

type ThemeMode = "light" | "dark";

type TabId = "dialog" | "memory" | "programs" | "synth" | "chain" | "status" | "cluster";

interface TabConfig extends TabItem {
  id: TabId;
  render: () => JSX.Element;
}

const tabs: TabConfig[] = [
  { id: "dialog", label: "Диалог", description: "Консоль Δ-VM с историей запросов", render: () => <DialogView /> },
  { id: "memory", label: "Память", description: "F-KV Explorer с деревом префиксов", render: () => <MemoryView /> },
  { id: "programs", label: "Программы", description: "Редактор и анализ байткодов", render: () => <ProgramsView /> },
  { id: "synth", label: "Синтез", description: "Монитор поиска программ", render: () => <SynthView /> },
  { id: "chain", label: "Блокчейн", description: "Цепочка знаний Kolibri Ω", render: () => <ChainView /> },
  { id: "status", label: "Статус", description: "Мониторинг узла и метрик", render: () => <StatusView /> },
  { id: "cluster", label: "Кластер", description: "Состояние соседей и репутация", render: () => <ClusterView /> }
];

const THEME_STORAGE_KEY = "kolibri-theme";

function determineInitialTheme(): ThemeMode {
  if (typeof window === "undefined") {
    return "dark";
  }
  const stored = window.localStorage.getItem(THEME_STORAGE_KEY);
  if (stored === "light" || stored === "dark") {
    return stored;
  }
  return window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark";
}

export default function App() {
  const [theme, setTheme] = useState<ThemeMode>(() => determineInitialTheme());
  const [activeTab, setActiveTab] = useState<TabId>("dialog");

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    document.body.setAttribute("data-theme", theme);
    window.localStorage.setItem(THEME_STORAGE_KEY, theme);
  }, [theme]);

  const toggleTheme = useCallback(() => {
    setTheme((prev) => (prev === "light" ? "dark" : "light"));
  }, []);

  const active = useMemo(() => tabs.find((tab) => tab.id === activeTab) ?? tabs[0], [activeTab]);

  const themeToggleLabel = theme === "light" ? "Переключить на тёмную тему" : "Переключить на светлую тему";
  const themeToggleText = theme === "light" ? "Тёмная тема" : "Светлая тема";

  return (
    <NotificationProvider>
      <div className="app-shell">
        <header className="app-header">
          <div className="brand">
            <span className="brand__title">Kolibri Ω Studio</span>
            <span className="brand__subtitle">Контроль ядра, памяти и сети</span>
          </div>
          <TabNavigation
            tabs={tabs.map(({ id, label, description }) => ({ id, label, description }))}
            activeId={active.id}
            onChange={(id) => setActiveTab(id as TabId)}
          />
          <div className="header-actions">
            <button type="button" className="theme-toggle" onClick={toggleTheme} aria-label={themeToggleLabel}>
              {themeToggleText}
            </button>
          </div>
        </header>
        <main className="app-content">
          <section id={`${active.id}-panel`} role="tabpanel" aria-labelledby={`${active.id}-tab`} className="view">
            <Suspense
              fallback={
                <div className="panel">
                  <SkeletonLines count={4} />
                </div>
              }
            >
              {active.render()}
            </Suspense>
          </section>
        </main>
      </div>
    </NotificationProvider>
  );
}
