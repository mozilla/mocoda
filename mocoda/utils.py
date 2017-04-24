# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import base64
import json
import zlib


def compress(data):
    cdata = json.dumps(data, separators=[',', ':'])
    cdata = zlib.compress(cdata, 9)
    cdata = base64.b64encode(cdata)

    return cdata


def decompress(data):
    cdata = base64.b64decode(data)
    cdata = zlib.decompress(cdata)
    cdata = json.loads(cdata)

    return cdata
