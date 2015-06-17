#include "geoip.h"

const char *GeoIP_country_code_by_ipnum() { return NULL; }
const char *GeoIP_country_name_by_ipnum() { return NULL; }

unsigned long GeoIP_addr_to_num(const char *addr)
{
    unsigned int c, octet, t;
    unsigned long ipnum;
    int i = 3;

    octet = ipnum = 0;
    while((c = *addr++))
    {
        if(c == '.')
        {
            if (octet > 255) return 0;
            ipnum <<= 8;
            ipnum += octet;
            i--;
            octet = 0;
        }
        else
        {
            t = octet;
            octet <<= 3;
            octet += t;
            octet += t;
            c -= '0';
            if (c > 9) return 0;
            octet += c;
        }
    }
    if ((octet > 255) || (i != 0)) return 0;
    ipnum <<= 8;
    return ipnum + octet;
}

char *GeoIP_num_to_addr(unsigned long ipnum)
{
    char *ret_str;
    char *cur_str;
    int octet[4];
    int num_chars_written;

    ret_str = (char *)malloc(sizeof(char)*16);
    cur_str = ret_str;
    loopi(4)
    {
        octet[3 - i] = ipnum % 256;
        ipnum >>= 8;
    }
    loopi(4)
    {
        num_chars_written = sprintf(cur_str, "%d", octet[i]);
        cur_str += num_chars_written;

        if(i < 3)
        {
            cur_str[0] = '.';
            cur_str++;
        }
    }
    return ret_str;
}
