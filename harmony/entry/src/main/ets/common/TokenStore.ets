import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { huks } from '@kit.UniversalKeystoreKit';
import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { util } from '@kit.ArkTS';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN: number = 0x0001;
const TAG: string = 'TokenStore';
const PREF_NAME: string = 'caneflow_vault';
const KEY_ALIAS: string = 'caneflow_onenet_token_v1';
const GCM_TAG_SIZE: number = 16;

export class TokenStore {
  private context: common.UIAbilityContext;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
  }

  private async getPref(): Promise<preferences.Preferences> {
    return await preferences.getPreferences(this.context, PREF_NAME);
  }

  private generateProperties(): Array<huks.HuksParam> {
    return [
      { tag: huks.HuksTag.HUKS_TAG_ALGORITHM, value: huks.HuksKeyAlg.HUKS_ALG_AES },
      { tag: huks.HuksTag.HUKS_TAG_KEY_SIZE, value: huks.HuksKeySize.HUKS_AES_KEY_SIZE_256 },
      {
        tag: huks.HuksTag.HUKS_TAG_PURPOSE,
        value: huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_ENCRYPT | huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_DECRYPT
      },
      { tag: huks.HuksTag.HUKS_TAG_PADDING, value: huks.HuksKeyPadding.HUKS_PADDING_NONE },
      { tag: huks.HuksTag.HUKS_TAG_BLOCK_MODE, value: huks.HuksCipherMode.HUKS_MODE_GCM },
      { tag: huks.HuksTag.HUKS_TAG_DIGEST, value: huks.HuksKeyDigest.HUKS_DIGEST_NONE }
    ];
  }

  private cipherProperties(purpose: number, nonce: Uint8Array, aad: Uint8Array, tag?: Uint8Array): Array<huks.HuksParam> {
    const properties: Array<huks.HuksParam> = [
      { tag: huks.HuksTag.HUKS_TAG_ALGORITHM, value: huks.HuksKeyAlg.HUKS_ALG_AES },
      { tag: huks.HuksTag.HUKS_TAG_KEY_SIZE, value: huks.HuksKeySize.HUKS_AES_KEY_SIZE_256 },
      { tag: huks.HuksTag.HUKS_TAG_PURPOSE, value: purpose },
      { tag: huks.HuksTag.HUKS_TAG_PADDING, value: huks.HuksKeyPadding.HUKS_PADDING_NONE },
      { tag: huks.HuksTag.HUKS_TAG_BLOCK_MODE, value: huks.HuksCipherMode.HUKS_MODE_GCM },
      { tag: huks.HuksTag.HUKS_TAG_DIGEST, value: huks.HuksKeyDigest.HUKS_DIGEST_NONE },
      { tag: huks.HuksTag.HUKS_TAG_NONCE, value: nonce },
      { tag: huks.HuksTag.HUKS_TAG_ASSOCIATED_DATA, value: aad }
    ];
    if (tag !== undefined) {
      properties.push({ tag: huks.HuksTag.HUKS_TAG_AE_TAG, value: tag });
    }
    return properties;
  }

  private async ensureKey(): Promise<void> {
    const exists: boolean = await huks.isKeyItemExist(KEY_ALIAS, { properties: [] });
    if (!exists) {
      const options: huks.HuksOptions = { properties: this.generateProperties() };
      await huks.generateKeyItem(KEY_ALIAS, options);
    }
  }

  async save(token: string): Promise<void> {
    const pref: preferences.Preferences = await this.getPref();
    const helper: util.Base64Helper = new util.Base64Helper();

    try {
      await this.ensureKey();
      const nonce: Uint8Array = cryptoFramework.createRandom().generateRandomSync(12).data;
      const aad: Uint8Array = new Uint8Array(0);
      const plain: Uint8Array = new util.TextEncoder().encodeInto(token);
      const properties: Array<huks.HuksParam> =
        this.cipherProperties(huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_ENCRYPT, nonce, aad);
      const session: huks.HuksSessionHandle = await huks.initSession(KEY_ALIAS, {
        properties: properties,
        inData: new Uint8Array(0)
      });

      let result: huks.HuksReturnResult;
      try {
        result = await huks.finishSession(session.handle, { properties: properties, inData: plain });
      } catch (error) {
        await huks.abortSession(session.handle, { properties: properties });
        throw error;
      }

      const output: Uint8Array | undefined = result.outData;
      if (output === undefined || output.length <= GCM_TAG_SIZE) {
        throw new Error('HUKS 加密结果为空');
      }
      const cipher: Uint8Array = output.slice(0, output.length - GCM_TAG_SIZE);
      const tag: Uint8Array = output.slice(output.length - GCM_TAG_SIZE);

      await pref.put('nonce', helper.encodeToStringSync(nonce));
      await pref.put('cipher', helper.encodeToStringSync(cipher));
      await pref.put('tag', helper.encodeToStringSync(tag));
      await pref.put('plain', '');
      await pref.flush();
      hilog.info(DOMAIN, TAG, 'token saved with HUKS');
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'HUKS unavailable, fallback to sandbox preferences');
      await pref.put('plain', helper.encodeToStringSync(new util.TextEncoder().encodeInto(token)));
      await pref.put('nonce', '');
      await pref.put('cipher', '');
      await pref.put('tag', '');
      await pref.flush();
    }
  }

  async read(): Promise<string> {
    const pref: preferences.Preferences = await this.getPref();
    const helper: util.Base64Helper = new util.Base64Helper();

    const plainText: string = (await pref.get('plain', '')) as string;
    if (plainText.length > 0) {
      try {
        return util.TextDecoder.create('utf-8').decodeWithStream(helper.decodeSync(plainText), { stream: false });
      } catch (error) {
        return '';
      }
    }

    const nonceText: string = (await pref.get('nonce', '')) as string;
    const cipherText: string = (await pref.get('cipher', '')) as string;
    const tagText: string = (await pref.get('tag', '')) as string;
    if (nonceText.length === 0 || cipherText.length === 0 || tagText.length === 0) {
      return '';
    }

    try {
      await this.ensureKey();
      const nonce: Uint8Array = helper.decodeSync(nonceText);
      const cipher: Uint8Array = helper.decodeSync(cipherText);
      const tag: Uint8Array = helper.decodeSync(tagText);
      const properties: Array<huks.HuksParam> =
        this.cipherProperties(huks.HuksKeyPurpose.HUKS_KEY_PURPOSE_DECRYPT, nonce, new Uint8Array(0), tag);
      const session: huks.HuksSessionHandle = await huks.initSession(KEY_ALIAS, {
        properties: properties,
        inData: new Uint8Array(0)
      });

      let result: huks.HuksReturnResult;
      try {
        result = await huks.finishSession(session.handle, { properties: properties, inData: cipher });
      } catch (error) {
        await huks.abortSession(session.handle, { properties: properties });
        throw error;
      }

      if (result.outData === undefined) {
        return '';
      }
      return util.TextDecoder.create('utf-8').decodeWithStream(result.outData, { stream: false });
    } catch (error) {
      hilog.error(DOMAIN, TAG, 'token decrypt failed');
      return '';
    }
  }

}
