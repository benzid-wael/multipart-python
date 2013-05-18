#include <Python.h>
#include "multipart_parser.h"
#include <structmember.h>
#include "iso646.h"
#include "stdbool.h"
#include "stdio.h"

typedef struct {
	PyObject_HEAD
	multipart_parser * parser;
	
	char * headerFieldInProgress;
	size_t headerFieldLength;
	size_t headerFieldSize;
	
	char * headerValueInProgress;
	size_t headerValueLength;
	size_t headerValueSize;
	
} multipart_Parser;

static PyMethodDef Parser_methods[] = { {NULL} };
static PyMemberDef Parser_members[] = { {NULL} };

static PyObject* Parser_new(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
	multipart_Parser * self = (multipart_Parser*)type->tp_alloc(type,0);
	
	if(self!=NULL)
	{
		self->parser = NULL;
		static const int STARTING_SIZE = 32;
		self->headerFieldInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerFieldLength = 0;
		self->headerFieldSize = STARTING_SIZE;
		
		self->headerValueInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerValueLength = 0;
		self->headerValueSize = STARTING_SIZE;
		
		if( self->headerFieldInProgress == NULL or self->headerValueInProgress == NULL)
		{
			PyMem_Free(self->headerFieldInProgress);
			PyMem_Free(self->headerValueInProgress);
			PyMem_Free(self);
			return PyErr_NoMemory();
		}
	}
	
	return (PyObject*)self;
}

static void Parser_dealloc(multipart_Parser * self)
{
	if(self->parser)
	{
		multipart_parser_free(self->parser);
	}
}
  
static int multipart_Parser_on_header_field(void * actor, const char * data, size_t length)
{
	printf("header field %zu bytes:%.*s\n",length,(int)length,data);
	multipart_Parser * const self = data;
	const int requiredSize = length + self->headerFieldLength + 1;
	if(requiredSize > self->headerFieldSize)
	{
		void * const newMem = PyMem_Realloc(self->headerFieldInProgress,requiredSize);
		if(not newMem)
		{
			return 1;
		}
		self->headerFieldInProgress = newMem;
		self->headerFieldSize = requiredSize;
	}
	memcpy(self->headerFieldInProgress + self->headerFieldLength, data,length);
	self->headerFieldLength += length;
	return 0;
}

static int multipart_Parser_on_header_value(void * actor, const char * data, size_t length)
{
	printf("header value %zu bytes:%.*s\n",length,(int)length,data);
	return 0;
}

static int multipart_Parser_on_part_data(void * actor , const char * data, size_t length)
{
	printf("part data %zu bytes:%.*s\n",length,(int)length,data);
	return 0;
}

static int multipart_Parser_on_header_value_end(void * actor)
{
	printf("header_value_end\n");
	return 0;
}


static multipart_parser_settings callbackRegistry = 
{
  multipart_Parser_on_header_field, //multipart_data_cb on_header_field;
  multipart_Parser_on_header_value, //multipart_data_cb on_header_value;
  multipart_Parser_on_part_data, //multipart_data_cb on_part_data;

  multipart_Parser_on_header_value_end, //multipart_notify_cb on_header_value_end;
  NULL, //multipart_notify_cb on_part_data_begin;
  NULL, //multipart_notify_cb on_headers_complete;
  NULL, //multipart_notify_cb on_part_data_end;
  NULL //multipart_notify_cb on_body_end;
};

static int Parser_init(multipart_Parser * const self, PyObject * args, PyObject * kwds)
{
	char const * boundary;
	static char * kwlist[] = {"boundary",NULL};
	if( not PyArg_ParseTupleAndKeywords(args,kwds,"s",kwlist,&boundary) )
	{
		return -1;
	}
	
	self->parser = multipart_parser_init(boundary,&callbackRegistry);
	if( not self->parser )
	{
		PyErr_SetString(PyExc_MemoryError,"multipart_parser_init returned NULL");
		return -1;
	}
	
	multipart_parser_set_data(self->parser,(void*)self);
	
	
	return 0;
}

static PyObject* Parser_call(multipart_Parser * const self, PyObject * args, PyObject * kwds)
{
	static char * kwlist[] = {"fin",NULL};
	PyObject * fin;
	if ( not PyArg_ParseTupleAndKeywords(args,kwds,"O",kwlist,&fin))
	{
		return NULL;
	}
	
	PyObject * const iter = PyObject_GetIter(fin);
	
	if(not iter) 
	{
		PyErr_SetString(PyExc_AttributeError,"argument must be iterable");
		return NULL;
	}
	
	size_t bytesParsed = 0;
	while(true)
	{
		PyObject * const i = PyIter_Next(iter);
		if(i == NULL)
		{
			break;
		}
		PyObject * const bytes = PyObject_Bytes(i);
		Py_DECREF(i);
		
		if( not bytes )
		{
			PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
			Py_DECREF(iter);
			return NULL;
			
		}
		
		char const * raw;
		Py_ssize_t length;
		
		if(-1==PyString_AsStringAndSize(bytes,&raw,&length))
		{
			PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
			Py_DECREF(iter);
			Py_DECREF(bytes);
			return NULL;
		}
		
		
		const size_t result = multipart_parser_execute(self->parser,raw,length) ;
		bytesParsed += result;
		if( length != result )
		{
			char errmsg[64];
			snprintf(errmsg,
		 	         sizeof(errmsg),
		 	         "input not multipart, failed on byte %zu",
		 	         bytesParsed);
		 	errmsg[sizeof(errmsg)-1]='\0';
		 	
			PyErr_SetString(PyExc_ValueError, errmsg);
			Py_DECREF(iter);
			Py_DECREF(bytes);
			return NULL;
		}
		Py_DECREF(bytes);
	}
	Py_DECREF(iter);
	
	Py_RETURN_NONE;
	
}

static PyTypeObject multipart_ParserType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
    "multipart.Parser",             /*tp_name*/
    sizeof(multipart_Parser), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Parser_dealloc,/*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    Parser_call,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "Parser objects",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Parser_methods,             /* tp_methods */
    Parser_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Parser_init,      /* tp_init */
    0,                         /* tp_alloc */
    Parser_new,                 /* tp_new */
};

static PyMethodDef multipart_methods[] = {
	{NULL}
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initmultipart(void) 
{
    PyObject* m;

    multipart_ParserType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&multipart_ParserType) < 0)
    {
        return;
    }

    m = Py_InitModule3("multipart", multipart_methods,
                       "Example module that creates an extension type.");

    Py_INCREF(&multipart_ParserType);
    PyModule_AddObject(m, "Parser", (PyObject *)&multipart_ParserType);
}
