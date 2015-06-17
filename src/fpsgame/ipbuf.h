#ifndef __IPBUF_H_
#define __IPBUF_H_

namespace game
{
    struct ipaddr
    {
        ipmask ip;
        uint datatype;
        void *data;
        void (*freecallback)(void *);
    };

    class ipbuf
    {
    public:
        void addip(ipmask &ip, uint datatype = 0, void *data = NULL, void (*freecallback)(void *) = NULL)
        {
            uint8_t firstbyte = ((uint8_t*)&ip.ip)[0];

            vector<ipaddr> &iv = ips[firstbyte&0xFF];

            ipaddr &ia = iv.add();

            ia.ip = ip;
            ia.datatype = datatype;
            ia.data = data;
            ia.freecallback = freecallback;

            ipcount++;
        }

        uint delip(uint32_t &ip_in, vector<ipmask> &removedips)
        {
            uint count = 0;

            uint8_t firstbyte = ((uint8_t*)&ip_in)[0];

            vector<ipaddr> &iv = ips[firstbyte&0xFF];

            loopvrev(iv)
            {
                if(iv[i].ip.check(ip_in))
                {
                    ipaddr ip = iv.remove(i);

                    if(ip.freecallback)
                        ip.freecallback(ip.data);

                    removedips.add(ip.ip);

                    count++;
                    ipcount--;
                }
            }

            return count;
        }

        ipmask* findip(uint32_t ip_in, uint *datatype = NULL, void **data = NULL, ipaddr **addr = NULL, int *start = NULL)
        {
            uint8_t firstbyte = ((uint8_t*)&ip_in)[0];

            vector<ipaddr> &iv = ips[firstbyte&0xFF];

            for(int i = (start ? *start : 0); i < iv.length(); i++)
            {
                if(iv[i].ip.check(ip_in))
                {
                    if(datatype)
                        *datatype = iv[i].datatype;

                    if(data)
                        *data = iv[i].data;

                    if(addr)
                        *addr = &iv[i];

                    if(start)
                        *start = i+1;

                    return &iv[i].ip;
                }
            }

            return NULL;
        }

        uint findips(uint32_t ip_in, vector<ipaddr*> &result)
        {
            int start = 0;

            do
            {
                ipaddr *ia;

                if(!findip(ip_in, NULL, NULL, &ia, &start))
                    break;

                result.add(ia);
            } while(true);

            return result.length();
        }

        uint findips(bool (*compare)(uint datatype, void *data, void *comparedata), void *comparedata, vector<ipaddr*> &result)
        {
            loopi(sizeofarray(ips))
            {
                vector<ipaddr> &iv = ips[i];

                loopvj(iv)
                {
                    if(compare(iv[j].datatype, iv[j].data, comparedata))
                        result.add(&iv[j]);
                }
            }

            return result.length();
        }

        void loopips(bool (*callback)(ipmask &ip, uint datatype, void *data, void *cbdata), void *cbdata)
        {
            loopi(sizeofarray(ips))
            {
                vector<ipaddr> &iv = ips[i];

                loopvj(iv)
                {
                    if(!callback(iv[j].ip, iv[j].datatype, iv[j].data, cbdata))
                        return;
                }
            }
        }

        uint getallips(vector<ipaddr *> &result)
        {
            if(isempty_())
                return 0;

            if((size_t)result.capacity() < size())
                result.growbuf(size()+1);

            loopi(256)
            {
                vector<ipaddr> &iv = ips[i];

                loopvj(iv)
                    result.add(&iv[j]);
            }

            return result.length();
        }

        bool isempty_() { return ipcount == 0; }
        size_t size() { return ipcount; }

        void clear()
        {
            loopi(sizeofarray(ips))
            {
                vector<ipaddr> &iv = ips[i];

                loopvjrev(iv)
                {
                    ipaddr ip = iv.remove(j);

                    if(ip.freecallback)
                        ip.freecallback(ip.data);
                }
            }
            ipcount = 0;
        }

        ipbuf() : ipcount(0) { }
        ~ipbuf() { clear(); }

    private:
        vector<ipaddr> ips[256];
        size_t ipcount;
    };
}
#endif
