import { cryptoFramework } from '@kit.CryptoArchitectureKit';
import { util } from '@kit.ArkTS';

export class TokenFactory {
  static async makeToken(product: string, productKey: string): Promise<string> {
    const expiration: number = Math.floor(Date.now() / 1000) + 86400;
    return TokenFactory.makeTokenForExpiry(product, productKey, expiration);
  }

  static async makeTokenForExpiry(product: string, productKey: string, expiration: number): Promise<string> {
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(product)) {
      throw new Error('产品 ID 格式错误');
    }
    if (productKey.length === 0) {
      throw new Error('请填写产品 AccessKey');
    }

    let keyBytes: Uint8Array;
    try {
      keyBytes = new util.Base64Helper().decodeSync(productKey);
    } catch (error) {
      throw new Error('产品 AccessKey 不是有效的 Base64');
    }

    const generator: cryptoFramework.SymKeyGenerator = cryptoFramework.createSymKeyGenerator('HMAC');
    const keyBlob: cryptoFramework.DataBlob = { data: keyBytes };
    const symKey: cryptoFramework.SymKey = await generator.convertKey(keyBlob);

    const mac: cryptoFramework.Mac = cryptoFramework.createMac('SHA256');
    await mac.init(symKey);

    const payload: string = `${expiration}\nsha256\nproducts/${product}\n2022-05-01`;
    const payloadBytes: Uint8Array = new util.TextEncoder().encodeInto(payload);
    await mac.update({ data: payloadBytes });

    const sign: cryptoFramework.DataBlob = await mac.doFinal();
    const signBase64: string = new util.Base64Helper().encodeToStringSync(sign.data);
    return `version=2022-05-01&res=${encodeURIComponent('products/' + product)}&et=${expiration}&method=sha256&sign=${encodeURIComponent(signBase64)}`;
  }
}
