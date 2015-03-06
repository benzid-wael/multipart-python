# -*- coding: utf-8 -*-

import multipart
import unittest
import hashlib
import random


class TestMultipart(unittest.TestCase):

    def test_badConstruction(self):
        self.assertRaises(TypeError, multipart.Parser)
        self.assertRaises(TypeError, multipart.Parser, 'foo')
        self.assertRaises(TypeError, multipart.Generator)

    def test_create(self):
        parser = multipart.Parser('x', '')
        # This test ensures that a multipart parser was created successfully
        self.assertTrue(isinstance(parser, multipart.Parser))

    def test_0(self):
        boundary = '------------------------------75766a6a01a6'
        for headers, data in multipart.Parser(boundary,
                                              open('tests/fake_stream0.txt')):

            for header in headers:
                print('HEADER={}'.format(header))

            for d in data:
                pass  # print 'LEN=' + str(len(d))

    def test_1(self):

        def wrapper(i):
            for j in i:
                for k in j:
                    yield k

        boundary = '------------------------------75766a6a01a6'
        for headers, data in multipart.Parser(boundary,
                                              open('tests/fake_stream0.txt')):
            for header in headers:
                print('HEADER={}'.format(header))

            for d in data:
                pass  # print 'LEN=' + str(len(d))

    def test_3(self):
        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             '0a5e6db148276bc7e3d5854179ecbf6e',  # random1
             '0a9fdb5ca02b919cb647f5c726d519b6',  # random2
             '686c70dee5e998275508d88c0f3390c4',  # random3
             '9b5ebc254dc324aae1f7366b1d01cb8f',  # random4
             '74dbb0e2ffdab211004aff8a98c58906',  # random5
             '62250b57c1f145f2baf212df3dab4945',  # random6
             'de542ac70dd1f67d9b2b8fb25004d23d',  # random7
             'b20b7cecab7ec5b610a3748431a78d34']  # random8
        boundary = '------------------------------8f9710048d91'
        for part, digest in zip(multipart.Parser(boundary,
                                                 open('tests/fake_stream1.txt')), digests):
            headers, data = part
            for header in headers:
                print 'HEADER=' + str(header)

            chksum = hashlib.md5()
            for d in data:
                # print 'LEN=' + str(len(d))
                chksum.update(d)

        self.assertTrue(chksum.hexdigest() == digest)

    def test_7(self):
        random.seed(1)

        def wrapper(i):
            allOfIt = i.read()
            while len(allOfIt) != 0:
                x = min(len(allOfIt), random.randint(4, 64))
                yield allOfIt[:x]
                allOfIt = allOfIt[x:]

        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             '0a5e6db148276bc7e3d5854179ecbf6e',  # random1
             '0a9fdb5ca02b919cb647f5c726d519b6',  # random2
             '686c70dee5e998275508d88c0f3390c4',  # random3
             '9b5ebc254dc324aae1f7366b1d01cb8f',  # random4
             '74dbb0e2ffdab211004aff8a98c58906',  # random5
             '62250b57c1f145f2baf212df3dab4945',  # random6
             'de542ac70dd1f67d9b2b8fb25004d23d',  # random7
             'b20b7cecab7ec5b610a3748431a78d34']  # random8

        boundary = '------------------------------8f9710048d91'
        for part, digest in zip(
                multipart.Parser(
                    boundary, wrapper(open('tests/fake_stream1.txt'))),
                digests):
            headers, data = part
            for header in headers:
                print('HEADER={}'.format(header))

            chksum = hashlib.md5()
            length = 0
            for d in data:
                # print 'LEN=' + str(len(d))
                chksum.update(d)
                length += len(d)

            print('length: {}'.format(length))
            self.assertTrue(chksum.hexdigest() == digest)

    def test_4(self):
        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             '0a5e6db148276bc7e3d5854179ecbf6e',  # random1
             '0a9fdb5ca02b919cb647f5c726d519b6',  # random2
             '686c70dee5e998275508d88c0f3390c4',  # random3
             '9b5ebc254dc324aae1f7366b1d01cb8f',  # random4
             '74dbb0e2ffdab211004aff8a98c58906',  # random5
             '62250b57c1f145f2baf212df3dab4945',  # random6
             'de542ac70dd1f67d9b2b8fb25004d23d',  # random7
             'b20b7cecab7ec5b610a3748431a78d34']  # random8

        boundary = '------------------------------8f9710048d91'
        for part, digest in zip(multipart.Parser(boundary,
                                                 open('tests/fake_stream1.txt')),
                                digests):
            _, data = part
            chksum = hashlib.md5()
            for d in data:
                chksum.update(d)

            self.assertTrue(chksum.hexdigest() == digest)

    def test_5(self):
        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             '0a5e6db148276bc7e3d5854179ecbf6e',  # random1
             '0a9fdb5ca02b919cb647f5c726d519b6',  # random2
             '686c70dee5e998275508d88c0f3390c4',  # random3
             '9b5ebc254dc324aae1f7366b1d01cb8f',  # random4
             '74dbb0e2ffdab211004aff8a98c58906',  # random5
             '62250b57c1f145f2baf212df3dab4945',  # random6
             'de542ac70dd1f67d9b2b8fb25004d23d',  # random7
             'b20b7cecab7ec5b610a3748431a78d34']  # random8
        skip = False
        boundary = '------------------------------8f9710048d91'
        for part, digest in zip(multipart.Parser(boundary,
                                                 open('tests/fake_stream1.txt')),
                                digests):
            if skip:
                skip = not Skip
                continue

            _, data = part
            chksum = hashlib.md5()
            for d in data:
                chksum.update(d)

            self.assertTrue(chksum.hexdigest() == digest)

    def test_6(self):
        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             'cd880b726e0a0dbd4237f10d15da46f4',
             '37b51d194a7513e45b56f6524f2d51f2']
        skip = False
        boundary = '------------------------------6f84f6ecbb53'
        for part, digest in zip(multipart.Parser(boundary,
                                                 open('tests/fake_stream2.txt')),
                                digests):
            if skip:
                skip = not Skip
                continue

            _, data = part
            chksum = hashlib.md5()
            for d in data:
                chksum.update(d)

            self.assertTrue(chksum.hexdigest() == digest)

    def test_multiline_headers(self):
        digests = \
            ['e3fb78474a477c528d92d01d4fc85a04',  # random0
             'cd880b726e0a0dbd4237f10d15da46f4',
             '37b51d194a7513e45b56f6524f2d51f2']
        skip = False
        boundary = '------------------------------6f84f6ecbb53'
        for part, digest in zip(multipart.Parser(boundary,
                                                 open('tests/fake_stream3.txt')),
                                digests):
            if skip:
                skip = not Skip
                continue

            _, data = part
            chksum = hashlib.md5()
            for d in data:
                chksum.update(d)

            self.assertTrue(chksum.hexdigest() == digest)


if __name__ == '__main__':
    unittest.main()
