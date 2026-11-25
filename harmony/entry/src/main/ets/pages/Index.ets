import { common } from '@kit.AbilityKit';
import { window, promptAction, router } from '@kit.ArkUI';
import { webview } from '@kit.ArkWeb';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { CloudClient } from '../common/CloudClient';
import { CommandRules } from '../common/CommandRules';
import { TokenFactory } from '../common/TokenFactory';
import { TokenStore } from '../common/TokenStore';
import { ConnectionStore } from '../common/ConnectionStore';
import { HostBridge } from '../common/HostBridge';
import { SpeechService } from '../common/SpeechService';
import { CommandRequest, HostActions, Snapshot } from '../common/Types';
import { ConfigPanel } from '../view/ConfigPanel';

const DOMAIN: number = 0x0001;
const TAG: string = 'Index';

class NoopActions implements HostActions {
  showConfig(): void {
  }

  refresh(): void {
  }

  command(request: CommandRequest): void {
  }

  speak(text: string): void {
  }

  theme(dark: boolean): void {
  }

  emit(event: Object): void {
  }
}

class PageActions implements HostActions {
  private showHandler: () => void;
  private refreshHandler: () => void;
  private commandHandler: (request: CommandRequest) => void;
  private speakHandler: (text: string) => void;
  private themeHandler: (dark: boolean) => void;
  private emitHandler: (event: Object) => void;

  constructor(
    showHandler: () => void,
    refreshHandler: () => void,
    commandHandler: (request: CommandRequest) => void,
    speakHandler: (text: string) => void,
    themeHandler: (dark: boolean) => void,
    emitHandler: (event: Object) => void
  ) {
    this.showHandler = showHandler;
    this.refreshHandler = refreshHandler;
    this.commandHandler = commandHandler;
    this.speakHandler = speakHandler;
    this.themeHandler = themeHandler;
    this.emitHandler = emitHandler;
  }

  showConfig(): void {
    this.showHandler();
  }

  refresh(): void {
    this.refreshHandler();
  }

  command(request: CommandRequest): void {
    this.commandHandler(request);
  }

  speak(text: string): void {
    this.speakHandler(text);
  }

  theme(dark: boolean): void {
    this.themeHandler(dark);
  }

  emit(event: Object): void {
    this.emitHandler(event);
  }
}

@Entry
@Component
struct Index {
  private controller: webview.WebviewController = new webview.WebviewController();
  private bridge: HostBridge = new HostBridge(new NoopActions());
  private tokenStore: TokenStore | null = null;
  private connectionStore: ConnectionStore | null = null;
  private speech: SpeechService = new SpeechService();
  private snapshot: Snapshot | null = null;
  private pollTimer: number = -1;
  private polling: boolean = false;
  private commanding: boolean = false;
  private resumed: boolean = true;
  private destroyed: boolean = false;
  private pageReady: boolean = false;
  private epoch: number = 0;

  @State live: boolean = false;
  @State product: string = 'F0v8J7x9tw';
  @State device: string = 'dev1';
  @State configVisible: boolean = false;
  @State loading: boolean = true;

  aboutToAppear(): void {
    this.bridge.actions = new PageActions(
      (): void => {
        this.openConfig();
      },
      (): void => {
        this.schedule(0);
      },
      (request: CommandRequest): void => {
        this.execute(request);
      },
      (text: string): void => {
        this.speech.speak(text);
      },
      (dark: boolean): void => {
        this.applyTheme(dark);
      },
      (event: Object): void => {
        this.emit(event);
      }
    );
    this.initStores();
  }

  aboutToDisappear(): void {
    this.destroyed = true;
    this.epoch += 1;
    if (this.pollTimer >= 0) {
      clearTimeout(this.pollTimer);
      this.pollTimer = -1;
    }
    this.speech.shutdown();
  }

  onPageShow(): void {
    this.resumed = true;
    this.schedule(0);
  }

  onPageHide(): void {
    this.resumed = false;
    if (this.pollTimer >= 0) {
      clearTimeout(this.pollTimer);
      this.pollTimer = -1;
    }
  }

  onBackPress(): boolean {
    if (this.configVisible) {
      this.configVisible = false;
      return true;
    }
    this.controller.runJavaScript('window.CaneBack ? window.CaneBack() : false', (error, result) => {
      if (error) {
        this.confirmExit();
        return;
      }
      if (result !== 'true') {
        this.confirmExit();
      }
    });
    return true;
  }

  private async initStores(): Promise<void> {
    try {
      const hostContext = this.getUIContext().getHostContext();
      if (hostContext === undefined) {
        throw new Error('无法获取应用上下文');
      }
      const context: common.UIAbilityContext = hostContext as common.UIAbilityContext;
      this.tokenStore = new TokenStore(context);
      this.connectionStore = new ConnectionStore(context);
      await this.connectionStore.init();
      this.product = await this.connectionStore.getString('product', this.product);
      this.device = await this.connectionStore.getString('deviceName', this.device);
      this.live = await this.connectionStore.getBoolean('live', false);
      if (this.pageReady) {
        this.sendMode();
        this.schedule(0);
      }
    } catch (error) {
      hilog.error(DOMAIN, TAG, 'init stores failed: ' + (error as Error).message);
      this.live = false;
    }
  }

  private openConfig(): void {
    if (this.commanding) {
      promptAction.showToast({ message: '请等待当前命令结果' });
      return;
    }
    this.configVisible = true;
  }

  private async connect(product: string, device: string, tokenInput: string, productKey: string): Promise<void> {
    const tokenStore: TokenStore | null = this.tokenStore;
    const connectionStore: ConnectionStore | null = this.connectionStore;
    if (tokenStore === null || connectionStore === null) {
      throw new Error('应用仍在初始化，请稍后再试');
    }
    if (!/^[A-Za-z0-9_-]{1,64}$/.test(product)) {
      throw new Error('产品 ID 只能包含字母、数字、下划线和短横线');
    }
    if (!/^[A-Za-z0-9_.:-]{1,64}$/.test(device)) {
      throw new Error('设备名称格式错误');
    }

    let token: string = tokenInput.trim();
    if (token.length === 0 && productKey.trim().length > 0) {
      token = await TokenFactory.makeToken(product, productKey.trim());
    }
    if (token.length === 0) {
      token = await tokenStore.read();
    }
    this.validateToken(token, product);
    if (tokenInput.trim().length > 0 || productKey.trim().length > 0) {
      await tokenStore.save(token);
    }

    await connectionStore.putString('product', product);
    await connectionStore.putString('deviceName', device);
    await connectionStore.putBoolean('live', true);

    this.product = product;
    this.device = device;
    this.live = true;
    this.snapshot = null;
    this.epoch += 1;
    this.configVisible = false;
    this.reloadWeb();
  }

  private async useDemo(): Promise<void> {
    if (this.commanding) {
      promptAction.showToast({ message: '请等待当前命令结果' });
      return;
    }
    const connectionStore: ConnectionStore | null = this.connectionStore;
    if (connectionStore !== null) {
      await connectionStore.putBoolean('live', false);
    }
    this.live = false;
    this.snapshot = null;
    this.epoch += 1;
    this.configVisible = false;
    this.reloadWeb();
  }

  private reloadWeb(): void {
    this.pageReady = false;
    this.loading = true;
    try {
      this.controller.refresh();
    } catch (error) {
      hilog.error(DOMAIN, TAG, 'refresh web failed');
    }
  }

  private validateToken(token: string, product: string): void {
    if (token.length === 0 || token.length > 1800 || token.includes('\n') || token.includes('\r')) {
      throw new Error('请填写完整的产品级 API token');
    }
    const parts: Map<string, string> = new Map();
    const pieces: string[] = token.split('&');
    for (const piece of pieces) {
      const index: number = piece.indexOf('=');
      if (index > 0) {
        try {
          parts.set(piece.substring(0, index), decodeURIComponent(piece.substring(index + 1)));
        } catch (error) {
          throw new Error('token 编码格式错误');
        }
      }
    }

    if (parts.get('version') === '2018-10-31') {
      throw new Error('旧版2018 token不适用：请用原产品 AccessKey重新生成2022版 token');
    }
    if (parts.get('res') !== 'products/' + product ||
      parts.get('version') !== '2022-05-01' ||
      parts.get('sign') === undefined ||
      parts.get('method') === undefined) {
      throw new Error('需要该产品的2022版 API token，不能使用设备级 token、用户级 AccessKey或设备密钥');
    }
    const etValue: string | undefined = parts.get('et');
    const expires: number = Number(etValue === undefined ? '0' : etValue);
    if (!Number.isFinite(expires) || expires <= Math.floor(Date.now() / 1000) + 60) {
      throw new Error('token 已过期或即将过期，请重新生成');
    }
  }

  private schedule(delay: number): void {
    if (this.pollTimer >= 0) {
      clearTimeout(this.pollTimer);
      this.pollTimer = -1;
    }
    if (!this.resumed || !this.pageReady || !this.live || this.destroyed) {
      return;
    }
    this.pollTimer = setTimeout(() => {
      this.pollNow();
    }, delay);
  }

  private async pollNow(): Promise<void> {
    if (!this.resumed || !this.pageReady || !this.live || this.polling || this.commanding || this.destroyed) {
      this.schedule(2000);
      return;
    }

    this.polling = true;
    const session: number = this.epoch;
    try {
      const tokenStore: TokenStore | null = this.tokenStore;
      if (tokenStore === null) {
        throw new Error('应用仍在初始化');
      }
      const token: string = await tokenStore.read();
      this.validateToken(token, this.product);
      const client: CloudClient = new CloudClient(token, this.product, this.device);
      const snapshot: Snapshot = await client.read();
      if (session !== this.epoch || this.destroyed || !this.resumed) {
        return;
      }
      this.snapshot = snapshot;
      this.emit(snapshot as Object);
      this.schedule(2000);
    } catch (error) {
      if (session === this.epoch && !this.destroyed) {
        this.snapshot = null;
        this.emit({ kind: 'connection-error', message: this.safeError(error) });
        this.schedule(10000);
      }
    } finally {
      this.polling = false;
    }
  }

  private async execute(request: CommandRequest): Promise<void> {
    if (!this.live) {
      this.emit({ kind: 'command-error', message: '当前是演示模式，未连接真实设备' });
      return;
    }
    if (this.commanding) {
      this.emit({ kind: 'command-error', message: '上一条命令仍在等待设备回执' });
      return;
    }
    try {
      CommandRules.validate(request, this.snapshot);
    } catch (error) {
      this.emit({ kind: 'command-error', message: this.safeError(error) });
      return;
    }

    this.commanding = true;
    this.snapshot = null;
    const session: number = this.epoch;
    if (this.pollTimer >= 0) {
      clearTimeout(this.pollTimer);
      this.pollTimer = -1;
    }

    try {
      const tokenStore: TokenStore | null = this.tokenStore;
      if (tokenStore === null) {
        throw new Error('应用仍在初始化');
      }
      const token: string = await tokenStore.read();
      this.validateToken(token, this.product);
      const client: CloudClient = new CloudClient(token, this.product, this.device);
      const result = await client.command(request);
      if (session !== this.epoch || this.destroyed) {
        return;
      }
      this.emit(result as Object);
      this.schedule(0);
    } catch (error) {
      if (session === this.epoch && !this.destroyed) {
        this.emit({
          kind: 'command-error',
          message: this.safeError(error) + '；未确认执行结果，请核对设备状态'
        });
        this.schedule(0);
      }
    } finally {
      this.commanding = false;
    }
  }

  private emit(event: Object): void {
    if (!this.pageReady || this.destroyed) {
      return;
    }
    try {
      const json: string = JSON.stringify(event);
      this.controller.runJavaScript(`window.CaneNativeEvent && window.CaneNativeEvent(${json});`, () => {});
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'emit failed');
    }
  }

  private sendMode(): void {
    this.emit({ kind: 'mode', live: this.live, product: this.product, device: this.device });
  }

  private safeError(error: Object): string {
    let message: string = '';
    if (error instanceof Error) {
      message = error.message;
    } else {
      message = String(error);
    }
    const lower: string = message.toLowerCase();
    if (lower.includes('timeout') || lower.includes('超时')) {
      return '网络或命令等待超时，请核对设备状态';
    }
    if (lower.includes('resolve') || lower.includes('host') || lower.includes('network')) {
      return '网络不可用，无法连接 OneNET';
    }
    if (lower.includes('401') || lower.includes('403')) {
      return '云授权无效或已过期，请更新产品级 API token';
    }
    return message.length > 0 ? message : '连接或响应异常，请检查网络和授权';
  }

  private async applyTheme(dark: boolean): Promise<void> {
    try {
      const hostContext = this.getUIContext().getHostContext();
      if (hostContext === undefined) {
        return;
      }
      const win: window.Window = await window.getLastWindow(hostContext as common.UIAbilityContext);
      await win.setWindowSystemBarProperties({
        statusBarColor: dark ? '#19251E' : '#FFFFFF',
        navigationBarColor: dark ? '#19251E' : '#FFFFFF',
        statusBarContentColor: dark ? '#FFFFFF' : '#1F2937',
        navigationBarContentColor: dark ? '#FFFFFF' : '#1F2937'
      });
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'apply theme failed');
    }
  }

  private confirmExit(): void {
    AlertDialog.show({
      title: '退出蔗序？',
      message: '退出应用不会停止设备产线。需要停机时请先暂停流程并核对设备回报。',
      primaryButton: {
        value: '取消',
        action: () => {
        }
      },
      secondaryButton: {
        value: '退出',
        action: () => {
          router.back();
        }
      }
    });
  }

  build() {
    Stack({ alignContent: Alignment.Center }) {
      Web({ src: $rawfile('ui/index.html'), controller: this.controller })
        .javaScriptAccess(true)
        .domStorageAccess(true)
        .imageAccess(true)
        .onlineImageAccess(false)
        .zoomAccess(false)
        .textZoomRatio(100)
        .javaScriptProxy({
          object: this.bridge,
          name: 'HarmonyHost',
          methodList: ['post'],
          controller: this.controller
        })
        .onPageEnd(() => {
          this.pageReady = true;
          this.loading = false;
          this.sendMode();
          this.schedule(0);
        })
        .width('100%')
        .height('100%')

      if (this.loading) {
        Column({ space: 12 }) {
          LoadingProgress()
            .width(34)
            .height(34)
            .color('#1F6F4A')
          Text('正在加载蔗序…')
            .fontSize(13)
            .fontColor('#4B5563')
        }
        .width('100%')
        .height('100%')
        .backgroundColor('#F5F7F6')
        .justifyContent(FlexAlign.Center)
      }

      if (this.configVisible) {
        ConfigPanel({
          initialProduct: this.product,
          initialDevice: this.device,
          onClose: (): void => {
            this.configVisible = false;
          },
          onConnect: (product: string, device: string, token: string, productKey: string): Promise<void> =>
            this.connect(product, device, token, productKey),
          onDemo: (): void => {
            this.useDemo();
          }
        })
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor('#F5F7F6')
  }
}
