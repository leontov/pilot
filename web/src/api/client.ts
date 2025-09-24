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

export function resolveApiBaseUrl(): string {
  const envBase = import.meta.env.VITE_API_BASE;
  if (typeof envBase === "string" && envBase.trim().length > 0) {
    return envBase.trim();
  }

  if (typeof window !== "undefined" && window.location?.origin) {
    return window.location.origin;
  }

  return "";
}

export interface KolibriClientOptions {
  baseUrl?: string;
  fetchImpl?: typeof fetch;
  defaultHeaders?: HeadersInit;
  timeoutMs?: number;
}

export class KolibriClient {
  constructor(private readonly options: KolibriClientOptions = {}) {}

  get baseUrl(): string {
    return this.options.baseUrl ?? "";
  }

  withBaseUrl(baseUrl: string): KolibriClient {
    return new KolibriClient({ ...this.options, baseUrl });
  }

  private buildUrl(path: string): string {
    const baseUrl = this.baseUrl;
    if (!baseUrl) {
      return path;
    }

    try {
      const normalizedBase = baseUrl.endsWith("/") ? baseUrl : `${baseUrl}/`;
      const normalizedPath = path.startsWith("/") ? path.slice(1) : path;
      return new URL(normalizedPath, normalizedBase).toString();
    } catch (error) {
      console.warn("KolibriClient: failed to construct request URL", error);
      return `${baseUrl}${path}`;
    }
  }

  private async request<TResponse = unknown, TBody = unknown>(path: string, init: RequestOptions = {}): Promise<TResponse> {
    const headers = new Headers();
    const applyHeaders = (source?: HeadersInit) => {
      if (!source) {
        return;
      }
      const resolved = new Headers(source);
      resolved.forEach((value, key) => {
        headers.set(key, value);
      });
    };

    applyHeaders(this.options.defaultHeaders);
    applyHeaders(init.headers);

    const method = init.method ?? "GET";
    if (method !== "GET" && method !== "HEAD" && !(init.body instanceof FormData) && !headers.has("Content-Type")) {
      headers.set("Content-Type", "application/json");
    }

    const fetchImpl = this.options.fetchImpl ?? fetch;
    const requestUrl = this.buildUrl(path);
    const requestInit: RequestInit = { ...init, headers };

    let controller: AbortController | undefined;
    let timeoutId: ReturnType<typeof setTimeout> | undefined;

    if (!requestInit.signal && this.options.timeoutMs && this.options.timeoutMs > 0 && typeof AbortController !== "undefined") {
      controller = new AbortController();
      timeoutId = setTimeout(() => {
        const reason = typeof DOMException !== "undefined"
          ? new DOMException("Request timed out", "TimeoutError")
          : undefined;
        controller?.abort(reason);
      }, this.options.timeoutMs);
      requestInit.signal = controller.signal;
    }

    let response: Response;
    try {
      response = await fetchImpl(requestUrl, requestInit);
    } finally {
      if (timeoutId) {
        clearTimeout(timeoutId);
      }
    }

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

const API_BASE_URL = resolveApiBaseUrl();

export const apiClient = new KolibriClient({ baseUrl: API_BASE_URL });

export function createApiClient(options: KolibriClientOptions = {}): KolibriClient {
  if (options.baseUrl === undefined) {
    return new KolibriClient({ ...options, baseUrl: resolveApiBaseUrl() });
  }

  return new KolibriClient(options);
}
