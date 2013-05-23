## Multipart form data parser for python

A python module of [Igor Afonov's](http://iafonov.github.com) [multipart-parser-c](https://github.com/iafonov/multipart-parser-c).

This module leverages the state machine written in C 
code to provide a multipart/form-data parser to python code. 
This module reads from any iterable object passed to it. It avoids the 
need to read the entire structure into memory by returning iterators.
This allows stream-to-disk or stream-to-database operations to be
implemented in Python code only.

Example usage of parsing a POST request and saving
each part to disk is shown in `example/saveUpload.py`. It starts
an HTTP server on port 8080 of the localhost that saves all parts of the
multipart upload to separate files on disk.

				
