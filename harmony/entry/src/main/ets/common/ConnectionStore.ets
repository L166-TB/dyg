import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';

const PREF_NAME: string = 'caneflow_connection';

export class ConnectionStore {
  private context: common.UIAbilityContext;
  private pref: preferences.Preferences | null = null;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
  }

  async init(): Promise<void> {
    this.pref = await preferences.getPreferences(this.context, PREF_NAME);
  }

  private async getPref(): Promise<preferences.Preferences> {
    if (this.pref === null) {
      this.pref = await preferences.getPreferences(this.context, PREF_NAME);
    }
    return this.pref;
  }

  async getString(key: string, defaultValue: string): Promise<string> {
    const pref: preferences.Preferences = await this.getPref();
    return (await pref.get(key, defaultValue)) as string;
  }

  async getBoolean(key: string, defaultValue: boolean): Promise<boolean> {
    const pref: preferences.Preferences = await this.getPref();
    return (await pref.get(key, defaultValue)) as boolean;
  }

  async putString(key: string, value: string): Promise<void> {
    const pref: preferences.Preferences = await this.getPref();
    await pref.put(key, value);
    await pref.flush();
  }

  async putBoolean(key: string, value: boolean): Promise<void> {
    const pref: preferences.Preferences = await this.getPref();
    await pref.put(key, value);
    await pref.flush();
  }
}
