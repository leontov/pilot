// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import type { KeyboardEvent } from "react";
import { useCallback, useEffect, useMemo, useRef } from "react";

export interface TabItem {
  id: string;
  label: string;
  description?: string;
}

export interface TabNavigationProps {
  tabs: TabItem[];
  activeId: string;
  onChange: (id: string) => void;
}

export function TabNavigation({ tabs, activeId, onChange }: TabNavigationProps) {
  const buttonsRef = useRef<Array<HTMLButtonElement | null>>([]);

  useEffect(() => {
    buttonsRef.current = buttonsRef.current.slice(0, tabs.length);
  }, [tabs.length]);

  const focusTab = useCallback((index: number) => {
    const button = buttonsRef.current[index];
    if (button) {
      button.focus();
    }
  }, []);

  const handleKeyDown = useCallback(
    (event: KeyboardEvent<HTMLButtonElement>, index: number) => {
      if (event.key === "ArrowRight" || event.key === "ArrowDown") {
        event.preventDefault();
        const nextIndex = (index + 1) % tabs.length;
        onChange(tabs[nextIndex]?.id ?? activeId);
        focusTab(nextIndex);
        return;
      }
      if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
        event.preventDefault();
        const prevIndex = (index - 1 + tabs.length) % tabs.length;
        onChange(tabs[prevIndex]?.id ?? activeId);
        focusTab(prevIndex);
        return;
      }
      if (event.key === "Home") {
        event.preventDefault();
        onChange(tabs[0]?.id ?? activeId);
        focusTab(0);
        return;
      }
      if (event.key === "End") {
        event.preventDefault();
        const lastIndex = tabs.length - 1;
        onChange(tabs[lastIndex]?.id ?? activeId);
        focusTab(lastIndex);
      }
    },
    [activeId, focusTab, onChange, tabs]
  );

  const activeTab = useMemo(() => tabs.find((tab) => tab.id === activeId), [activeId, tabs]);

  return (
    <div className="tab-navigation">
      <div className="tab-navigation__list" role="tablist" aria-label="Основные разделы Kolibri Ω Studio">
        {tabs.map((tab, index) => (
          <button
            key={tab.id}
            ref={(element) => {
              buttonsRef.current[index] = element;
            }}
            type="button"
            role="tab"
            className="tab-navigation__button"
            aria-selected={activeId === tab.id}
            aria-controls={`${tab.id}-panel`}
            id={`${tab.id}-tab`}
            tabIndex={activeId === tab.id ? 0 : -1}
            onClick={() => onChange(tab.id)}
            onKeyDown={(event) => handleKeyDown(event, index)}
          >
            <span className="tab-navigation__label">{tab.label}</span>
            {tab.description ? <span className="tab-navigation__description">{tab.description}</span> : null}
          </button>
        ))}
      </div>
      {activeTab?.description ? (
        <p className="tab-navigation__meta" aria-live="polite">
          {activeTab.description}
        </p>
      ) : null}
    </div>
  );
}
