// Copyright (c) 2025 Кочуров Владислав Евгеньевич

import { FormEvent, useCallback, useEffect, useMemo, useState } from "react";

type ThemeMode = "light" | "dark";
type Language = "ru" | "en";

type ChatMessage = {
  role: "user" | "ai";
  content: string;
};

type BeforeInstallPromptEvent = Event & {
  prompt: () => Promise<void>;
  userChoice: Promise<{ outcome: "accepted" | "dismissed"; platform: string }>;
};

type InstallStatus = "idle" | "installed" | "dismissed";

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
  techStack: {
    title: string;
    lead: string;
    items: { title: string; description: string; highlight: string }[];
  };
  useCases: {
    title: string;
    lead: string;
    items: { title: string; description: string; impact: string }[];
  };
  faq: { title: string; items: { question: string; answer: string }[] };
  cta: { title: string; description: string; action: string };
  install: {
    button: string;
    ariaLabel: string;
    badge: string;
    message: string;
    installed: string;
    dismissed: string;
  };
  chat: {
    title: string;
    status: string;
    ariaLabel: string;
    placeholder: string;
    send: string;
    initialMessages: ChatMessage[];
    autoReplies: string[];
  };
  footer: string;
}> = {
  ru: {
    nav: [
      { id: "vision", label: "Миссия" },
      { id: "pillars", label: "Опоры" },
      { id: "architecture", label: "Архитектура" },
      { id: "stack", label: "Технологии" },
      { id: "use-cases", label: "Сценарии" },
      { id: "roadmap", label: "Дорожная карта" },
      { id: "faq", label: "FAQ" },
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
    techStack: {
      title: "Технологический стек Kolibri",
      lead: "Каждый слой Kolibri Ω выстроен вокруг десятичной точности — от виртуальной машины до визуализации знаний.",
      items: [
        {
          title: "Δ-VM Core",
          description: "Интерпретатор на C с резидентным размером менее 50 МБ и строгими лимитами газа для детерминизма.",
          highlight: "C · SIMD · JSON Trace",
        },
        {
          title: "F-KV Fabric",
          description: "Фрактальная память с десятичным арифметическим кодированием и префиксным поиском за миллисекунды.",
          highlight: "Trie · Arithmetic Coding",
        },
        {
          title: "Синтез знаний",
          description: "Комбинация перебора, MCTS и генетического поиска повышает PoE и снижает MDL.",
          highlight: "Search · PoE Engine",
        },
        {
          title: "Kolibri Studio",
          description: "Веб-интерфейс на React/Vite с живыми дашбордами, трассировками и управлением узлом.",
          highlight: "React · Vite · WebGL",
        },
      ],
    },
    useCases: {
      title: "Сценарии применения",
      lead: "Kolibri Ω превращает формальные знания в оцифрованные рабочие процессы для бизнеса и науки.",
      items: [
        {
          title: "Финансовые регламенты",
          description: "Оцифровка правил расчёта и проверок снижает операционные риски и ускоряет аудит.",
          impact: "−65% времени на валидацию",
        },
        {
          title: "Научные лаборатории",
          description: "Kolibri управляет каталогом формул и автоматически подбирает оптимальные эксперименты.",
          impact: "+3× скорость открытия",
        },
        {
          title: "Индустриальные протоколы",
          description: "Δ-VM выполняет проверяемые последовательности действий для промышленной автоматизации.",
          impact: "99.9% соблюдение процедур",
        },
      ],
    },
    faq: {
      title: "Частые вопросы",
      items: [
        {
          question: "Чем Kolibri отличается от LLM?",
          answer:
            "Kolibri Ω опирается на программы и формальные правила вместо весов. Решения прозрачны, воспроизводимы и трассируются.",
        },
        {
          question: "Можно ли интегрировать Kolibri в существующие системы?",
          answer:
            "Да, ядро компактно, запускается локально и предоставляет HTTP API для диалогов, выполнения программ и доступа к памяти.",
        },
        {
          question: "Как обеспечивается безопасность знаний?",
          answer:
            "Блокчейн знаний Proof-of-Use фиксирует происхождение, а криптографические подписи и репутация защищают сеть.",
        },
      ],
    },
    cta: {
      title: "Присоединяйтесь к полёту Колибри",
      description:
        "Инвесторы, исследователи и инженеры — давайте вместе зададим новый стандарт интеллектуальных систем, основанных на цифрах и формулах.",
      action: "Назначить встречу",
    },
    install: {
      button: "Установить Kolibri Ω",
      ariaLabel: "Установить веб-приложение Kolibri Ω на устройство",
      badge: "PWA · Офлайн-режим",
      message:
        "Установите Kolibri Ω как приложение: офлайн-доступ, мгновенный запуск и живые обновления.",
      installed: "Kolibri Ω установлена",
      dismissed: "Можно установить позже через меню браузера",
    },
    chat: {
      title: "Kolibri ИИ",
      status: "Онлайн",
      ariaLabel: "Окно чата Kolibri ИИ",
      placeholder: "Спросите о возможностях Kolibri...",
      send: "Отправить",
      initialMessages: [
        { role: "ai", content: "Привет! Я Kolibri ИИ и помогу разобраться в десятичном интеллекте." },
        { role: "user", content: "Какие компоненты входят в платформу?" },
        { role: "ai", content: "Δ-VM, фрактальная память F-KV и блокчейн знаний объединены в единую систему." },
      ],
      autoReplies: [
        "Мы показываем трассировку Δ-VM и PoE-метрики в Kolibri Studio в реальном времени.",
        "Колибри легко внедряется: компактное ядро и HTTP API v1 запускаются за минуты.",
        "Узлы сети обмениваются программами через Proof-of-Use, сохраняя качество знаний.",
      ],
    },
    footer: "© 2024 Kolibri Ω. Все знания — в цифрах.",
  },
  en: {
    nav: [
      { id: "vision", label: "Vision" },
      { id: "pillars", label: "Pillars" },
      { id: "architecture", label: "Architecture" },
      { id: "stack", label: "Technology" },
      { id: "use-cases", label: "Use cases" },
      { id: "roadmap", label: "Roadmap" },
      { id: "faq", label: "FAQ" },
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
    techStack: {
      title: "Kolibri technology stack",
      lead: "Every Kolibri Ω layer is engineered for decimal precision — from execution to knowledge visualization.",
      items: [
        {
          title: "Δ-VM Core",
          description: "A C interpreter under 50 MB with strict gas limits for deterministic execution.",
          highlight: "C · SIMD · JSON Trace",
        },
        {
          title: "F-KV Fabric",
          description: "Fractal memory with decimal arithmetic coding delivering millisecond prefix lookups.",
          highlight: "Trie · Arithmetic Coding",
        },
        {
          title: "Knowledge synthesis",
          description: "Enumeration, MCTS, and genetic search collaborate to raise PoE while lowering MDL.",
          highlight: "Search · PoE Engine",
        },
        {
          title: "Kolibri Studio",
          description: "A React/Vite web cockpit with live dashboards, traces, and node orchestration.",
          highlight: "React · Vite · WebGL",
        },
      ],
    },
    useCases: {
      title: "Use cases",
      lead: "Kolibri Ω transforms formal knowledge into digitized workflows for business and science.",
      items: [
        {
          title: "Financial compliance",
          description: "Digitized rulebooks automate checks and reduce operational risk while accelerating audits.",
          impact: "−65% validation effort",
        },
        {
          title: "Research labs",
          description: "Kolibri curates formula catalogs and proposes optimal experiment plans on demand.",
          impact: "+3× discovery velocity",
        },
        {
          title: "Industrial protocols",
          description: "Δ-VM executes verifiable action sequences for mission-critical automation.",
          impact: "99.9% procedure adherence",
        },
      ],
    },
    faq: {
      title: "Frequently asked questions",
      items: [
        {
          question: "How is Kolibri different from LLMs?",
          answer:
            "Kolibri Ω relies on programs and formal rules instead of weights. Decisions are transparent, reproducible, and traceable.",
        },
        {
          question: "Can Kolibri integrate with existing systems?",
          answer:
            "Yes. The compact core runs on-premises and exposes HTTP APIs for dialogue, program execution, and memory access.",
        },
        {
          question: "How do you secure the knowledge base?",
          answer:
            "The Proof-of-Use knowledge blockchain tracks provenance while cryptographic signatures and reputation protect the network.",
        },
      ],
    },
    cta: {
      title: "Join the Kolibri flight",
      description:
        "Investors, researchers, and engineers — let’s define the new standard for digit- and formula-native intelligence together.",
      action: "Book a meeting",
    },
    install: {
      button: "Install Kolibri Ω",
      ariaLabel: "Install the Kolibri Ω web app on your device",
      badge: "PWA · Offline ready",
      message:
        "Add Kolibri Ω to your home screen for offline insights, instant launch, and live updates.",
      installed: "Kolibri Ω is installed",
      dismissed: "You can install it later from your browser menu",
    },
    chat: {
      title: "Kolibri AI",
      status: "Online",
      ariaLabel: "Kolibri AI chat window",
      placeholder: "Ask about Kolibri’s capabilities...",
      send: "Send",
      initialMessages: [
        { role: "ai", content: "Hello! I’m Kolibri AI, ready to guide you through decimal-native intelligence." },
        { role: "user", content: "What components power the platform?" },
        { role: "ai", content: "Δ-VM, fractal F-KV memory, and the knowledge blockchain operate as one system." },
      ],
      autoReplies: [
        "We surface Δ-VM traces and PoE metrics live inside Kolibri Studio.",
        "Kolibri deploys fast: the compact core and HTTP API v1 are production-ready within minutes.",
        "Network nodes exchange programs via Proof-of-Use while preserving knowledge quality.",
      ],
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
  return window.matchMedia("(prefers-color-scheme: light)").matches
    ? "light"
    : "dark";
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
  const [chatMessages, setChatMessages] = useState<ChatMessage[]>(
    () => [...copy[determineInitialLanguage()].chat.initialMessages]
  );
  const [chatInput, setChatInput] = useState("");
  const [autoReplyIndex, setAutoReplyIndex] = useState(0);
  const [installPromptEvent, setInstallPromptEvent] = useState<BeforeInstallPromptEvent | null>(null);
  const [installStatus, setInstallStatus] = useState<InstallStatus>(() => {
    if (typeof window === "undefined") {
      return "idle";
    }
    const standaloneMedia = window.matchMedia("(display-mode: standalone)");
    const isStandalone = standaloneMedia.matches || (window.navigator as { standalone?: boolean }).standalone;
    return isStandalone ? "installed" : "idle";
  });
  const [installAvailable, setInstallAvailable] = useState(false);

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    document.body.setAttribute("data-theme", theme);
    window.localStorage.setItem(THEME_STORAGE_KEY, theme);
  }, [theme]);

  useEffect(() => {
    document.documentElement.lang = language;
    window.localStorage.setItem(LANGUAGE_STORAGE_KEY, language);
    setChatMessages([...copy[language].chat.initialMessages]);
    setChatInput("");
    setAutoReplyIndex(0);
  }, [language]);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }

    const handleBeforeInstallPrompt = (event: Event) => {
      event.preventDefault();
      const promptEvent = event as BeforeInstallPromptEvent;
      setInstallPromptEvent(promptEvent);
      setInstallAvailable(true);
      setInstallStatus("idle");
    };

    window.addEventListener("beforeinstallprompt", handleBeforeInstallPrompt as EventListener);

    return () => {
      window.removeEventListener("beforeinstallprompt", handleBeforeInstallPrompt as EventListener);
    };
  }, []);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }

    const handleInstalled = () => {
      setInstallStatus("installed");
      setInstallAvailable(false);
      setInstallPromptEvent(null);
    };

    window.addEventListener("appinstalled", handleInstalled);

    return () => {
      window.removeEventListener("appinstalled", handleInstalled);
    };
  }, []);

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }

    const media = window.matchMedia("(display-mode: standalone)");
    if (media.matches) {
      setInstallStatus("installed");
      setInstallAvailable(false);
      setInstallPromptEvent(null);
    }

    const handleChange = (event: MediaQueryListEvent) => {
      if (event.matches) {
        setInstallStatus("installed");
        setInstallAvailable(false);
        setInstallPromptEvent(null);
      }
    };

    media.addEventListener("change", handleChange);

    return () => {
      media.removeEventListener("change", handleChange);
    };
  }, []);

  useEffect(() => {
    if (typeof window === "undefined" || installStatus !== "dismissed") {
      return;
    }

    const timeout = window.setTimeout(() => setInstallStatus("idle"), 6000);
    return () => window.clearTimeout(timeout);
  }, [installStatus]);

  const toggleTheme = useCallback(() => {
    setTheme((prev) => (prev === "light" ? "dark" : "light"));
  }, []);

  const switchLanguage = useCallback((value: Language) => {
    setLanguage(value);
  }, []);

  const handleInstall = useCallback(async () => {
    if (!installPromptEvent) {
      return;
    }

    installPromptEvent.prompt();
    try {
      const choice = await installPromptEvent.userChoice;
      if (choice.outcome === "accepted") {
        setInstallStatus("installed");
      } else {
        setInstallStatus("dismissed");
      }
    } catch {
      setInstallStatus("dismissed");
    } finally {
      setInstallPromptEvent(null);
      setInstallAvailable(false);
    }
  }, [installPromptEvent]);

  const content = useMemo(() => copy[language], [language]);

  const canShowInstallButton = installAvailable && installPromptEvent !== null;
  const installStatusMessage =
    installStatus === "installed"
      ? content.install.installed
      : installStatus === "dismissed"
        ? content.install.dismissed
        : "";
  const installStatusClass =
    installStatus === "installed" ? "install-status install-status--ready" : "install-status install-status--muted";

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

  const handleChatSubmit = useCallback(
    (event: FormEvent<HTMLFormElement>) => {
      event.preventDefault();
      const trimmed = chatInput.trim();
      if (!trimmed) {
        return;
      }

      const replies = copy[language].chat.autoReplies;
      const reply = replies.length > 0 ? replies[autoReplyIndex % replies.length] : "";

      setChatMessages((prev) => [
        ...prev,
        { role: "user", content: trimmed },
        ...(reply ? [{ role: "ai", content: reply }] : []),
      ]);
      setChatInput("");
      if (replies.length > 0) {
        setAutoReplyIndex((prev) => (prev + 1) % replies.length);
      }
    },
    [autoReplyIndex, chatInput, language]
  );

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
          {canShowInstallButton && (
            <button
              type="button"
              className="install-button"
              onClick={handleInstall}
              aria-label={content.install.ariaLabel}
            >
              {content.install.button}
            </button>
          )}
          <button type="button" className="theme-toggle" onClick={toggleTheme} aria-label={themeToggleLabel}>
            {themeToggleText}
          </button>
          {!canShowInstallButton && installStatus === "installed" && (
            <span className="install-status install-status--header" role="status" aria-live="polite">
              {content.install.installed}
            </span>
          )}
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
            <div className="pwa-callout">
              <span className="pwa-chip">{content.install.badge}</span>
              <p>{content.install.message}</p>
              {installStatusMessage && (
                <span className={installStatusClass} aria-live="polite" role="status">
                  {installStatusMessage}
                </span>
              )}
            </div>
            <div className="metrics-grid">
              {content.metrics.map((metric) => (
                <div key={metric.label} className="metric-card">
                  <span className="metric-value">{metric.value}</span>
                  <span className="metric-label">{metric.label}</span>
                </div>
              ))}
            </div>
            <aside className="chat-panel" aria-label={content.chat.ariaLabel}>
              <header className="chat-header">
                <div className="chat-avatar" aria-hidden="true">
                  Ω
                </div>
                <div>
                  <p className="chat-title">{content.chat.title}</p>
                  <p className="chat-status">{content.chat.status}</p>
                </div>
              </header>
              <div className="chat-messages" role="log" aria-live="polite">
                {chatMessages.map((message, index) => (
                  <div key={`${message.role}-${index}`} className={`chat-message chat-message-${message.role}`}>
                    <span>{message.content}</span>
                  </div>
                ))}
              </div>
              <form className="chat-input" onSubmit={handleChatSubmit}>
                <label className="sr-only" htmlFor="chat-entry">
                  {content.chat.placeholder}
                </label>
                <input
                  id="chat-entry"
                  type="text"
                  value={chatInput}
                  onChange={(event) => setChatInput(event.target.value)}
                  placeholder={content.chat.placeholder}
                  autoComplete="off"
                />
                <button type="submit">{content.chat.send}</button>
              </form>
            </aside>
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

        <section id="stack" className="section">
          <h2 className="section-title">{content.techStack.title}</h2>
          <p className="section-lead">{content.techStack.lead}</p>
          <div className="tech-grid">
            {content.techStack.items.map((item) => (
              <article key={item.title} className="tech-card">
                <div>
                  <h3>{item.title}</h3>
                  <p>{item.description}</p>
                </div>
                <span className="tech-highlight">{item.highlight}</span>
              </article>
            ))}
          </div>
        </section>

        <section id="use-cases" className="section">
          <h2 className="section-title">{content.useCases.title}</h2>
          <p className="section-lead">{content.useCases.lead}</p>
          <div className="usecases-grid">
            {content.useCases.items.map((item) => (
              <article key={item.title} className="case-card">
                <header>
                  <h3>{item.title}</h3>
                  <span className="case-impact">{item.impact}</span>
                </header>
                <p>{item.description}</p>
              </article>
            ))}
          </div>
        </section>

        <section id="faq" className="section">
          <h2 className="section-title">{content.faq.title}</h2>
          <div className="faq-list">
            {content.faq.items.map((item) => (
              <details key={item.question} className="faq-item">
                <summary>
                  <span>{item.question}</span>
                  <span aria-hidden="true" className="faq-icon">
                    ＋
                  </span>
                </summary>
                <p>{item.answer}</p>
              </details>
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
