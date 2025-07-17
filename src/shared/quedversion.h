#ifndef __VERSION_H_
#define __VERSION_H_

#define QUED_VERSION_MAJOR "0"
#define QUED_VERSION_MINOR "1"
#define QUED_VERSION_PATCH "4"
#define QUED_VERSION_TYPE  "alpha"

const char *getversionmajor()
{
    return QUED_VERSION_MAJOR;
}

const char *getversionminor()
{
    return QUED_VERSION_MINOR;
}

const char *getversionpatch()
{
    return QUED_VERSION_PATCH;
}

const char *getfullversion()
{
    return tempformatstring("v%s.%s.%s", QUED_VERSION_MAJOR, QUED_VERSION_MINOR, QUED_VERSION_PATCH);
}

const char *getfullversionname()
{
    return tempformatstring("v%s.%s.%s-%s", QUED_VERSION_MAJOR, QUED_VERSION_MINOR, QUED_VERSION_PATCH, QUED_VERSION_TYPE);
}

#endif // __VERSION_H_
