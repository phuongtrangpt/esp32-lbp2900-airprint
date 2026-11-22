// Remember the user's preference independently of browser autoplay permission.
export const AUDIO_PREFERENCE_KEY = 'vietphuong.audio.enabled';
export interface AudioAlertState { enabled: boolean; ready: boolean; pending: boolean }
export class AudioAlerts {
  enabled = false;
  context: AudioContext | null = null;
  pending = false;
  private disposed = false;
  private flight: Promise<boolean> | null = null;
  private storage: () => Pick<Storage, 'getItem' | 'setItem'>;
  private makeContext: () => AudioContext;
  private changed: (state: AudioAlertState) => void;

  constructor(storage: () => Pick<Storage, 'getItem' | 'setItem'>,
              makeContext: () => AudioContext,
              changed: (state: AudioAlertState) => void) {
    this.storage = storage;
    this.makeContext = makeContext;
    this.changed = changed;
    try { this.enabled = storage().getItem(AUDIO_PREFERENCE_KEY) === 'true'; } catch { /* storage unavailable */ }
    this.emit();
  }
  private emit() {
    if (!this.disposed) this.changed({ enabled: this.enabled,
      ready: this.enabled && this.context?.state === 'running', pending: this.pending });
  }
  setEnabled(enabled: boolean) {
    if (this.disposed) return;
    this.enabled = enabled;
    try { this.storage().setItem(AUDIO_PREFERENCE_KEY, String(enabled)); } catch { /* private/restricted storage */ }
    if (!enabled) this.close();
    this.emit();
  }
  activate(): Promise<boolean> {
    if (this.disposed || !this.enabled) return Promise.resolve(false);
    if (this.context?.state === 'running') return Promise.resolve(true);
    if (this.flight) return this.flight;
    this.pending = true;
    this.emit();
    let ctx: AudioContext;
    let resumed: Promise<void>;
    try {
      ctx = this.context ??= this.makeContext();
      ctx.onstatechange = () => this.emit();
      // Keep resume directly in the pointer/key handler's activation scope.
      resumed = ctx.resume();
    } catch {
      this.pending = false;
      this.emit();
      return Promise.resolve(false);
    }
    const attempt = resumed.then(() =>
      !this.disposed && this.enabled && this.context === ctx && ctx.state === 'running'
    ).catch(() => false).finally(() => {
      if (this.flight === attempt) {
        this.flight = null;
        this.pending = false;
        this.emit();
      }
    });
    this.flight = attempt;
    return attempt;
  }
  private close() {
    const ctx = this.context;
    this.context = null;
    this.flight = null;
    this.pending = false;
    if (ctx) {
      ctx.onstatechange = null;
      void ctx.close().catch(() => {});
    }
  }
  dispose() {
    this.disposed = true;
    this.close(); // navigation must not overwrite the saved preference
  }
}
