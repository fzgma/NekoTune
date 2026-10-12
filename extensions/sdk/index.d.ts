export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type Params = Record<string, any>;
export type Disposable = () => void | Promise<void>;
export interface Track {
  id: string;
  title: string;
  artist?: string;
  album?: string;
  duration_ms?: number;
  cover_url?: string;
}
export interface ResolvedAudio {
  url: string;
  headers?: Record<string, string>;
  expiresAt?: number;
  extension?: string;
  maxBytes?: number;
  allowedHosts?: string[];
  contentTypes?: string[];
}
export interface MusicProvider {
  search(params: { query: string; cursor?: string }): Promise<{ tracks: Track[]; cursor?: string }>;
  track(params: { id: string }): Promise<Track>;
  resolve(params: { id: string; purpose?: 'play' | 'download' }): Promise<ResolvedAudio>;
  download?(params: { id: string }): Promise<ResolvedAudio>;
  downloaded?(params: { id: string; path: string; song_id: number; existing: boolean }): Promise<Params>;
}
export interface LyricsProvider {
  search(params: Params): Promise<{ candidates: Params[] }>;
  resolve(params: Params): Promise<Params>;
}
export interface ExtensionContext {
  readonly id: string;
  readonly version: string;
  readonly directory: string;
  readonly dataDirectory: string;
  config: Params;
  host: { call<T = Params>(method: string, params?: Params): Promise<T> };
  events: {
    on(event: string, callback: (event: Params) => void | Promise<void>): Disposable;
    emit(event: string, data?: Params): Promise<void>;
  };
  services: {
    register(
      id: string,
      handler: (params: Params) => Params | Promise<Params>,
      descriptor?: Params,
    ): Promise<Disposable>;
    call<T = Params>(extension: string, service: string, params?: Params): Promise<T>;
  };
  commands: {
    register(
      id: string,
      handler: (params: Params) => any,
      descriptor?: { title?: string; shortcut?: string },
    ): Promise<Disposable>;
  };
  ui: {
    register(kind: 'themes', id: string, descriptor: ThemeDescriptor): Promise<Disposable>;
    register(
      kind: 'pages' | 'settings' | 'slots' | 'themes' | 'menus' | 'toolbars',
      id: string,
      descriptor: Params,
    ): Promise<Disposable>;
  };
  music: {
    register(
      id: string,
      provider: MusicProvider,
      descriptor?: { name?: string; download?: boolean; page?: string },
    ): Promise<Disposable>;
  };
  lyrics: {
    register(id: string, provider: LyricsProvider, descriptor?: { name?: string }): Promise<Disposable>;
  };
  storage: { get<T = Json>(key: string): Promise<T | null>; set(key: string, value: Json): Promise<void> };
  secrets: {
    get(key: string): Promise<string | null>;
    set(key: string, value: string): Promise<void>;
    delete(key: string): Promise<void>;
  };
  settings: { update(config: Params): Promise<void> };
  tasks: {
    report(task: string, progress: { state: string; progress?: number; message?: string }): Promise<void>;
  };
  subscriptions: Disposable[];
  log: Pick<Console, 'info' | 'warn' | 'error' | 'debug' | 'log'>;
}
/** Theme token names, ranges and defaults are defined in shared/themes/catalog.json. */
export type ThemeTokens = Record<string, string | number | boolean>;
export interface ThemeBackground {
  source: string;
  opacity?: number;
  overlayColor?: string;
  overlayOpacity?: number;
  blur?: number;
}
export interface ThemeVariant {
  tokens?: ThemeTokens;
  background?: ThemeBackground;
}
export interface ThemeDescriptor {
  title: string | { zh?: string; en?: string };
  author?: string;
  schemaVersion: 1;
  tokens?: ThemeTokens;
  variants: { light: ThemeVariant; dark: ThemeVariant };
  ui?: {
    background?: string;
    shell?: string;
    bottomPlayer?: string;
    pages?: Partial<
      Record<
        | 'home'
        | 'library'
        | 'queue'
        | 'lyrics'
        | 'settings'
        | 'extensions'
        | 'music_sources'
        | 'lyrics_debug',
        string
      >
    >;
  };
}
