#include <Python.h>
#include "multipart_parser.h"
#include <structmember.h>
#include "iso646.h"
#include "stdbool.h"
#include "stdio.h"
#include "string.h"

static PyObject * multipartModule = NULL;

typedef struct {
	PyObject_HEAD
	multipart_parser * parser;
	
	//The field of the header currently being parsed
	char * headerFieldInProgress;
	size_t headerFieldLength;
	size_t headerFieldSize;
	
	//The value of the header currently being parsed
	char * headerValueInProgress;
	size_t headerValueLength;
	size_t headerValueSize;
	
	char * data;
	
	//The callable object that reads data
	PyObject * readIterator;
	//The number of bytes read from the iterator
	size_t bytesParsed;

	//Set to true if currently in the body of a part
	bool headersComplete;
	//Set to true if done with the multipart
	bool dataComplete;

	PyObject ** iteratorQueue;
	size_t iteratorQueueLengthInPairs;
	size_t iteratorQueueSizeInPairs;
	ssize_t currentIteratorPair;
	ssize_t outgoingIteratorPair;
	
} multipart_Parser;



static PyObject* Parser_new(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
	multipart_Parser * self = (multipart_Parser*)type->tp_alloc(type,0);
	
	if(self!=NULL)
	{
		self->iteratorQueue = NULL;
		self->iteratorQueueLengthInPairs = 0;
		self->iteratorQueueSizeInPairs = 0;
		self->currentIteratorPair = -1;
		self->outgoingIteratorPair = 0;
		
		self->parser = NULL;
		self->bytesParsed = 0;

		self->headersComplete = true;
		self->dataComplete = false;
		self->readIterator = NULL;
		static const int STARTING_SIZE = 2;
		self->headerFieldInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerFieldLength = 0;
		self->headerFieldSize = STARTING_SIZE;
		
		self->headerValueInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerValueLength = 0;
		self->headerValueSize = STARTING_SIZE;
		
		//If memory allocation fails in either case,
		//give up.
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
	
	PyMem_Free(self->headerFieldInProgress);
	PyMem_Free(self->headerValueInProgress);
	
	Py_XDECREF(self->readIterator);
	
	for(size_t i = 0;i < self->iteratorQueueLengthInPairs ; i+=2)
	{
		Py_XDECREF(self->iteratorQueue[i]);
		Py_XDECREF(self->iteratorQueue[i+1]);
	}
	
	PyMem_Free(self->iteratorQueue);
}
  
static bool queuePush(multipart_Parser * const self)
{
	
	self->currentIteratorPair += 1;
	//TODO check for exceeding bounds of queue
	
	PyObject * const generatorObject = PyObject_GetAttrString(multipartModule,"Generator");
	
	if(not generatorObject)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find multipart.Generator");
		return false;
	}
	
	PyObject * const headerIterator = PyObject_Call(generatorObject,NULL,NULL);
	PyObject * const bodyIterator = PyObject_Call(generatorObject,NULL,NULL);
	
	if(not headerIterator or not bodyIterator)
	{
		Py_XDECREF(headerIterator);
		Py_XDECREF(bodyIterator);
		
		self->currentIteratorPair-=1;
		
		PyErr_NoMemory();
		return false;
	}
	
	self->iteratorQueue[self->currentIteratorPair*2] = headerIterator;
	self->iteratorQueue[self->currentIteratorPair*2+1] = bodyIterator;
	
	return true;
}
  
static int multipart_Parser_on_header_field(void * actor, const char * data, size_t length)
{
	
	printf("header field %zu bytes:%.*s\n",length,(int)length,data);
	multipart_Parser * const self = actor;
	
	//Check for this being headers on a new part
	if(self->headersComplete)
	{
		printf("new part\n");
		if(not queuePush(self))
		{
			return 1;
		}
		self->headersComplete = false;
	}
	
	
	//Calculate the requried size after adding the new data
	const int requiredSize = length + self->headerFieldLength + 1;
	
	//Reallocate memory if neccessary
	if(requiredSize > self->headerFieldSize)
	{
		void * const newMem = PyMem_Realloc(self->headerFieldInProgress,requiredSize);
		if(not newMem)
		{
			//Return -1 to indicate failure in callback to the parser.
			return -1;
		}
		self->headerFieldInProgress = newMem;
		self->headerFieldSize = requiredSize;
	}
	
	//Copy the memory onto the field and update the length;
	memcpy(self->headerFieldInProgress + self->headerFieldLength, data,length);
	self->headerFieldLength += length;
	return 0;
}

static int multipart_Parser_on_header_value(void * actor, const char * data, size_t length)
{
	printf("header value %zu bytes:%.*s\n",length,(int)length,data);
	
	multipart_Parser * const self = actor;
	
	const int requiredSize = length + self->headerValueLength + 1;
	
	if(requiredSize > self->headerValueSize)
	{
		void * const newMem = PyMem_Realloc(self->headerValueInProgress,requiredSize);
		if(not newMem)
		{
			return -1;
		}
		
		self->headerValueInProgress = newMem;
		self->headerValueSize = requiredSize;
	}
	
	memcpy(self->headerValueInProgress + self->headerValueLength, data, length);
	self->headerValueLength += length;
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
	multipart_Parser * const self = actor;
	printf("%.*s=%.*s\n",(int)self->headerFieldLength,self->headerFieldInProgress,
	(int)self->headerValueLength,self->headerValueInProgress);
	
	self->headerValueLength = 0;
	self->headerFieldLength = 0;
	return 0;
}

static int multipart_Parser_on_headers_complete(void * actor)
{
	multipart_Parser * const self = actor;
	self->headersComplete = true;
	
	return 0;
}

static int multipart_Parser_on_part_data_end(void * actor)
{
	multipart_Parser * const self = actor;
	//self->headersComplete = false;
	
	return 0;
}

static int multipart_Parser_on_body_end(void * actor)
{
	multipart_Parser * const self = actor;
	self->dataComplete = true;
	
	return 0;
}

static multipart_parser_settings callbackRegistry = 
{
  multipart_Parser_on_header_field, //multipart_data_cb on_header_field;
  multipart_Parser_on_header_value, //multipart_data_cb on_header_value;
  multipart_Parser_on_part_data, //multipart_data_cb on_part_data;

  multipart_Parser_on_header_value_end, //multipart_notify_cb on_header_value_end;
  NULL, //multipart_notify_cb on_part_data_begin;
  multipart_Parser_on_headers_complete, //multipart_notify_cb on_headers_complete;
  multipart_Parser_on_part_data_end, //multipart_notify_cb on_part_data_end;
  multipart_Parser_on_body_end //multipart_notify_cb on_body_end;
};

static bool allocateIteratorQueue(multipart_Parser * const self)
{
	static const int STARTING_SIZE = 3;
	const int SIZE_BYTES = sizeof(PyObject*)*STARTING_SIZE*2;
	self->iteratorQueue = PyMem_Malloc(SIZE_BYTES);
	
	if(not self->iteratorQueue)
	{
		PyErr_NoMemory();
		return false;
	}
	bzero(self->iteratorQueue,SIZE_BYTES);
	self->iteratorQueueSizeInPairs = STARTING_SIZE;
	return true;
}

static int Parser_init(multipart_Parser * const self, PyObject * args, PyObject * kwds)
{

	char const * boundary;
	PyObject * fin;
	static char * kwlist[] = {"boundary","fin",NULL};
	if( not PyArg_ParseTupleAndKeywords(args,kwds,"sO",kwlist,&boundary,&fin) )
	{
		return -1;
	}
	
	self->readIterator = PyObject_GetIter(fin);
	
	if(not self->readIterator) 
	{
		PyErr_SetString(PyExc_AttributeError,"fin must be iterable");
		return -1;
	}
	
	self->parser = multipart_parser_init(boundary,&callbackRegistry);
	if( not self->parser )
	{
		PyErr_SetString(PyExc_MemoryError,"multipart_parser_init returned NULL");
		return -1;
	}
	
	multipart_parser_set_data(self->parser,(void*)self);
	
	if(not allocateIteratorQueue(self))
	{
		return -1;
	}

	
	return 0;
}

static PyObject* Parser_iter(PyObject * self)
{
	Py_INCREF(self);
	return self;
}

static PyObject* Parser_read(multipart_Parser * const self, PyObject * unused0, PyObject * unused1)
{

	PyObject * const i = PyIter_Next(self->readIterator);
	
	if(i == NULL)
	{
		Py_RETURN_NONE;
	}
	
	PyObject * const bytes = PyObject_Bytes(i);
	Py_DECREF(i);
	
	if( not bytes )
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		return NULL;
		
	}
	
	char const * raw;
	Py_ssize_t length;
	
	if(-1==PyString_AsStringAndSize(bytes,&raw,&length))
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		Py_DECREF(bytes);
		return NULL;
	}

	const size_t result = multipart_parser_execute(self->parser,raw,length) ;
	self->bytesParsed += result;
	if( length != result )
	{
		char errmsg[64];
		snprintf(errmsg,
				 sizeof(errmsg),
				 "input not multipart, failed on byte %zu",
				 self->bytesParsed);
		errmsg[sizeof(errmsg)-1]='\0';
		
		PyErr_SetString(PyExc_ValueError, errmsg);
		Py_DECREF(bytes);
		return NULL;
	}
	Py_DECREF(bytes);
	
	Py_RETURN_NONE;
}

static PyObject* Parser_iternext(multipart_Parser * const self)
{
	
	if(self->dataComplete)
	{
		return NULL;
	}

	PyObject * const read = PyObject_GetAttrString((PyObject*)self,"read");
	
	if(not read)
	{
		PyErr_SetString(PyExc_NameError,"Could not find self.read");
		return NULL;
	}
	
	if(1!=PyCallable_Check(read))
	{
		PyErr_SetString(PyExc_TypeError,"self.read is not callable");
		return NULL;
	}
	
	//Check to see if the iterator pair being returned is getting ahead
	//of the iterator pair being populated
	while(self->outgoingIteratorPair > self->currentIteratorPair)
	{
		PyObject * const callresult = PyObject_Call(read,NULL,NULL);
		
		if(not callresult)
		{
			return NULL;
		}
		
		Py_DECREF(callresult);
		
		if(self->dataComplete)
		{
			return NULL;
		}
	}
	
	PyObject * retval = PyTuple_Pack(2,self->iteratorQueue[self->outgoingIteratorPair*2],self->iteratorQueue[self->outgoingIteratorPair*2+1]);
	
	if(not retval)
	{
		PyErr_SetString(PyExc_RuntimeError,"Failed to build tuple");
		return NULL;
	}
	
	self->outgoingIteratorPair += 1;
	
	return retval;
}

typedef struct
{
	PyObject_HEAD
	PyObject * actor;
	iternextfunc callback;
}multipart_Generator;

static PyMethodDef Generator_methods[] = { {NULL} };
static PyMemberDef Generator_members[] = { {NULL} };

static int Generator_init(PyTypeObject * self, PyObject *args, PyObject *kwds)
{
	return 0;
}

static PyTypeObject multipart_GeneratorType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
    "multipart.CallbackGenerator",             /*tp_name*/
    sizeof(multipart_Generator), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    0                          ,/*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_ITER,        /*tp_flags*/
    "Generator object",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    Generator_methods,             /* tp_methods */
    Generator_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Generator_init,      /* tp_init */
    0,                         /* tp_alloc */
    0,                 /* tp_new */
    0, /* tp_iter */
    0 //CallbackGenerator_iternext /* tp_iternext */
};

static PyMethodDef Parser_methods[] = { {"read",(PyCFunction)Parser_read, METH_KEYWORDS, "read from input source"},{NULL,NULL,0,NULL} };
static PyMemberDef Parser_members[] = { {NULL} };

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
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_HAVE_CLASS | Py_TPFLAGS_HAVE_ITER,        /*tp_flags*/
    "Parser objects",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    Parser_iter,		               /* tp_iter */
    Parser_iternext,              /* tp_iternext */
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
    Parser_new                 /* tp_new */

};

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
