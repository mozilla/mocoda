#ifndef __UTILS_HXX__
#define __UTILS_HXX__

#include <cstdlib>
#include <string>
#include <limits.h>

namespace utils
{

    bool exist(const std::string & fileName);
    bool startswith(const std::string & a, const std::string & b);
    std::string getRealPath(const std::string & path);
    std::string getEnv(const char * name);

}


#endif // __UTILS_HXX__
