// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { useCallback, useEffect, useRef, useState } from "react";

export interface LiveQueryOptions<T> {
  fetcher: () => Promise<T>;
  /**
   * Interval in milliseconds for automatic refresh. Set to 0 or a falsy value to disable polling.
   */
  intervalMs?: number;
  /**
   * Whether the query should run. Useful for gating behind user interaction.
   */
  enabled?: boolean;
  /**
   * Trigger an immediate fetch on mount. Defaults to true.
   */
  immediate?: boolean;
  /**
   * Optional initial data to use before the first fetch resolves.
   */
  initialData?: T;
  /**
   * Error handler invoked whenever the fetcher throws.
   */
  onError?: (error: unknown) => void;
}

export interface LiveQueryState<T> {
  data: T | undefined;
  error: Error | null;
  isLoading: boolean;
  lastUpdated: Date | null;
  refresh: () => Promise<T | undefined>;
}

function toError(error: unknown): Error {
  if (error instanceof Error) {
    return error;
  }
  return new Error(typeof error === "string" ? error : "Неизвестная ошибка запроса");
}

/**
 * useLiveQuery adds resilient polling to imperative fetchers.
 * It automatically deduplicates concurrent refreshes and exposes the timestamp of the last successful update.
 */
export function useLiveQuery<T>(options: LiveQueryOptions<T>): LiveQueryState<T> {
  const { fetcher, intervalMs, enabled = true, immediate = true, initialData, onError } = options;
  const [data, setData] = useState<T | undefined>(initialData);
  const [error, setError] = useState<Error | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const isMountedRef = useRef(true);
  const refreshInFlightRef = useRef<Promise<T | undefined> | null>(null);

  useEffect(() => () => {
    isMountedRef.current = false;
  }, []);

  const refresh = useCallback(async () => {
    if (!enabled) {
      return data;
    }
    if (refreshInFlightRef.current) {
      return refreshInFlightRef.current;
    }
    const run = async () => {
      setIsLoading(true);
      try {
        const result = await fetcher();
        if (isMountedRef.current) {
          setData(result);
          setError(null);
          setLastUpdated(new Date());
        }
        return result;
      } catch (unknownError) {
        const normalized = toError(unknownError);
        if (isMountedRef.current) {
          setError(normalized);
        }
        onError?.(normalized);
        return undefined;
      } finally {
        if (isMountedRef.current) {
          setIsLoading(false);
        }
        refreshInFlightRef.current = null;
      }
    };

    const promise = run();
    refreshInFlightRef.current = promise;
    return promise;
  }, [data, enabled, fetcher, onError]);

  useEffect(() => {
    if (!enabled || !immediate) {
      return;
    }
    void refresh();
  }, [enabled, immediate, refresh]);

  useEffect(() => {
    if (!enabled || !intervalMs || intervalMs <= 0) {
      return;
    }
    const id = setInterval(() => {
      void refresh();
    }, intervalMs);
    return () => clearInterval(id);
  }, [enabled, intervalMs, refresh]);

  return { data, error, isLoading, lastUpdated, refresh };
}
