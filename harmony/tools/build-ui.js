'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const sourceDir = path.join(root, 'ui-src');
const outputDir = path.join(root, 'entry', 'src', 'main', 'resources', 'rawfile', 'ui');
const outputFile = path.join(outputDir, 'index.html');

function read(name) {
  return fs.readFileSync(path.join(sourceDir, name), 'utf8');
}

const htmlSource = read('index.html');
const css = read('styles.css');
const dataContract = read('data-contract.js');
const deviceProfile = read('device-profile.js');
const app = read('app.js');
const markSvg = read('mark.svg');

for (const [name, content] of [['styles.css', css], ['app.js', app], ['data-contract.js', dataContract], ['device-profile.js', deviceProfile]]) {
  if (content.includes('</script') && name !== 'styles.css') {
    throw new Error(name + ' 含有 </script，不能安全内联');
  }
  if (content.includes('</style')) {
    throw new Error(name + ' 含有 </style，不能安全内联');
  }
}

const markDataUri = 'data:image/svg+xml,' + encodeURIComponent(markSvg);

let html = htmlSource;
html = html.replace('<link rel="stylesheet" href="./styles.css">', () => `<style>\n${css}\n</style>`);
html = html.replace('<link rel="icon" href="./mark.svg" type="image/svg+xml">', () => `<link rel="icon" href="${markDataUri}" type="image/svg+xml">`);
html = html.split('<img src="./mark.svg" alt="">').join(`<img src="${markDataUri}" alt="">`);
html = html.replace('<script src="./data-contract.js"></script>', () => `<script>\n${dataContract}\n</script>`);
html = html.replace('<script src="./device-profile.js"></script>', () => `<script>\n${deviceProfile}\n</script>`);
html = html.replace('<script src="./app.js"></script>', () => `<script>\n${app}\n</script>`);

if (html.includes('src="./') || html.includes('href="./styles.css')) {
  throw new Error('仍有未内联的本地资源');
}

fs.mkdirSync(outputDir, { recursive: true });
fs.writeFileSync(outputFile, html, 'utf8');
console.log('单文件网页已生成: ' + outputFile + ' (' + html.length + ' bytes)');
