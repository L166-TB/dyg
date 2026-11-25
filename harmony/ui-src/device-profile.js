/* 适配用户 2026-10-30 固件。云字段保留原值，只在显示与参数提交边界换算。 */
(() => {
  'use strict';
  const mass = key => ['weight_kg','weigh_target_kg','fill_target_kg'].includes(key);
  const profile = {
    display(key, value) { return typeof value === 'number' && mass(key) ? value * 1000 : value; },
    wire(key, value) { return mass(key) ? value / 1000 : value; },
    unit(key, stage, fallback='') { return key === 'fill_target_kg' || (key === 'weight_kg' && stage === 4) ? 'g' : mass(key) ? 'kg' : fallback; },
    digits(key, fallback=0) { return mass(key) ? 1 : fallback; },
    interlocked(v) { return Boolean(v.fire_detected || v.person_detected || (v.environment_faults & ~512) || v.process_faults); },
    alarmKey(v) { return [v.fire_detected||0,v.person_detected||0,v.environment_faults||0,v.process_faults||0,v.process_alarm||0].join('/'); },
    hasAlarm(v) { return Boolean(v.fire_detected || v.person_detected || v.environment_faults || v.process_faults || v.process_alarm); },
    next(stage) { return ({1:2,2:6,6:3,3:4,4:5,5:7,7:1})[stage] || 1; },
    main(stage) { return stage === 6 ? 2 : stage === 7 ? 6 : stage === 8 ? 6 : stage; },
    alarm(v) {
      const code = v.process_alarm;
      if (!code) return '无工序报警';
      if (code === 1) return ({1:'称重超重',2:'杂质浓度超限',6:'浑浊度超限，请现场过滤',3:'煮糖温度过高',4:'分装超重，超过200 g上限'})[v.stage] || '工序超过上限';
      if (code === 2) return ({1:'称重不足',3:'煮糖温度偏低',4:'分装偏少',5:'冷却未达标'})[v.stage] || '工序低于目标';
      return ({3:'工序采样故障',4:'本工序超时',5:'现场安全联锁'})[code] || `工序报警 ${code}`;
    }
  };
  if (typeof module !== 'undefined') module.exports = profile;
  else window.CaneDeviceProfile = profile;
})();
