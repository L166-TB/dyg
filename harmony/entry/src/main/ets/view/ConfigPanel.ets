import { TokenFactory } from '../common/TokenFactory';

@Component
export struct ConfigPanel {
  @Prop initialProduct: string = '';
  @Prop initialDevice: string = '';
  @State product: string = '';
  @State device: string = '';
  @State token: string = '';
  @State productKey: string = '';
  @State errorText: string = '';
  @State busy: boolean = false;

  onClose: () => void = () => {};
  onConnect: (product: string, device: string, token: string, productKey: string) => Promise<void> =
    async () => {};
  onDemo: () => void = () => {};

  aboutToAppear(): void {
    this.product = this.initialProduct;
    this.device = this.initialDevice;
  }

  private async generateToken(): Promise<void> {
    this.busy = true;
    this.errorText = '';
    try {
      if (this.product.trim().length === 0) {
        throw new Error('请填写产品 ID');
      }
      if (this.productKey.trim().length === 0) {
        throw new Error('请填写产品 AccessKey');
      }
      this.token = await TokenFactory.makeToken(this.product.trim(), this.productKey.trim());
    } catch (error) {
      this.errorText = (error as Error).message;
    } finally {
      this.busy = false;
    }
  }

  private async connect(): Promise<void> {
    this.busy = true;
    this.errorText = '';
    try {
      await this.onConnect(this.product.trim(), this.device.trim(), this.token.trim(), this.productKey.trim());
    } catch (error) {
      this.errorText = (error as Error).message;
    } finally {
      this.busy = false;
    }
  }

  build() {
    Stack({ alignContent: Alignment.Center }) {
      Column()
        .width('100%')
        .height('100%')
        .backgroundColor('#99000000')
        .onClick(() => this.onClose())

      Column({ space: 10 }) {
        Row() {
          Column({ space: 2 }) {
            Text('连接 OneNET')
              .fontSize(18)
              .fontWeight(FontWeight.Bold)
              .fontColor('#111827')
            Text('产品级 2022 版 API token')
              .fontSize(11)
              .fontColor('#6B7280')
          }
          .alignItems(HorizontalAlign.Start)
          .layoutWeight(1)

          Button('关闭')
            .fontSize(12)
            .height(30)
            .backgroundColor('#F3F4F6')
            .fontColor('#374151')
            .onClick(() => this.onClose())
        }
        .width('100%')

        TextInput({ text: this.product, placeholder: '产品 ID，例如 F0v8J7x9tw' })
          .height(44)
          .fontSize(13)
          .maxLength(64)
          .onChange((value: string) => {
            this.product = value;
          })

        TextInput({ text: this.device, placeholder: '设备名称，例如 dev1' })
          .height(44)
          .fontSize(13)
          .maxLength(64)
          .onChange((value: string) => {
            this.device = value;
          })

        TextInput({ text: this.token, placeholder: '2022 版产品 API token（可留空使用已保存 token）' })
          .height(44)
          .fontSize(13)
          .maxLength(1800)
          .type(InputType.Password)
          .onChange((value: string) => {
            this.token = value;
          })

        TextInput({ text: this.productKey, placeholder: '产品 AccessKey（仅用于本机生成 token）' })
          .height(44)
          .fontSize(13)
          .maxLength(256)
          .type(InputType.Password)
          .onChange((value: string) => {
            this.productKey = value;
          })

        Row({ space: 8 }) {
          Button(this.busy ? '处理中…' : '生成 24 小时 token')
            .fontSize(12)
            .height(36)
            .layoutWeight(1)
            .backgroundColor('#E8F3EC')
            .fontColor('#1F6F4A')
            .enabled(!this.busy)
            .onClick(() => {
              this.generateToken();
            })

          Button('使用演示数据')
            .fontSize(12)
            .height(36)
            .layoutWeight(1)
            .backgroundColor('#F3F4F6')
            .fontColor('#374151')
            .enabled(!this.busy)
            .onClick(() => this.onDemo())
        }
        .width('100%')

        if (this.errorText.length > 0) {
          Text(this.errorText)
            .fontSize(11)
            .fontColor('#B42318')
            .width('100%')
        }

        Button(this.busy ? '连接中…' : '连接真实设备')
          .width('100%')
          .height(44)
          .fontSize(14)
          .fontWeight(FontWeight.Medium)
          .backgroundColor('#1F6F4A')
          .fontColor('#FFFFFF')
          .enabled(!this.busy)
          .onClick(() => {
            this.connect();
          })

        Text('token 由鸿蒙 HUKS 加密保存；设备密钥不写入网页。产品 ID 与设备名称只保存在本机。')
          .fontSize(10)
          .fontColor('#6B7280')
          .lineHeight(15)
          .width('100%')
      }
      .width('92%')
      .padding(18)
      .backgroundColor('#FFFFFF')
      .borderRadius(18)
      .shadow({ radius: 24, color: '#33000000', offsetY: 8 })
    }
    .width('100%')
    .height('100%')
  }
}
