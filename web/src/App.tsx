// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useCallback, useEffect, useMemo, useState } from "react";

type ThemeMode = "light" | "dark";
type Language = "ru" | "en";

const THEME_STORAGE_KEY = "kolibri-theme";
const LANGUAGE_STORAGE_KEY = "kolibri-language";

const copy: Record<Language, {
  nav: { id: string; label: string }[];
  hero: {
    eyebrow: string;
    title: string;
    subtitle: string;
    primaryCta: string;
    secondaryCta: string;
  };
  metrics: { value: string; label: string }[];
  pillars: { title: string; description: string }[];
  architecture: { title: string; description: string }[];
  timeline: { label: string; description: string }[];
  cta: { title: string; description: string; action: string };
  footer: string;
}> = {
  ru: {
    nav: [
      { id: "vision", label: "Миссия" },
      { id: "pillars", label: "Опоры" },
      { id: "architecture", label: "Архитектура" },
      { id: "roadmap", label: "Дорожная карта" },
      { id: "cta", label: "Связаться" },
    ],
    hero: {
      eyebrow: "Kolibri Ω",
      title: "Цифровой интеллект нового поколения",
      subtitle:
        "Мы создаём искусственный интеллект, работающий на десятичной логике, программах и формальных знаниях. Kolibri Ω объединяет виртуальную машину, фрактальную память и блокчейн знаний, чтобы превратить каждую формулу в действие.",
      primaryCta: "Стать партнёром",
      secondaryCta: "Узнать архитектуру",
    },
    metrics: [
      { value: "≤ 50 МБ", label: "Компактное ядро" },
      { value: "< 50 мс", label: "P95 ответа" },
      { value: "+20%", label: "Рост PoE за ночь" },
      { value: "24 ч", label: "Непрерывной работы" },
    ],
    pillars: [
      {
        title: "Детерминированная Δ-VM",
        description:
          "Stack-based виртуальная машина с десятичными инструкциями обеспечивает прозрачность, предсказуемость и трассировку каждого шага.",
      },
      {
        title: "Фрактальная память F-KV",
        description:
          "10-арный trie хранит программы, факты и эпизоды. Колибри мгновенно достаёт знания по префиксу и обновляет их в реальном времени.",
      },
      {
        title: "Блокчейн знаний",
        description:
          "Proof-of-Use фиксирует только полезные программы. Сеть Kolibri делится улучшениями, сохраняя целостность и происхождение знаний.",
      },
    ],
    architecture: [
      {
        title: "Δ-VM v2",
        description:
          "Интерпретатор на C с ограничением по газу, пошаговым JSON-логом и возможностью JIT-ускорения критичных участков.",
      },
      {
        title: "F-KV v2",
        description:
          "Фрактальная память с десятичным арифметическим кодированием хранит миллионы ключей и возвращает результаты за миллисекунды.",
      },
      {
        title: "Синтез знаний",
        description:
          "Колибри рассматривает знание как программу: перебор, MCTS и генетический поиск генерируют кандидатов, повышая PoE и снижая MDL.",
      },
      {
        title: "Kolibri Studio",
        description:
          "Web-панель контроля с диалогами, памятью, анализом программ и мониторингом кластера. Живой контроль интеллекта.",
      },
    ],
    timeline: [
      {
        label: "Спринт A",
        description: "Δ-VM v2, F-KV v2, метрики PoE/MDL, Kolibri Studio с диалогом и памятью.",
      },
      {
        label: "Спринт B",
        description: "Сеть и синтез: обмен программами, Proof-of-Use, оркестратор поисковых стратегий.",
      },
      {
        label: "Спринт C",
        description: "Блокчейн знаний, кластер, мониторинг и подготовка демонстрации уровня world-class.",
      },
    ],
    cta: {
      title: "Присоединяйтесь к полёту Колибри",
      description:
        "Инвесторы, исследователи и инженеры — давайте вместе зададим новый стандарт интеллектуальных систем, основанных на цифрах и формулах.",
      action: "Назначить встречу",
    },
    footer: "© 2024 Kolibri Ω. Все знания — в цифрах.",
  },
  en: {
    nav: [
      { id: "vision", label: "Vision" },
      { id: "pillars", label: "Pillars" },
      { id: "architecture", label: "Architecture" },
      { id: "roadmap", label: "Roadmap" },
      { id: "cta", label: "Contact" },
    ],
    hero: {
      eyebrow: "Kolibri Ω",
      title: "The decimal-native intelligence",
      subtitle:
        "We engineer an AI that reasons through decimal logic, programs, and formal knowledge. Kolibri Ω unites a virtual machine, fractal memory, and a knowledge blockchain so every formula becomes executable.",
      primaryCta: "Become a partner",
      secondaryCta: "Explore the architecture",
    },
    metrics: [
      { value: "≤ 50 MB", label: "Compact core" },
      { value: "< 50 ms", label: "P95 response" },
      { value: "+20%", label: "PoE growth overnight" },
      { value: "24 h", label: "Continuous uptime" },
    ],
    pillars: [
      {
        title: "Deterministic Δ-VM",
        description:
          "A stack-based decimal instruction set delivers transparent execution with per-step tracing and guaranteed halting gas limits.",
      },
      {
        title: "Fractal F-KV memory",
        description:
          "The 10-ary trie stores programs, facts, and episodes. Kolibri retrieves knowledge by prefix instantly and syncs updates in real time.",
      },
      {
        title: "Knowledge blockchain",
        description:
          "Proof-of-Use accepts only beneficial programs. The Kolibri network shares improvements while preserving provenance and integrity.",
      },
    ],
    architecture: [
      {
        title: "Δ-VM v2",
        description:
          "A C-based interpreter with gas limits, step-by-step JSON traces, and optional JIT acceleration for critical paths.",
      },
      {
        title: "F-KV v2",
        description:
          "Fractal memory with decimal arithmetic coding stores millions of keys and serves them in single-digit milliseconds.",
      },
      {
        title: "Knowledge synthesis",
        description:
          "Kolibri treats knowledge as programs: enumeration, MCTS, and genetic search generate candidates that raise PoE and reduce MDL.",
      },
      {
        title: "Kolibri Studio",
        description:
          "A control cockpit with dialogue, memory explorer, program analytics, and cluster monitoring. Real-time governance of intelligence.",
      },
    ],
    timeline: [
      {
        label: "Sprint A",
        description: "Δ-VM v2, F-KV v2, PoE/MDL metrics, Kolibri Studio with dialogue and memory views.",
      },
      {
        label: "Sprint B",
        description: "Network & synthesis: program exchange, Proof-of-Use, orchestration of search strategies.",
      },
      {
        label: "Sprint C",
        description: "Knowledge blockchain, cluster operations, monitoring, and world-class demo readiness.",
      },
    ],
    cta: {
      title: "Join the Kolibri flight",
      description:
        "Investors, researchers, and engineers — let’s define the new standard for digit- and formula-native intelligence together.",
      action: "Book a meeting",
    },
    footer: "© 2024 Kolibri Ω. Knowledge rendered in digits.",
  },
};

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

function determineInitialLanguage(): Language {
  if (typeof window === "undefined") {
    return "ru";
  }
  const stored = window.localStorage.getItem(LANGUAGE_STORAGE_KEY);
  if (stored === "ru" || stored === "en") {
    return stored;
  }
  const browser = window.navigator.language.toLowerCase();
  return browser.startsWith("ru") ? "ru" : "en";
}

export default function App() {
  const [theme, setTheme] = useState<ThemeMode>(() => determineInitialTheme());
  const [language, setLanguage] = useState<Language>(() => determineInitialLanguage());

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    document.body.setAttribute("data-theme", theme);
    window.localStorage.setItem(THEME_STORAGE_KEY, theme);
  }, [theme]);

  useEffect(() => {
    document.documentElement.lang = language;
    window.localStorage.setItem(LANGUAGE_STORAGE_KEY, language);
  }, [language]);

  const toggleTheme = useCallback(() => {
    setTheme((prev) => (prev === "light" ? "dark" : "light"));
  }, []);

  const switchLanguage = useCallback((value: Language) => {
    setLanguage(value);
  }, []);

  const content = useMemo(() => copy[language], [language]);

  const themeToggleLabel =
    theme === "light"
      ? language === "ru"
        ? "Переключить на тёмную тему"
        : "Switch to dark theme"
      : language === "ru"
        ? "Переключить на светлую тему"
        : "Switch to light theme";

  const themeToggleText =
    theme === "light" ? (language === "ru" ? "Тёмная тема" : "Dark") : language === "ru" ? "Светлая тема" : "Light";

  return (
    <div className="landing">
      <header className="landing-header">
        <a className="brand" href="#vision">
          <span className="brand-mark" aria-hidden="true">
            Ω
          </span>
          <span className="brand-text">Kolibri</span>
        </a>
        <nav aria-label={language === "ru" ? "Основные разделы" : "Main sections"} className="landing-nav">
          {content.nav.map((item) => (
            <a key={item.id} className="nav-link" href={`#${item.id}`}>
              {item.label}
            </a>
          ))}
        </nav>
        <div className="header-actions">
          <div className="language-switch" role="group" aria-label={language === "ru" ? "Выбор языка" : "Language switch"}>
            <button
              type="button"
              className="language-option"
              aria-pressed={language === "ru"}
              onClick={() => switchLanguage("ru")}
            >
              Русский
            </button>
            <button
              type="button"
              className="language-option"
              aria-pressed={language === "en"}
              onClick={() => switchLanguage("en")}
            >
              English
            </button>
          </div>
          <button type="button" className="theme-toggle" onClick={toggleTheme} aria-label={themeToggleLabel}>
            {themeToggleText}
          </button>
        </div>
      </header>

      <main className="landing-main">
        <section id="vision" className="hero">
          <div className="hero-content">
            <p className="hero-eyebrow">{content.hero.eyebrow}</p>
            <h1 className="hero-title">{content.hero.title}</h1>
            <p className="hero-subtitle">{content.hero.subtitle}</p>
            <div className="hero-cta">
              <a className="cta-primary" href="mailto:hello@kolibri.ai">
                {content.hero.primaryCta}
              </a>
              <a className="cta-secondary" href="#architecture">
                {content.hero.secondaryCta}
              </a>
            </div>
            <div className="metrics-grid">
              {content.metrics.map((metric) => (
                <div key={metric.label} className="metric-card">
                  <span className="metric-value">{metric.value}</span>
                  <span className="metric-label">{metric.label}</span>
                </div>
              ))}
            </div>
          </div>
          <div className="hero-visual" aria-hidden="true">
            <div className="orb" />
            <div className="trace" />
          </div>
        </section>

        <section id="pillars" className="section">
          <h2 className="section-title">{language === "ru" ? "Три ключевые опоры" : "Three key pillars"}</h2>
          <p className="section-lead">
            {language === "ru"
              ? "Kolibri Ω сочетает вычислительную точность, гибкую память и доказуемую полезность знаний."
              : "Kolibri Ω fuses computational precision, adaptive memory, and provable usefulness of knowledge."}
          </p>
          <div className="pillars-grid">
            {content.pillars.map((pillar) => (
              <article key={pillar.title} className="pillar-card">
                <h3>{pillar.title}</h3>
                <p>{pillar.description}</p>
              </article>
            ))}
          </div>
        </section>

        <section id="architecture" className="section">
          <h2 className="section-title">{language === "ru" ? "Полная архитектура" : "Full architecture"}</h2>
          <div className="architecture-grid">
            {content.architecture.map((block) => (
              <article key={block.title} className="architecture-card">
                <h3>{block.title}</h3>
                <p>{block.description}</p>
              </article>
            ))}
          </div>
        </section>

        <section id="roadmap" className="section">
          <h2 className="section-title">{language === "ru" ? "Дорожная карта" : "Roadmap"}</h2>
          <div className="timeline">
            {content.timeline.map((step, index) => (
              <div key={step.label} className="timeline-item">
                <div className="timeline-index">{index + 1}</div>
                <div>
                  <h3>{step.label}</h3>
                  <p>{step.description}</p>
                </div>
              </div>
            ))}
          </div>
        </section>

        <section id="cta" className="section">
          <div className="cta-panel">
            <h2>{content.cta.title}</h2>
            <p>{content.cta.description}</p>
            <a className="cta-primary" href="mailto:partnerships@kolibri.ai">
              {content.cta.action}
            </a>
          </div>
        </section>
      </main>

      <footer className="landing-footer">{content.footer}</footer>
    </div>
  );
}
