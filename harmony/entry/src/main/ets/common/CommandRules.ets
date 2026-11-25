import { CommandRequest, Snapshot } from './Types';
import { FRESH_MS } from './DataContract';

const MANUAL_PREFIX: string = 'cane-onenet-manual-';
const JUICE_PREFIX: string = 'cane-onenet-juice-';

export class CommandRules {
  static validate(request: CommandRequest | null | undefined, snapshot: Snapshot | null): void {
    if (request === null || request === undefined) {
      throw new Error('命令格式错误');
    }
    if (!request.command || !request.params) {
      throw new Error('命令格式错误');
    }
    if (snapshot === null || !snapshot.fresh || Date.now() - snapshot.oldestAt > FRESH_MS) {
      throw new Error('采样未就绪或数据过期，控制已锁定');
    }

    const command: string = request.command;
    const params: Record<string, Object> = request.params;
    const values: Record<string, Object | null> = snapshot.values;
    const keys: string[] = Object.keys(params);

    if (command === 'next_stage') {
      const expected: number = CommandRules.numberOf(params['expected_stage'], -1);
      if (keys.length !== 1 || !CommandRules.has(keys, 'expected_stage') ||
        expected !== Math.trunc(expected) || expected < 1 || expected > 6) {
        throw new Error('下一步需要当前工序编号1～6');
      }
      const firmware: string = CommandRules.stringValue(values['firmware_version']);
      if (!(firmware.startsWith(MANUAL_PREFIX) || firmware.startsWith(JUICE_PREFIX))) {
        throw new Error('请先更新设备为手动流程固件');
      }
      if (expected !== CommandRules.numberOf(values['stage'], -1)) {
        throw new Error('工序已变化，请按新采样重新操作');
      }
      if (CommandRules.numberOf(values['fire_detected'], 0) !== 0 ||
        CommandRules.numberOf(values['person_detected'], 0) !== 0 ||
        CommandRules.numberOf(values['environment_faults'], 0) !== 0 ||
        CommandRules.numberOf(values['process_faults'], 0) !== 0) {
        throw new Error('安全联锁或采样故障阻止下一步');
      }
      return;
    }

    if (command === 'set_run') {
      const running: number = CommandRules.numberOf(params['running'], -1);
      if (keys.length !== 1 || !CommandRules.has(keys, 'running') || (running !== 0 && running !== 1)) {
        throw new Error('启停参数错误');
      }
      if (running === 1 &&
        (CommandRules.numberOf(values['fire_detected'], 0) !== 0 ||
          CommandRules.numberOf(values['person_detected'], 0) !== 0 ||
          CommandRules.numberOf(values['environment_faults'], 0) !== 0 ||
          CommandRules.numberOf(values['process_faults'], 0) !== 0)) {
        throw new Error('安全联锁阻止启动');
      }
      return;
    }

    if (command === 'set_targets') {
      if (CommandRules.numberOf(values['running'], 0) !== 0 || keys.length !== 4) {
        throw new Error('请暂停流程后修改全部四项目标');
      }
      CommandRules.range(params, 'weigh_target_kg', 0.010, 3, 0.001);
      CommandRules.range(params, 'fill_target_kg', 0.005, 1, 0.001);
      CommandRules.range(params, 'boil_target_c', 20, 200, 1);
      CommandRules.range(params, 'cool_target_c', 10, 90, 1);
      return;
    }

    if (command === 'tare') {
      if (keys.length !== 0 || CommandRules.numberOf(values['running'], 0) !== 0) {
        throw new Error('请暂停并确认空载后去皮');
      }
      return;
    }

    if (command === 'ack_alarm') {
      if (keys.length !== 0) {
        throw new Error('确认报警不接受参数');
      }
      return;
    }

    throw new Error('不支持的命令');
  }

  private static has(keys: string[], name: string): boolean {
    return keys.indexOf(name) >= 0;
  }

  private static range(params: Record<string, Object>, name: string, min: number, max: number, step: number): void {
    const value: number = CommandRules.numberOf(params[name], Number.NaN);
    const steps: number = (value - min) / step;
    if (!Number.isFinite(value) || value < min || value > max || Math.abs(steps - Math.round(steps)) > 0.000001) {
      throw new Error('参数范围或步长不符：' + name);
    }
  }

  private static numberOf(value: Object | null | undefined, fallback: number): number {
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

  private static stringValue(value: Object | null | undefined): string {
    if (value === null || value === undefined) {
      return '';
    }
    return String(value);
  }
}
