# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from collections import defaultdict
import json
import sqlite3
from . import utils


def get_table(conn, tableName, rowid):
    cursor = conn.cursor()
    if rowid:
        cursor.execute('SELECT ROWID,* FROM {};'.format(tableName))
    else:
        cursor.execute('SELECT * FROM {};'.format(tableName))

    return cursor.fetchall()


def get_tables(conn):
    tables = [('definitions', True),
              ('declarations', True),
              ('callgraph_resolved', False),
              ('callgraph_unresolved', False),
              ('overrides_resolved', False),
              ('overrides_unresolved', False)]

    return {args[0]: get_table(conn, *args) for args in tables}


def short_fun(funname):
    return funname.replace(', ', ',').replace(' *', '*').replace(' &', '&')


def get_defs(defs):
    rows_def = [0] * (len(defs) + 1)
    fun2rowid = defaultdict(lambda: list())
    files = set()
    for rowid, filename, funname, begin, end in defs:
        funname = short_fun(funname)
        rows_def[rowid] = [filename, funname, begin, end, []]
        fun2rowid[funname].append(rowid)
        files.add(filename)

    files = list(files)
    file2id = {f: n for n, f in enumerate(files)}
    for i in range(1, len(rows_def)):
        d = rows_def[i]
        d[0] = file2id[d[0]]

    return rows_def, fun2rowid, files


def get_decls(decls, fun2rowid):
    rows_dec = [0] * (len(decls) + 1)
    for rowid, _, funname, _, _, definition in decls:
        if definition is None:
            # try to resolve the name
            funname = short_fun(funname)
            r = fun2rowid.get(funname, [])
            if len(r) == 1:
                definition = r[0]
            else:
                definition = 0
        rows_dec[rowid] = definition

    return rows_dec


def get_overrides(overs_r, overs_u, rows_dec):
    overrides = defaultdict(lambda: set())
    for d, od in overs_r:
        overrides[d].add(od)
        overrides[od].add(d)

    for d, od in overs_u:
        od = rows_dec[od]
        if od:
            overrides[d].add(od)
            overrides[od].add(d)

    return {k: list(sorted(v)) for k, v in overrides.items()}


def set_caller_callee(callgraph, caller, callee,
                      line, col, virtual, overrides):
    if callee:
        if virtual:
            if callee in overrides:
                callee = overrides[callee]
        if isinstance(callee, list) and len(callee) == 1:
            callee = callee[0]
        callgraph[caller].append([callee, line, col])


def get_callgraph(cg_r, cg_u, rows_def, rows_dec, overrides):
    callgraph = defaultdict(lambda: list())

    for caller, callee, line, col, virtual in cg_r:
        set_caller_callee(callgraph, caller, callee,
                          line, col, virtual, overrides)

    for caller, callee, line, col, virtual in cg_u:
        if callee and rows_dec[callee]:
            callee = rows_dec[callee]
            if callee and rows_def[callee]:
                set_caller_callee(callgraph, caller, callee,
                                  line, col, virtual, overrides)

    return callgraph


def get_data(rows_def, files, callgraph, rev):
    for caller, callees in callgraph.items():
        rows_def[caller][-1].append(callees)

    return {'files': files,
            'defs': rows_def,
            'revision': rev}


def mk_data(dbpath, rev, out='', compress=False):
    conn = sqlite3.connect(dbpath)
    tables = get_tables(conn)
    conn.close()

    rows_def, fun2rowid, files = get_defs(tables['definitions'])
    rows_dec = get_decls(tables['declarations'], fun2rowid)
    overrides = get_overrides(tables['overrides_resolved'],
                              tables['overrides_unresolved'],
                              rows_dec)
    callgraph = get_callgraph(tables['callgraph_resolved'],
                              tables['callgraph_unresolved'],
                              rows_def,
                              rows_dec,
                              overrides)
    data = get_data(rows_def, files, callgraph, rev)

    if out:
        with open(out, 'w') as Out:
            if compress:
                cdata = {'data': utils.compress(data)}
                json.dump(cdata, Out, separators=[',', ':'])
            else:
                json.dump(data, separators=[',', ':'])

            return data
    else:
        return data
