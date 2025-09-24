// Copyright (c) 2024 Кочуров Владислав Евгеньевич

export interface DialogRequest {
  input: string;
}

export interface DialogResponse {
  answer: string;
  trace?: unknown;
  timestamp?: string;
}

export interface VmRunRequest {
  program: number[];
  gasLimit?: number;
}

export interface VmRunResponse {
  status?: string;
  result?: unknown;
  trace?: unknown;
  steps?: number;
  gasUsed?: number;
}

export interface MemoryValue {
  key: string;
  value: string;
}

export interface MemoryProgram {
  id: string;
  description?: string;
  score?: number;
  bytecode?: number[];
}

export interface MemoryResponse {
  values?: MemoryValue[];
  programs?: MemoryProgram[];
}

export interface ProgramSubmitRequest {
  bytecode: number[];
  notes?: string;
}

export interface ProgramSubmitResponse {
  programId?: string;
  poe?: number;
  mdl?: number;
  score?: number;
  accepted?: boolean;
  trace?: unknown;
}

export interface ChainSubmitRequest {
  programId: string;
}

export interface ChainSubmitResponse {
  status: string;
  blockId?: string;
  position?: number;
}

export interface MemoryStats {
  total?: number;
  used?: number;
}

export interface PeerInfo {
  id: string;
  role?: string;
  latency?: number;
  status?: string;
  address?: string;
  score?: number;
}

export interface HealthResponse {
  uptime: number;
  memory: MemoryStats;
  peers?: PeerInfo[];
  blocks?: number;
  version?: string;
}

export interface MetricsResponse {
  uptime?: number;
  memory?: MemoryStats;
  peers?: PeerInfo[];
  blocks?: number;
  tasksInFlight?: number;
  lastBlockTime?: string;
  [key: string]: unknown;
}

export class ApiError<T = unknown> extends Error {
  readonly status: number;
  readonly payload?: T;

  constructor(message: string, status: number, payload?: T) {
    super(message);
    this.name = "ApiError";
    this.status = status;
    this.payload = payload;
  }
}

const STATUS_MESSAGES: Record<number, string> = {
  400: "Некорректный запрос к API диалога. Проверьте отправляемые данные.",
  401: "Требуется аутентификация для обращения к ядру Kolibri Ω.",
  403: "Доступ к диалоговому эндпоинту запрещён.",
  404: "Эндпоинт диалога недоступен, проверьте запуск ядра Kolibri Ω.",
  409: "Конфликт при обращении к диалоговому API. Повторите попытку позже.",
  429: "Превышен лимит обращений к ядру Kolibri Ω. Подождите перед новой попыткой.",
  500: "Внутренняя ошибка ядра Kolibri Ω. Проверьте логи сервиса.",
  502: "Некорректный ответ от backend. Убедитесь, что сервис запущен корректно.",
  503: "Диалоговый сервис временно недоступен. Попробуйте повторить запрос позже.",
  504: "Превышено время ожидания ответа от ядра Kolibri Ω."
};

export function formatApiError(error: unknown): string {
  if (error instanceof ApiError) {
    const predefined = STATUS_MESSAGES[error.status];
    if (predefined) {
      return predefined;
    }
    const fallback = error.message || "Запрос к API завершился ошибкой.";
    return `Ошибка API (${error.status}). ${fallback}`;
  }

  if (error instanceof Error) {
    return error.message;
  }

  return "Неизвестная ошибка";
}

interface RequestOptions extends RequestInit {
  skipJson?: boolean;
}

export class KolibriClient {
  constructor(private readonly baseUrl = "") {}

  private async request<TResponse = unknown, TBody = unknown>(path: string, init: RequestOptions = {}): Promise<TResponse> {
    const headers = new Headers(init.headers ?? {});
    if (!(init.body instanceof FormData) && !headers.has("Content-Type") && init.method && init.method !== "GET") {
      headers.set("Content-Type", "application/json");
    }

    const response = await fetch(`${this.baseUrl}${path}`, { ...init, headers });
    if (!response.ok) {
      let payload: unknown;
      try {
        payload = await response.json();
      } catch (error) {
        // ignore JSON parse errors
      }
      const message = payload && typeof payload === "object" && "error" in payload ? String((payload as { error: unknown }).error) : response.statusText;
      throw new ApiError(message || "Request failed", response.status, payload);
    }

    if (init.skipJson || response.status === 204) {
      return undefined as TResponse;
    }

    const text = await response.text();
    if (!text) {
      return undefined as TResponse;
    }

    return JSON.parse(text) as TResponse;
  }

  dialog(request: DialogRequest): Promise<DialogResponse> {
    return this.request<DialogResponse>("/api/v1/dialog", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  runProgram(request: VmRunRequest): Promise<VmRunResponse> {
    return this.request<VmRunResponse>("/api/v1/vm/run", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  getMemory(prefix: string): Promise<MemoryResponse> {
    const search = new URLSearchParams({ prefix });
    return this.request<MemoryResponse>(`/api/v1/fkv/get?${search.toString()}`, {
      method: "GET"
    });
  }

  submitProgram(request: ProgramSubmitRequest): Promise<ProgramSubmitResponse> {
    return this.request<ProgramSubmitResponse>("/api/v1/program/submit", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  submitChain(request: ChainSubmitRequest): Promise<ChainSubmitResponse> {
    return this.request<ChainSubmitResponse>("/api/v1/chain/submit", {
      method: "POST",
      body: JSON.stringify(request)
    });
  }

  health(): Promise<HealthResponse> {
    return this.request<HealthResponse>("/api/v1/health", {
      method: "GET"
    });
  }

  metrics(): Promise<MetricsResponse> {
    return this.request<MetricsResponse>("/api/v1/metrics", {
      method: "GET"
    });
  }
}

export const apiClient = new KolibriClient();
