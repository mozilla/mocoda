// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iostream>
#include <cstdlib>

#include "DB.hxx"
#include "utils.hxx"

namespace mocoda
{
    DB::DB() : db(nullptr)
    {
        const std::string path = utils::getEnv("MOCODA_DATABASE");
        if (!path.empty())
        {
            const bool exists = utils::exist(path);
            const int rc = sqlite3_open(path.c_str(), &db);
            if (rc)
            {
                std::cerr << "Can't open database: "
                          << sqlite3_errmsg(db)
                          << ": " << path
                          << std::endl;
            }

            if (!exists)
            {
                create();
            }

            os << "BEGIN IMMEDIATE TRANSACTION;";
        }
    }

    DB::~DB()
    {
        if (db)
        {
            sqlite3_close(db);
        }
    }

    void DB::handleError(const int rc, char * err)
    {
        if ((rc != SQLITE_OK) && err)
        {
            std::cerr << "SQL error: "
                      << err
                      << std::endl;
            sqlite3_free(err);
        }
    }

    void DB::insertDefinition(const Info & i)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO definitions (FILENAME,FUNNAME,BEGIN,END) VALUES ("
               << '\"' << i.filename << "\","
               << '\"' << i.funname << "\","
               << i.begin << ','
               << i.end
               << ");";
        }
    }

    void DB::insertDeclaration(const Info & i, const Info & def)
    {
        if (db)
        {
            os << "UPDATE OR IGNORE declarations SET DEF=("
               << "SELECT ROWID FROM definitions WHERE FILENAME=\"" << def.filename << '\"'
               << " AND FUNNAME=\"" << def.funname << '\"'
               << " AND BEGIN=" << def.begin
               << " AND END=" << def.end
               << ") WHERE FILENAME=\"" << i.filename << '\"'
               << " AND FUNNAME=\"" << i.funname << '\"'
               << " AND BEGIN=" << i.begin
               << " AND END=" << i.end << ';'
               << "INSERT OR IGNORE INTO declarations (FILENAME,FUNNAME,BEGIN,END,DEF) VALUES ("
               << '\"' << i.filename << "\","
               << '\"' << i.funname << "\","
               << i.begin << ','
               << i.end << ','
               << "(SELECT ROWID FROM definitions WHERE FILENAME=\"" << def.filename << '\"'
               << " AND FUNNAME=\"" << def.funname << '\"'
               << " AND BEGIN=" << def.begin
               << " AND END=" << def.end
               << "));";
        }
    }

    void DB::insertDeclaration(const Info & i)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO declarations (FILENAME,FUNNAME,BEGIN,END,DEF) VALUES ("
               << '\"' << i.filename << "\","
               << '\"' << i.funname << "\","
               << i.begin << ','
               << i.end << ','
               << "NULL);";
        }
    }

    void DB::insertCallResolved(const Info & caller, const Info & callee, const std::size_t line, const std::size_t col, const bool isvirtual)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO callgraph_resolved (CALLER,CALLEE,LINE,COL,VIRTUAL) VALUES ("
               << "(SELECT ROWID FROM definitions WHERE FILENAME=\"" << caller.filename << '\"'
               << " AND FUNNAME=\"" << caller.funname << '\"'
               << " AND BEGIN=" << caller.begin
               << " AND END=" << caller.end
               << "),(SELECT ROWID FROM definitions WHERE FILENAME=\"" << callee.filename << '\"'
               << " AND FUNNAME=\"" << callee.funname << '\"'
               << " AND BEGIN=" << callee.begin
               << " AND END=" << callee.end << "),"
               << line << ','
               << col << ','
               << isvirtual
               << ");";
        }
    }

    void DB::insertCallUnresolved(const Info & caller, const Info & callee, const std::size_t line, const std::size_t col, const bool isvirtual)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO callgraph_unresolved (CALLER,CALLEE,LINE,COL,VIRTUAL) VALUES ("
               << "(SELECT ROWID FROM definitions WHERE FILENAME=\"" << caller.filename << '\"'
               << " AND FUNNAME=\"" << caller.funname << '\"'
               << " AND BEGIN=" << caller.begin
               << " AND END=" << caller.end
               << "),(SELECT ROWID FROM declarations WHERE FILENAME=\"" << callee.filename << '\"'
               << " AND FUNNAME=\"" << callee.funname << '\"'
               << " AND BEGIN=" << callee.begin
               << " AND END=" << callee.end << "),"
               << line << ','
               << col << ','
               << isvirtual
               << ");";
        }
    }

    void DB::insertVirtualResolved(const Info & def, const Info & vdef)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO overrides_resolved (DEF,VDEF) VALUES ("
               << "(SELECT ROWID FROM definitions WHERE FILENAME=\"" << def.filename << '\"'
               << " AND FUNNAME=\"" << def.funname << '\"'
               << " AND BEGIN=" << def.begin
               << " AND END=" << def.end
               << "),(SELECT ROWID FROM definitions WHERE FILENAME=\"" << vdef.filename << '\"'
               << " AND FUNNAME=\"" << vdef.funname << '\"'
               << " AND BEGIN=" << vdef.begin
               << " AND END=" << vdef.end
               << "));";
        }
    }

    void DB::insertVirtualUnresolved(const Info & def, const Info & vdec)
    {
        if (db)
        {
            os << "INSERT OR IGNORE INTO overrides_unresolved (DEF,VDEC) VALUES ("
               << "(SELECT ROWID FROM definitions WHERE FILENAME=\"" << def.filename << '\"'
               << " AND FUNNAME=\"" << def.funname << '\"'
               << " AND BEGIN=" << def.begin
               << " AND END=" << def.end
               << "),(SELECT ROWID FROM declarations WHERE FILENAME=\"" << vdec.filename << '\"'
               << " AND FUNNAME=\"" << vdec.funname << '\"'
               << " AND BEGIN=" << vdec.begin
               << " AND END=" << vdec.end
               << "));";
        }
    }

    void DB::commit()
    {
        if (db)
        {
            char * err = nullptr;
            os << "COMMIT TRANSACTION;";
            const int rc = sqlite3_exec(db, os.str().c_str(), nullptr, nullptr, &err);
            handleError(rc, err);

            os.str("");
            os.clear();
        }
    }

    void DB::create()
    {
        if (db)
        {
            char * err = nullptr;
            const char * s =
                "CREATE TABLE definitions(FILENAME CHAR(256),FUNNAME TEXT,BEGIN INTEGER,END INTEGER,UNIQUE(FILENAME,FUNNAME,BEGIN,END));"
                "CREATE TABLE declarations(FILENAME CHAR(256),FUNNAME TEXT,BEGIN INTEGER,END INTEGER,DEF INTEGER,FOREIGN KEY(DEF) REFERENCES definitions(ROWID),UNIQUE(FILENAME,FUNNAME,BEGIN,END));"
                "CREATE TABLE callgraph_resolved(CALLER INTEGER,CALLEE INTEGER,LINE INTEGER,COL INTEGER,VIRTUAL BOOLEAN,FOREIGN KEY(CALLER) REFERENCES definitions(ROWID),FOREIGN KEY(CALLEE) REFERENCES definitions(ROWID),UNIQUE(CALLER,CALLEE,LINE,COL,VIRTUAL));"
                "CREATE TABLE callgraph_unresolved(CALLER INTEGER,CALLEE INTEGER,LINE INTEGER,COL INTEGER,VIRTUAL BOOLEAN,FOREIGN KEY(CALLER) REFERENCES definitions(ROWID),FOREIGN KEY(CALLEE) REFERENCES declarations(ROWID),UNIQUE(CALLER,CALLEE,LINE,COL,VIRTUAL));"
                "CREATE TABLE overrides_resolved(DEF INTEGER,VDEF INTEGER,FOREIGN KEY(DEF) REFERENCES definitions(ROWID),FOREIGN KEY(VDEF) REFERENCES definitions(ROWID),UNIQUE(DEF,VDEF));"
                "CREATE TABLE overrides_unresolved(DEF INTEGER,VDEC INTEGER,FOREIGN KEY(DEF) REFERENCES definitions(ROWID),FOREIGN KEY(VDEC) REFERENCES declarations(ROWID),UNIQUE(DEF,VDEC));";

            const int rc = sqlite3_exec(db, s, nullptr, nullptr, &err);
            handleError(rc, err);
        }
    }
}
