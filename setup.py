
from setuptools import setup, Extension

module1 = Extension('multipart',
                    sources = ['multipart/multipart_parser.c','multipart/multipart.c'])

setup (name = 'multipart',
       version = '0.1',
       description = 'This is a demo package',
       ext_modules = [module1],
       test_suite='tests')
