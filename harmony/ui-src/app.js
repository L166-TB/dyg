/* 鸿蒙手机界面：演示数据与原生真实云采样分开，控制经原生白名单发送。 */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const $$ = s => [...document.querySelectorAll(s)];
  const icons = {
    grid:'<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    layers:'<path d="m12 3 10 5-10 5L2 8 12 3Zm-9 10 9 5 9-5M3 18l9 5 9-5"/>',
    shield:'<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6l8-3Z"/>',
    'shield-check':'<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6l8-3Z"/><path d="m8 12 3 3 5-6"/>',
    sliders:'<path d="M4 4v16M12 4v16M20 4v16M1 9h6m2 7h6m2-9h6"/>',
    settings:'<path d="m9 3-.7 3-2 .9-2.7-.9-2 3.5L4 12l-.4 2.2-2 1.9 2 3.5 2.8-.9 2 .9.7 3h4l.7-3 2-.9 2.8.9 2-3.5-2-1.9L20 12l2.4-2.5-2-3.5-2.7.9-2-.9-.7-3H9Z" transform="translate(0 -1) scale(.96)"/><circle cx="12" cy="12" r="3"/>',
    moon:'<path d="M21 13a9 9 0 0 1-10-10 9 9 0 1 0 10 10Z"/>',
    sun:'<circle cx="12" cy="12" r="4"/><path d="M12 1v2m0 18v2M1 12h2m18 0h2M4 4l2 2m12 12 2 2M4 20l2-2M18 6l2-2"/>',
    info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.5"/>',
    'arrow-right':'<path d="M4 12h15m-6-6 6 6-6 6"/>',
    calendar:'<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M7 3v4m10-4v4M3 10h18"/>',
    package:'<path d="m12 2 9 5v10l-9 5-9-5V7l9-5Zm0 10 9-5M12 12 3 7m9 5v10M7.5 4.5l9 5"/>',
    radio:'<path d="M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14M8 8a6 6 0 0 0 0 8m8-8a6 6 0 0 1 0 8"/><circle cx="12" cy="12" r="1.5"/>',
    thermometer:'<path d="M9 14V5a3 3 0 0 1 6 0v9a5 5 0 1 1-6 0Z"/><path d="M12 8v9"/>',
    droplet:'<path d="M12 2s8 9 8 13a8 8 0 0 1-16 0c0-4 8-13 8-13Z"/>',
    scale:'<path d="M12 3v18M5 21h14M3 7h18M6 7l-4 8h8L6 7Zm12 0-4 8h8l-4-8Z"/>',
    pause:'<path d="M8 5v14M16 5v14" stroke-width="4"/>',
    play:'<path d="m8 4 13 8-13 8V4Z"/>',
    terminal:'<rect x="2" y="4" width="20" height="16" rx="2"/><path d="m6 9 3 3-3 3m6 0h5"/>',
    'chevron-down':'<path d="m6 9 6 6 6-6"/>',
    check:'<path d="m5 12 4 4L19 6"/>',
    snowflake:'<path d="M12 2v20M3.3 7l17.4 10M3.3 17 20.7 7M9 4l3 3 3-3m-6 16 3-3 3 3"/>',
    send:'<path d="m22 2-7 20-4-9L2 9l20-7ZM11 13 22 2"/>',
    x:'<path d="m6 6 12 12M6 18 18 6"/>',
    monitor:'<rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8m-4-4v4"/>',
    flame:'<path d="M13 2c2 7-4 7-2 12 3-1 4-3 4-5 7 6 4 13-3 13C1 22 2 12 7 8c-1 5 0 7 2 7-2-6 4-8 4-13Z"/>',
    'wifi-off':'<path d="m3 3 18 18M2 8a16 16 0 0 1 4-2m4-1a16 16 0 0 1 12 3M5 12a11 11 0 0 1 4-2m6 0 4 2m-11 4a6 6 0 0 1 8 0m-4 4h.01"/>',
    clock:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    refresh:'<path d="M20 7v5h-5M4 17v-5h5M6 6a8 8 0 0 1 14 6M4 12a8 8 0 0 0 14 6"/>',
    fan:'<circle cx="12" cy="12" r="2"/><path d="M10 10c-4-6 1-9 4-7 3 2 0 6-1 7m1 2c7-1 7 5 4 7-3 1-5-4-5-5m-2 0c-3 7-8 4-8 1 0-4 5-3 7-3"/>',
    user:'<circle cx="12" cy="7" r="4"/><path d="M4 21v-2a8 8 0 0 1 16 0v2"/>',
    light:'<path d="M9 18h6m-6 3h6M8 14a7 7 0 1 1 8 0l-1 2H9l-1-2Z"/>',
  };
  const icon = name => `<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icons[name] || icons.info}</svg>`;
  function hydrate(root = document) { root.querySelectorAll('[data-icon]').forEach(e => { e.innerHTML = icon(e.dataset.icon); }); }
  const escape = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const nav = [['overview','grid','总览','生产总览'],['production','layers','产线','产线控制'],['alarms','shield','安全','安全与报警'],['parameters','sliders','参数','工艺参数']];
  const profile = window.CaneDeviceProfile;
  const stages = [
    {name:'待机',description:'检查现场和有效采样后人工启动。',field:'weight_kg',target:'weigh_target_kg',label:'当前重量',unit:'kg',digits:1},
    {name:'称重进料',description:'称重达标停止进料；按下一步进入榨汁。称重页按新固件显示 kg。',field:'weight_kg',target:'weigh_target_kg',label:'当前净重',unit:'kg',digits:1},
    {name:'榨汁提纯 · 榨汁',description:'进入榨汁会自动暂停，请再点继续运行。液位达到180 cm后停止进料；按下一步进入提纯。',field:'hopper_level',target:null,label:'榨汁液位比例',unit:'%',digits:0},
    {name:'煮糖控温',description:'监测物料温度；超过105℃风扇高速散热。按下一步进入分装。',field:'object_temperature',target:'boil_target_c',label:'物料表面温度',unit:'℃',digits:1},
    {name:'糖浆分装',description:'进入时去皮一次。本次糖浆重量按g显示，读数稳定后判定一次；目标以上关阀，超过200g报警。不代表自动完成三份分装。',field:'weight_kg',target:'fill_target_kg',label:'本次糖浆重量',unit:'g',digits:1},
    {name:'冷却复核',description:'温度高于45℃时风扇高速，适宜后恢复通风档；按下一步进入糖块入库。',field:'object_temperature',target:null,label:'物料表面温度',unit:'℃',digits:1},
    {name:'榨汁提纯 · 提纯',description:'初始浑浊度45%。请在现场按PC13开启过滤阀，约12秒降至10%。手机无法远程开阀；完成后再按下一步。',field:'concentration_demo',target:null,label:'浑浊度演示值',unit:'%',digits:0},
    {name:'糖块入库',description:'监测入库环境温湿度。湿度≥70%提示防潮、≤68%解除。当前固件云接口不接受工序7，请在现场按下一批。',field:'air_humidity',target:null,label:'仓储环境湿度',unit:'%RH',digits:0},
    {name:'流程完成',description:'请在现场确认并开始下一批。',field:'air_humidity',target:null,label:'环境湿度',unit:'%RH',digits:0}
  ];
  const mainStages = [1,2,3,4,5,7];
  const host = window.HarmonyHost || window.AndroidHost;
  let live = Boolean(host), cloudLabel = '等待配置状态', commandReply = null, oldestAt = 0;
  const post = (type, extra={}) => host?.post(JSON.stringify({type,...extra}));
  const initial = () => ({air_temperature:25,air_humidity:60,weight_kg:.9,object_temperature:36.5,smoke_relative:0,light_relative:75,hopper_level:44,process_distance_mm:1000,concentration_demo:45,hall_adc:3102,stage:1,running:1,fill_count:0,fill_total:3,batch_count:2,tare_pending:0,fire_detected:0,person_detected:0,process_alarm:0,environment_faults:0,process_faults:0,environment_valid:15,process_valid:15,feeder_on:1,valve_open:0,stir_setting:0,process_fan_percent:60,warehouse_fan_percent:0,lamp_on:0,weigh_target_kg:.9,fill_target_kg:.2,boil_target_c:102,cool_target_c:45,concentration_limit:10,uptime_s:1122,sample_seq:1,firmware_version:'2026-10-30 代码适配示例'});
  let values = initial(), stale = false, nextTimeout = false, busy = false, page = 'overview', filter = 'all', period = 60, pendingAction = null, toastTimer, lastSample = Date.now(), generation = 0;
  let demoPurifyTarget = 10; // 仅示例模式使用；真实模式只显示设备上报。
  function enterDemoPurify() { demoPurifyTarget=10; values.concentration_demo=45; }
  let events = [], history = [{at:Date.now(),value:values.object_temperature}];
  if(live){ values=Object.fromEntries(window.CANE_DATA_CONTRACT.map(f=>[f.name,null])); stale=true; history=[]; lastSample=0; }
  let night = false, speech = false;
  try { night = localStorage.getItem('caneflow.theme') === 'dark'; speech = localStorage.getItem('caneflow.speech') === 'true'; } catch {}
  function say(text) {
    if (!speech) return;
    if(host){post('speak',{text});return;}
    if (!('speechSynthesis' in window)) return;
    const message = new SpeechSynthesisUtterance(text); message.lang = 'zh-CN'; message.rate = 1;
    window.speechSynthesis.cancel(); window.speechSynthesis.speak(message);
  }
  function toast(message) { clearTimeout(toastTimer); $('#toast').textContent = message; $('#toast').classList.add('show'); toastTimer = setTimeout(() => $('#toast').classList.remove('show'), 3400); }
  function addEvent(title, detail, type = 'command', severity = '') { events.unshift({title,detail,type,severity,at:Date.now()}); events = events.slice(0,100); renderEvents(); }
  function time(at) { return new Date(at).toLocaleTimeString('zh-CN',{hour12:false,timeZone:'Asia/Shanghai'}); }
  function renderEvents() {
    function row(e) { return `<div class="event-item"><span class="event-marker ${escape(e.severity)}">${icon(e.type==='alarm'?'shield':'check')}</span><div><h3>${escape(e.title)}</h3><p>${escape(e.detail)}</p></div><time class="event-time">${time(e.at)}</time></div>`; }
    $('#overview-events').innerHTML = events.slice(0,3).map(row).join('');
    const selected = events.filter(e => filter==='all' || e.type===filter);
    $('#all-events').innerHTML = selected.length ? selected.map(row).join('') : '<p class="empty-events">本次暂无此类事件</p>';
  }
  function danger() { return profile.interlocked(values); }
  function outputs() {
    if(live)return; // 真实模式只能显示设备上报，不推算执行器反馈。
    const run = values.running && !danger();
    values.process_fan_percent = run ? ((values.stage===5&&values.object_temperature>45)||(values.stage===3&&values.object_temperature>105)?100:60):0;
    values.warehouse_fan_percent = 0;
    values.feeder_on = Number(run && values.stage===1 && values.weight_kg < values.weigh_target_kg);
    values.valve_open = Number(run && values.stage===4 && values.weight_kg < values.fill_target_kg);
    values.stir_setting = run && (values.stage===2||values.stage===6) ? 70 : 0;
  }
  function drawNav() { $('.bottom-nav').innerHTML = nav.map(([id,i,label]) => `<button class="nav-item ${page===id?'active':''}" data-page="${id}" ${page===id?'aria-current="page"':''}>${icon(i)}<span>${label}</span>${id==='alarms'&&profile.hasAlarm(values)?'<span class="nav-count">1</span>':''}</button>`).join(''); }
  function setPage(id, announce = true) {
    if (!nav.some(n => n[0]===id)) id='overview';
    page=id; $$('.page').forEach(e => {e.hidden=e.id!==`page-${id}`;});
    $('#breadcrumb').textContent=nav.find(n=>n[0]===id)[3]; drawNav();
    if(location.hash!==`#${id}`) location.hash=id;
    window.scrollTo({top:0,behavior:'instant'});
    if(announce) say(nav.find(n=>n[0]===id)[3]);
  }
  function format(key,digits) { const n=profile.display(key,values[key]); return typeof n==='number' && Number.isFinite(n) ? n.toFixed(profile.digits(key,digits===undefined?0:Number(digits))) : String(n??'—'); }
  function setTheme() {
    document.documentElement.dataset.theme=night?'dark':'light';
    $('#dark-switch').setAttribute('aria-checked',String(night));
    $('#theme-toggle').innerHTML=icon(night?'sun':'moon');
    $('#theme-toggle').setAttribute('aria-label',night?'切换日间模式':'切换深色模式');
    $('meta[name="theme-color"]').content=night?'#19251e':'#ffffff';
    try {localStorage.setItem('caneflow.theme',night?'dark':'light');} catch {}
    post('theme',{dark:night});drawChart();
  }
  function render() {
    outputs();
    $$('[data-value]').forEach(e => {e.textContent=format(e.dataset.value,e.dataset.digits);});
    $$('[data-unit]').forEach(e => {e.textContent=profile.unit(e.dataset.unit,values.stage);});
    const stage=stages[values.stage]||stages[0], safe=!danger(), mainStage=profile.main(values.stage);
    const bindings={'stage-name':stage.name,'stage-description':stage.description,'stage-code':`0${mainStage} / 06`,'stage-number':`0${mainStage}`,'health':!safe?'安全联锁已触发':stale?'示例数据已过期':values.running?'产线运行正常':'流程已暂停','safety':!safe?'安全联锁触发':profile.hasAlarm(values)?'存在报警提示':'现场正常','run-state':values.running?'流程运行中':'流程已暂停'};
    $$('[data-bind]').forEach(e=>{e.textContent=bindings[e.dataset.bind]??'';});
    const percent=values.weigh_target_kg>0?Math.min(100,Math.max(0,values.weight_kg/values.weigh_target_kg*100)):0;
    $('#weight-progress').style.width=`${percent}%`; $('#weight-percent').textContent=`${percent.toFixed(1)}%`;
    const secs=values.uptime_s||0; $('#uptime').textContent=[Math.floor(secs/3600),Math.floor(secs/60)%60,secs%60].map(n=>String(n).padStart(2,'0')).join(':');
    $('#portion-dots').innerHTML=Array.from({length:values.fill_total},(_,i)=>`<span class="${i<values.fill_count?'done':''}"></span>`).join('');
    $('#pipeline-mini').innerHTML=mainStages.map((id,i)=>{const s=stages[id];return `${i?'<span class="connector"></span>':''}<span class="step ${mainStage===i+1?'active':''}">${s.name.slice(0,2)}</span>`;}).join('');
    $('#stage-track').dataset.stage=String(values.stage);
    $('#stage-track').innerHTML=mainStages.map((id,i)=>{const s=stages[id];return `<div class="stage-node ${mainStage===i+1?'active':''}"><span class="stage-index">0${i+1}</span><h3>${i===1?'榨汁提纯':s.name}</h3><p>${mainStage===i+1?(values.running?'执行中':'已暂停'):'演示工序'}</p></div>`;}).join('');
    $('#process-reading-label').textContent=stage.label;
    $('#process-reading').innerHTML=`${format(stage.field,stage.digits)} <small>${stage.unit}</small>`;
    $('#process-target').textContent=({2:'液位满量程 180 cm / 100%',5:'高速散热阈值 45 ℃（固件固定）',6:'过滤终值 10% · 开阀状态未上报',7:`环境 ${format('air_temperature',1)} ℃ · 湿度告警70% / 解除68%`,8:'请在现场操作下一批'})[values.stage] || `目标 ${format(stage.target,stage.digits)} ${stage.unit}`;
    const run=$('#run-button'); run.innerHTML=icon(values.running?'pause':'play')+(values.running?'暂停流程':'继续运行'); run.disabled=busy||stale||(!values.running&&!safe);
    $$('[data-command="tare"]').forEach(e=>{e.disabled=busy||stale||Boolean(values.running);e.title=values.running?'请先暂停流程':'';});
    $$('[data-command="ack_alarm"]').forEach(e=>e.disabled=busy||stale);
    const advance=$('#advance-button'), manualAvailable=!live||(/^cane-onenet-(manual|juice)-/.test(String(values.firmware_version)));
    advance.innerHTML=icon('arrow-right')+(values.stage===7?'下一批（现场操作）':'下一步');
    advance.disabled=busy||stale||!safe||!manualAvailable||!(values.stage>=1&&values.stage<=6)||(values.stage===6&&values.concentration_demo>10);
    advance.title=values.stage===7?'本次固件云解析器仅接受1～6，请使用现场下一批按键':values.stage===6&&values.concentration_demo>10?'请现场按PC13开阀，等待过滤完成':!manualAvailable?'请更新设备为手动流程固件':'仅按下才推进；进入榨汁后需要再次继续运行';
    const actuatorData=[['fan','通风 / 散热风扇','流程运行60%；煮糖>105℃或冷却>45℃时100%；暂停停转',values.process_fan_percent,' %'],['layers','入料 / 分装闸门','仅显示设备上传反馈，过滤阀无独立遥测',values.feeder_on?'入料开启':values.valve_open?'分装开启':'已关闭',''],['refresh','工艺电机','与设备遥测同步',values.stir_setting,' %']];
    $('#actuators').innerHTML=actuatorData.map(([i,n,h,v,u])=>`<div class="actuator-row"><span class="actuator-icon ${v!==0&&v!=='已关闭'?'on':''}">${icon(i)}</span><div><h3>${n}</h3><p>${h}</p></div><strong>${v}<small>${u}</small></strong></div>`).join('');
    $('#fire-chip').classList.toggle('danger',Boolean(values.fire_detected)); $('#fire-chip').innerHTML=`<span class="dot"></span>明火${values.fire_detected?'危险':'正常'}`;
    $('#person-chip').classList.toggle('danger',Boolean(values.person_detected)); $('#person-chip').innerHTML=`<span class="dot"></span>人员${values.person_detected?'危险':'正常'}`;
    $('#safety-banner').classList.toggle('danger',profile.hasAlarm(values));
    $('#safety-description').textContent=safe?'当前无安全联锁，危险解除后仍需人工启动。':'明火或人员危险触发停机，确认报警不能解除现场危险。';
    $('#alarm-count').textContent=`${Number(values.fire_detected)+Number(values.person_detected)+Number(Boolean(values.environment_faults))+Number(Boolean(values.process_faults))+Number(Boolean(values.process_alarm))} 项异常`;
    $('#safety-grid').innerHTML=[['flame','明火检测',values.fire_detected?'检测到明火':'正常','触发立即停机',values.fire_detected],['user','危险区人员',values.person_detected?'人员进入':'正常','人员进入触发联锁',values.person_detected],['layers','相对烟雾',`${values.smoke_relative??'—'}%`,'云端未上报ppm；设备按500ppm判断',Boolean(values.environment_faults&32)],['droplet','环境湿度',`${values.air_humidity??'—'} %RH`,'≥70%报警 / ≤68%解除，不停线',Boolean(values.environment_faults&512)],['info','工序报警',profile.alarm(values),'以设备报警码为准',Boolean(values.process_alarm)],['radio','采样状态',stale?'数据过期':'示例有效',stale?'控制已锁定':'安全与必需采样有效',stale]].map(([i,n,v,h,d])=>`<div class="safety-tile ${d?'danger':''}">${icon(i)}<h3>${n}</h3><strong>${v}</strong><p>${h}</p></div>`).join('');
    const locked=Boolean(values.running)||stale||busy;
    $$('#target-form input').forEach(e=>e.disabled=locked); $('#save-parameters').disabled=locked; $('#restore-parameters').disabled=busy;
    $('#parameter-lock').textContent=stale?'数据过期，已锁定':values.running?'运行中锁定':'暂停，可修改';
    if(!$('#parameter-feedback').dataset.result) $('#parameter-feedback').textContent=stale?'示例数据过期，恢复正常后再操作。':values.running?'请先在产线页暂停流程，再修改目标值。':'流程已暂停，可设置参数；下发前将再次确认。';
    document.body.classList.toggle('stale-data',stale);
    $('.preview-notice').classList.toggle('stale-banner',stale);
    $('.preview-notice>span:not([data-icon])').textContent=stale?'示例数据已过期，控制已禁用。请在设置中恢复正常。':'界面演示 · 示例数据，不连接实际设备';
    $('#sync-caption').textContent=stale?`最后更新 ${time(lastSample)} · 示例已过期`:`更新于 ${time(lastSample)} · 示例数据`;
    $('.connection-strip strong').textContent=stale?'示例数据已过期':'示例数据持续更新';
    $('#speech-switch').setAttribute('aria-checked',String(speech));
    if(live){
      const info=stale?(cloudLabel||'云数据过期，控制已锁定'):'OneNET 真实数据 · 以设备回报为准';
      $('.preview-notice>span:not([data-icon])').textContent=info;
      $('.preview-pill').innerHTML='<span class="dot"></span>'+(stale?'等待云数据':'真实设备');
      $('#sync-caption').textContent=lastSample?`设备采样 ${time(lastSample)} · ${stale?'过期 / 不完整':'有效'}`:'尚无设备采样';
      $('.connection-strip strong').textContent=stale?'控制锁定':'OneNET 数据已读取';
      $('#safety-description').textContent=stale?'数据尚未就绪或已过期，不能据此判断现场安全。':safe?(profile.hasAlarm(values)?'设备报告报警提示，请查看明细；湿度偏高不触发停线。':'当前未报告安全联锁；危险解除后仍需人工启动。'):'设备报告安全联锁，请检查现场；确认不会消除危险。';
      $$('[data-bind="safety"]').forEach(e=>e.textContent=stale?'状态待核对':!safe?'安全联锁触发':profile.hasAlarm(values)?'存在报警提示':'现场正常');
      $$('[data-bind="health"]').forEach(e=>e.textContent=stale?'等待有效采样':!safe?'安全联锁已触发':values.running?'产线运行中':'流程已暂停');
      $('#stage-track').querySelectorAll('.stage-node p').forEach(e=>{if(e.textContent==='演示工序')e.textContent='等待设备切换';});
      $('#parameter-feedback').textContent=$('#parameter-feedback').dataset.result?$('#parameter-feedback').textContent:stale?'等待完整有效的设备采样。':values.running?'请先暂停流程再修改参数。':'参数提交后请核对设备回报。';
      $('#safety-grid').innerHTML=$('#safety-grid').innerHTML.replace('示例有效','设备有效').replace('演示相对值','相对值').replace('安全与必需采样有效',`环境 / 过程有效位 ${values.environment_valid??'—'} / ${values.process_valid??'—'}`);
      if(stale)$('#safety-grid').querySelectorAll('strong').forEach(e=>{if(e.textContent==='正常')e.textContent='待核对';});
      $$('.sparkline,.level-bars').forEach(e=>e.hidden=true);
      if(stale){$('#fire-chip').textContent='明火状态待核对';$('#person-chip').textContent='人员状态待核对';$('#actuators').querySelectorAll('strong').forEach(e=>e.textContent='待核对');$('#actuators').querySelectorAll('.actuator-icon').forEach(e=>e.classList.remove('on'));}
    }else{
      $('.preview-pill').innerHTML='<span class="dot"></span>交互演示';
    }
    $('.production-layout>.panel:last-child .tag').textContent=live?'设备遥测':'示例遥测';
    $('.command-panel .small-label').textContent=live?'云命令 · 核对设备回执':'演示操作 · 不发送至实际设备';
    renderTelemetry();drawNav();drawChart();
  }
  function renderTelemetry() {
    const groups=new Map();
    (window.CANE_DATA_CONTRACT||[]).forEach(item=>{if(!groups.has(item.group))groups.set(item.group,[]);groups.get(item.group).push(item);});
    $('#telemetry-groups').innerHTML=[...groups].map(([name,items])=>`<div class="telemetry-group"><h3>${escape(name)}</h3><div class="telemetry-values">${items.map(item=>{let digits=item.name.endsWith('_kg')?4:item.value_type==='decimal'?1:0;return `<div class="telemetry-value"><span>${escape(item.description)}</span><strong>${escape(format(item.name,digits))} ${escape(profile.unit(item.name,values.stage,item.unit_symbol))}</strong><small>${escape(item.name)}</small></div>`;}).join('')}</div></div>`).join('');
  }
  function drawChart() {
    const svg=$('#trend-chart'); if(!svg)return;
    const now=Date.now(), points=history.filter(p=>now-p.at<=period*1000);
    const vals=points.map(p=>p.value), low=Math.floor(Math.min(30,...vals)/10)*10, high=Math.max(low+20,Math.ceil(Math.max(50,...vals)/10)*10);
    const x=at=>45+(at-(now-period*1000))/(period*1000)*620, y=v=>145-(v-low)/(high-low)*130;
    const grid=Array.from({length:4},(_,i)=>{const n=low+(high-low)*i/3,py=y(n);return `<line x1="45" x2="666" y1="${py}" y2="${py}" stroke="var(--line)" stroke-dasharray="3 5"/><text x="30" y="${py+3}" font-size="10" fill="var(--muted)" text-anchor="end">${n.toFixed(0)}</text>`;}).join('');
    const line=points.map((p,i)=>`${i?'L':'M'}${x(p.at).toFixed(1)},${y(p.value).toFixed(1)}`).join(' ');
    const area=points.length>1?`<path d="${line} L${x(points.at(-1).at)},145 L${x(points[0].at)},145Z" fill="url(#chart-gradient)"/><path d="${line}" fill="none" stroke="var(--green2)" stroke-width="2.5"/>`:'';
    svg.innerHTML=`<defs><linearGradient id="chart-gradient" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#6ca987" stop-opacity=".2"/><stop offset="1" stop-color="#6ca987" stop-opacity="0"/></linearGradient></defs>${grid}${area}${points.map(p=>`<circle cx="${x(p.at)}" cy="${y(p.value)}" r="2.5" fill="var(--green2)"/>`).join('')}<text x="45" y="174" font-size="10" fill="var(--muted)">${period===60?'1 分钟前':'5 分钟前'}</text><text x="666" y="174" font-size="10" fill="var(--muted)" text-anchor="end">现在</text>`;
    $('#sample-count').textContent=String(points.length);
  }
  function restoreTargets() { $$('#target-form input').forEach(e=>{e.value=Number.isFinite(values[e.name])?profile.display(e.name,values[e.name]).toFixed(0):'';}); }
  function feedback(message,type='') {const e=$('#parameter-feedback');e.textContent=message;e.className=`form-feedback ${type}`;e.dataset.result=type;}
  function confirmation(title,message,action) {$('#confirm-title').textContent=title;$('#confirm-text').textContent=message;pendingAction=action;$('#confirm-dialog').showModal();}
  const resultText={0:'执行成功',1:'参数错误',2:'安全联锁阻止',3:'采样未就绪',4:'流程状态不允许',5:'去皮或过滤前置步骤未完成，请检查现场',9:'设备忙，请稍后核对状态'};
  async function execute(command,params={}) {
    if(busy)return; if(stale){toast('数据已过期，无法执行操作');return;}
    if(live){await executeCloud(command,params);return;}
    const currentGeneration=generation, request={command,params};busy=true;render();
    $('#command-status').className='command-status';$('#command-status').innerHTML=`${icon('clock')}<div><strong>等待模拟回执</strong><p>${escape(JSON.stringify(request))}</p></div>`;
    const timeout=nextTimeout;nextTimeout=false;
    await new Promise(r=>setTimeout(r,timeout?2200:650));
    if(currentGeneration!==generation)return;
    let result=0;
    if(stale)result=3;
    else if(!timeout){
      if(command==='set_run') {
        if(params.running&&danger())result=2;else values.running=params.running;
      } else if(command==='next_stage') {
        if(danger())result=2;
        else if(params.expected_stage!==values.stage)result=4;
        else {if(values.stage===7)values.batch_count++;values.stage=profile.next(values.stage);if(values.stage===2)values.running=0;values.fill_count=0;values.tare_pending=Number(values.stage===4);if(values.stage===6)enterDemoPurify();}
      } else if(command==='set_targets') {
        if(values.running)result=4;else Object.assign(values,params);
      } else if(command==='tare') {
        if(values.running)result=4;else values.weight_kg=0;
      }
    }
    busy=false;
    const description=timeout?'命令超时，执行结果未知；请核对设备状态，不要盲目重复操作。':`模拟回执：${resultText[result]}（result_code=${result}）`;
    $('#command-status').className=`command-status ${timeout||result?'error':'success'}`;
    $('#command-status').innerHTML=`${icon(timeout?'clock':result?'info':'check')}<div><strong>${timeout?'未收到模拟回执':resultText[result]}</strong><p>${escape(description)}</p><p>${escape(JSON.stringify(request))}</p></div>`;
    const names={set_run:params.running?'启动流程':'暂停流程',set_targets:'设置目标参数',tare:'空载去皮',ack_alarm:'确认报警',next_stage:'下一步'};
    addEvent(names[command]+(timeout?' · 超时':result?' · 已拒绝':' · 成功'),description,'command',timeout||result?'warning':'');
    toast(timeout?'命令超时，结果未知':`演示：${names[command]}${result?'被拒绝':'成功'}`);say(timeout?'命令超时，请核对设备状态':`${names[command]}，${resultText[result]}`);
    if(command==='set_targets')feedback(timeout?'未收到回执，参数保持原值。':result?'参数未更新。':'模拟回执成功，示例设备已回报新参数。',timeout||result?'error':'success');
    if(!timeout&&!result&&command==='set_run'&&params.running)setPage('production',false);
    if(!timeout&&!result&&command==='tare')setPage('production',false);
    if(!timeout&&!result&&command==='ack_alarm')setPage('alarms',false);
    render();
  }
  function scenario(name) {
    if(live){toast('真实模式禁止修改传感器数据');return;}
    $('#settings-dialog').close();
    if(name==='fire') {
      values.fire_detected=1;values.running=0;outputs();addEvent('明火报警 · 流程已停机','风扇、进料和分装关闭；确认不会消除危险。','alarm','alarm');setPage('alarms',false);toast('演示：明火触发联锁，已自动进入安全页');say('明火报警，流程已停止，请检查现场');
    } else if(name==='stale') {
      stale=true;addEvent('示例数据已过期','控制已锁定；最后已知状态不代表当前设备状态。','alarm','warning');toast('演示：采样暂停，控制已锁定');
    } else if(name==='timeout') {nextTimeout=true;toast('下次操作将演示命令超时');}
    else {const was=danger()||stale;values.fire_detected=values.person_detected=values.environment_faults=values.process_faults=0;stale=false;lastSample=Date.now();if(was){addEvent('示例危险已解除','流程保持原启停状态；联锁停机后需人工重新启动。','alarm');say('危险已解除，请确认现场安全后人工启动');}toast('已恢复正常示例数据');}
    render();
  }
  document.addEventListener('click', e => {
    const target=e.target.closest('button,a');if(!target)return;
    if(target.dataset.page){e.preventDefault();setPage(target.dataset.page);}
    if(target.hasAttribute('data-settings')) {
      const dialog = $('#settings-dialog');
      dialog.showModal();
      // 每次打开从连接信息开始，避免保留上次演示工具的滚动位置。
      dialog.querySelector('.dialog-body').scrollTop = 0;
    }
    if(target.dataset.close)$('#'+target.dataset.close).close();
    if(target.dataset.scenario)scenario(target.dataset.scenario);
    if(target.dataset.filter){filter=target.dataset.filter;$$('#event-filter button').forEach(b=>b.classList.toggle('active',b===target));renderEvents();}
    if(target.dataset.period){period=Number(target.dataset.period);$$('#chart-period button').forEach(b=>b.classList.toggle('active',b===target));drawChart();}
    if(target.dataset.command==='next-stage')execute('next_stage',{expected_stage:values.stage});
    if(target.dataset.command==='toggle-run') {
      if(values.running)execute('set_run',{running:0});
      else confirmation('启动流程？','请确认现场安全。启动后设备将执行当前工序；危险或数据过期时拒绝执行。',()=>execute('set_run',{running:1}));
    }
    if(target.dataset.command==='tare')confirmation('确认空载去皮？','请确认称重台已空载。将请求设备执行去皮，请核对设备回执和新采样。',()=>execute('tare'));
    if(target.dataset.command==='ack_alarm')execute('ack_alarm');
  });
  $('#confirm-action').addEventListener('click',()=>{$('#confirm-dialog').close();const fn=pendingAction;pendingAction=null;if(fn)fn();});
  $('#theme-toggle').addEventListener('click',()=>{night=!night;setTheme();say(night?'夜间模式':'日间模式');});
  $('#dark-switch').addEventListener('click',()=>{night=!night;setTheme();});
  $('#speech-switch').addEventListener('click',()=>{if(!host&&!('speechSynthesis'in window)){toast('当前浏览器不支持语音播报');return;}speech=!speech;try{localStorage.setItem('caneflow.speech',String(speech));}catch{}render();if(speech)say('中文操作播报已开启');else if(!host)window.speechSynthesis.cancel();});
  $('#restore-parameters').addEventListener('click',()=>{restoreTargets();feedback('已恢复当前设备回报参数。','success');});
  $('#target-form').addEventListener('submit',e=>{
    e.preventDefault();if(values.running||stale||busy)return;
    const params={};
    for(const input of $$('#target-form input')) {
      const value=Number(input.value),min=Number(input.min),max=Number(input.max),step=Number(input.step);
      if(input.value.trim()===''||!Number.isFinite(value)||value<min||value>max||Math.abs((value-min)/step-Math.round((value-min)/step))>1e-6){feedback(`请检查${input.closest('label').querySelector('.field-title').textContent}的范围和步长。`,'error');input.focus();return;}
      params[input.name]=profile.wire(input.name,value);
    }
    confirmation('下发新的目标参数？',`进料 ${(params.weigh_target_kg*1000).toFixed(0)} kg；每份 ${(params.fill_target_kg*1000).toFixed(0)} g；煮糖 ${params.boil_target_c} ℃；冷却 ${params.cool_target_c} ℃。请核对目标参数，确认后提交。`,()=>execute('set_targets',params));
  });
  $('#next-stage').addEventListener('click',()=>{
    if(live||busy)return;values.stage=profile.next(values.stage);if(values.stage===2)values.running=0;values.fill_count=0;
    if(values.stage===1)values.weight_kg=.9;
    if(values.stage===6)enterDemoPurify();
    if(values.stage===3)values.object_temperature=103.5;
    if(values.stage===4)values.weight_kg=.018;
    if(values.stage===5){values.object_temperature=47;values.fill_count=1;}
    addEvent(`切换示例工序：${stages[values.stage].name}`,'仅用于检查手机界面，不代表真实工序完成。','system');$('#settings-dialog').close();setPage('production');render();
  });
  $('#reset-demo').addEventListener('click',()=>{
    if(live)return;generation++;values=initial();stale=nextTimeout=busy=false;lastSample=Date.now();history=[{at:lastSample,value:values.object_temperature}];events=[];$('#settings-dialog').close();$('#parameter-feedback').dataset.result='';restoreTargets();$('#command-status').className='command-status';$('#command-status').innerHTML=icon('terminal')+'<div><strong>等待操作</strong><p>本次演示已重置，点击控制按钮查看模拟回执。</p></div>';addEvent('界面演示已重置','当前为示例数据，未连接实际设备。','system');setPage('overview');render();
  });
  window.addEventListener('hashchange',()=>{const id=location.hash.slice(1);if(id!==page)setPage(id,false);});
  $$('dialog').forEach(d=>d.addEventListener('click',e=>{if(e.target===d){const r=d.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)d.close();}}));
  $('#today').textContent=new Date().toLocaleDateString('zh-CN',{month:'2-digit',day:'2-digit'});
  $$('.sparkline').forEach(e=>{e.innerHTML='<svg viewBox="0 0 180 40" preserveAspectRatio="none"><path d="M0 33 15 28 30 30 45 20 60 24 75 14 90 16 105 7 120 13 135 8 150 15 165 6 180 10" fill="none" stroke="#8eb09a" stroke-width="1.5"/></svg>';});
  $('.level-bars').innerHTML=Array.from({length:12},(_,i)=>`<span class="${i<5?'on':''}" style="height:${10+i}px"></span>`).join('');
  hydrate();setTheme();restoreTargets();addEvent('进入示例工作空间','已载入含入库的六主工序与37项示例数据；未连接 OneNET。','system');setPage(location.hash.slice(1)||'overview',false);render();
  setInterval(()=>{
    if(live){if(!stale&&Date.now()-oldestAt>30000){stale=true;cloudLabel='设备数据超过30秒未更新，控制已锁定';render();}return;}
    if(stale)return;
    values.uptime_s+=3;values.sample_seq++;values.air_temperature=25+Math.sin(values.sample_seq/5)*.3;values.object_temperature+=Math.sin(values.sample_seq/4)*.12;
    if(values.stage===6&&values.running&&!danger())values.concentration_demo=Math.max(demoPurifyTarget,values.concentration_demo-20);
    lastSample=Date.now();history.push({at:lastSample,value:values.object_temperature});history=history.filter(p=>lastSample-p.at<300000);render();
  },3000);

  async function executeCloud(command,params){
    busy=true;render();const request={command,params};
    $('#command-status').className='command-status';$('#command-status').innerHTML=icon('clock')+'<div><strong>已提交，等待设备回执</strong><p>命令不会自动重发；请等待结果并核对设备回报。</p></div>';
    const answer=await new Promise(resolve=>{const timer=setTimeout(()=>{commandReply=null;resolve({kind:'command-error',message:'等待超时，执行结果未知'});},40000);commandReply=result=>{clearTimeout(timer);commandReply=null;resolve(result);};post('command',{request});});
    busy=false;const confirmed=answer.kind==='command', ok=confirmed&&answer.result_code===0;
    const text=confirmed?`设备回执：${resultText[answer.result_code]}（result_code=${answer.result_code}）`:answer.message||'执行结果未知，请核对设备状态';
    $('#command-status').className='command-status '+(ok?'success':'error');$('#command-status').innerHTML=icon(ok?'check':'info')+`<div><strong>${ok?'设备已确认执行':confirmed?'设备已拒绝':'执行结果未确认'}</strong><p>${escape(text)}</p><p>参数和输出以新采样为准。</p></div>`;
    addEvent(`云命令 ${command}`,text,'command',ok?'':'warning');toast(text);say(text);
    if(command==='set_targets')feedback(ok?'设备回执成功，等待新参数采样；请核对设备回报值。':text,ok?'success':'error');
    stale=true;cloudLabel='命令结束，等待新采样核对设备状态';post('refresh');render();
  }
  window.CaneNativeEvent=e=>{
    if(e.kind==='mode'){
      const changed=live!==e.live;live=e.live;generation++;history=[];events=[];
      $('#demo-tools').hidden=live;
      $('#source-title').textContent=live?'OneNET 真实设备':'交互演示模式';
      $('#source-detail').textContent=live?'鸿蒙原生 HTTPS 通信，不使用设备 MQTT 身份。':'本机示例数据，不发送云命令。';
      $('#device-label').textContent=e.product+' / '+e.device;
      $('#real-status').textContent=live?'等待设备有效采样':'尚未启用';
      $('#source-tag').textContent=live?'真实':'演示';
      $('.batch-card .panel-title .tag').textContent=live?'设备批次':'演示批次';
      $('.trend-panel .panel-title h2 small').textContent=live?'设备采样':'示例';
      $('.trend-panel .panel-title p').textContent=live?'仅记录本次打开后读取的设备采样':'仅展示本次界面演示生成的数据';
      $('.events-note').textContent='仅记录本次应用会话，不代表完整设备历史记录';
      $('.app-footer>span:last-child').textContent='HarmonyOS ArkTS v1.0.0 · '+(live?'真实云模式':'示例模式');
      if(live){values=Object.fromEntries(window.CANE_DATA_CONTRACT.map(f=>[f.name,null]));lastSample=0;stale=true;cloudLabel='正在读取 OneNET 数据，控制暂时锁定';}
      else {values=initial();lastSample=Date.now();stale=false;history=[{at:lastSample,value:values.object_temperature}];}
      addEvent(live?'进入真实设备空间':'进入示例工作空间',live?'等待37项设备采样，过期或不完整数据不允许控制。':'仅本机示例数据，未连接设备。','system');restoreTargets();render();
    }else if(e.kind==='snapshot'&&live){
      const previous=danger(), previousAlarm=profile.hasAlarm(values), previousAlarmKey=profile.alarmKey(values), previousSeq=values.sample_seq, first=!lastSample;
      values=e.values;lastSample=e.at;oldestAt=e.oldestAt;stale=!e.fresh;cloudLabel=e.presentFields<37?`已收到 ${e.presentFields}/37 项有效字段，等待其余分组`:!e.samplingValid?'设备报告传感器采样无效，控制已锁定':e.freshFields<37?`分组更新：${e.freshFields}/37 项在30秒内，其余字段已过期；控制锁定`:'37项数据已同步，传感器采样有效';
      if(!stale&&values.sample_seq!==previousSeq&&Number.isFinite(values.object_temperature)){history.push({at:e.timestamps.object_temperature,value:values.object_temperature});history=history.filter(p=>Date.now()-p.at<300000);}
      if(profile.hasAlarm(values)&&previousAlarmKey!==profile.alarmKey(values)){setPage('alarms',false);addEvent(danger()?'设备报告安全联锁':'设备报告报警提示',profile.alarm(values)+(values.environment_faults&512?'；环境湿度过高，请注意防潮':''),'alarm','alarm');say(danger()?'设备报告安全联锁，请检查现场':values.environment_faults&512?'环境湿度过高，请注意防潮':profile.alarm(values));}
      if((previous||previousAlarm)&&!profile.hasAlarm(values)&&!stale){setPage('production',false);addEvent('安全联锁已解除','已返回当前工序，请确认现场安全后点击继续运行。','system');say('报警已解除，请确认安全后继续运行');}
      if(first&&!busy)restoreTargets();$('#real-status').textContent=e.presentFields<37?'等待完整分组':!e.samplingValid?'传感器采样无效':stale?'部分字段超过30秒':'37项实时采样有效';render();
    }else if(e.kind==='connection-error'&&live){stale=true;cloudLabel=e.message;$('#real-status').textContent=e.message;render();}
    else if((e.kind==='command'||e.kind==='command-error')&&commandReply)commandReply(e);
  };
  window.CaneBack=()=>{const dialog=$$('dialog').find(d=>d.open);if(dialog){dialog.close();return true;}if(page!=='overview'){setPage('overview');return true;}return false;};
  $('#native-config').addEventListener('click',()=>post('configure'));
  $('#refresh-cloud').addEventListener('click',()=>{if(live)post('refresh');else toast('当前为演示模式');});
})();
