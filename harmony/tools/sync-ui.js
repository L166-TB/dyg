'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const sourceDir = path.resolve(root, '..', '..', '_zhexu_src', 'ui');
const targetDir = path.join(root, 'ui-src');

fs.mkdirSync(targetDir, { recursive: true });

const files = ['index.html', 'styles.css', 'app.js', 'data-contract.js', 'device-profile.js', 'mark.svg'];
for (const file of files) {
  const from = path.join(sourceDir, file);
  const to = path.join(targetDir, file);
  if (!fs.existsSync(from)) {
    throw new Error('缺少网页源文件: ' + from);
  }
  fs.copyFileSync(from, to);
}

function replaceFile(file, pairs) {
  const full = path.join(targetDir, file);
  let text = fs.readFileSync(full, 'utf8');
  for (const [from, to] of pairs) {
    if (!text.includes(from)) {
      console.warn('[skip] ' + file + ' 未找到: ' + from.slice(0, 50));
      continue;
    }
    text = text.split(from).join(to);
  }
  fs.writeFileSync(full, text, 'utf8');
}

replaceFile('index.html', [
  ['<title>蔗序 · 安卓上位机</title>', '<title>蔗序 · 鸿蒙上位机</title>'],
  ['Java 安卓 v1.2.0 · 示例模式', 'HarmonyOS ArkTS v1.0.0 · 示例模式'],
  ['由安卓 Keystore 加密保存；设备密钥不写入网页或 APK。', '由鸿蒙 HUKS 加密保存；设备密钥不写入网页或 HAP。'],
  ['安卓原生中文语音，开启后播报页面与操作', '鸿蒙原生中文语音，开启后播报页面与操作'],
  [
    `<meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'none'; object-src 'none'; frame-src 'none'; base-uri 'none'; form-action 'none'">`,
    `<meta http-equiv="Content-Security-Policy" content="default-src 'self' data:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'none'; object-src 'none'; frame-src 'none'; base-uri 'none'; form-action 'none'">`
  ]
]);

replaceFile('app.js', [
  ['/* 安卓手机界面：演示数据与Java真实云采样分开，控制经原生白名单发送。 */', '/* 鸿蒙手机界面：演示数据与原生真实云采样分开，控制经原生白名单发送。 */'],
  ['const host = window.AndroidHost;', 'const host = window.HarmonyHost || window.AndroidHost;'],
  ["'Java 安卓 v1.2.0 · '", "'HarmonyOS ArkTS v1.0.0 · '"],
  ["'Java 原生 HTTPS 通信，不使用设备 MQTT 身份。'", "'鸿蒙原生 HTTPS 通信，不使用设备 MQTT 身份。'"]
]);

replaceFile('styles.css', [
  ['/* 安卓手机优先：桌面预览保持同一手机布局，无桌面侧栏。 */', '/* 鸿蒙手机优先：桌面预览保持同一手机布局，无桌面侧栏。 */'],
  ['/* 安卓全屏容器 */', '/* 鸿蒙全屏容器 */']
]);

console.log('网页源已同步到: ' + targetDir);
