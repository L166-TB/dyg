/* 与设备37项数据流一致，仅公开字段，无凭据。 */
window.CANE_DATA_CONTRACT = [
  {
    "name": "air_temperature",
    "description": "仓储温度",
    "unit_symbol": "℃",
    "value_type": "decimal",
    "group": "传感数据"
  },
  {
    "name": "air_humidity",
    "description": "仓储湿度",
    "unit_symbol": "%RH",
    "value_type": "decimal",
    "group": "传感数据"
  },
  {
    "name": "weight_kg",
    "description": "当前净重",
    "unit_symbol": "kg",
    "value_type": "decimal",
    "group": "传感数据"
  },
  {
    "name": "object_temperature",
    "description": "物料表面温度",
    "unit_symbol": "℃",
    "value_type": "decimal",
    "group": "传感数据"
  },
  {
    "name": "smoke_relative",
    "description": "烟雾相对信号",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "light_relative",
    "description": "相对照度",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "hopper_level",
    "description": "甘蔗液位",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "process_distance_mm",
    "description": "过程探头距离",
    "unit_symbol": "mm",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "hall_adc",
    "description": "霍尔原始采样",
    "unit_symbol": "",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "concentration_demo",
    "description": "杂质浓度",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "传感数据"
  },
  {
    "name": "stage",
    "description": "当前实际工序",
    "unit_symbol": "",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "running",
    "description": "流程运行状态",
    "unit_symbol": "",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "fill_count",
    "description": "本批已完成份数",
    "unit_symbol": "份",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "fill_total",
    "description": "本批目标份数",
    "unit_symbol": "份",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "batch_count",
    "description": "完成批次数",
    "unit_symbol": "批",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "tare_pending",
    "description": "分装等待去皮",
    "unit_symbol": "",
    "value_type": "int",
    "group": "流程状态"
  },
  {
    "name": "fire_detected",
    "description": "明火检测",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "person_detected",
    "description": "危险区人员检测",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "process_alarm",
    "description": "工序报警码",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "environment_faults",
    "description": "环境故障位图",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "process_faults",
    "description": "过程故障位图",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "environment_valid",
    "description": "环境采样有效位图",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "process_valid",
    "description": "过程采样有效位图",
    "unit_symbol": "",
    "value_type": "int",
    "group": "安全与有效性"
  },
  {
    "name": "feeder_on",
    "description": "进料使能",
    "unit_symbol": "",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "valve_open",
    "description": "分装阀开启",
    "unit_symbol": "",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "stir_setting",
    "description": "工艺电机设定",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "process_fan_percent",
    "description": "单风扇实际输出",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "warehouse_fan_percent",
    "description": "旧通风兼容字段（非第二只风扇）",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "lamp_on",
    "description": "工作照明使能",
    "unit_symbol": "",
    "value_type": "int",
    "group": "执行器状态"
  },
  {
    "name": "weigh_target_kg",
    "description": "称重目标",
    "unit_symbol": "kg",
    "value_type": "decimal",
    "group": "目标参数"
  },
  {
    "name": "fill_target_kg",
    "description": "每份分装目标",
    "unit_symbol": "kg",
    "value_type": "decimal",
    "group": "目标参数"
  },
  {
    "name": "boil_target_c",
    "description": "煮糖目标温度",
    "unit_symbol": "℃",
    "value_type": "decimal",
    "group": "目标参数"
  },
  {
    "name": "cool_target_c",
    "description": "冷却目标温度",
    "unit_symbol": "℃",
    "value_type": "decimal",
    "group": "目标参数"
  },
  {
    "name": "concentration_limit",
    "description": "杂质浓度目标",
    "unit_symbol": "%",
    "value_type": "int",
    "group": "目标参数"
  },
  {
    "name": "uptime_s",
    "description": "设备运行秒数",
    "unit_symbol": "s",
    "value_type": "int",
    "group": "链路诊断"
  },
  {
    "name": "sample_seq",
    "description": "报告序号",
    "unit_symbol": "",
    "value_type": "int",
    "group": "链路诊断"
  },
  {
    "name": "firmware_version",
    "description": "固件版本",
    "unit_symbol": "",
    "value_type": "string",
    "group": "链路诊断"
  }
];
