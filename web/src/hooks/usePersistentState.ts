// Copyright (c) 2024 Кочуров Владислав Евгеньевич

import { Dispatch, SetStateAction, useEffect, useState } from "react";

function readStorage<T>(key: string, fallback: T): T {
  if (typeof window === "undefined") {
    return fallback;
  }
  try {
    const stored = window.localStorage.getItem(key);
    if (stored == null) {
      return fallback;
    }
    return JSON.parse(stored) as T;
  } catch (error) {
    console.warn(`Failed to read persistent state for key "${key}"`, error);
    return fallback;
  }
}

export function usePersistentState<T>(key: string, initialValue: T): [T, Dispatch<SetStateAction<T>>] {
  const [state, setState] = useState<T>(() => readStorage(key, initialValue));

  useEffect(() => {
    if (typeof window === "undefined") {
      return;
    }
    try {
      window.localStorage.setItem(key, JSON.stringify(state));
    } catch (error) {
      console.warn(`Failed to persist state for key "${key}"`, error);
    }
  }, [key, state]);

  return [state, setState];
}
