#ifndef CANE_CLOUD_JSON_H
#define CANE_CLOUD_JSON_H
#include <stdint.h>
/* 严格解析约定的command和params；拒绝未知字段、重复字段及非十进制定点值。 */
int cane_cloud_parse(const char *json,unsigned *kind,int32_t values[4]);
#endif
