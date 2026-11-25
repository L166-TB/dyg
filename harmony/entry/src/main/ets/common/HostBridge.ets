import { CommandRequest, HostActions } from './Types';

export class HostBridge {
  actions: HostActions;

  constructor(actions: HostActions) {
    this.actions = actions;
  }

  post(json: string): void {
    if (json.length === 0 || json.length > 2000) {
      return;
    }
    try {
      const request: Record<string, Object> = JSON.parse(json) as Record<string, Object>;
      const type: string = request['type'] === undefined ? '' : String(request['type']);
      if (type === 'configure') {
        this.actions.showConfig();
      } else if (type === 'refresh') {
        this.actions.refresh();
      } else if (type === 'command') {
        this.actions.command(request['request'] as CommandRequest);
      } else if (type === 'speak') {
        this.actions.speak(request['text'] === undefined ? '' : String(request['text']));
      } else if (type === 'theme') {
        this.actions.theme(Boolean(request['dark']));
      }
    } catch (error) {
      this.actions.emit({ kind: 'connection-error', message: '请求格式错误' });
    }
  }
}
