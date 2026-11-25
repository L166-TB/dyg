export interface Datapoint {
  at: string;
  value: string | number;
}

export interface Datastream {
  id: string;
  datapoints?: Datapoint[];
}

export interface CloudData {
  datastreams?: Datastream[];
  cmd_resp?: string;
  cmd_uuid?: string;
}

export interface CloudEnvelope {
  code: number;
  msg?: string;
  data?: CloudData;
}

export interface Snapshot {
  kind: string;
  values: Record<string, Object | null>;
  timestamps: Record<string, number>;
  at: number;
  oldestAt: number;
  freshFields: number;
  presentFields: number;
  samplingValid: boolean;
  fresh: boolean;
}

export interface CommandResult {
  kind: string;
  result_code: number;
  result_desc: string;
  cmd_uuid: string;
}

export interface CommandRequest {
  command: string;
  params: Record<string, Object>;
}

export interface HostActions {
  showConfig(): void;
  refresh(): void;
  command(request: CommandRequest): void;
  speak(text: string): void;
  theme(dark: boolean): void;
  emit(event: Object): void;
}
