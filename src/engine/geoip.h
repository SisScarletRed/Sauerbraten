#ifndef __GEOIP_H_
#define __GEOIP_H_

#include "engine.h"

extern const char *GeoIP_country_code_by_ipnum();
extern const char *GeoIP_country_name_by_ipnum();
extern unsigned long GeoIP_addr_to_num(const char *addr);
extern char *GeoIP_num_to_addr(unsigned long ipnum);

#endif // __GEOIP_H_
