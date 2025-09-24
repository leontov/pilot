// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useEffect, useRef } from "react";

interface AutoRefreshOptions {
  intervalMs?: number;
  runImmediately?: boolean;
}

export function useAutoRefresh(callback: () => void | Promise<void>, isActive: boolean, options: AutoRefreshOptions = {}) {
  const { intervalMs = 10000, runImmediately = false } = options;
  const callbackRef = useRef(callback);

  useEffect(() => {
    callbackRef.current = callback;
  }, [callback]);

  useEffect(() => {
    if (!isActive) {
      return;
    }

    if (runImmediately) {
      void callbackRef.current();
    }

    const handle = window.setInterval(() => {
      void callbackRef.current();
    }, intervalMs);

    return () => window.clearInterval(handle);
  }, [isActive, intervalMs, runImmediately]);
}
