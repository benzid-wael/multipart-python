
from setuptools import setup, Extension

sources = [
    'multipart/multipart_parser.c',
    'multipart/multipart.c',
    'multipart/multipart_Parser.c',
    'multipart/multipart_Generator.c'
]

multipart = Extension('multipart', sources=sources,
                      extra_compile_args=['-std=gnu99', '-O0'])

setup(
    name='multipart',
    version='0.1',
    description='This is a demo package',
    ext_modules=[multipart],
    test_suite='tests'
)
