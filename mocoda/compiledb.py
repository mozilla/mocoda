# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import shutil
import tempfile
import hglib
import json
from . import finalizedb
from . import mergedb
from . import utils


def get_mach(root):
    mach_path = os.path.join(root, 'build/mach_bootstrap.py')
    import imp
    with open(mach_path, 'r') as fh:
        imp.load_module('mach_bootstrap', fh, mach_path,
                        ('.py', 'r', imp.PY_SOURCE))
    import mach_bootstrap
    return mach_bootstrap.bootstrap(root)


def env(restore=False, __env=[]):
    if restore:
        os.environ.clear()
        os.environ.update(__env[0])
    else:
        __env.append(dict(os.environ))


def tmp(create=True, __tmp=[]):
    if create:
        __tmp.append(tempfile.mkdtemp())
        return __tmp[0]
    else:
        if __tmp:
            shutil.rmtree(__tmp[0])
        return None


def stdout(restore=False, __stdout=[]):
    if restore:
        sys.stdout = __stdout[0]
    else:
        __stdout.append(sys.stdout)
        sys.stdout = open('/dev/null', 'w')


def pre():
    env(restore=False)
    tmpdir = os.environ.get('MOCODA_TMPDIR', '')
    if not tmpdir:
        tmpdir = tmp(create=True)

    stdout(restore=False)

    root = os.environ['MOCODA_ROOT']
    if not root.endswith(os.sep):
        root += os.sep
        os.environ['MOCODA_ROOT'] = root

    os.chdir(root)
    lock = os.path.join(tmpdir, 'lock')
    open(lock, 'a').close()
    os.environ['MOCODA_LOCK'] = lock
    output = os.environ.get('MOCODA_OUTPUT', '')

    return root, tmpdir, output


def post():
    env(restore=True)
    tmp(create=False)
    stdout(restore=True)


def compile_tree(root, rev, output, db):
    r = get_mach(root).run(['build', 'pre-export'])
    if r != 0:
        r = get_mach(root).run(['configure'])
        if r != 0:
            raise '\'mach configure\' failed.'
        get_mach(root).run(['build', 'pre-export'])

    get_mach(root).run(['build', 'export'])

    os.environ['MOCODA_DATABASE'] = db
    get_mach(root).run(['build', 'compile'])

    return finalizedb.mk_data(db, rev, output, compress=True)


def get_logs(client, rev_start, rev_end):
    if rev_end:
        revrange = '{}:{}'.format(rev_start, rev_end)
    else:
        revrange = '{}:tip'.format(rev_start)
    return client.log(revrange=revrange, nomerges=True)


def update(rev_start=None, rev_end=None, clobber=False, update=False):
    root, tmpdir, output_dir = pre()
    compile_data = get_data_from_cache()
    if rev_start is None:
        if compile_data:
            rev_start = compile_data['revision']
        else:
            raise 'No previous compilation data'

    client = hglib.open(root)
    if update:
        client.pull(update=True)
    logs = get_logs(client, rev_start, rev_end)

    output_file = ''
    for i, log in enumerate(reversed(logs)):
        if clobber and i == 0:
            get_mach(root).run(['clobber'])

        _, rev, _, _, author, desc, _ = log
        client.update(rev=rev)

        if output_dir:
            f = 'compilation_data_{}.json'.format(rev)
            output_file = os.path.join(output_dir, f)
        db = os.path.join(tmpdir, 'database_{}.sqlite'.format(rev))
        data = compile_tree(root, rev, output_file, db)
        if compile_data:
            patch = client.export([rev])
            compile_data, changes = mergedb.merge(patch, compile_data, data)
            push_changes(rev, author, desc, changes)
        else:
            compile_data = data

    put_data_in_cache(compile_data)
    post()


def react():
    update(clobber=False, update=True)


def prepare(rev=None):
    root, tmpdir, output_dir = pre()
    client = hglib.open(root)
    client.pull(update=True)
    if rev:
        client.update(rev=rev)
    else:
        rev = client.log(limit=1)[0][1]
    get_mach(root).run(['clobber'])
    db = os.path.join(tmpdir, 'database_{}.sqlite'.format(rev))
    data = compile_tree(root, rev, '', db)
    put_data_in_cache(data)
    post()


def get_data_from_cache():
    path = os.environ.get('MOCODA_PATH_CACHE', '')
    if path:
        path = os.path.join(path, 'compilation_data.json')
        with open(path, 'r') as In:
            data = json.load(In)['data']
            return utils.decompress(data)
    return None


def put_data_in_cache(data):
    path = os.environ.get('MOCODA_PATH_CACHE', '')
    if path:
        path = os.path.join(path, 'compilation_data.json')
        with open(path, 'w') as Out:
            data = {'data': utils.compress(data)}
            json.dump(data, Out, separators=[',', ':'])


def push_changes(rev, author, desc, changes):
    with open('/tmp/changes.log', 'a') as Out:
        changes = {'revision': rev,
                   'author': author,
                   'desc': desc,
                   'changes': changes}
        Out.write(str(changes))
        Out.write('\n')


def update_repo():
    root, tmpdir = pre()
    client = hglib.open(root)
    client.pull(update=True)
    log = client.log(limit=1)[0]
    rev = log[1]
    compile_data = get_data_from_cache()

    db = os.path.join(tmpdir, 'database_{}.sqlite'.format(rev))
    data = compile_tree(root, '', db)
    patch = client.export([rev])
    compile_data, changes = mergedb.merge(patch, compile_data, data)

    put_data_in_cache(compile_data)
    push_changes(changes)

    post()


# if __name__ == '__main__':
#    prepare('e7ac597e55b6')
#    update(rev_start='e6a9a5606617', rev_end='f44860760c1f')
