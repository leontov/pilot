// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useCallback, useEffect, useMemo, useState } from "react";
import { NotificationProvider } from "./components/NotificationCenter";
import { ChainView } from "./views/ChainView";
import { ClusterView } from "./views/ClusterView";
import { DialogView } from "./views/DialogView";
import { MemoryView } from "./views/MemoryView";
import { ProgramsView } from "./views/ProgramsView";
import { SchedulerView } from "./views/SchedulerView";
import { StatusView } from "./views/StatusView";
import { SynthView } from "./views/SynthView";
import { MonitoringView } from "./views/MonitoringView";

type ThemeMode = "light" | "dark";

type TabId =
  | "dialog"
  | "memory"
  | "programs"
  | "scheduler"
  | "synth"
  | "chain"
  | "monitoring"
  | "status"
  | "cluster";

interface TabConfig {
  id: TabId;
  label: string;
  render: () => JSX.Element;
}

const tabs: TabConfig[] = [
  { id: "dialog", label: "Диалог", render: () => <DialogView /> },
  { id: "memory", label: "Память", render: () => <MemoryView /> },
  { id: "programs", label: "Программы", render: () => <ProgramsView /> },
  { id: "scheduler", label: "Задачи", render: () => <SchedulerView /> },
  { id: "synth", label: "Синтез", render: () => <SynthView /> },
  { id: "chain", label: "Блокчейн", render: () => <ChainView /> },
  { id: "monitoring", label: "Мониторинг", render: () => <MonitoringView /> },
  { id: "status", label: "Статус", render: () => <StatusView /> },
  { id: "cluster", label: "Кластер", render: () => <ClusterView /> }
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
          <nav aria-label="Основные разделы Kolibri Ω Studio">
            <ul className="tab-list" role="tablist">
              {tabs.map((tab) => (
                <li key={tab.id}>
                  <button
                    type="button"
                    role="tab"
                    aria-selected={activeTab === tab.id}
                    aria-controls={`${tab.id}-panel`}
                    id={`${tab.id}-tab`}
                    className="tab-button"
                    onClick={() => setActiveTab(tab.id)}
                  >
                    {tab.label}
                  </button>
                </li>
              ))}
            </ul>
          </nav>
          <div className="header-actions">
            <button type="button" className="theme-toggle" onClick={toggleTheme} aria-label={themeToggleLabel}>
              {themeToggleText}
            </button>
          </div>
        </header>
        <main className="app-content">
          <section id={`${active.id}-panel`} role="tabpanel" aria-labelledby={`${active.id}-tab`} className="view">
            {active.render()}
          </section>
        </main>
      </div>
    </NotificationProvider>
  );
}
