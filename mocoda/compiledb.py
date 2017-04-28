# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import tempfile
import hglib
import json
import subprocess
from distutils.spawn import find_executable
import logging
from . import finalizedb
from . import mergedb
from . import utils


logger = logging.getLogger(__name__)


def mach(root, mach_args, check_exit=True):
    """
    Run a command in the repo through subprocess
    Supports optional gecko-env
    """
    assert isinstance(mach_args, list)
    cmd = []

    # Use gecko env when available
    gecko_env = find_executable('gecko-env')
    if gecko_env is not None:
        cmd = [gecko_env, ]

    # Add local mach to required command
    cmd += ['./mach', ] + mach_args
    logger.debug('Running command {}'.format(' '.join(cmd)))

    # Run command with env
    proc = subprocess.Popen(cmd, cwd=root)
    exit = proc.wait()
    if exit != 0 and check_exit is True:
        raise Exception('Invalid exit code for command {}: {}'.format(cmd, exit))  # NOQA

    return exit


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


def pre():
    env(restore=False)
    tmpdir = os.environ.get('MOCODA_TMPDIR', '')
    if not tmpdir:
        tmpdir = tmp(create=True)

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


def compile_tree(root, rev, output, db):
    r = mach(root, ['build', 'pre-export'], check_exit=False)
    if r != 0:
        mach(root, ['configure'])
        mach(root, ['build', 'pre-export'])

    mach(root, ['build', 'export'])

    os.environ['MOCODA_DATABASE'] = db
    mach(root, ['build', 'compile'])

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
            raise Exception('No previous compilation data')

    client = hglib.open(root)
    if update:
        client.pull(update=True)
    logs = get_logs(client, rev_start, rev_end)

    output_file = ''
    for i, log in enumerate(logs):
        if clobber and i == 0:
            mach(root, ['clobber'])

        _, rev, _, _, author, desc, _ = log
        rev = rev.decode('ascii')
        logger.info('Compile for revision {}'.format(rev))
        client.update(rev=rev)

        if output_dir:
            f = 'compilation_data_{}.json'.format(rev)
            output_file = os.path.join(output_dir, f)
        db = os.path.join(tmpdir, 'database_{}.sqlite'.format(rev))
        data = compile_tree(root, rev, output_file, db)
        if compile_data:
            patch = client.export([str.encode(rev)])
            patch = patch.decode('ascii')
            compile_data, changes = mergedb.merge(patch, compile_data, data)
            push_changes(rev, author, desc, changes)
        else:
            compile_data = data

    put_data_in_cache(compile_data)
    post()


def prepare(rev=None, parent=False):
    root, tmpdir, output_dir = pre()
    client = hglib.open(root)
    client.pull(update=True)
    if parent and rev:
        parents = client.parents(rev=rev)
        if parents:
            rev = parents[0][1]
            rev = rev.decode('ascii')
        else:
            raise Exception('No parent for revision {} !'.format(rev))  # NOQA

    if rev:
        client.update(rev=rev)
    else:
        rev = client.log(limit=1)[0][1]
        rev = rev.decode('ascii')

    mach(root, ['clobber'])
    mach(root, ['configure'])
    db = os.path.join(tmpdir, 'database_{}.sqlite'.format(rev))
    data = compile_tree(root, rev, '', db)

    put_data_in_cache(data)
    post()


def react(msg):
    if isinstance(msg, dict):
        rev = msg['payload']['heads'][0]
    else:
        rev = msg

    prepare(rev=rev, parent=True)
    update(rev_start=rev, clobber=False, update=False)


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
