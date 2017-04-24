// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef __INFO_HXX__
#define __INFO_HXX__

#include <iostream>
#include <string>

namespace mocoda
{
    struct Info
    {
        std::string filename;
        std::string funname;
        std::size_t begin;
        std::size_t end;

        Info(const std::string & __filename, const std::string & __funname, const std::size_t __begin, const std::size_t __end);
        Info();

        operator bool() const
            {
                return begin <= end;
            }
    };
}

std::ostream & operator<<(std::ostream & os, const mocoda::Info & i);

#endif // __INFO_HXX__
