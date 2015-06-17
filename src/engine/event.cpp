#include "event.h"

namespace event
{
    void run(int type, const char *args)
    {
        strtool id;
        id.append("on");
        id.append(EVENTNAMES[type]);

        strtool exec;
        exec.append(id.str());
        if(args && args[0])
        {
            exec.add(' ');
            exec.append(args);
        }

        if(identexists(id.str())) execute(exec.str());
    }
}
