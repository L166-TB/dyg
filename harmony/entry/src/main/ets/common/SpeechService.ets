import { textToSpeech } from '@kit.CoreSpeechKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN: number = 0x0001;
const TAG: string = 'SpeechService';

export class SpeechService {
  private engine: textToSpeech.TextToSpeechEngine | undefined = undefined;
  private creating: boolean = false;

  private async ensureEngine(): Promise<void> {
    if (this.engine !== undefined || this.creating) {
      return;
    }
    this.creating = true;
    try {
      const params: textToSpeech.CreateEngineParams = {
        language: 'zh-CN',
        person: 0,
        online: 1,
        extraParams: {}
      };
      this.engine = await textToSpeech.createEngine(params);
    } catch (error) {
      const businessError = error as BusinessError;
      hilog.warn(DOMAIN, TAG, 'createEngine failed: ' + businessError.code);
    } finally {
      this.creating = false;
    }
  }

  async speak(text: string): Promise<void> {
    const content: string = text.trim();
    if (content.length === 0) {
      return;
    }
    await this.ensureEngine();
    if (this.engine === undefined) {
      return;
    }
    const params: textToSpeech.SpeakParams = {
      requestId: 'cane_' + Date.now(),
      extraParams: {
        'queueMode': 0,
        'speed': 1.0,
        'volume': 1.0,
        'pitch': 1.0,
        'playType': 1
      }
    };
    try {
      await this.engine.speak(content.substring(0, 180), params);
    } catch (error) {
      const businessError = error as BusinessError;
      hilog.warn(DOMAIN, TAG, 'speak failed: ' + businessError.code);
    }
  }

  async shutdown(): Promise<void> {
    if (this.engine === undefined) {
      return;
    }
    try {
      await this.engine.shutdown();
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'shutdown failed');
    }
    this.engine = undefined;
  }
}
