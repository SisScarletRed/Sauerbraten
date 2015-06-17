#ifndef __IPIGNORE_H_
#define __IPIGNORE_H_

namespace ipignore
{
    bool isignored(int cn, const char *text = NULL);
    bool isignored(uint ip);
    bool checkaddress(const char *address, bool remove = false);
    void startup();
    void shutdown();
} //namespace ipignore


#endif
