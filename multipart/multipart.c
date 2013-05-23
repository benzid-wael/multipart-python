#include <Python.h>

#include <structmember.h>
#include "iso646.h"
#include "stdbool.h"
#include "stdio.h"
#include "string.h"
#include "multipart_Parser.h"
#include "multipart_Generator.h"

PyObject * multipartModule = NULL;



static PyMethodDef multipart_methods[] = {
	
	{NULL,NULL,0,NULL}
};



#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initmultipart(void) 
{
    

    multipart_GeneratorType.tp_new = &PyType_GenericNew;
    
    if (PyType_Ready(&multipart_ParserType) < 0 or PyType_Ready(&multipart_GeneratorType) < 0)
    {
        return;
    }

    multipartModule = Py_InitModule3("multipart", multipart_methods,
                       "Example module that creates an extension type.");

    Py_INCREF(&multipart_ParserType);
    
    PyModule_AddObject(multipartModule, "Parser", (PyObject *)&multipart_ParserType);
    PyModule_AddObject(multipartModule, "Generator", (PyObject *)&multipart_GeneratorType);
}
