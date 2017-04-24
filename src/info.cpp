// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include "info.hxx"

namespace mocoda
{
    Info::Info(const std::string & __filename, const std::string & __funname,
               const std::size_t __begin, const std::size_t __end) : filename(__filename),
                                                                     funname(__funname),
                                                                     begin(__begin),
                                                                     end(__end) { }
    Info::Info() : filename(""),
                   funname(""),
                   begin(1),
                   end(0) { }
}
    
std::ostream & operator<<(std::ostream & os, const mocoda::Info & i)
{
    os << "File: " << i.filename << '\n'
       << "Function: " << i.funname << '\n'
       << "Begin: " << i.begin << '\n'
       << "End: " << i.end;
    
    return os;
}
