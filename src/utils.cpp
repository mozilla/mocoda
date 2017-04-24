#include <fstream>

#include "utils.hxx"

namespace utils
{

    bool exist(const std::string & fileName)
    {
        std::ifstream infile(fileName);
        return infile.good();
    }

    bool startswith(const std::string & a, const std::string & b)
    {
        return b.empty() || (a.length() > b.length() && a.compare(0, b.length(), b) == 0);
    }

    std::string getRealPath(const std::string & path)
    {
        static char real_path[PATH_MAX];
        realpath(path.c_str(), real_path);
        return std::string(real_path);
    }
    
    std::string getEnv(const char * name)
    {
        char * s = std::getenv(name);
        if (s)
        {
            return std::string(s);
        }
        return std::string();
    }
    
}
