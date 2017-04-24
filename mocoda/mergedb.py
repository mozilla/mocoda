# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from collections import defaultdict
import whatthepatch


def get_path(path):
    if path.startswith('a/') or path.startswith('b/'):
        return path[2:]
    return path


def get_files(patch):
    files = set()
    for diff in patch:
        files.add(get_path(diff.header.old_path))
        files.add(get_path(diff.header.new_path))
    return files


def get_data(patch_files, source):
    data = defaultdict(lambda: dict())
    files = source['files']
    defs = source['defs']
    for i in range(1, len(defs)):
        l = defs[i]
        f = files[l[0]]
        if f in patch_files:
            # data[filename][funname] = [begin, end]
            data[f][l[1]] = l[2:-1]
    return data


def get_function(data, line):
    for funname, be in data.items():
        if be[0] <= line <= be[1]:
            yield funname


def get_changes(patch, before, after):
    results = {}
    for diff in patch:
        touched = set()
        path_before = get_path(diff.header.old_path)
        path_after = get_path(diff.header.new_path)
        funs_before = before[path_before]
        funs_after = after[path_after]
        for change in diff.changes:
            if change[0] is None:
                # new line is inserted
                line = change[1]
                touched |= set(get_function(funs_after, line))
            elif change[1] is None:
                # line is removed
                line = change[0]
                touched |= set(get_function(funs_before, line))

        # TODO: if we have the same funname but for different
        # functions (possible with meth in anonymous classes)
        # then we could have a pb
        funs_before_k = set(funs_before.keys())
        funs_after_k = set(funs_after.keys())

        funs_before_k &= touched
        funs_after_k &= touched

        # added funs are in touched and in funs_after and not in funs_before
        added = funs_after_k - funs_before_k
        added = [[f, funs_after[f][0], funs_after[f][1]] for f in added]

        # removed funs are in touched and not in funs_after and in funs_before
        rem = funs_before_k - funs_after_k
        rem = [[f, funs_before[f][0], funs_before[f][1]] for f in rem]

        # modified funs are in touched and in funs_after and in funs_before
        modif = funs_before_k & funs_after_k
        modif = [[f, funs_before[f][0], funs_before[f][1]] for f in modif]

        # TODO: the path can changed... so not the same after and before
        if added or rem or modif:
            results[path_before] = {'added': added,
                                    'removed': rem,
                                    'modified': modif}

    return results


def rewrite(source):
    files = source['files']
    defs = source['defs']
    for i in range(1, len(defs)):
        d = defs[i]
        d[0] = files[d[0]]


def merge(patch, before, after):
    patch = list(whatthepatch.parse_patch(patch))
    patch_files = get_files(patch)
    before_data = get_data(patch_files, before)
    after_data = get_data(patch_files, after)
    changes = get_changes(patch, before_data, after_data)

    # now we merge
    p = set(patch_files)
    b = p & set(before['files'])
    a = p & set(after['files'])
    deleted = b - a
    inserted = a - b
    modified = a & b
    changed = inserted | modified
    touched = deleted | changed

    rewrite(before)
    rewrite(after)

    before_defs = before['defs']
    after_defs = after['defs']
    toremove = []
    for i in range(1, len(before_defs)):
        d = before_defs[i]
        if d[0] in touched:
            toremove.append(i)

    for i in reversed(toremove):
        del before_defs[i]

    for i in range(1, len(after_defs)):
        d = after_defs[i]
        if d[0] in changed:
            before_defs.append(d)

    files = set()
    for i in range(1, len(before_defs)):
        d = before_defs[i]
        files.add(d[0])
    files = list(files)

    file2id = {f: n for n, f in enumerate(files)}
    for i in range(1, len(before_defs)):
        d = before_defs[i]
        d[0] = file2id[d[0]]

    before['files'] = files
    before['revision'] = after['revision']

    return before, changes


def debug(root, path, revrange, compile_data):
    import hglib
    import json
    import os
    from . import utils

    def get(rev):
        f = 'compilation_data_{}.json'.format(rev)
        p = os.path.join(path, f)
        data = json.load(open(p, 'r'))['data']
        return utils.decompress(data)

    client = hglib.open(root)
    logs = client.log(revrange=revrange)
    logs = list(reversed(logs))
    for log in logs:
        rev = log[1]
        patch = client.export([rev], git=True)
        after = get(rev)
        compile_data, changes = merge(patch, compile_data, after)
        print(rev)
        print(changes)
