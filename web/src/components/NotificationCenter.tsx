// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { createContext, PropsWithChildren, useCallback, useContext, useEffect, useMemo, useRef, useState } from "react";

export type NotificationType = "info" | "success" | "error";

export interface NotificationOptions {
  title: string;
  message?: string;
  type?: NotificationType;
  timeout?: number;
}

export interface Notification extends NotificationOptions {
  id: string;
  type: NotificationType;
}

interface NotificationContextValue {
  notify: (options: NotificationOptions) => string;
  dismiss: (id: string) => void;
}

const NotificationContext = createContext<NotificationContextValue | undefined>(undefined);

function createId() {
  if (typeof crypto !== "undefined" && "randomUUID" in crypto) {
    return crypto.randomUUID();
  }
  return Math.random().toString(36).slice(2, 10);
}

export function NotificationProvider({ children }: PropsWithChildren) {
  const [items, setItems] = useState<Notification[]>([]);
  const timers = useRef(new Map<string, number>());

  const dismiss = useCallback((id: string) => {
    setItems((prev) => prev.filter((item) => item.id !== id));
    const handle = timers.current.get(id);
    if (handle) {
      window.clearTimeout(handle);
      timers.current.delete(id);
    }
  }, []);

  const notify = useCallback(
    ({ title, message, type = "info", timeout = 5000 }: NotificationOptions) => {
      const id = createId();
      setItems((prev) => [...prev, { id, title, message, type }]);
      if (timeout > 0) {
        const handle = window.setTimeout(() => dismiss(id), timeout);
        timers.current.set(id, handle);
      }
      return id;
    },
    [dismiss]
  );

  useEffect(() => () => timers.current.forEach((handle) => window.clearTimeout(handle)), []);

  const value = useMemo(() => ({ notify, dismiss }), [notify, dismiss]);

  return (
    <NotificationContext.Provider value={value}>
      {children}
      <div className="notification-center" role="region" aria-live="assertive">
        {items.map((item) => (
          <div key={item.id} className="notification" data-type={item.type}>
            <h4>{item.title}</h4>
            {item.message ? <p>{item.message}</p> : null}
            <button type="button" onClick={() => dismiss(item.id)} aria-label="Закрыть уведомление">
              Закрыть
            </button>
          </div>
        ))}
      </div>
    </NotificationContext.Provider>
  );
}

export function useNotifications() {
  const ctx = useContext(NotificationContext);
  if (!ctx) {
    throw new Error("useNotifications must be used within NotificationProvider");
  }
  return ctx;
}
