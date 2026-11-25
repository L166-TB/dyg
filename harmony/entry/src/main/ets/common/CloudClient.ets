import { http } from '@kit.NetworkKit';
import { util } from '@kit.ArkTS';
import { CloudEnvelope, Datastream, Snapshot, CommandResult, CommandRequest } from './Types';
import { DATA_CONTRACT, FRESH_MS } from './DataContract';

const BASE_URL: string = 'https://iot-api.heclouds.com';

export class CloudClient {
  private token: string;
  private product: string;
  private device: string;

  constructor(token: string, product: string, device: string) {
    this.token = token;
    this.product = product;
    this.device = device;
  }

  private async request(path: string, body: CommandRequest | null): Promise<CloudEnvelope> {
    const client: http.HttpRequest = http.createHttp();
    try {
      const headers: Record<string, string> = {
        'Authorization': this.token,
        'Accept': 'application/json'
      };
      let options: http.HttpRequestOptions;
      if (body === null) {
        options = {
          method: http.RequestMethod.GET,
          header: headers,
          connectTimeout: 10000,
          readTimeout: 12000,
          expectDataType: http.HttpDataType.STRING,
          usingCache: false
        };
      } else {
        headers['Content-Type'] = 'application/json; charset=utf-8';
        options = {
          method: http.RequestMethod.POST,
          header: headers,
          extraData: JSON.stringify(body),
          connectTimeout: 10000,
          readTimeout: 22000,
          expectDataType: http.HttpDataType.STRING,
          usingCache: false
        };
      }

      const response: http.HttpResponse = await client.request(BASE_URL + path, options);
      if (response.responseCode === 401 || response.responseCode === 403) {
        throw new Error('云授权无效或已过期，请更新产品级 API token');
      }
      if (response.responseCode < 200 || response.responseCode >= 300) {
        throw new Error('OneNET HTTP ' + response.responseCode);
      }
      const text: string = typeof response.result === 'string' ? response.result : '';
      if (text.length === 0) {
        throw new Error('云响应为空');
      }
      if (text.length > 2097152) {
        throw new Error('云响应过大');
      }
      const envelope: CloudEnvelope = JSON.parse(text) as CloudEnvelope;
      CloudClient.checkResponse(envelope);
      return envelope;
    } finally {
      client.destroy();
    }
  }

  private static checkResponse(result: CloudEnvelope): void {
    const code: number = Number(result.code);
    if (code === 200 || code === 0) {
      return;
    }
    if (code === 401) {
      throw new Error('产品级 API token 无效或已过期');
    }
    if (code === 403) {
      throw new Error('该产品没有读取设备数据的 API 权限');
    }
    if (code === 404) {
      throw new Error('OneNET 找不到该产品或设备，请核对产品 ID / 设备名称');
    }
    if (code === 429) {
      throw new Error('OneNET 请求过于频繁，请稍后再刷新');
    }
    let message: string = result.msg === undefined ? '' : result.msg;
    if (message.length > 80) {
      message = message.substring(0, 80);
    }
    throw new Error('OneNET code=' + code + (message.length > 0 ? '：' + message : ''));
  }

  async read(): Promise<Snapshot> {
    const envelope: CloudEnvelope = await this.request(this.queryPath(), null);
    return CloudClient.parse(envelope);
  }

  async command(body: CommandRequest): Promise<CommandResult> {
    const envelope: CloudEnvelope = await this.request(this.commandPath(15), body);
    const data = envelope.data;
    if (data === undefined) {
      throw new Error('云响应缺少 data');
    }
    const encoded: string = data.cmd_resp === undefined ? '' : data.cmd_resp;
    if (encoded.length === 0) {
      throw new Error('未收到设备回执，执行结果未知');
    }

    let responseText: string;
    try {
      const bytes: Uint8Array = new util.Base64Helper().decodeSync(encoded);
      responseText = util.TextDecoder.create('utf-8').decodeWithStream(bytes, { stream: false });
    } catch (error) {
      throw new Error('设备回执无法解码');
    }

    const response: Record<string, Object> = JSON.parse(responseText) as Record<string, Object>;
    const code: number = CloudClient.toNumber(response['result_code'], -1);
    if (code < 0 || code > 4) {
      throw new Error('设备回执格式不符，执行结果未知');
    }
    return {
      kind: 'command',
      result_code: code,
      result_desc: CloudClient.toStringValue(response['result_desc']),
      cmd_uuid: data.cmd_uuid === undefined ? '' : data.cmd_uuid
    };
  }

  private queryPath(): string {
    const ids: string = encodeURIComponent(DATA_CONTRACT.join(','));
    return '/datapoint/history-datapoints?product_id=' + encodeURIComponent(this.product) +
      '&device_name=' + encodeURIComponent(this.device) +
      '&limit=1&sort=DESC&datastream_id=' + ids;
  }

  private commandPath(timeout: number): string {
    return '/cmd?product_id=' + encodeURIComponent(this.product) +
      '&device_name=' + encodeURIComponent(this.device) +
      '&timeout=' + timeout;
  }

  private static parse(envelope: CloudEnvelope): Snapshot {
    const data = envelope.data;
    if (data === undefined) {
      throw new Error('云响应缺少 data');
    }
    const streams = data.datastreams;
    if (streams === undefined) {
      throw new Error('云响应缺少 datastreams');
    }

    const now: number = Date.now();
    const present: Map<string, Datastream> = new Map();
    for (const stream of streams) {
      present.set(stream.id, stream);
    }

    const values: Record<string, Object | null> = {};
    const timestamps: Record<string, number> = {};
    let presentFields: number = 0;
    let freshFields: number = 0;
    let oldestAt: number = 0;
    let latestAt: number = 0;

    for (const name of DATA_CONTRACT) {
      const stream: Datastream | undefined = present.get(name);
      let newestAt: number = 0;
      let newestValue: string | number | null = null;

      if (stream !== undefined && stream.datapoints !== undefined) {
        for (const point of stream.datapoints) {
          const at: number = CloudClient.timestamp(point.at);
          if (at > newestAt) {
            newestAt = at;
            newestValue = point.value;
          }
        }
      }

      let value: Object | null = null;
      if (newestAt > 0 && newestValue !== null) {
        if (name === 'firmware_version') {
          value = String(newestValue);
        } else {
          const numeric: number = Number(newestValue);
          value = Number.isFinite(numeric) ? numeric : null;
        }
        timestamps[name] = newestAt;
        presentFields += 1;
        if (oldestAt === 0 || newestAt < oldestAt) {
          oldestAt = newestAt;
        }
        if (newestAt > latestAt) {
          latestAt = newestAt;
        }
        if (now - newestAt <= FRESH_MS && newestAt <= now + 5000) {
          freshFields += 1;
        }
      }
      values[name] = value;
    }

    const environment: number = CloudClient.toNumber(values['environment_valid'], -1);
    const process: number = CloudClient.toNumber(values['process_valid'], -1);
    const firmware: string = CloudClient.toStringValue(values['firmware_version']);
    const juiceFirmware: boolean = firmware.startsWith('cane-onenet-juice-');
    const valid: boolean = environment >= 0 && process >= 0 &&
      (juiceFirmware
        ? ((environment & 7) === 7 && (process & 3) === 3)
        : (environment === 15 && process === 15));

    return {
      kind: 'snapshot',
      values: values,
      timestamps: timestamps,
      at: latestAt,
      oldestAt: presentFields > 0 ? oldestAt : 0,
      freshFields: freshFields,
      presentFields: presentFields,
      samplingValid: valid,
      fresh: freshFields === DATA_CONTRACT.length && valid
    };
  }

  private static timestamp(text: string): number {
    if (text.length === 0) {
      return 0;
    }
    let parsed: number = Date.parse(text);
    if (!Number.isNaN(parsed)) {
      return parsed;
    }
    parsed = Date.parse(text.replace(' ', 'T') + '+08:00');
    return Number.isNaN(parsed) ? 0 : parsed;
  }

  private static toNumber(value: Object | null | undefined, fallback: number): number {
    if (typeof value === 'number') {
      return value;
    }
    if (typeof value === 'string') {
      const numeric: number = Number(value);
      return Number.isFinite(numeric) ? numeric : fallback;
    }
    if (typeof value === 'boolean') {
      return value ? 1 : 0;
    }
    return fallback;
  }

  private static toStringValue(value: Object | null | undefined): string {
    if (value === null || value === undefined) {
      return '';
    }
    return String(value);
  }
}
