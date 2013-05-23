## Multipart form data parser

A python module of [Igor Afonov's](http://iafonov.github.com) [multipart-parser-c](https://github.com/iafonov/multipart-parser-c).

This module leverages the state machine written in C 
code to provide a multipart/form-data parser to python code. 
This module reads from any iterable object passed to it. It avoids the 
need to read the entire structure into memory by returning iterators.
This allows stream-to-disk or stream-to-database operations to be
implemented in Python code only.

Example usage parsing a saved body of a POST request and saving
each part to disk is available in example/.

				
