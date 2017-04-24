# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
from mozillapulse import consumers
from datetime import datetime
from mozillapulse.config import PulseConfiguration

import compiledb


class MyCodeConsumer(consumers.GenericConsumer):

    def __init__(self, **kwargs):
        super(MyCodeConsumer, self).__init__(
            PulseConfiguration(**kwargs), ['exchange/hgpushes/v1'], **kwargs)

# Define a callback
def got_message(body, message):
    print(body)
    compiledb.react()
    message.ack()

pulse = MyCodeConsumer(user='*****', password='*****')
pulse.configure(topic=['mozilla-central'], callback=got_message)

os.environ['MOCODA_ROOT'] = '/home/calixte/dev/mozilla/mozilla-central.hg/'
os.environ['MOCODA_PATH_CACHE'] = '/home/calixte/sqlite/tutu'
os.environ['MOCODA_TMPDIR'] = '/home/calixte/sqlite/tutu'
os.environ['MOCODA_OUTPUT'] = '/home/calixte/sqlite/tutu'

compiledb.prepare()
pulse.listen()
