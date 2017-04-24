// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef __DB_HXX__
#define __DB_HXX__

#include <sstream>
#include <sqlite3.h>
#include <string>

#include "info.hxx"

namespace mocoda
{
    class DB
    {
        sqlite3 * db;
        std::ostringstream os;

    public:

        DB();
        ~DB();

        void insertVirtual(const Info & i);
        void insertDefinition(const Info & i);
        void insertDeclaration(const Info & i, const Info & def);
        void insertDeclaration(const Info & i);
        void insertCallResolved(const Info & caller, const Info & callee, const std::size_t line, const std::size_t col, const bool isvirtual);
        void insertCallUnresolved(const Info & caller, const Info & callee, const std::size_t line, const std::size_t col, const bool isvirtual);
        void insertVirtualResolved(const Info & def, const Info & vdef);
        void insertVirtualUnresolved(const Info & def, const Info & vdec);
        void commit();
        void create();

    private:

        void handleError(const int rc, char * err);
    };
}
#endif // __DB_HXX__
